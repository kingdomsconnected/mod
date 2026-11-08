#pragma once

#include "../../cryengine/entitysystem/i_entity.h"
#include "../../patterns.h"
#include "../../types.h"
#include "../entitymodule/e_actor_gender.h"
#include "c_buff_instance.h"
#include "c_combat_soul.h"
#include "c_inventory_soul.h"
#include "c_soul_property_notifier.h"
#include "c_soul_state_storage.h"
#include "e_soul_derived_stat.h"
#include "s_character_body_description.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Actor;
}

namespace SDK::wh::rpgmodule {
    class C_SoulGroup;

    struct S_SoulQueryContext;

    /**
     * Primary soul interface; the game owns and destroys every soul.
     *
     * The named slots are each proven against a call site rather than
     * counted off the table: `GetName` is what `C_SoulUiNameOverride` logs a
     * soul by, `GetUIName` is what the actor-soul-name flow node reads, and
     * `GetDerivedStat` is the `+0x2C8` call the `RelativeCarriedWeight`
     * evaluator at `0x18113D800` makes twice in three instructions. `GetState`
     * is the `+0x270` call in `HasShootingStamina` at `0x1815F5F80`.
     *
     * Deliberately no virtual destructor: the native table has no destructor
     * slot before these, and one would shift every index that follows.
     */
    class I_Soul {
      public:
        /**
         * The address of this soul's WUID.
         *
         * Slot 0, and a two-instruction `lea rax, [rcx+30h] / ret` -- it hands
         * back a pointer into the soul rather than the value. Every native
         * lookup that keys on a soul passes this, including the dog activity
         * resolver at `0x1808ADD68`.
         */
        virtual const std::uint64_t *GetWuidAddress() = 0; // slot 0
        virtual void unk_fn0001()                     = 0;
        virtual void unk_fn0002()                     = 0;
        /**
         * The soul's internal name -- the identity the soul row carries, not
         * the one the UI shows. `C_Soul`'s own constructor seeds it with
         * "<not-initialized-soul>", and the engine logs souls by it.
         */
        virtual const char *GetName() const   = 0; // slot 3
        virtual void unk_fn0004()             = 0;
        virtual void unk_fn0005()             = 0;
        virtual bool IsDead() const           = 0; // slot 6
        virtual bool IsUnconscious() const    = 0; // slot 7
        virtual void unk_fn0008()             = 0;
        virtual void unk_fn0009()             = 0;
        virtual void unk_fn0010()             = 0;
        virtual bool IsMounted() const         = 0; // slot 11, 0x18046D3F8
        virtual bool IsSleeping() const       = 0; // slot 12
        virtual void unk_fn0013()             = 0;
        virtual void unk_fn0014()             = 0;
        virtual void unk_fn0015()             = 0;
        virtual void unk_fn0016()             = 0;
        virtual void unk_fn0017()             = 0;
        virtual void unk_fn0018()             = 0;
        virtual void unk_fn0019()             = 0;
        virtual void unk_fn0020()             = 0;
        virtual void unk_fn0021()             = 0;
        virtual void unk_fn0022()             = 0;
        virtual void unk_fn0023()             = 0;
        virtual void unk_fn0024()             = 0;
        virtual void unk_fn0025()             = 0;
        virtual void unk_fn0026()             = 0;
        virtual void unk_fn0027()             = 0;
        virtual void unk_fn0028()             = 0;
        virtual void unk_fn0029()             = 0;
        virtual void unk_fn0030()             = 0;
        virtual void unk_fn0031()             = 0;
        virtual void unk_fn0032()             = 0;
        virtual void unk_fn0033()             = 0;
        virtual void unk_fn0034()             = 0;
        virtual void unk_fn0035()             = 0;
        virtual void unk_fn0036()             = 0;
        virtual void unk_fn0037()             = 0;
        virtual void unk_fn0038()             = 0;
        /** The archetype row this soul runs on; the source of its race and gender. */
        virtual int GetArchetypeId() const = 0; // slot 39

