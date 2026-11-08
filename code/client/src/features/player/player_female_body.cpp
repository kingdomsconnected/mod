#include "features/player/player_female_body.h"

#include "core/application.h"
#include "sdk/cryengine/animation/c_controller_opt_non_virtual.h"
#include "sdk/cryengine/animation/character_manager.h"
#include "sdk/cryengine/entitysystem/i_entity_proxy.h"
#include "sdk/cryengine/scriptsystem/i_script_table.h"
#include "sdk/cryengine/system/s_system_global_environment.h"
#include "sdk/patterns.h"
#include "shared/game/hand_frames.h"

#include <logging/logger.h>
#include <utils/hooking/hooking.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

/**
 * One Mannequin vocabulary for every human body.
 *
 * The game animates a man through `kcd_male_controllerdefs.xml` and
 * `kcd_male_database.adb`, and a woman through `wh_female_*`: two
 * vocabularies -- fragment ids, tags, scopes -- that only partly overlap.
 * Hers has 846 fragments to his 2218, so a woman cannot mount, draw a
 * weapon, fight, die or play most of the work and interaction animations,
 * while 327 of her fragments (her dialogue gestures, her housekeeping and
 * quest animations) and 70 of her tags exist nowhere in his.
 *
 * Both are joined into his, applied to the game's files as they load:
 *
 * - `kcd_male_fragmentids.xml`, `kcd_male_tags.xml` and
 *   `kcd_male_controllerdefs.xml` gain every fragment id, tag, fragment
 *   definition and scope only hers has. A tag joins the group of the same
 *   name, because the game sets item and stance tags through their group;
 *   ids move, identically on every client. The tags gain `female`.
 * - `kcd_male_database.adb` gains every fragment of `wh_female_database.adb`
 *   and its sub-databases, each variant with `female` added to its tags.
 *   `LoadDatabaseData` (0x180945260) never applies a SubADB's `Tags` to its
 *   variants, and `AddEntry` (0x180944CB4) pools a variant into the list of
 *   any equal tag set, so the tag has to be in the variant itself. Appended
 *   after his own list, her variants rank after his on a tie
 *   (`TTagSortedList::Sort`, a stable sort by tag count), so a woman plays
 *   her own animation where one exists and his where it does not, and a
 *   man, never carrying the tag, never plays hers.
 * - every other database written against her vocabulary -- the slave,
 *   weapon and door databases bound beside a woman's body -- is pointed at
 *   his, which now holds all of it.
 *
 * Every body standing on her skeleton then has its class's `wh_female_*`
 * controller definition and database replaced by his as its animated
 * character builds the controller, which is marked `female`.
 */

/**
 * A woman's hands on the man's joint frames.
 *
 * Every clip stores each joint's absolute local rotation, and `male.chr` and
 * `female.chr` name their hand joints alike but orient them differently: her
 * hand is rolled about 76 degrees from his around the bone, and every finger
 * inherits it. So a male clip -- every fragment she borrows from the male
 * database -- bent her hands about the wrong axes.
 *
 * Nothing here runs while a body animates. Her hand joints are put on his
 * frames once, as each thing that carries them loads, and what was authored
 * against her old frames is converted with them:
 *
 * - her skeleton's bones and hand colliders, before
 *   `SkelLoader_Helpers::InitializeBones` derives the default pose, the
 *   skinning bind and the ragdoll from them;
 * - her clips, as each of her databases builds them, before
 *   `GlobalAnimationHeaderCAF::InitControllerLookup` makes a clip playable;
 * - her aim and look poses, baked in `DirectionalBlends.img`, once her
 *   skeleton's IK lists are read.
 *
 * Male clips are left alone and then play on her natively. A rotation q
 * authored on her skeleton becomes inverse(C(parent)) * q * C(joint), which
 * leaves every vertex of her mesh exactly where it was; an additive delta is
 * conjugated by C(parent) instead, and a translation rotated by
 * inverse(C(parent)).
 */
namespace KCDC::Features::Player {
    namespace {
        constexpr const char *kVocabularyLogger = "BodyVocabulary";

        constexpr const char *kMaleTags        = "Animations/Mannequin/ADB/kcd_male_tags.xml";
        constexpr const char *kMaleFragmentIds = "Animations/Mannequin/ADB/kcd_male_fragmentids.xml";
        constexpr const char *kMaleController  = "Animations/Mannequin/ADB/kcd_male_controllerdefs.xml";
        constexpr const char *kMaleDatabase    = "Animations/Mannequin/ADB/kcd_male_database.adb";

        constexpr const char *kFemaleTags        = "Animations/Mannequin/ADB/wh_female_tags.xml";
        constexpr const char *kFemaleFragmentIds = "Animations/Mannequin/ADB/wh_female_fragmentids.xml";
        constexpr const char *kFemaleController  = "Animations/Mannequin/ADB/wh_female_controllerdefs.xml";
        constexpr const char *kFemaleDatabase    = "Animations/Mannequin/ADB/wh_female_database.adb";

        constexpr const char *kFemaleBodyModel = "humans/female/skeleton/female.cdf";

        // The tag her variants require; appended last, after every tag
        // either vocabulary declares.
        constexpr const char *kFemaleTag = "female";

