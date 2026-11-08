#include <utils/safe_win32.h>

#include "book_service.h"

#include "book_scripting.h"

#include "core/application.h"
#include "features/ground_item/ground_item_service.h"
#include "features/player/player_inventory_service.h"
#include "common/module_support.h"

#include "shared/game/cry_guid_text.h"

#include "sdk/cryengine/3dengine/i_material.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_render_proxy.h"
#include "sdk/cryengine/scaleform/s_ui_arguments.h"
#include "sdk/patterns.h"
#include "sdk/wh/entitymodule/c_player.h"
#include "sdk/wh/entitymodule/c_world_inventory.h"
#include "sdk/wh/entitymodule/s_item_class_guid.h"
#include "sdk/wh/guimodule/c_ui_book.h"

#include <logging/logger.h>
#include <mafianet/get_time.h>
#include <utils/hooking/hooking.h>

#include <cctype>
#include <cstring>
#include <unordered_set>
#include <utility>

namespace KCDC::Features::Book {
    namespace {
        constexpr const char *kLogger = "Book";

        /** What a page's localization key starts with; the book id and page index follow. */
        constexpr char kLabelPrefix[]         = "kcdc_book_";
        constexpr std::size_t kLabelPrefixLen = sizeof(kLabelPrefix) - 1;

        /** The collision-only sub-material every carrier model opens with (`Shader="NoDraw"`). */
        constexpr char kProxyMaterial[] = "proxy";

        /** EEfResTextures::EFTT_DIFFUSE. The page itself is slot 11, EFTT_OPACITY (Book.lua `TexSlot`). */
        constexpr int kDiffuseSlot = 0;

        /** The movie's page-break marker, `TextExtension.C_NEW_PAGE`. */
        constexpr char kNewPage[] = "<newpage/>";

        std::uint64_t NowMilliseconds() {
            return MafiaNet::GetTimeUS() / 1000;
        }

        /**
         * The three vanilla documents that nothing references: not the
         * document reward, requirement or skill tables, no inventory preset,
         * no level placement, no Lua and no quest graph (UseBookTrigger names
         * books by GUID). Reading one therefore has no native quest effect.
         */
        const char *CarrierGuid(BookDefinition::Carrier carrier) {
            switch (carrier) {
            case BookDefinition::Carrier::PlainBook: return "3eeff16e-c667-414e-a6a6-9c015cf4e44f";
            case BookDefinition::Carrier::Letter: return "a54ab5ef-d4a2-4929-9045-1a1efde935c5";
            case BookDefinition::Carrier::Book:
            default: return "e1afae05-60d6-4494-9b4d-0f4bd5a90527";
            }
        }

        std::string LabelOf(std::uint32_t id, std::size_t page) {
            return std::string(kLabelPrefix) + std::to_string(id) + "_" + std::to_string(page);
        }

        bool EndsWithNoCase(const char *text, const char *suffix) {
            const std::size_t textLength   = std::strlen(text);
            const std::size_t suffixLength = std::strlen(suffix);
            if (textLength < suffixLength) {
                return false;
            }
            const char *tail = text + (textLength - suffixLength);
            for (std::size_t i = 0; i < suffixLength; ++i) {
                const char character = tail[i] == '\\' ? '/' : tail[i];
                if (std::tolower(static_cast<unsigned char>(character)) != std::tolower(static_cast<unsigned char>(suffix[i]))) {
                    return false;
                }
            }
            return true;
        }

        /** The direct child `<tag key="value">` of `parent`, or an invalid ref. */
        SDK::XmlNodeRef FindChild(const SDK::XmlNodeRef &parent, const char *tag, const char *key, const char *value) {
            const int count = parent->getChildCount();
            for (int index = 0; index < count; ++index) {
                SDK::XmlNodeRef child = parent->getChild(index);
                if (child.IsValid() && child->isTag(tag) && child->haveAttr(key) && std::strcmp(child->getAttr(key), value) == 0) {
                    return child;
                }
            }
            return {};
        }

        /** Reads `<digits>` at `cursor`, advancing it; false when there are none. */
        bool ReadNumber(const char *&cursor, std::uint64_t &value) {
            const char *start = cursor;
            value             = 0;
            while (*cursor >= '0' && *cursor <= '9') {
                value = value * 10U + static_cast<std::uint64_t>(*cursor - '0');
                ++cursor;
            }
            return cursor != start;
        }
    } // namespace