        // `archetype+0x18` and `+0x1C`. MakeLookAsSoul refuses to copy an
        // appearance across a mismatch in either.
        virtual wh::entitymodule::E_Race GetRace() const                 = 0; // slot 40
        virtual wh::entitymodule::E_ActorGender GetGender() const        = 0; // slot 41
        virtual void unk_fn0042()             = 0;
        virtual void unk_fn0043()             = 0;
        virtual void unk_fn0044()             = 0;
        virtual void unk_fn0045()             = 0;
        virtual void unk_fn0046()             = 0;
        virtual void unk_fn0047()             = 0;
        virtual void unk_fn0048()             = 0;
        virtual void unk_fn0049()             = 0;
        virtual void unk_fn0050()             = 0;
        virtual void unk_fn0051()             = 0;
        virtual void unk_fn0052()             = 0;
        virtual void unk_fn0053()             = 0;
        virtual void unk_fn0054()             = 0;
        virtual void unk_fn0055()             = 0;
        virtual void unk_fn0056()             = 0;
        virtual I_CombatSoul *GetCombatSoul() = 0; // slot57, CArrow collision
        virtual void unk_fn0058()             = 0;
        virtual void unk_fn0059()             = 0;
        virtual void unk_fn0060()             = 0;
        virtual void unk_fn0061()             = 0;
        virtual void unk_fn0062()             = 0;
        virtual void unk_fn0063()             = 0;
        virtual void unk_fn0064()             = 0;
        virtual void unk_fn0065()             = 0;
        virtual void unk_fn0066()             = 0;
        /**
         * The soul's display name, as a **localization key** -- see `SetUIName`.
         *
         * Answered from the soul registry's UI-name override map first (keyed
         * on the WUID, written by `C_SoulUiNameOverride` and
         * `storm::C_SetUiName`), and from `m_uiName` when that has nothing.
         * Borrowed: valid only until the name is next written.
         */
        virtual const char *GetUIName() const = 0; // slot 67
        virtual void unk_fn0068()             = 0;
        virtual void unk_fn0069()             = 0;
        /**
         * The GUID of the AI brain this soul asks for.
         *
         * Slot 70, and a two-instruction `lea rax, [rcx+3A0h] / ret` -- the
         * address of a 16-byte field, not a copy. `C_NPCManager`'s record
         * initialisation (`0x180D48678`) reads it, looks the brain template up
         * by it, and falls back to a bare `Default` brain when the lookup
         * misses. A soul whose brain does not name the `DogCompanion`
         * subbrain therefore produces a dog that cannot think.
         */
        virtual const CryGUID *GetBrainGuid()                                                      = 0; // slot 70
        virtual void unk_fn0071()                                                                  = 0;
        virtual void unk_fn0072()                                                                  = 0;
        virtual void unk_fn0073()                                                                  = 0;
        virtual void unk_fn0074()                                                                  = 0;
        virtual void unk_fn0075()                                                                  = 0;
        virtual C_SoulPropertyNotifier *GetPropertyEvents()                                        = 0; // slot 76
        virtual void unk_fn0077()                                                                  = 0;
        virtual float GetState(E_SoulState state) const                                            = 0; // slot 78, HasShootingStamina
        virtual void unk_fn0079()                                                                  = 0;
        virtual float GetStateMax(E_SoulState state) const                                         = 0; // slot 80
        virtual void unk_fn0081()                                                                  = 0;
        // Raises a stat to `level` through `AddXPFromToStatLevel` when it is lower; never lowers. RTTR names both. Builds an unscaled `C_StatXPEffect`.
        virtual void AdvanceToStatLevel(std::uint32_t stat, std::uint32_t level)                  = 0; // slot 82, 0x180FA2F3C
        virtual void unk_fn0083()                                                                  = 0;
        virtual void AddXPFromToStatLevel(std::uint32_t stat, std::uint32_t from, std::uint32_t to) = 0; // slot 84, 0x180FA3000
        virtual float GetStat(std::uint32_t stat, const S_SoulQueryContext *context) const         = 0; // slot 85, raw context
        virtual void unk_fn0086()                                                                  = 0;
        virtual float GetRelativeStat(std::uint32_t stat, const S_SoulQueryContext *context) const = 0; // slot 87
        virtual void unk_fn0088()                                                                  = 0;
        // Slot 89 takes a raw nullable context. Slot 91 unwraps a shared_ptr.
        virtual float GetDerivedStat(E_SoulDerivedStat::Type stat, const S_SoulQueryContext *context) const = 0; // slot 89
        virtual void unk_fn0090()                                                                           = 0;
        virtual void unk_fn0091()                                                                           = 0;
        virtual void unk_fn0092()                                                                           = 0;
        virtual void unk_fn0093()                                                                           = 0;
        virtual void unk_fn0094()                                                                           = 0;
        virtual void unk_fn0095()                                                                           = 0;
        virtual void unk_fn0096()                                                                           = 0;
        virtual void unk_fn0097()                                                                           = 0;