        char FoldPathChar(char c) {
            return c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        bool EndsWithPath(const char *text, const char *suffix) {
            const std::size_t textLength   = std::strlen(text);
            const std::size_t suffixLength = std::strlen(suffix);
            if (textLength < suffixLength) {
                return false;
            }
            for (std::size_t i = 0; i < suffixLength; ++i) {
                if (FoldPathChar(text[textLength - suffixLength + i]) != FoldPathChar(suffix[i])) {
                    return false;
                }
            }
            return true;
        }

        /** Mannequin finds every name by its lowercase CRC, so names are compared folded. */
        std::string Folded(const char *name) {
            std::string folded(name);
            for (char &c : folded) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return folded;
        }

        std::string WithFemaleTag(const char *tags) {
            return *tags == '\0' ? std::string(kFemaleTag) : std::string(tags) + "+" + kFemaleTag;
        }

        /** `section`, created at the end of `root` when the file has none. */
        SDK::XmlNodeRef SectionOf(const SDK::XmlNodeRef &root, const char *section) {
            SDK::XmlNodeRef found = root->findChild(section);
            return found.IsValid() ? found : root->newChild(section);
        }

        /** The element names directly under `section`, folded. */
        void CollectChildTags(const SDK::XmlNodeRef &section, std::unordered_set<std::string> &names) {
            if (!section.IsValid()) {
                return;
            }
            for (int i = 0; i < section->getChildCount(); ++i) {
                names.insert(Folded(section->getChild(i)->getTag()));
            }
        }

        /** The `name` attributes of every `<Tag>` under a tag definition's `<Tags>`, groups included. */
        void CollectTagNames(const SDK::XmlNodeRef &tags, std::unordered_set<std::string> &names) {
            if (!tags.IsValid()) {
                return;
            }
            for (int i = 0; i < tags->getChildCount(); ++i) {
                const SDK::XmlNodeRef child = tags->getChild(i);
                if (child->isTag("Group")) {
                    CollectTagNames(child, names);
                }
                else {
                    names.insert(Folded(child->getAttr("name")));
                }
            }
        }

        /** The `<Group name=...>` of the man's tags, created at their end when he has none. */
        SDK::XmlNodeRef GroupNamed(const SDK::XmlNodeRef &tags, const char *name) {
            const std::string wanted = Folded(name);
            for (int i = 0; i < tags->getChildCount(); ++i) {
                SDK::XmlNodeRef child = tags->getChild(i);
                if (child->isTag("Group") && Folded(child->getAttr("name")) == wanted) {
                    return child;
                }
            }
            SDK::XmlNodeRef group = tags->newChild("Group");
            group->setAttr("name", name);
            return group;
        }

        /**
         * Her tags he lacks, each into his group of the same name -- the game
         * sets `r_broom` or `l_eggBasket` through the hand's group, and a tag
         * outside it is dropped -- then `female`.
         */
        std::size_t ExtendTags(const SDK::XmlNodeRef &document, const SDK::XmlNodeRef &hers) {
            const SDK::XmlNodeRef tags = document->findChild("Tags");
            const SDK::XmlNodeRef her  = hers->findChild("Tags");
            if (!tags.IsValid() || !her.IsValid()) {
                return 0;
            }
            std::unordered_set<std::string> known;
            CollectTagNames(tags, known);

            std::size_t added = 0;
            for (int i = 0; i < her->getChildCount(); ++i) {
                const SDK::XmlNodeRef child = her->getChild(i);
                if (!child->isTag("Group")) {
                    if (known.insert(Folded(child->getAttr("name"))).second) {
                        tags->addChild(child->clone(true));
                        ++added;
                    }
                    continue;
                }
                SDK::XmlNodeRef group;
                for (int t = 0; t < child->getChildCount(); ++t) {
                    const SDK::XmlNodeRef tag = child->getChild(t);
                    if (!known.insert(Folded(tag->getAttr("name"))).second) {
                        continue;
                    }
                    if (!group.IsValid()) {
                        group = GroupNamed(tags, child->getAttr("name"));
                    }
                    group->addChild(tag->clone(true));
                    ++added;
                }
            }
            tags->newChild("Tag")->setAttr("name", kFemaleTag);
            return added;
        }

        /** Her fragment ids he lacks, sub-tag definitions and all, after his. */
        std::size_t ExtendFragmentIds(const SDK::XmlNodeRef &document, const SDK::XmlNodeRef &hers, const fu2::function<SDK::XmlNodeRef(const char *) const> &load) {
            const SDK::XmlNodeRef ids = document->findChild("Tags");
            const SDK::XmlNodeRef her = hers->findChild("Tags");
            if (!ids.IsValid() || !her.IsValid()) {
                return 0;
            }
            std::unordered_set<std::string> known;
            CollectTagNames(ids, known);
            // His imported ids (the optional test fragments) are his too.
            if (const SDK::XmlNodeRef imports = document->findChild("Imports"); imports.IsValid()) {
                for (int i = 0; i < imports->getChildCount(); ++i) {
                    const SDK::XmlNodeRef imported = load(imports->getChild(i)->getAttr("filename"));
                    if (imported.IsValid()) {
                        CollectTagNames(imported->findChild("Tags"), known);
                    }
                }
            }

            std::size_t added = 0;
            for (int i = 0; i < her->getChildCount(); ++i) {
                const SDK::XmlNodeRef id = her->getChild(i);
                if (known.insert(Folded(id->getAttr("name"))).second) {
                    ids->addChild(id->clone(true));
                    ++added;
                }
            }
            return added;
        }

        /** The children of her `section` he has no element of that name for, after his. */
        std::size_t AppendMissingChildren(const SDK::XmlNodeRef &document, const SDK::XmlNodeRef &hers, const char *section) {
            const SDK::XmlNodeRef her = hers->findChild(section);
            if (!her.IsValid()) {
                return 0;
            }
            const SDK::XmlNodeRef his = SectionOf(document, section);
            std::unordered_set<std::string> known;
            CollectChildTags(his, known);

            std::size_t added = 0;
            for (int i = 0; i < her->getChildCount(); ++i) {
                const SDK::XmlNodeRef child = her->getChild(i);
                if (known.insert(Folded(child->getTag())).second) {
                    his->addChild(child->clone(true));
                    ++added;
                }
            }
            return added;
        }

        /**
         * One of her databases, sub-databases first as `LoadDatabaseData`
         * reads them, onto the end of his lists with `female` added to every
         * variant and every blend's tag states.
         */
        void AppendWomansDatabase(const char *path, const SDK::XmlNodeRef &fragments, const SDK::XmlNodeRef &blends, const fu2::function<SDK::XmlNodeRef(const char *) const> &load, std::size_t &fragmentCount, std::size_t &variantCount) {
            const SDK::XmlNodeRef database = load(path);
            if (!database.IsValid()) {
                return;
            }
            if (const SDK::XmlNodeRef subDatabases = database->findChild("SubADBs"); subDatabases.IsValid()) {
                for (int i = 0; i < subDatabases->getChildCount(); ++i) {
                    AppendWomansDatabase(subDatabases->getChild(i)->getAttr("File"), fragments, blends, load, fragmentCount, variantCount);
                }
            }

            if (const SDK::XmlNodeRef list = database->findChild("FragmentList"); list.IsValid()) {
                for (int i = 0; i < list->getChildCount(); ++i) {
                    const SDK::XmlNodeRef fragment = list->getChild(i)->clone(true);
                    for (int v = 0; v < fragment->getChildCount(); ++v) {
                        const SDK::XmlNodeRef variant = fragment->getChild(v);
                        variant->setAttr("Tags", WithFemaleTag(variant->getAttr("Tags")).c_str());
                        ++variantCount;
                    }
                    fragments->addChild(fragment);
                    ++fragmentCount;
                }
            }

            if (const SDK::XmlNodeRef list = database->findChild("FragmentBlendList"); list.IsValid()) {
                for (int i = 0; i < list->getChildCount(); ++i) {
                    const SDK::XmlNodeRef blend = list->getChild(i)->clone(true);
                    for (int v = 0; v < blend->getChildCount(); ++v) {
                        const SDK::XmlNodeRef variant = blend->getChild(v);
                        variant->setAttr("from", WithFemaleTag(variant->getAttr("from")).c_str());
                        variant->setAttr("to", WithFemaleTag(variant->getAttr("to")).c_str());
                    }
                    blends->addChild(blend);
                }
            }
        }

        /** A database written against her vocabulary, pointed at his. */
        void AdoptMaleVocabulary(SDK::XmlNodeRef &document) {
            const bool fragments = EndsWithPath(document->getAttr("FragDef"), kFemaleFragmentIds);
            const bool tags      = EndsWithPath(document->getAttr("TagDef"), kFemaleTags);
            if (!fragments && !tags) {
                return;
            }
            SDK::XmlNodeRef edited = document->clone(true);
            if (!edited.IsValid()) {
                return;
            }
            if (fragments) {
                edited->setAttr("FragDef", kMaleFragmentIds);
            }
            if (tags) {
                edited->setAttr("TagDef", kMaleTags);
            }
            document = edited;
        }

        /** The value of a class script field, or the empty string. */
        const char *ScriptString(SDK::IScriptTable *table, const char *key) {
            const char *value = nullptr;
            return table->GetValue(key, value) && value != nullptr ? value : "";
        }
    } // namespace