    bool BookService::ExtendGameConfig(const char *file, SDK::XmlNodeRef &document) {
        // InitActionMaps may be handed a profile override path; only the name is fixed.
        const bool profile = EndsWithNoCase(file, "/defaultprofile.xml");
        const bool help    = !profile && EndsWithNoCase(file, "libs/config/defaultactionhelp.xml");
        if (!profile && !help) {
            return false;
        }

        // Cloned because a document out of a pak can be the read-only node,
        // on which every write is silently dropped; an unexpected shape
        // leaves the game's own document in place.
        SDK::XmlNodeRef edited = document->clone(true);
        if (!edited.IsValid()) {
            return false;
        }

        if (profile) {
            // `minigame` is open_pause_menu + help_dialog + minigame_exit.
            // forge_builder_slots, the one minigame whose exit is a tap,
            // includes minigame_exit without open_pause_menu; reading gets
            // the same pair, so Escape no longer feeds the pause menu here.
            SDK::XmlNodeRef map     = FindChild(edited, "actionmap", "name", "reading");
            SDK::XmlNodeRef include = map.IsValid() ? FindChild(map, "include", "actionmap", "minigame") : SDK::XmlNodeRef {};
            if (!include.IsValid()) {
                return false;
            }
            include->setAttr("actionmap", "minigame_exit");
            SDK::XmlNodeRef help = map->newChild("include");
            if (!help.IsValid()) {
                return false;
            }
            help->setAttr("actionmap", "help_dialog");
        }
        else {
            // C_ActionHintManager::OnAction fires a Release hint on key-up
            // unless a hold took the press. The included minigame_exit set
            // keeps its own Hold hint, so a held key still closes the book,
            // exactly as forge_builder_slots' Release + Hold pair does.
            SDK::XmlNodeRef set  = FindChild(edited, "action_help_set", "actionmap", "reading");
            SDK::XmlNodeRef hint = set.IsValid() ? FindChild(set, "hint", "action", "minigame_exit") : SDK::XmlNodeRef {};
            if (!hint.IsValid()) {
                return false;
            }
            hint->setAttr("type", "Release");
        }

        document = edited;
        return true;
    }

    BookService &BookService::Get() {
        static BookService instance;
        return instance;
    }

    std::uint32_t BookService::Open(BookDefinition definition) {
        _lastError.clear();
        if (_session.phase != Phase::Idle) {
            _lastError = "a book is already open";
            return 0;
        }

        auto *human = KCDC::Common::LocalHumanActor();
        if (human == nullptr) {
            _lastError = "there is no local player to read it";
            return 0;
        }

        // The same gate the inventory's own Read action asks first: no
        // reading on horseback, in combat, swimming and the rest.
        auto *player = static_cast<SDK::wh::entitymodule::C_Player *>(human);
        if (!hook::this_call<bool>(SDK::gPatterns.C_Player__CanReadBookHere, player)) {
            _lastError = "the player cannot read here";
            return 0;
        }

        auto *reading = SDK::wh::playermodule::C_Reading::Of(human, true);
        if (reading == nullptr) {
            _lastError = "the game has no reading minigame for the player";
            return 0;
        }
        if (reading->IsActive()) {
            _lastError = "the player is already reading";
            return 0;
        }

        const std::uint64_t carrierWuid = CreateCarrier(definition.carrier);
        if (carrierWuid == 0) {
            return 0;
        }

        const std::uint32_t id = _nextId++;

        // In place before Start: the native path can reach PushContentToUI
        // before it returns, and that detour recognises the carrier from here.
        _session               = {};
        _session.phase         = Phase::Opening;
        _session.id            = id;
        _session.carrierWuid   = carrierWuid;
        _session.reading       = reading;
        _session.requestedAtMs = NowMilliseconds();
        _session.pageCount     = definition.pages.size();
        PublishText(id, std::move(definition.pages));
        _session.definition = std::move(definition);

        // Spawned in hand, the inventory path. It never asks which holder
        // the item is in, and the global WUID lookup resolves world items too.
        reading->Start(SDK::wh::framework::WUID {carrierWuid}, true);

        // Start stores the item only once its own gates passed.
        if (reading->GetBookWuid().value != carrierWuid) {
            DeleteCarrier(carrierWuid);
            PublishText(0, {});
            _session   = {};
            _lastError = "the game refused to open the book";
            return 0;
        }

        Framework::Logging::GetLogger(kLogger)->debug("Opening book {} on carrier WUID {:#x}", id, carrierWuid);
        return id;
    }