        // Modifier kinds are distinct from E_SoulDerivedStat values.
        virtual float ApplyModifiers(std::uint32_t kind, float base, const void *context) const      = 0; // slot 98
        virtual void unk_fn0099()                                                                    = 0;
        // The skill counterparts of slots 82 and 84: the XP between two levels on the curve, as one unscaled `C_SkillXPEffect`.
        virtual void AdvanceToSkillLevel(std::uint32_t skill, std::uint32_t level)                  = 0; // slot 100, 0x181684828
        virtual void AddXPFromToSkillLevel(std::uint32_t skill, std::uint32_t from, std::uint32_t to) = 0; // slot 101, 0x18168484C
        virtual float GetSkill(std::uint32_t skill, const S_SoulQueryContext *context) const         = 0; // slot 102
        virtual void unk_fn0103()                                                                    = 0;
        virtual float GetRelativeSkill(std::uint32_t skill, const S_SoulQueryContext *context) const = 0; // slot 104
        virtual void unk_fn0105()                                                                    = 0;
        virtual void unk_fn0106()                                                                    = 0;
        virtual bool HasAbility(std::uint32_t ability) const                                         = 0; // slot107, alchemy IDs19/30/63/92, 0x1809DCB78
        virtual void unk_fn0108()                                                                    = 0;
        virtual void unk_fn0109()                                                                    = 0;
        virtual void unk_fn0110()                                                                    = 0;

        /**
         * Whether any buff on this soul carries an AI tag.
         *
         * The cheapest condition probe the game has: it walks the active buff
         * index with no table lookup at all. The 23 tags in `buff_ai_tag.xml`
         * -- `poison`, `bleed`, `alcohol_drunk`, `unconscious` -- are what the
         * game's own AI reasons in. A tag id of -1 answers false.
         */
        virtual bool HasBuffWithAITag(int aiTagId) = 0; // slot 111

        virtual void unk_fn0112() = 0;
        virtual void unk_fn0113() = 0;
        virtual void unk_fn0114() = 0;
        /**
         * Whether the soul owns a perk: a walk of `C_PerkList`'s active vector
         * (`C_Soul+0x798`), so a puppet whose list nobody filled answers false.
         * Answers true for every perk while a global override at
         * `*(g_RPGModule+0xA8)+0x60` is set. `notifyUse` raises the perk-used
         * HUD message when the answer is yes.
         */
        virtual bool HasPerk(const CryGUID &perk, bool notifyUse) = 0; // slot 115, 0x1804D2760
        virtual void unk_fn0116() = 0;
        virtual void unk_fn0117() = 0;

        /**
         * The group this soul leads: its companions, and who it follows.
         *
         * Slot 118 (`+0x3B0`). `C_ScriptBindPlayer::SetPlayerDog`
         * (`0x182AC2C24`), `C_SetCompanion` (`0x1817759E4`) and
         * `GetLocalPlayerDogSoul` (`0x180A1F424`) all reach a soul group
         * through it and then possess, release or enumerate companions.
         */
        virtual C_SoulGroup *GetSoulGroup() = 0; // slot 118