    void FemaleBody::ExtendMannequinFile(const char *file, SDK::XmlNodeRef &document, const fu2::function<SDK::XmlNodeRef(const char *) const> &load) {
        const bool tags       = EndsWithPath(file, kMaleTags);
        const bool ids        = EndsWithPath(file, kMaleFragmentIds);
        const bool controller = EndsWithPath(file, kMaleController);
        const bool database   = EndsWithPath(file, kMaleDatabase);
        if (!tags && !ids && !controller && !database) {
            if (EndsWithPath(file, ".adb")) {
                AdoptMaleVocabulary(document);
            }
            return;
        }

        // A document out of a pak is the read-only node, on which every
        // write is silently dropped; each edit goes onto a clone.
        SDK::XmlNodeRef edited = document->clone(true);
        if (!edited.IsValid()) {
            return;
        }

        if (database) {
            const SDK::XmlNodeRef fragments = SectionOf(edited, "FragmentList");
            const SDK::XmlNodeRef blends    = SectionOf(edited, "FragmentBlendList");
            std::size_t fragmentCount       = 0;
            std::size_t variantCount        = 0;
            AppendWomansDatabase(kFemaleDatabase, fragments, blends, load, fragmentCount, variantCount);
            Framework::Logging::GetLogger(kVocabularyLogger)->info("{}: {} of her fragments joined, {} variants behind the female tag", file, fragmentCount, variantCount);
            KCDC::Core::gApplication->GetStreamedAssetService().ExtendMannequinFile(file, edited);
            document = edited;
            return;
        }

        const char *herFile        = tags ? kFemaleTags : (ids ? kFemaleFragmentIds : kFemaleController);
        const SDK::XmlNodeRef hers = load(herFile);
        if (!hers.IsValid()) {
            Framework::Logging::GetLogger(kVocabularyLogger)->error("{}: {} did not load; a woman's own animations stay out of the shared vocabulary", file, herFile);
            return;
        }

        if (tags) {
            Framework::Logging::GetLogger(kVocabularyLogger)->info("{}: {} of her tags joined, and the female tag", file, ExtendTags(edited, hers));
        }
        else if (ids) {
            Framework::Logging::GetLogger(kVocabularyLogger)->info("{}: {} of her fragment ids joined", file, ExtendFragmentIds(edited, hers, load));
        }
        else {
            const std::size_t definitions = AppendMissingChildren(edited, hers, "FragmentDefs");
            const std::size_t scopes      = AppendMissingChildren(edited, hers, "ScopeDefs");
            const std::size_t contexts    = AppendMissingChildren(edited, hers, "ScopeContextDefs");
            Framework::Logging::GetLogger(kVocabularyLogger)->info("{}: {} of her fragment definitions, {} scopes and {} scope contexts joined", file, definitions, scopes, contexts);
        }
        // The streamed clips' fragment, declared beside hers in the same shared vocabulary.
        KCDC::Core::gApplication->GetStreamedAssetService().ExtendMannequinFile(file, edited);
        document = edited;
    }

    bool FemaleBody::AdoptHumanMannequin(SDK::IEntity *entity) {
        auto *proxy                = entity != nullptr ? static_cast<SDK::IEntityProxy *>(entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Script))) : nullptr;
        SDK::IScriptTable *script  = proxy != nullptr ? proxy->GetScriptTable() : nullptr;
        SDK::ScriptTablePtr fields = script != nullptr ? script->GetTable("Properties") : SDK::ScriptTablePtr {};
        if (!fields || !EndsWithPath(ScriptString(fields.get(), "fileModel"), kFemaleBodyModel)) {
            return false;
        }