    bool BookService::Close() {
        if (_session.phase != Phase::Opening && _session.phase != Phase::Open) {
            return false;
        }
        _session.closeReason = "script";
        // The exit the player's own key takes; ReadingFinished picks it up.
        _session.reading->Finish();
        if (_session.phase != Phase::Closing) {
            // Finish ran without reaching the detour (the session never got
            // as far as binding): end it here rather than leave it open.
            _session.phase = Phase::Closing;
        }
        return true;
    }

    std::uint32_t BookService::GetOpenBookId() const {
        return _session.phase == Phase::Idle ? 0 : _session.id;
    }

    bool BookService::ResolveLabel(const char *label, SDK::CryStringT<char> &out) const {
        if (label == nullptr) {
            return false;
        }
        if (*label == '@') {
            ++label;
        }
        if (std::strncmp(label, kLabelPrefix, kLabelPrefixLen) != 0) {
            return false;
        }

        const char *cursor = label + kLabelPrefixLen;
        std::uint64_t id   = 0;
        std::uint64_t page = 0;
        if (!ReadNumber(cursor, id) || *cursor != '_') {
            return false;
        }
        ++cursor;
        if (!ReadNumber(cursor, page) || *cursor != '\0') {
            return false;
        }

        std::lock_guard lock(_textMutex);
        // A closed book's keys answer blank rather than showing the key.
        if (id != _text.id || page >= _text.pages.size()) {
            out.Assign("");
            return true;
        }
        if (page + 1 < _text.pages.size()) {
            out.Assign((_text.pages[page] + kNewPage).c_str());
        }
        else {
            out.Assign(_text.pages[page].c_str());
        }
        return true;
    }

    bool BookService::PushContent(SDK::wh::entitymodule::C_DocumentItem *document) {
        if (document == nullptr || (_session.phase != Phase::Opening && _session.phase != Phase::Open)) {
            return false;
        }
        // Recognised through the minigame rather than the item: BindAndOpenBook
        // begins the interaction on this document's entity
        // (C_Minigame::BeginInteraction, +0x28) immediately before OpenBook
        // reaches here, and Start stored the carrier as the book it reads.
        const SDK::wh::playermodule::C_Reading *reading = _session.reading;
        if (reading->GetBookWuid().value != _session.carrierWuid || reading->GetInteractionEntityId() != document->GetEntityId()) {
            return false;
        }

        // The carrier's own text must never show, even with no screen bound.
        auto *ui = document->GetBookUI();
        if (ui == nullptr) {
            return true;
        }
        SendLayout(static_cast<SDK::wh::guimodule::C_UIBook *>(ui));
        ApplyTexture(document);
        return true;
    }