        virtual void unk_fn0119() = 0;
        virtual void unk_fn0120() = 0;
        virtual void unk_fn0121() = 0;
        /**
         * The soul of the horse this soul is riding, or null.
         *
         * Slot 122 (0x18046E768). **The engine's own answer to "what am I
         * on"**, and the way the game's own HUD finds it: the bar feed at
         * 0x18046EC78 reaches the animal through `playerSoul->
         * GetRiddenHorseSoul()` and reads `GetRelativeState(.., Stamina)` off
         * it. Two pointer hops, against a search of every replicated horse
         * comparing entity ids -- so prefer it to any loop that is really
         * asking this question.
         *
         * Pair it with `GetActor(0)` to reach the body, and with
         * `IsMounted` when only the yes/no is wanted.
         */
        virtual C_Soul *GetRiddenHorseSoul() const = 0; // slot 122
        virtual void unk_fn0123() = 0;
        virtual void unk_fn0124() = 0;
        virtual void unk_fn0125() = 0;
        virtual void unk_fn0126() = 0;
        virtual void unk_fn0127() = 0;
        /**
         * The body wearing this soul, from one of its two actor handles.
         *
         * Slot 128 (0x180646A94): reads `C_Soul + 0xCE0 + index * 8`, takes
         * that handle's entity id and resolves it through the actor system.
         * `index` must be 0 or 1 and anything else answers null; every caller
         * inspected passes 0.
         */
        virtual wh::entitymodule::C_Actor *GetActor(std::uint8_t index) const = 0; // slot 128
        virtual void unk_fn0129() = 0;
        virtual void unk_fn0130() = 0;
        virtual void unk_fn0131() = 0;
        virtual void unk_fn0132() = 0;
        virtual void unk_fn0133() = 0;
        virtual void unk_fn0134() = 0;
        virtual void unk_fn0135() = 0;
        virtual void unk_fn0136() = 0;
        virtual std::uint8_t AddActorHandle(SDK::IEntity *entity) = 0; // slot 137
        virtual void unk_fn0138() = 0;
        virtual void unk_fn0139() = 0;
        virtual void unk_fn0140() = 0;
        virtual void unk_fn0141() = 0;

        /**
         * Rebuilds the character from the body description as it now
         * resolves: `GetActor(0)` then `C_Actor::RebuildClothing`. A write
         * through slot 163 shows up only once this runs; slot 146 runs it
         * itself.
         */
        virtual void RefreshAppearance() = 0; // slot 142

        /** Slot 143: applies the butchered appearance; loot generation is separate. */
        virtual bool Butcher()              = 0;
        virtual bool CanBeButchered() const = 0; // slot 144
        virtual void unk_fn0145() = 0;

        /**
         * Writes one part of one layer and rebuilds the character.
         *
         * `layer` is one of `S_CharacterBodyDescription::kLayer*`, and only
         * `kLayerBarber` and `kLayerLookAs` are coherent: the read picks
         * look-as for 2 and barber for everything else, while the write
         * goes through `SetBodyDescription`, so `kLayerBase` reads the
         * barber layer and writes the base, and anything above 2 reads and
         * writes nothing. `C_Barber::TryHair` is this with part 2 and
         * `kLayerBarber`.
         */
        virtual void SetBodyDescriptionPart(std::uint8_t part, const CryStringT<char> &name, std::uint32_t layer) = 0; // slot 146

        /**
         * Empties one part of the look-as layer, so reads fall through to
         * the barber layer and then to the base. Will not allocate that
         * layer, so on a soul it has never been written to this is a silent
         * no-op.
         */
        virtual void ClearOverrideBodyDescriptionPart(std::uint8_t part) = 0; // slot 147

        /**
         * Whether this soul has a look-as layer *and* resolves `part` to
         * `name`. False outright when the layer is absent, so it cannot tell
         * "not set" from "not ours".
         */
        virtual bool IsOverriddenBodyDescriptionPart(std::uint8_t part, const CryStringT<char> &name) = 0; // slot 148