        // Written on the entity's own table, so the class -- and every body
        // of it not on her skeleton -- keeps what it says.
        if (EndsWithPath(ScriptString(script, "ActionController"), kFemaleController)) {
            script->SetValue("ActionController", kMaleController);
        }
        if (EndsWithPath(ScriptString(script, "AnimDatabase3P"), kFemaleDatabase)) {
            script->SetValue("AnimDatabase3P", kMaleDatabase);
        }
        return true;
    }

    thread_local FemaleBody::FemaleDatabaseLoad *FemaleBody::_femaleDatabaseLoad = nullptr;
    std::mutex FemaleBody::_femaleDatabaseClonesLock;
    std::unordered_map<const SDK::CInternalDatabaseInfo *, std::vector<std::unique_ptr<std::byte[]>>> FemaleBody::_femaleDatabaseClones;
    std::mutex FemaleBody::_directionalPosesLock;
    std::vector<bool> FemaleBody::_directionalPosesAdopted;

    namespace {
        constexpr const char *kHandFramesLogger = "HandFrames";

        constexpr const char *kFemaleSkeletonSuffix = "humans/female/skeleton/female.chr";
        constexpr const char *kFemaleClipPrefix     = "animations/humans/female/";

        // ECompressionInformation values a database's tracks use.
        constexpr std::uint8_t kNoCompressQuat        = 1;
        constexpr std::uint8_t kNoCompressVec3        = 2;
        constexpr std::uint8_t kSmallTree48BitQuat    = 5;
        constexpr std::uint8_t kSmallTree64BitExtQuat = 8;

        // Quantized quaternion component ranges for 15-bit and 21/20-bit keys.
        constexpr float kRange       = 0.707106781186f;
        constexpr float kMax15Bit    = 23170.0f;
        constexpr float kMax21Bit    = 1482909.0f;
        constexpr float kMax20Bit    = 741454.0f;
        constexpr std::size_t kTrackAlignment = 16;


        bool StartsWithPath(const char *text, const char *prefix) {
            for (std::size_t i = 0; prefix[i] != '\0'; ++i) {
                if (text[i] == '\0' || FoldPathChar(text[i]) != FoldPathChar(prefix[i])) {
                    return false;
                }
            }
            return true;
        }

        /** `_strnicmp` over every offset, as `GlobalAnimationHeaderAIM::ProcessPoses` matches a token. */
        bool ContainsNoCase(const char *text, const char *token) {
            const std::size_t textLength  = std::strlen(text);
            const std::size_t tokenLength = std::strlen(token);
            if (tokenLength == 0 || textLength < tokenLength) {
                return false;
            }
            for (std::size_t start = 0; start + tokenLength <= textLength; ++start) {
                if (_strnicmp(text + start, token, tokenLength) == 0) {
                    return true;
                }
            }
            return false;
        }

        // --- Rotations ------------------------------------------------------

        constexpr SDK::Quat kIdentity {0.0f, 0.0f, 0.0f, 1.0f};

        SDK::Quat Multiply(const SDK::Quat &a, const SDK::Quat &b) {
            return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
        }

        SDK::Quat Conjugate(const SDK::Quat &q) {
            return {-q.x, -q.y, -q.z, q.w};
        }

        SDK::Quat Normalized(const SDK::Quat &q) {
            const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            return {q.x / length, q.y / length, q.z / length, q.w / length};
        }

        bool IsIdentityRotation(const SDK::Quat &q) {
            return std::fabs(q.w) >= 1.0f - 1e-7f;
        }

        SDK::Vec3 Rotate(const SDK::Quat &q, const SDK::Vec3 &v) {
            // v + 2w(u x v) + 2u x (u x v), u = q.xyz
            const SDK::Vec3 t {2.0f * (q.y * v.z - q.z * v.y), 2.0f * (q.z * v.x - q.x * v.z), 2.0f * (q.x * v.y - q.y * v.x)};
            return {v.x + q.w * t.x + (q.y * t.z - q.z * t.y), v.y + q.w * t.y + (q.z * t.x - q.x * t.z), v.z + q.w * t.z + (q.x * t.y - q.y * t.x)};
        }

        /** The rotation matrix of `q`, row-major, as a Matrix34's upper 3x3 holds it. */
        void ToMatrix(const SDK::Quat &q, float m[3][3]) {
            const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
            const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
            const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
            m[0][0] = 1.0f - 2.0f * (yy + zz), m[0][1] = 2.0f * (xy - wz), m[0][2] = 2.0f * (xz + wy);
            m[1][0] = 2.0f * (xy + wz), m[1][1] = 1.0f - 2.0f * (xx + zz), m[1][2] = 2.0f * (yz - wx);
            m[2][0] = 2.0f * (xz - wy), m[2][1] = 2.0f * (yz + wx), m[2][2] = 1.0f - 2.0f * (xx + yy);
        }

        SDK::Quat FrameOf(const Shared::Game::HandFrameEntry *entry) {
            return entry != nullptr ? SDK::Quat {entry->frame.x, entry->frame.y, entry->frame.z, entry->frame.w} : kIdentity;
        }

        /** q' = pre * q * post. */
        struct RotationChange {
            SDK::Quat pre;
            SDK::Quat post;

            bool IsIdentity() const {
                return IsIdentityRotation(pre) && IsIdentityRotation(post);
            }

            SDK::Quat Apply(const SDK::Quat &q) const {
                return Normalized(Multiply(Multiply(pre, q), post));
            }
        };

        /**
         * What a joint's tracks need when authored on her old frames. Absent
         * for a joint outside her hands and for one kept verbatim.
         */
        struct JointChange {
            const Shared::Game::HandFrameEntry *entry  = nullptr;
            const Shared::Game::HandFrameEntry *parent = nullptr;

            static JointChange Of(std::uint32_t jointCrc) {
                JointChange change;
                change.entry = Shared::Game::FindHandFrame(jointCrc);
                if (change.entry == nullptr || change.entry->kind == Shared::Game::HandFrameKind::Verbatim) {
                    change.entry = nullptr;
                    return change;
                }
                change.parent = Shared::Game::FindHandFrame(change.entry->parentCrc);
                return change;
            }

            bool Applies() const {
                return entry != nullptr;
            }

            RotationChange Rotation(bool additive) const {
                const SDK::Quat parentFrame = FrameOf(parent);
                return {Conjugate(parentFrame), additive ? parentFrame : FrameOf(entry)};
            }

            /** Translations are in the parent's frame, absolute or additive alike. */
            SDK::Quat Translation() const {
                return Conjugate(FrameOf(parent));
            }
        };

        // --- Compressed keys ----------------------------------------------

        std::size_t RotationKeySize(std::uint8_t format) {
            switch (format) {
            case kNoCompressQuat: return sizeof(SDK::Quat);
            case kSmallTree48BitQuat: return 6;
            case kSmallTree64BitExtQuat: return 8;
            default: return 0;
            }
        }

        std::size_t PositionKeySize(std::uint8_t format) {
            return format == kNoCompressVec3 ? sizeof(SDK::Vec3) : 0;
        }

        /** The three stored components' slots around the dropped one, in ascending order. */
        void StoredSlots(unsigned dropped, unsigned slots[3]) {
            for (unsigned i = 0, c = 0; i < 4; ++i) {
                if (i != dropped) {
                    slots[c++] = i;
                }
            }
        }

        // `SmallTree48BitQuat::ToExternalType` (0x180AAEF2C).
        SDK::Quat Decode48(const std::byte *key) {
            std::uint16_t m[3];
            std::memcpy(m, key, sizeof(m));
            const unsigned dropped = m[2] > 0xBFFF ? 3 : m[2] > 0x7999 ? 2 : m[2] > 0x3999 ? 1 : 0;
            const unsigned packed[3] = {m[0] & 0x7FFFu, ((m[0] >> 15) + (m[1] << 1)) & 0x7FFFu, ((m[1] >> 14) + (m[2] << 2)) & 0x7FFFu};
            unsigned slots[3];
            StoredSlots(dropped, slots);
            float c[4]  = {};
            float sum   = 0.0f;
            for (int i = 0; i < 3; ++i) {
                c[slots[i]] = static_cast<float>(packed[i]) * (1.0f / kMax15Bit) - kRange;
                sum += c[slots[i]] * c[slots[i]];
            }
            c[dropped] = std::sqrt(std::max(0.0f, 1.0f - sum));
            return {c[0], c[1], c[2], c[3]};
        }

        // `SmallTree48BitQuat::ToInternalType`.
        void Encode48(const SDK::Quat &rotation, std::byte *key) {
            float c[4] = {rotation.x, rotation.y, rotation.z, rotation.w};
            unsigned dropped = 0;
            for (unsigned i = 1; i < 4; ++i) {
                if (std::fabs(c[i]) > std::fabs(c[dropped])) {
                    dropped = i;
                }
            }
            if (c[dropped] < 0.0f) {
                for (float &component : c) {
                    component = -component;
                }
            }
            unsigned slots[3];
            StoredSlots(dropped, slots);
            std::uint64_t value = 0;
            for (int i = 0; i < 3; ++i) {
                value |= static_cast<std::uint64_t>(static_cast<std::uint16_t>(std::floor((c[slots[i]] + kRange) * kMax15Bit + 0.5f))) << (15 * i);
            }
            value |= static_cast<std::uint64_t>(dropped) << 46;
            const std::uint16_t m[3] = {static_cast<std::uint16_t>(value), static_cast<std::uint16_t>(value >> 16), static_cast<std::uint16_t>(value >> 32)};
            std::memcpy(key, m, sizeof(m));
        }

        // `SmallTree64BitExtQuat::ToExternalType` (0x183777160).
        SDK::Quat Decode64Ext(const std::byte *key) {
            std::uint32_t m[2];
            std::memcpy(m, key, sizeof(m));
            const unsigned dropped = (m[1] >> 30) & 3;
            const unsigned packed[3] = {m[0] & 0x1FFFFFu, ((m[0] >> 21) + (m[1] << 11)) & 0x1FFFFFu, (m[1] >> 10) & 0xFFFFFu};
            const float scale[3]     = {kMax21Bit, kMax21Bit, kMax20Bit};
            unsigned slots[3];
            StoredSlots(dropped, slots);
            float c[4] = {};
            float sum  = 0.0f;
            for (int i = 0; i < 3; ++i) {
                c[slots[i]] = static_cast<float>(packed[i]) / scale[i] - kRange;
                sum += c[slots[i]] * c[slots[i]];
            }
            c[dropped] = std::sqrt(std::max(0.0f, 1.0f - sum));
            return {c[0], c[1], c[2], c[3]};
        }

        // `SmallTree64BitExtQuat::ToInternalType`.
        void Encode64Ext(const SDK::Quat &rotation, std::byte *key) {
            float c[4] = {rotation.x, rotation.y, rotation.z, rotation.w};
            unsigned dropped = 0;
            for (unsigned i = 1; i < 4; ++i) {
                if (std::fabs(c[i]) > std::fabs(c[dropped])) {
                    dropped = i;
                }
            }
            if (c[dropped] < 0.0f) {
                for (float &component : c) {
                    component = -component;
                }
            }
            unsigned slots[3];
            StoredSlots(dropped, slots);
            std::uint64_t value = static_cast<std::uint64_t>(std::floor((c[slots[0]] + kRange) * kMax21Bit + 0.5f));
            value |= static_cast<std::uint64_t>(std::floor((c[slots[1]] + kRange) * kMax21Bit + 0.5f)) << 21;
            value |= static_cast<std::uint64_t>(std::floor((c[slots[2]] + kRange) * kMax20Bit + 0.5f)) << 42;
            value |= static_cast<std::uint64_t>(dropped) << 62;
            const std::uint32_t m[2] = {static_cast<std::uint32_t>(value), static_cast<std::uint32_t>(value >> 32)};
            std::memcpy(key, m, sizeof(m));
        }

        void ConvertRotationKeys(std::byte *keys, std::uint32_t count, std::uint8_t format, const RotationChange &change) {
            const std::size_t size = RotationKeySize(format);
            for (std::uint32_t k = 0; k < count; ++k) {
                std::byte *key = keys + k * size;
                if (format == kNoCompressQuat) {
                    SDK::Quat q;
                    std::memcpy(&q, key, sizeof(q));
                    q = change.Apply(q);
                    std::memcpy(key, &q, sizeof(q));
                }
                else if (format == kSmallTree48BitQuat) {
                    Encode48(change.Apply(Decode48(key)), key);
                }
                else {
                    Encode64Ext(change.Apply(Decode64Ext(key)), key);
                }
            }
        }

        void ConvertPositionKeys(std::byte *keys, std::uint32_t count, const SDK::Quat &change) {
            for (std::uint32_t k = 0; k < count; ++k) {
                SDK::Vec3 t;
                std::memcpy(&t, keys + k * sizeof(t), sizeof(t));
                t = Rotate(change, t);
                std::memcpy(keys + k * sizeof(t), &t, sizeof(t));
            }
        }

        std::size_t AlignTrack(std::size_t size) {
            return (size + kTrackAlignment - 1) & ~(kTrackAlignment - 1);
        }
    } // namespace

    // --- Skeleton ------------------------------------------------------------

    bool FemaleBody::IsFemaleSkeleton(const char *path) {
        return path != nullptr && EndsWithPath(path, kFemaleSkeletonSuffix);
    }

    void FemaleBody::AdoptMaleHandFrames(SDK::CSkinningInfo &skinning) {
        SDK::DynArray<SDK::CryBoneDescData> &bones    = skinning.GetBones();
        SDK::DynArray<SDK::PhysicalProxy> &colliders = skinning.GetPhysicalProxies();

        std::size_t reframed = 0;
        std::size_t turned   = 0;
        for (std::uint32_t b = 0; b < bones.Size(); ++b) {
            SDK::CryBoneDescData &bone                 = bones[b];
            const Shared::Game::HandFrameEntry *entry = Shared::Game::FindHandFrame(bone.m_nControllerID);
            if (entry == nullptr || entry->kind != Shared::Game::HandFrameKind::Frame || IsIdentityRotation(FrameOf(entry))) {
                continue;
            }

            // B2W' = B2W * C: the joint keeps its place, its axes turn.
            float frame[3][3];
            ToMatrix(FrameOf(entry), frame);
            SDK::Matrix34 &b2w = bone.m_DefaultB2W;
            float rotated[3][3];
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    rotated[r][c] = b2w.m[r][0] * frame[0][c] + b2w.m[r][1] * frame[1][c] + b2w.m[r][2] * frame[2][c];
                }
            }
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    b2w.m[r][c] = rotated[r][c];
                }
            }

            // W2B' = inverse(B2W'): the transposed rotation, and -R^T t.
            SDK::Matrix34 &w2b = bone.m_DefaultW2B;
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    w2b.m[r][c] = rotated[c][r];
                }
                w2b.m[r][3] = -(rotated[0][r] * b2w.m[0][3] + rotated[1][r] * b2w.m[1][3] + rotated[2][r] * b2w.m[2][3]);
            }
            ++reframed;

            // The collider is in the bone's frame; turn it back so it stays where the hand is.
            const SDK::Quat back = Conjugate(FrameOf(entry));
            for (const SDK::CryBonePhysics &physics : bone.m_PhysInfo) {
                if (physics.pPhysGeom <= 0) {
                    continue;
                }
                for (std::uint32_t p = 0; p < colliders.Size(); ++p) {
                    SDK::PhysicalProxy &collider = colliders[p];
                    if (collider.ChunkID != static_cast<std::uint32_t>(physics.pPhysGeom)) {
                        continue;
                    }
                    for (std::uint32_t v = 0; v < collider.m_arrPoints.Size(); ++v) {
                        collider.m_arrPoints[v] = Rotate(back, collider.m_arrPoints[v]);
                    }
                    ++turned;
                }
            }
        }

        Framework::Logging::GetLogger(kHandFramesLogger)->info("female.chr: {} hand joints on the male frames, {} hand colliders turned with them", reframed, turned);
    }

    // --- Aim and look poses ----------------------------------------------------

    void FemaleBody::AdoptMaleHandFramesInDirectionalPoses(const SDK::CDefaultSkeleton &skeleton) {
        auto *manager = static_cast<SDK::CharacterManager *>(SDK::SSystemGlobalEnvironment::Get().pCharacterManager);
        SDK::DynArray<SDK::GlobalAnimationHeaderAIM> &headers = manager->GetGlobalAIM();
        const SDK::DynArray<SDK::CDefaultSkeleton::SJoint> &joints = skeleton.GetJoints();

        std::lock_guard<std::mutex> lock(_directionalPosesLock);
        _directionalPosesAdopted.resize(std::max<std::size_t>(_directionalPosesAdopted.size(), headers.Size()), false);

        std::size_t adopted = 0;
        for (std::uint32_t h = 0; h < headers.Size(); ++h) {
            SDK::GlobalAnimationHeaderAIM &header = headers[h];
            const char *path                      = header.GetFilePath();
            if (_directionalPosesAdopted[h] || !header.IsCreated() || path == nullptr || !StartsWithPath(path, kFemaleClipPrefix)) {
                continue;
            }

            // The definition ProcessPoses would give the clip: aim first, then look.
            const SDK::DynArray<SDK::CDefaultSkeleton::SJointsAimIK_Rot> *rotations = nullptr;
            for (std::uint32_t i = 0; i < skeleton.GetAimBlends().Size() && rotations == nullptr; ++i) {
                if (ContainsNoCase(path, skeleton.GetAimBlends()[i].m_AnimToken.m_pData)) {
                    rotations = &skeleton.GetAimRotations();
                }
            }
            for (std::uint32_t i = 0; i < skeleton.GetLookBlends().Size() && rotations == nullptr; ++i) {
                if (ContainsNoCase(path, skeleton.GetLookBlends()[i].m_AnimToken.m_pData)) {
                    rotations = &skeleton.GetLookRotations();
                }
            }
            if (rotations == nullptr) {
                continue;
            }

            SDK::DynArray<SDK::AimIKPose> &poses = header.GetPoses();
            for (std::uint32_t p = 0; p < poses.Size(); ++p) {
                SDK::DynArray<SDK::Quat> &rotation = poses[p].m_arrRotation;
                const std::uint32_t count          = std::min(rotation.Size(), rotations->Size());
                for (std::uint32_t r = 0; r < count; ++r) {
                    const std::int16_t joint = (*rotations)[r].m_nJointIdx;
                    if (joint < 0 || static_cast<std::uint32_t>(joint) >= joints.Size()) {
                        continue;
                    }
                    // The poses are local rotations; the blender takes the
                    // additive delta between two of them, which the same
                    // change keeps consistent.
                    const JointChange change = JointChange::Of(joints[joint].m_nJointCRC32);
                    if (change.Applies()) {
                        rotation[r] = change.Rotation(false).Apply(rotation[r]);
                    }
                }
            }
            _directionalPosesAdopted[h] = true;
            ++adopted;
        }

        if (adopted != 0) {
            Framework::Logging::GetLogger(kHandFramesLogger)->info("{} of her aim and look pose clips put on the male hand frames", adopted);
        }
    }

    // --- Clips -------------------------------------------------------------------

    /**
     * One of her databases while `ReadController905` builds it. A database
     * stores each distinct key track once and lets any number of controllers
     * point at it, so a track one hand joint shares with a joint that needs
     * a different change -- or none -- cannot be rewritten where it lies.
     * The whole chunk is read first to know every track's users: a track all
     * of whose users agree is converted in place, once; a controller on a
     * track they disagree about gets a copy of its own.
     */
    struct FemaleBody::FemaleDatabaseLoad {
        /** Who a track's users need: kNone, one change, or kMixed. */
        static constexpr std::uint32_t kUnseen = 0xFFFFFFFF;
        static constexpr std::uint32_t kNone   = 0;
        static constexpr std::uint32_t kMixed  = 0xFFFFFFFE;

        struct Track {
            std::uint32_t users = kUnseen;
            bool converted      = false;
        };

        struct Controller {
            std::uint32_t rotationTrack;
            std::uint32_t positionTrack;
        };

        struct Clip {
            bool additive;
            std::unordered_map<std::uint32_t, Controller> controllers;
        };

        const SDK::CInternalDatabaseInfo *database = nullptr;
        std::unordered_map<std::uint64_t, Clip> clips;
        std::vector<Track> rotationTracks;
        std::vector<Track> positionTracks;
        std::vector<std::unique_ptr<std::byte[]>> clones;
        std::size_t missingClips = 0;
        std::size_t unsupported  = 0;

        static void Use(std::vector<Track> &tracks, std::uint32_t index, std::uint32_t need) {
            if (index >= tracks.size()) {
                return;
            }
            Track &track = tracks[index];
            if (track.users == kUnseen) {
                track.users = need;
            }
            else if (track.users != need) {
                track.users = kMixed;
            }
        }

        /** The change a joint's rotation track needs, numbered so equal changes compare equal. */
        static std::uint32_t RotationNeed(const JointChange &change, bool additive) {
            if (!change.Applies() || change.Rotation(additive).IsIdentity()) {
                return kNone;
            }
            return static_cast<std::uint32_t>((change.entry - Shared::Game::g_HandFrames.data()) + 1) * 2 + (additive ? 1 : 0);
        }

        static std::uint32_t PositionNeed(const JointChange &change) {
            if (!change.Applies() || IsIdentityRotation(change.Translation())) {
                return kNone;
            }
            return static_cast<std::uint32_t>((change.parent - Shared::Game::g_HandFrames.data()) + 1) * 2;
        }

        /**
         * Reads a controller chunk without constructing controllers.
         * Returns false when the chunk does not have the expected layout.
         */
        bool Scan(const SDK::IChunkFile::ChunkDesc &chunk) {
            const auto *begin = static_cast<const std::uint8_t *>(chunk.data);
            const auto *end   = begin + chunk.size;
            if (chunk.bSwapEndian || chunk.size < 16) {
                return false;
            }
            std::uint32_t header[4];
            std::memcpy(header, begin, sizeof(header));
            const std::uint32_t positionKeys = header[0], rotationKeys = header[1], keyTimes = header[2], animations = header[3];

            const std::uint8_t *data  = begin + 16;
            const std::uint8_t *first = data;
            data += keyTimes * 2 + 7 * 4;                           // key-time sizes, formats
            data += positionKeys * 2 + 9 * 4;                       // position sizes, formats
            data += rotationKeys * 2 + 9 * 4;                       // rotation sizes, formats
            data += keyTimes * 4 + positionKeys * 4;                // key-time and position offsets
            if (data + (rotationKeys + 1) * 4 > end) {
                return false;
            }
            std::int32_t trackEnd;
            std::memcpy(&trackEnd, data + rotationKeys * 4, sizeof(trackEnd));
            data += (rotationKeys + 1) * 4;

            const bool inPlace   = trackEnd < 0;
            std::uint32_t padding = 0;
            if (inPlace) {
                std::memcpy(&padding, data, sizeof(padding));
                data += sizeof(padding);
            }
            const std::ptrdiff_t misalignment = (data - first) % 4;
            data += misalignment > 0 ? 4 - misalignment : 0;
            if (!inPlace) {
                data += (static_cast<std::uint32_t>(std::abs(trackEnd)) + 3) & ~3u;
            }
            data += padding;
            const std::uint8_t *animationBlock = data;

            rotationTracks.assign(rotationKeys, Track {});
            positionTracks.assign(positionKeys, Track {});
            for (std::uint32_t a = 0; a < animations; ++a) {
                if (data + 2 > end) {
                    return false;
                }
                std::uint16_t nameLength;
                std::memcpy(&nameLength, data, sizeof(nameLength));
                const char *name = reinterpret_cast<const char *>(data + 2);
                data += 2 + nameLength;

                std::uint32_t assetFlags;
                std::memcpy(&assetFlags, data, sizeof(assetFlags));
                data += 132; // MotionParams905

                std::uint16_t footPlants;
                std::memcpy(&footPlants, data, sizeof(footPlants));
                data += 2 + footPlants;

                std::uint16_t controllerCount;
                std::memcpy(&controllerCount, data, sizeof(controllerCount));
                data += 2;
                std::int32_t infoOffset = static_cast<std::int32_t>(data - animationBlock);
                if (inPlace) {
                    std::memcpy(&infoOffset, data, sizeof(infoOffset));
                    data += sizeof(infoOffset);
                }
                else {
                    data += controllerCount * 20;
                }
                const std::uint8_t *infos = animationBlock + infoOffset;
                if (data > end || infos < begin || infos + controllerCount * 20 > end) {
                    return false;
                }

                const std::uint64_t hash = hook::call<std::uint64_t>(SDK::gPatterns.CityHash64Lowercase, name, static_cast<std::uint32_t>(nameLength));
                Clip &clip    = clips[hash];
                clip.additive = (assetFlags & SDK::GlobalAnimationHeaderCAF::kFlagAdditive) != 0;
                for (std::uint16_t c = 0; c < controllerCount; ++c) {
                    // CControllerInfo: id, posKeyTime, pos, rotKeyTime, rot.
                    std::uint32_t info[5];
                    std::memcpy(info, infos + c * 20, sizeof(info));
                    clip.controllers[info[0]] = Controller {info[4], info[2]};

                    const JointChange change = JointChange::Of(info[0]);
                    Use(rotationTracks, info[4], RotationNeed(change, clip.additive));
                    Use(positionTracks, info[2], PositionNeed(change));
                }
            }
            return true;
        }
    };

    void FemaleBody::BeginFemaleDatabase(const SDK::CInternalDatabaseInfo &database, const SDK::IChunkFile::ChunkDesc &chunk) {
        const char *path = database.GetFilePath();
        if (path == nullptr || !StartsWithPath(path, kFemaleClipPrefix)) {
            return;
        }

        auto load      = std::make_unique<FemaleDatabaseLoad>();
        load->database = &database;
        if (!load->Scan(chunk)) {
            Framework::Logging::GetLogger(kHandFramesLogger)->error("{}: controller chunk not laid out as expected; her clips in it keep her own hand frames", path);
            return;
        }
        _femaleDatabaseLoad = load.release();
    }

    void FemaleBody::EndFemaleDatabase() {
        std::unique_ptr<FemaleDatabaseLoad> load(_femaleDatabaseLoad);
        _femaleDatabaseLoad = nullptr;
        if (load == nullptr) {
            return;
        }

        if (load->missingClips != 0 || load->unsupported != 0) {
            Framework::Logging::GetLogger(kHandFramesLogger)->warn("{}: {} clips not found in the scan, {} tracks in a format not converted", load->database->GetFilePath(), load->missingClips, load->unsupported);
        }
        if (!load->clones.empty()) {
            std::lock_guard<std::mutex> lock(_femaleDatabaseClonesLock);
            auto &owned = _femaleDatabaseClones[load->database];
            for (auto &clone : load->clones) {
                owned.push_back(std::move(clone));
            }
        }
    }

    void FemaleBody::ReleaseFemaleDatabase(const SDK::CInternalDatabaseInfo &database) {
        std::lock_guard<std::mutex> lock(_femaleDatabaseClonesLock);
        _femaleDatabaseClones.erase(&database);
    }

    void FemaleBody::AdoptMaleHandFramesInClip(SDK::GlobalAnimationHeaderCAF &clip, std::uint32_t controllerCount) {
        FemaleDatabaseLoad *load = _femaleDatabaseLoad;
        if (load == nullptr) {
            return;
        }
        const auto found = load->clips.find(clip.GetFilePathHash());
        if (found == load->clips.end()) {
            ++load->missingClips;
            return;
        }
        const FemaleDatabaseLoad::Clip &scanned = found->second;

        SDK::IController **controllers = clip.GetControllers();
        for (std::uint32_t i = 0; i < controllerCount; ++i) {
            // Every controller a database builds is one of these (ReadController905).
            auto *controller = static_cast<SDK::CControllerOptNonVirtual *>(controllers[i]);
            const auto tracks = scanned.controllers.find(controller->GetID());
            if (tracks == scanned.controllers.end()) {
                continue;
            }

            const JointChange change = JointChange::Of(controller->GetID());
            if (!change.Applies()) {
                continue;
            }
            const RotationChange rotationChange = change.Rotation(scanned.additive);
            const SDK::Quat positionChange      = change.Translation();

            SDK::ControllerData &rotation = controller->Rotation();
            SDK::ControllerData &position = controller->Position();
            const bool rotates  = rotation.IsPresent() && !rotationChange.IsIdentity();
            const bool moves    = position.IsPresent() && !IsIdentityRotation(positionChange);
            if (rotates && RotationKeySize(rotation.m_eCompressionType) == 0) {
                ++load->unsupported;
                continue;
            }
            if (moves && PositionKeySize(position.m_eCompressionType) == 0) {
                ++load->unsupported;
                continue;
            }

            FemaleDatabaseLoad::Track *rotationTrack = rotates && tracks->second.rotationTrack < load->rotationTracks.size() ? &load->rotationTracks[tracks->second.rotationTrack] : nullptr;
            FemaleDatabaseLoad::Track *positionTrack = moves && tracks->second.positionTrack < load->positionTracks.size() ? &load->positionTracks[tracks->second.positionTrack] : nullptr;
            const bool rotationShared = rotationTrack != nullptr && rotationTrack->users == FemaleDatabaseLoad::kMixed;
            const bool positionShared = positionTrack != nullptr && positionTrack->users == FemaleDatabaseLoad::kMixed;

            // Tracks every user agrees about change where they lie, once.
            if (rotationTrack != nullptr && !rotationShared && !rotationTrack->converted) {
                ConvertRotationKeys(rotation.Keys(), rotation.KeyCount(), rotation.m_eCompressionType, rotationChange);
                rotationTrack->converted = true;
            }
            if (positionTrack != nullptr && !positionShared && !positionTrack->converted) {
                ConvertPositionKeys(position.Keys(), position.KeyCount(), positionChange);
                positionTrack->converted = true;
            }
            if (!rotationShared && !positionShared) {
                continue;
            }

            // Copying needs every present track's key size, converted or not.
            if ((rotation.IsPresent() && RotationKeySize(rotation.m_eCompressionType) == 0) || (position.IsPresent() && PositionKeySize(position.m_eCompressionType) == 0)) {
                ++load->unsupported;
                continue;
            }

            // A track its users disagree about: this controller gets a copy
            // of both its tracks, keys and key times together, so the
            // offsets stay relative to the copy. The defragmenter rebases
            // only pointers into its own block (CInternalDatabaseInfo::Relocate,
            // 0x1837A2044), so it leaves this one alone.
            const std::size_t rotationTimes = rotation.IsPresent() ? AlignTrack(rotation.KeyTimesSize()) : 0;
            const std::size_t rotationKeys  = rotation.IsPresent() ? AlignTrack(rotation.KeyCount() * RotationKeySize(rotation.m_eCompressionType)) : 0;
            const std::size_t positionTimes = position.IsPresent() ? AlignTrack(position.KeyTimesSize()) : 0;
            const std::size_t positionKeys  = position.IsPresent() ? AlignTrack(position.KeyCount() * PositionKeySize(position.m_eCompressionType)) : 0;
            auto block      = std::make_unique<std::byte[]>(AlignTrack(sizeof(SDK::CControllerOptNonVirtual)) + rotationTimes + rotationKeys + positionTimes + positionKeys);
            std::byte *next = block.get();
            auto *clone     = reinterpret_cast<SDK::CControllerOptNonVirtual *>(next);
            std::memcpy(clone, controller, sizeof(SDK::CControllerOptNonVirtual));
            next += AlignTrack(sizeof(SDK::CControllerOptNonVirtual));

            SDK::ControllerData &cloneRotation = clone->Rotation();
            if (rotation.IsPresent()) {
                std::memcpy(next, rotation.KeyTimes(), rotation.KeyTimesSize());
                cloneRotation.m_nDataOffs = static_cast<std::int32_t>(next - reinterpret_cast<std::byte *>(&cloneRotation));
                next += rotationTimes;
                const std::uint32_t count = rotation.KeyCount();
                std::memcpy(next, rotation.Keys(), count * RotationKeySize(rotation.m_eCompressionType));
                cloneRotation.m_nKeysOffs = static_cast<std::int32_t>(next - reinterpret_cast<std::byte *>(&cloneRotation));
                if (rotationShared) {
                    ConvertRotationKeys(next, count, rotation.m_eCompressionType, rotationChange);
                }
                next += rotationKeys;
            }

            SDK::ControllerData &clonePosition = clone->Position();
            if (position.IsPresent()) {
                std::memcpy(next, position.KeyTimes(), position.KeyTimesSize());
                clonePosition.m_nDataOffs = static_cast<std::int32_t>(next - reinterpret_cast<std::byte *>(&clonePosition));
                next += positionTimes;
                const std::uint32_t count = position.KeyCount();
                std::memcpy(next, position.Keys(), count * PositionKeySize(position.m_eCompressionType));
                clonePosition.m_nKeysOffs = static_cast<std::int32_t>(next - reinterpret_cast<std::byte *>(&clonePosition));
                if (positionShared) {
                    ConvertPositionKeys(next, count, positionChange);
                }
            }

            controllers[i] = clone;
            load->clones.push_back(std::move(block));
        }
    }
} // namespace KCDC::Features::Player