    void BookService::ApplyTexture(SDK::wh::entitymodule::C_DocumentItem *document) const {
        if (_session.definition.texture.empty()) {
            return;
        }
        auto *entity   = document->GetEntity();
        auto *proxy    = entity != nullptr ? static_cast<SDK::IEntityRenderProxy *>(entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Render))) : nullptr;
        auto *material = proxy != nullptr ? proxy->GetRenderMaterial(0) : nullptr;
        if (material == nullptr) {
            Framework::Logging::GetLogger(kLogger)->warn("Book {} has no render material to retexture", _session.id);
            return;
        }

        // The sub-material the book screen's binder (0x18073D68C) picks --
        // the first one not named `proxy` -- and has by now cloned in place
        // into this entity's own material to render the page into. Writing
        // the same clone keeps the swap exactly as private as the page, and
        // it goes with the carrier when the carrier is deleted.
        for (int index = 0; index < material->GetSubMtlCount(); ++index) {
            auto *sub = material->GetSubMtl(index);
            if (sub == nullptr || std::strcmp(sub->GetName(), kProxyMaterial) == 0) {
                continue;
            }
            const SDK::ScopedCryString texture(_session.definition.texture.c_str());
            sub->SetTexture(texture, kDiffuseSlot);
            return;
        }
        Framework::Logging::GetLogger(kLogger)->warn("Book {} found no page material to retexture", _session.id);
    }

    bool BookService::SetLegibility(float legibility) {
        if (_session.phase != Phase::Open) {
            return false;
        }
        _session.definition.legibility = legibility < 0.0F ? 0.0F : (legibility > 1.0F ? 1.0F : legibility);

        auto *carrier  = FindCarrier();
        auto *pickable = carrier != nullptr ? carrier->GetPickableItem() : nullptr;
        if (pickable == nullptr) {
            return false;
        }
        // The carrier's pickable is the document: Book.lua's class is
        // DocumentItem, and C_Reading only binds pickables of kind 2.
        auto *document = static_cast<SDK::wh::entitymodule::C_DocumentItem *>(pickable);
        auto *ui       = document->GetBookUI();
        if (ui == nullptr) {
            return false;
        }
        SendLayout(static_cast<SDK::wh::guimodule::C_UIBook *>(ui));
        return true;
    }

    void BookService::SendLayout(SDK::wh::guimodule::C_UIBook *screen) const {
        // What C_UIBook::SetFullBook does with a descriptor's content, with
        // keys this service answers instead of a localization file's.
        SDK::ScopedUIArguments texts;
        for (std::size_t page = 0; page < _session.pageCount; ++page) {
            texts.AppendString(("@" + LabelOf(_session.id, page)).c_str());
        }
        SDK::ScopedUIArguments images;
        for (const BookImage &image : _session.definition.images) {
            images.AppendUInt(image.page);
            images.AppendString(image.image.c_str());
        }

        const SDK::ScopedCryString textsName("Texts");
        const SDK::ScopedCryString imagesName("Images");
        const SDK::ScopedCryString function("SetFullBook");
        screen->SetFlashArguments(textsName, texts.Get());
        screen->SetFlashArguments(imagesName, images.Get());

        // The script's own legibility, never the reader's reading skill.
        const float dyslexiaLevel = _session.definition.legibility;
        const int documentType    = _session.definition.documentType;
        const int visualCategory  = _session.definition.visualCategory;
        screen->CallFlashFunction(function, dyslexiaLevel, documentType, visualCategory);
    }

    bool BookService::IsCarrier(const SDK::wh::entitymodule::C_Item *item) const {
        return item != nullptr && _session.phase != Phase::Idle && item->GetId() == _session.carrierWuid;
    }

    bool BookService::IsReadingCarrier(const SDK::wh::playermodule::C_Reading *reading) const {
        // Still valid in Closing: RequestExit runs Finish first, and the
        // session is only dropped on the next sweep.
        return reading != nullptr && _session.phase != Phase::Idle && reading == _session.reading && reading->GetBookWuid().value == _session.carrierWuid;
    }

    void BookService::ReadingFinished(SDK::wh::playermodule::C_Reading *reading) {
        if (reading == nullptr || reading != _session.reading || (_session.phase != Phase::Opening && _session.phase != Phase::Open)) {
            return;
        }
        _session.phase = Phase::Closing;
    }

    void BookService::Update() {
        switch (_session.phase) {
        case Phase::Opening:
            if (_session.reading->IsActive()) {
                _session.phase = Phase::Open;
                KCDC::Scripting::Events::BookOpened(_session.id);
            }
            else if (NowMilliseconds() - _session.requestedAtMs > kOpenTimeoutMs) {
                Framework::Logging::GetLogger(kLogger)->warn("Book {} never reached the player's hands; giving up", _session.id);
                _session.closeReason = "failed";
                _session.reading->Finish();
                EndSession("failed");
            }
            break;
        case Phase::Closing: EndSession(_session.closeReason); break;
        default: break;
        }
    }

    void BookService::Reset() {
        if (_session.phase == Phase::Idle) {
            return;
        }
        // Only finish a minigame that still belongs to a live local body.
        auto *human = KCDC::Common::LocalHumanActor();
        if ((_session.phase == Phase::Opening || _session.phase == Phase::Open) && human != nullptr && SDK::wh::playermodule::C_Reading::Of(human, false) == _session.reading) {
            _session.reading->Finish();
        }
        EndSession("sessionOver");
    }

    void BookService::NativeContextChanged() {
        if (_session.phase == Phase::Idle) {
            return;
        }
        // The level the minigame and the carrier lived in is going; neither
        // pointer is safe to touch any more, and the world inventory lookup
        // in EndSession simply finds nothing left to delete.
        _session.reading = nullptr;
        EndSession("sessionOver");
    }

    SDK::wh::entitymodule::C_Item *BookService::FindCarrier() const {
        auto *world = KCDC::Features::GroundItem::WorldInventory();
        if (world == nullptr || _session.carrierWuid == 0) {
            return nullptr;
        }
        auto *item = world->FindItemById(_session.carrierWuid);
        return item != nullptr ? item : world->FindOwnedItemById(_session.carrierWuid);
    }

    std::uint64_t BookService::CreateCarrier(BookDefinition::Carrier carrier) {
        auto *world = KCDC::Features::GroundItem::WorldInventory();
        if (world == nullptr || KCDC::Core::gApplication == nullptr) {
            _lastError = "world inventory is not available";
            return 0;
        }

        SDK::wh::entitymodule::S_ItemClassGUID itemClass {};
        if (!KCDC::Shared::Game::CryGuidFromText(CarrierGuid(carrier), itemClass.m_low, itemClass.m_high)) {
            _lastError = "the carrier class is malformed";
            return 0;
        }

        // Identified by difference, as ground items are: CreateItems hands
        // back a descriptor that does not say which stack it became.
        std::unordered_set<std::uint64_t> before;
        before.reserve(world->GetItemCount());
        for (std::size_t i = 0; i < world->GetItemCount(); ++i) {
            if (auto *native = world->GetItemAt(i)) {
                before.insert(native->GetId());
            }
        }

        // Projected, so player inventory sync does not read it as a change
        // the player made.
        {
            KCDC::Features::Player::PlayerInventoryService::AuthorityProjection applying(KCDC::Core::gApplication->GetPlayerInventoryService());
            world->CreateItems(itemClass, 1, false);
        }

        for (std::size_t i = 0; i < world->GetItemCount(); ++i) {
            auto *candidate = world->GetItemAt(i);
            if (candidate == nullptr || before.contains(candidate->GetId())) {
                continue;
            }
            const SDK::wh::entitymodule::S_ItemClassGUID created = candidate->InvokeGetItemClassRaw();
            if (created.m_low == itemClass.m_low && created.m_high == itemClass.m_high) {
                return candidate->GetId();
            }
        }

        _lastError = "the carrier item could not be created";
        return 0;
    }

    void BookService::DeleteCarrier(std::uint64_t carrierWuid) const {
        auto *world = KCDC::Features::GroundItem::WorldInventory();
        if (world == nullptr || carrierWuid == 0 || KCDC::Core::gApplication == nullptr) {
            return;
        }
        auto *item = world->FindItemById(carrierWuid);
        if (item == nullptr) {
            item = world->FindOwnedItemById(carrierWuid);
        }
        if (item == nullptr) {
            return;
        }

        KCDC::Features::Player::PlayerInventoryService::AuthorityProjection applying(KCDC::Core::gApplication->GetPlayerInventoryService());
        if (item->GetPickableItem() != nullptr) {
            item->UnbindPickable();
        }
        world->RemoveItem(item);
    }

    void BookService::EndSession(const char *reason) {
        const std::uint32_t id          = _session.id;
        const std::uint64_t carrierWuid = _session.carrierWuid;
        _session                        = {};
        PublishText(0, {});
        DeleteCarrier(carrierWuid);
        Framework::Logging::GetLogger(kLogger)->debug("Book {} closed ({})", id, reason);
        // Last: a handler may open the next book straight away.
        KCDC::Scripting::Events::BookClosed(id, reason);
    }

    void BookService::PublishText(std::uint32_t id, std::vector<std::string> pages) {
        std::lock_guard lock(_textMutex);
        _text.id    = id;
        _text.pages = std::move(pages);
    }
} // namespace KCDC::Features::Book