        virtual void unk_fn0149() = 0;
        virtual void unk_fn0150() = 0;
        virtual void unk_fn0151() = 0;
        virtual void unk_fn0152() = 0;
        virtual void unk_fn0153() = 0;
        virtual void unk_fn0154() = 0;
        virtual void unk_fn0155() = 0;
        virtual void unk_fn0156() = 0;
        virtual void unk_fn0157() = 0;
        virtual void unk_fn0158() = 0;
        virtual void unk_fn0159() = 0;
        virtual void unk_fn0160() = 0;
        virtual void unk_fn0161() = 0;

        /**
         * Fills `out` with the five parts as they currently resolve.
         *
         * A void taking the out pointer because that is the ABI of the
         * member returning by value that it is: `this` in RCX, the hidden
         * return pointer in RDX.
         *
         * Every string it writes is on the game's heap and only the engine's
         * destructor may free them, which is why nothing calls this --
         * applying an appearance goes through slot 146 instead.
         */
        virtual void GetBodyDescription(S_CharacterBodyDescription *out) = 0; // slot 162

        /**
         * Replaces a whole layer's five parts in one write, allocating the
         * override layer if needed. A layer outside `kLayerBase`..
         * `kLayerLookAs` returns silently, and `int` rather than slot 146's
         * `unsigned` because the natives disagree.
         *
         * Does not rebuild the character. `C_Actor::MakeLookAsSoul` is slot
         * 162 into this with `kLayerLookAs`, then `RebuildClothing`.
         */
        virtual void SetBodyDescription(const S_CharacterBodyDescription &description, int layer) = 0; // slot 163

        /**
         * Whether the soul can eat an item of that class now: resolves the
         * class and asks the gameplay evaluator (operation 7). The check both
         * `soul:CanEatItem` and `soul:EatItem` make before consuming
         * (`C_Soul::CanEat` 0x182D25DF4; RTTR names this slot `CanEat`).
         */
        virtual bool CanEat(const CryGUID &itemClass) const = 0; // slot 164, +0x520
        virtual void unk_fn0165() = 0;
        /**
         * The engine's own way to move a soul state, and **not an assignment**.
         *
         * `C_Soul::SetState` (0x1809DC648, slot 166 / +0x530) clamps the value
         * to `GetStateMax`, computes the delta against the stored state, and
         * then **submits an event** carrying that delta to the RPG event bus;
         * the bus writes the number and runs the death and cause bookkeeping
         * synchronously in ProcessEvent (0x1809DB51C), before queuing the event
         * for listener delivery. Consecutive calls therefore see the previous
         * call's applied value. Nothing is stored by this function itself.
         *
         * So this is the call for a body whose own engine is meant to react --
         * a horse, an animal, an NPC. Poking `C_SoulStateStorage`'s
         * `SetReplicatedState` instead is the **player-puppet** path, which
         * exists precisely because a puppet must *not* run that bookkeeping;
         * see the note on that method.
         *
         * Health is state 0 and takes its own cause; every other state shares
         * one. RTTR registers the identifier `SetState` against this slot.
         *
         * **A Health write of exactly zero does nothing**: the function opens
         * with `if (state || |value| > 1e-5)`. Anything below zero passes and
         * is clamped to zero, so a kill through here is a write of -1.
         */
        virtual void SetState(E_SoulState state, float value) = 0; // slot 166, +0x530
        virtual void unk_fn0167() = 0;
        virtual void unk_fn0168() = 0;
        virtual void unk_fn0169() = 0;
        virtual void unk_fn0170() = 0;
        virtual void unk_fn0171() = 0;
        virtual void unk_fn0172() = 0;
        virtual void unk_fn0173() = 0;
        virtual void unk_fn0174() = 0;
        virtual void unk_fn0175() = 0;

        /**
         * Points the soul at a different `soul_archetype.xml` row, which is
         * the only way to move its race and gender. False for an id the
         * archetype database does not have; otherwise it re-resolves the
         * archetype pointer at `+0xCB8` that slots 40 and 41 read through.
         *
         * Row 0 `NPC` is human and male, row 1 `NPC_Female` human and
         * female.
         */
        virtual bool SetArchetypeId(int archetypeId) = 0; // slot 176

        static constexpr std::uint32_t kReloadAnimationSpeedModifier = 71;

        /**
         * A derived stat read with no query context.
         *
         * Deliberately not an overload of `GetDerivedStat`: MSVC emits
         * same-name virtuals in reverse declaration order, so overloading one
         * in a re-typed engine interface quietly moves its slots.
         */
        float GetDerivedStatValue(E_SoulDerivedStat::Type stat) const {
            return GetDerivedStat(stat, nullptr);
        }

        /**
         * What one `S_CharacterBodyDescription::k*` part resolves to, look-as
         * layer first.
         *
         * The read to use: it borrows a reference into whichever layer
         * answered, where slot 162 copies five CryStrings the caller would
         * then have to destroy through a foreign heap. Valid until the next
         * write to that layer.
         */
        const CryStringT<char> &ResolveBodyDescriptionPart(std::uint8_t part) const;

        /** The same, as a C string; never null, empty when no layer answers. */
        const char *BodyDescriptionPart(std::uint8_t part) const {
            const char *value = ResolveBodyDescriptionPart(part).m_pData;
            return value != nullptr ? value : "";
        }
    };

    static_assert(sizeof(I_Soul) == 0x8, "I_Soul size mismatch");

    /** Complete concrete RPG soul.
     *
     * The constructor at `0x1803F1C7C` proves the whole layout: two vptrs, the
     * soul's WUID at +0x30, its internal name at +0x40, the embedded
     * `C_InventorySoul` at +0x198 and the display name at +0x3E8. Its final
     * member is an eight-byte SRW lock at +0xD18, proving the complete
     * 0xD20-byte allocation extent.
     */
    class C_Soul: public I_Soul {
      public:
        /** The soul's world-unique id: the registry's slot table and the UI-name override map are both keyed on it. */
        std::uint64_t GetWuid() const {
            return m_wuid;
        }

        /**
         * The soul this one is a companion of, or null when it is nobody's.
         *
         * Only meaningful on an animal. `C_SoulGroup::PossessAnimal` refuses
         * an animal that already has one, so this is what a re-possession has
         * to test before it releases.
         */
        I_Soul *GetCompanionMaster() const {
            return m_pCompanionMaster;
        }

        /**
         * Points this soul at a different brain template.
         *
         * The NPC's brain is resolved once, at record initialisation, so
         * writing this afterwards changes nothing by itself -- it exists so a
         * soul whose brain the mod has replaced still reports the brain it is
         * actually running.
         */
        void SetBrainGuid(const CryGUID &brain) {
            m_brain = brain;
        }

        C_InventorySoul *GetInventorySoul() {
            return &m_inventorySoul;
        }
        const C_InventorySoul *GetInventorySoul() const {
            return &m_inventorySoul;
        }

        SDK::IEntity *GetActorEntityHandle(std::uint8_t index) const { return index < 2 ? m_actorEntities[index] : nullptr; }

        /**
         * Sets a soul state directly, the way the engine's own tick does.
         *
         * 0x18046EA68, public and **non-virtual** -- not the slot-166
         * `SetState` above. It clamps the value between the state's floor and
         * `GetStateMax`, then hands it to
         * `C_SoulStateStorage::ApplyState`, which stores it and runs the
         * effect and listener bookkeeping. `cause` may be null.
         *
         * This is what `C_SoulStateStorage::UpdateStamina` (0x180645E74) uses
         * every frame to regenerate stamina, so it is the right writer for a
         * state that simply *is* a number. The virtual one is the right writer
         * for damage, because it raises a cause on the RPG event bus and the
         * bus is what runs death.
         *
         * One trap: **health writes are gated globally.** The first line is
         * `if (state == 0 && <global>) return 0.0;`, so a Health write can be
         * refused outright without saying so.
         */
        float SetStateWithCause(E_SoulState state, float value, const C_SoulStateEffect *cause = nullptr) {
            return hook::this_call<float>(gPatterns.C_Soul__SetStateWithCause, this, state, value, cause);
        }

        C_SoulStateStorage18070F3E8 *GetActiveStateStorage() {
            return &m_activeState;
        }
        const C_SoulStateStorage18070F3E8 *GetActiveStateStorage() const {
            return &m_activeState;
        }

        /**
         * Sets the soul's display name **key**, not its text.
         *
         * `m_uiName` is loaded from `skald_character.ui_name_string_name` and
         * every consumer '@'-prefixes it before resolving, so free text put
         * here renders as "@YourText". Written through the engine's own
         * `CryStringT<char>` assignment, so both heaps stay the game's.
         */
        void SetUIName(const char *name) {
            m_uiName.Assign(name);
        }

        /**
         * Every buff currently attached to this soul.
         *
         * A soul holds two collections and this is the complete one. The
         * owning vector at `+0x548` receives only what `AddBuff` put there,
         * so a buff created through any other path never appears in it; this
         * index is maintained by the instances themselves, and it is what the
         * derived-stat, AI-tag and tick paths all walk.
         *
         * Borrowed and volatile: the tick removes expired instances, so copy
         * what is wanted rather than holding the pointers across a frame.
         */
        const C_BuffInstanceBase *const *ActiveBuffsBegin() const {
            return m_activeBuffs;
        }

        const C_BuffInstanceBase *const *ActiveBuffsEnd() const {
            return m_activeBuffsEnd;
        }

        /**
         * Whether this soul's body takes the player's material audio switch.
         *
         * Bit 4 of the flags word at `+0xCF0`: `UpdatePlayerMaterialAudio`
         * (0x180BE21E8) does nothing at all while it is clear. What else the
         * bit means is not modeled.
         */
        bool HasPlayerMaterialAudio() const {
            return (m_flags0CF0 & 0x10U) != 0;
        }

      private:
        std::byte m_opaque0008[0x28];    // 0x008, second vptr and the two id blocks in front of the WUID
        std::uint64_t m_wuid;            // 0x030
        std::byte m_opaque0038[0x8];     // 0x038
        CryStringT<char> m_name;         // 0x040, "<not-initialized-soul>" until the soul is built
        std::byte m_opaque0048[0x10];    // 0x048, zeroed from a 16-byte constant
        std::byte m_propertyNotifier[sizeof(C_SoulPropertyNotifier)]; // 0x058, the C_SoulPropertyNotifier GetPropertyEvents (slot 76) returns; abstract, so held as storage
        std::uint64_t m_state00A8[8];    // 0x0A8, zeroed by the constructor
        bool m_state00E8;                // 0x0E8
        std::byte m_padding00E9[0x7];
        std::byte m_combatSoul[sizeof(C_CombatSoul)]; // 0x0F0, the C_CombatSoul GetCombatSoul (slot 57) returns; abstract, so held as storage
        C_InventorySoul m_inventorySoul; // 0x198
        void *m_state02E0[4];            // 0x2E0, zeroed by the constructor
        std::byte m_opaque0300[0x10];    // 0x300, zeroed from a 16-byte constant
        std::uint32_t m_state0310;       // 0x310
        std::byte m_padding0314[0x4];
        std::byte m_opaque0318[0x8];     // 0x318
        void *m_state0320[3];            // 0x320, zeroed by 0x1803F1EBC
        C_Soul *m_pSoul0338;             // 0x338, this soul
        std::byte m_opaque0340[0x60];    // 0x340, built by 0x1803F244C

        // 0x3A0, the brain template this soul asks the NPC manager for.
        CryGUID m_brain;

        std::byte m_opaque03B0[0x38];  // 0x3B0
        CryStringT<char> m_uiName;     // 0x3E8, empty until the soul's init step assigns it
        std::byte m_opaque03F0[0x148]; // 0x3F0

        // 0x538, the soul this animal is a companion of, or null. Written by
        // C_SoulGroup::PossessAnimal and cleared by ReleaseAnimal, and the
        // field PossessAnimal refuses a second master on.
        I_Soul *m_pCompanionMaster;

        std::byte m_opaque0540[0x20];              // 0x540

        // The state block 0x18070F31C builds at 0x498: the active soul state
        // and the backup copy it is restored from, with a back-pointer between.
        C_SoulStateStorage18070F3E8 m_activeState; // 0x560
        std::byte m_opaque07C8[0x68];              // 0x7C8
        C_Soul *m_pStateOwner;                     // 0x830, this soul
        std::uint32_t m_state0838;                 // 0x838
        float m_state083C;                         // 0x83C, FLT_MAX until set
        C_SoulStateStorage18070F3E8 m_backupState; // 0x840
        std::byte m_opaque0AA8[0x30];              // 0xAA8, built by 0x181190860 against this soul
        void *m_state0AD8[3];                      // 0xAD8, zeroed by the constructor
        C_Soul *m_pStateBlockOwner;                // 0xAF0, this soul

        // 0xAF8, the first member of the soul's own context object: a sorted
        // vector<C_BuffInstanceBase *> the attach and detach slots maintain.
        const C_BuffInstanceBase **m_activeBuffs;
        const C_BuffInstanceBase **m_activeBuffsEnd;
        const C_BuffInstanceBase **m_activeBuffsCapacity;

        std::byte m_opaque0B10[0x1D0]; // 0xB10
        SDK::IEntity *m_actorEntities[2]; // 0xCE0, AddActorHandle slot 137
        std::uint32_t m_flags0CF0;          // 0xCF0
        std::byte m_opaque0CF4[0x2C];       // 0xCF4

        static void AssertLayout() {
            static_assert(offsetof(C_Soul, m_wuid) == 0x30, "C_Soul::m_wuid offset mismatch");
            static_assert(offsetof(C_Soul, m_name) == 0x40, "C_Soul::m_name offset mismatch");
            static_assert(offsetof(C_Soul, m_inventorySoul) == 0x198, "C_Soul::m_inventorySoul offset mismatch");
            static_assert(offsetof(C_Soul, m_propertyNotifier) == 0x58, "C_Soul::m_propertyNotifier offset mismatch");
            static_assert(offsetof(C_Soul, m_combatSoul) == 0xF0, "C_Soul::m_combatSoul offset mismatch");
            static_assert(offsetof(C_Soul, m_pSoul0338) == 0x338, "C_Soul::m_pSoul0338 offset mismatch");
            static_assert(offsetof(C_Soul, m_pStateOwner) == 0x830, "C_Soul::m_pStateOwner offset mismatch");
            static_assert(offsetof(C_Soul, m_backupState) == 0x840, "C_Soul::m_backupState offset mismatch");
            static_assert(offsetof(C_Soul, m_pStateBlockOwner) == 0xAF0, "C_Soul::m_pStateBlockOwner offset mismatch");
            static_assert(offsetof(C_Soul, m_brain) == 0x3A0, "C_Soul::m_brain offset mismatch");
            static_assert(offsetof(C_Soul, m_uiName) == 0x3E8, "C_Soul::m_uiName offset mismatch");
            static_assert(offsetof(C_Soul, m_pCompanionMaster) == 0x538, "C_Soul::m_pCompanionMaster offset mismatch");
            static_assert(offsetof(C_Soul, m_activeState) == 0x560, "C_Soul::m_activeState offset mismatch");
            static_assert(offsetof(C_Soul, m_activeBuffs) == 0xAF8, "C_Soul::m_activeBuffs offset mismatch");
            static_assert(offsetof(C_Soul, m_actorEntities) == 0xCE0, "C_Soul::m_actorEntities offset mismatch");
            static_assert(offsetof(C_Soul, m_flags0CF0) == 0xCF0, "C_Soul::m_flags0CF0 offset mismatch");
        }
    };

    static_assert(sizeof(C_Soul) == 0xD20, "C_Soul size mismatch");
} // namespace SDK::wh::rpgmodule
