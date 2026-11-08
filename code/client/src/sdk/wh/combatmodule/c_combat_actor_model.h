#pragma once

#include "../framework/wuid.h"
#include "../shared/c_signal.h"
#include "../shared/c_model_property.h"
#include "c_combat_target.h"
#include "e_combat_actor_state_id.h"
#include "e_combat_hand_slot.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    class C_CombatActor;

    /**
     * The combat actor's model: everything about a fight that is state rather
     * than behaviour.
     *
     * Forty-eight `C_ModelProperty` entries, `0x30` apart, built one after the
     * other by the constructor at 0x1810F10CC. Every one of them carries
     * `C_CombatActorModelOwnership`, which is where the name used here comes
     * from -- the model type itself has no RTTI, so treat the *name* as a
     * label and the *layout* as fact. The layout is read straight off the
     * table pointer the constructor writes at each offset, and the value
     * widths off the width of the store into each value slot.
     *
     * What each unnamed field means is a separate question from what it is,
     * and it is not answered here. Six of the ints are constructed with a
     * static default of -1, which is the shape of an id that can be absent --
     * the zone, attack-type and action-type ids the `combat_*` tables are
     * keyed on all have that shape. Which is which is established by watching
     * them move against the game rather than by reading it.
     *
     * Held by C_CombatActor at +0x210.
     */
    class C_CombatActorModel {
      public:
        /** How the fields below are classified for the debug overlay. */
        enum class FieldKind : std::uint8_t {
            Bool,
            Int32,
            UInt32,
            Float,
            State,     // E_CombatActorStateId::Type
            HandSlot,  // entitymodule::E_HandSlot::Type
            Byte,      // E_BlockMode / E_AnimActionMovementState
            Time,      // CTimeValue, as its raw count
            Reference, // C_ModelRefProperty
        };

        /** One row of the model, for code that walks it rather than naming it. */
        struct FieldDescriptor {
            std::uint16_t offset;
            FieldKind kind;
            const char *label;
        };

        // --- Named fields -------------------------------------------------

        /**
         * What this fighter is doing. The one field whose meaning is proven,
         * and the one whose signal carries the old value as well as the new.
         */
        E_CombatActorStateId::Type GetState() const {
            return m_state.GetValue();
        }

        void SetStateWithoutSignal(E_CombatActorStateId::Type state) {
            m_state.SetValueWithoutSignal(state);
        }

        entitymodule::E_HandSlot::Type GetHandSlotA() const {
            return m_handSlot01B0.GetValue();
        }

        /**
         * The weapon in the hand this fighter strikes with, as
         * `FillCombatHitRecord` (0x180725DAC) reads it for a blow's record:
         * the hand-weapon array indexed by hand slot A.
         */
        framework::WUID GetStrikingWeaponId() const {
            const std::uint32_t hand = m_handSlot01B0.GetValue();
            return hand < 2 ? m_handWeaponIds[hand] : framework::WUID {framework::WUID::kInvalid};
        }

        entitymodule::E_HandSlot::Type GetHandSlotB() const {
            return m_handSlot0600.GetValue();
        }

        void SetHandSlotAWithoutSignal(entitymodule::E_HandSlot::Type hand) {
            m_handSlot01B0.SetValueWithoutSignal(hand);
        }

        void SetHandSlotBWithoutSignal(entitymodule::E_HandSlot::Type hand) {
            m_handSlot0600.SetValueWithoutSignal(hand);
        }

        E_BlockMode::Type GetBlockMode() const {
            return m_blockMode.GetValue();
        }

        void SetBlockModeWithoutSignal(E_BlockMode::Type mode) {
            m_blockMode.SetValueWithoutSignal(mode);
        }

        E_AnimActionMovementState::Type GetAnimMovementState() const {
            return m_animMovementState.GetValue();
        }

        void SetAnimMovementStateWithoutSignal(E_AnimActionMovementState::Type state) {
            m_animMovementState.SetValueWithoutSignal(state);
        }

        /**
         * The fighter this one is squared up against, or null.
         *
         * Not a property -- a plain pointer the opponent manager keeps, which
         * is why it is read directly. It decides which way an attack is
         * selected: with an opponent the factory matches its rows against the
         * combat zone, and without one it falls back to a path that only a
         * player-controlled body is allowed down.
         */
        C_CombatActor *GetOpponent() const {
            return m_pOpponent;
        }

        /**
         * Whom this fighter has locked on to, or null. The lock the opponent
         * picker sets; the opponent above follows it once the fighter is at
         * rest.
         */
        IEntity *GetTargetEntity() const {
            return m_pTarget != nullptr ? m_pTarget->GetEntity() : nullptr;
        }

        /**
         * One of the eight combat request bits at +0xB58.
         *
         * Indices read off their call sites in the player's own controller.
         * They are what stop `C_CombatActor::PostUpdate` from ending combat
         * mode once the running action has finished, so a body meaning to hold
         * a guard has to hold the same bit the played body holds.
         */
        enum CombatRequest : std::int32_t {
            COMBAT_REQUEST_MODE_LOCK    = 0, //< Holds combat mode open on its own timer.
            COMBAT_REQUEST_WEAPON_DRAWN = 1,
            COMBAT_REQUEST_ATTACK       = 5, //< Set by BeginAttack, cleared by AbortAttack.
            COMBAT_REQUEST_BLOCK        = 6, //< Set by BeginGuard (not for a fighter with an opponent), cleared by EndGuard.
        };

        /** Sets one request bit, through the engine's own setter so it signals. */
        void SetCombatRequest(CombatRequest request, bool value);

        /**
         * Whether this body's fists -- or its weapon -- are up.
         *
         * The first property in the model, which is why the engine reads it as
         * `(*(*model + 8))(model)` rather than through anything named. It is
         * what `C_CombatActor::RequestFightingStance` eventually sets, and
         * `RequestGuard` refuses to build a guard action while it is false --
         * so a body with the guard set and the stance still on its way up has
         * every field right and nothing to show.
         */
        bool IsFightingStanceUp() const {
            return m_fightingStanceUp.GetValue();
        }

        /**
         * Whether this fighter asked for the counter after its perfect block.
         *
         * `C_CombatActor::RequestRiposte` (0x181899D6C) raises it while the
         * counter window is open; the perfect block it belongs to listens
         * (`OnRiposteRequestChanged`, 0x1819BEFD0) and turns into its own
         * strike-back, and clears it again on its Enter and Exit. Its opponent
         * reads it too: a perfect-block window against a fighter whose counter
         * is coming only opens when this is up, and that window is the
         * defender's slow motion (`C_CombatActor::IsRiposteRequested`, vtable
         * slot 37, 0x18072DA8C).
         */
        bool IsRiposteRequested() const {
            return m_riposteRequested.GetValue();
        }

        /** Raises or drops the counter request through the engine's own setter, so its listeners hear it. */
        void SetRiposteRequested(bool value);

        /**
         * The `combat_action_type` row of whatever the fighter is doing, as
         * `EnterCombatMode` and `LeaveCombatMode` set it.
         */
        std::int32_t GetCombatActionType() const {
            return m_combatActionType.GetValue();
        }

        /**
         * The direction of the combat star this fighter is aiming at.
         *
         * A row id of `wh::combatmodule::C_CombatZoneDatabase`, -1 when the
         * fighter has none. Both the attack factory and the guard factory
         * read it when they pick a fragment, so it is the field that decides
         * which way a swing comes from and which way an arm guard is held --
         * which is why a replicated fighter has to carry it.
         *
         * Written by `C_CombatActor::SetCombatZone`, never here: the engine's
         * setter fires the signal the animation side listens on.
         */
        std::int32_t GetCombatZone() const {
            return m_combatZone.GetValue();
        }

        /** The zone before the current one, as SetCombatZone mirrors it. */
        std::int32_t GetPreviousCombatZone() const {
            return m_previousCombatZone.GetValue();
        }

        /**
         * The `combat_input_class` row the fighter's current attack came from,
         * -1 when it is not attacking, and whether that input is still held.
         */
        std::int32_t GetAttackInputClass() const {
            return m_attackInputClass.GetValue();
        }

        bool IsAttackInputActive() const {
            return m_attackInputActive.GetValue();
        }

        /**
         * The guard every subsystem is asking for, resolved to the highest.
         *
         * Read to tell an arm guard that is up from one that is down without
         * subscribing to anything; write through
         * `C_CombatActor::RequestGuard`, which is what turns a change into a
         * guard action.
         */
        const shared::C_ModelMaxProperty &GetGuardRequest() const {
            return m_guardRequest;
        }

        /**
         * Whether the guard is reading to strike: the guard action's
         * `RefreshGuardState` (0x180939CD8) sets it as it turns a held attack
         * input into ReadingToStrike from a row for the requested zone
         * (+0x180), and clears it otherwise and in its Exit. While it is set,
         * as in the guard, `RequestCombatZone` applies a zone at once.
         */
        bool IsGuardReadingToStrike() const {
            return m_guardReadingToStrike.GetValue();
        }

        /** The side the guard is held on, as `TryApplyCombatZone` (0x180936A28) writes it with each zone it applies. */
        std::int32_t GetGuardSide() const {
            return m_int00C0.GetValue();
        }

        /** Whatever the model's opponent reference currently points at. */
        const void *GetReferenceTarget(std::size_t index) const;

        // --- The six id-shaped ints ---------------------------------------
        //
        // Constructed with a static default of -1 rather than 0, which is what
        // an id that can be absent looks like. Exposed as one indexed group
        // because nothing yet distinguishes them from each other.

        static constexpr std::size_t kIdFieldCount = 6;

        std::int32_t GetIdField(std::size_t index) const;
        void SetIdFieldWithoutSignal(std::size_t index, std::int32_t value);

        // --- Generic access -----------------------------------------------

        /** Every field, in layout order. */
        static const FieldDescriptor *Fields();
        static std::size_t FieldCount();

        /** Reads one field as a 64-bit pattern, whatever its declared width. */
        std::uint64_t ReadFieldBits(const FieldDescriptor &field) const;

      private:
        /**
         * C_ModelArrayProperty<int, E_HandSlot, 2> with a custom default, as
         * 0x1810F0894 builds it: its table, one int per hand, a zeroed int and
         * slot, then the per-element and array signals. 0x48 apart in the
         * model, so +0x40 is never written by the constructor.
         */
        struct S_HandSlotIntArrayProperty {
            const void *m_pVTable;            // 0x00
            std::int32_t m_values[2];         // 0x08
            std::uint32_t m_value10;          // 0x10, 0 by default
            std::byte m_padding14[0x4];       // 0x14
            std::uint64_t m_value18;          // 0x18, 0 by default
            shared::C_Signal m_elementSignal; // 0x20, dispatch 0x185666960
            shared::C_Signal m_signal;        // 0x30, dispatch 0x185666A00
            std::byte m_padding40[0x8];       // 0x40
        };

        shared::C_ModelProperty<bool> m_fightingStanceUp;                             // 0x000
        shared::C_ModelProperty<E_CombatActorStateId::Type> m_state;                  // 0x030, default Idle
        shared::C_ModelProperty<std::int32_t> m_combatActionType;                     // 0x060, custom default
        shared::C_ModelProperty<std::int32_t> m_int0090;                              // 0x090, custom default
        shared::C_ModelProperty<std::int32_t> m_int00C0;                              // 0x0C0, custom default
        shared::C_ModelProperty<std::int32_t> m_combatZone;                           // 0x0F0, custom default
        shared::C_ModelProperty<std::int32_t> m_id0120;                               // 0x120, default -1
        shared::C_ModelProperty<std::int32_t> m_int0150;                              // 0x150, custom default
        shared::C_ModelProperty<std::int32_t> m_previousCombatZone;                   // 0x180, default -1
        shared::C_ModelProperty<entitymodule::E_HandSlot::Type> m_handSlot01B0;       // 0x1B0
        shared::C_ModelProperty<float> m_float01E0;                                   // 0x1E0
        shared::C_ModelProperty<std::int32_t> m_id0210;                               // 0x210, default -1
        shared::C_ModelProperty<std::int32_t> m_attackInputClass;                     // 0x240, default -1
        shared::C_ModelProperty<std::int32_t> m_id0270;                               // 0x270, default -1
        shared::C_ModelProperty<bool> m_attackInputActive;                            // 0x2A0
        shared::C_ModelProperty<bool> m_guardReadingToStrike;                         // 0x2D0
        shared::C_ModelProperty<bool> m_bool0300;                                     // 0x300
        shared::C_ModelProperty<bool> m_bool0330;                                     // 0x330
        shared::C_ModelProperty<bool> m_bool0360;                                     // 0x360
        shared::C_ModelProperty<bool> m_riposteRequested;                             // 0x390
        shared::C_ModelProperty<bool> m_bool03C0;                                     // 0x3C0
        shared::C_ModelProperty<float> m_float03F0;                                   // 0x3F0, custom default
        shared::C_ModelProperty<float> m_float0420;                                   // 0x420, custom default
        shared::C_ModelProperty<float> m_float0450;                                   // 0x450, custom default
        shared::C_ModelProperty<bool> m_bool0480;                                     // 0x480
        shared::C_ModelProperty<bool> m_bool04B0;                                     // 0x4B0
        shared::C_ModelProperty<bool> m_bool04E0;                                     // 0x4E0
        shared::C_ModelRefProperty m_ref0510;                                         // 0x510
        shared::C_ModelProperty<bool> m_bool0540;                                     // 0x540
        shared::C_ModelProperty<float> m_float0570;                                   // 0x570
        shared::C_ModelProperty<bool> m_bool05A0;                                     // 0x5A0
        shared::C_ModelProperty<std::int32_t> m_id05D0;                               // 0x5D0, default -1
        shared::C_ModelProperty<entitymodule::E_HandSlot::Type> m_handSlot0600;       // 0x600
        shared::C_ModelMaxProperty m_guardRequest;                                    // 0x630
        shared::C_ModelProperty<E_BlockMode::Type> m_blockMode;                       // 0x650
        shared::C_ModelProperty<bool> m_bool0680;                                     // 0x680
        shared::C_ModelRefProperty m_ref06B0;                                         // 0x6B0
        shared::C_ModelProperty<std::int64_t> m_time06E0;                             // 0x6E0, CTimeValue
        shared::C_ModelProperty<std::int64_t> m_time0710;                             // 0x710, CTimeValue
        shared::C_ModelRefProperty m_ref0740;                                         // 0x740
        shared::C_ModelRefProperty m_ref0770;                                         // 0x770
        shared::C_ModelRefProperty m_ref07A0;                                         // 0x7A0
        shared::C_ModelRefProperty m_ref07D0;                                         // 0x7D0
        shared::C_ModelRefProperty m_ref0800;                                         // 0x800
        shared::C_ModelProperty<bool> m_bool0830;                                     // 0x830, 0x1810F1C08
        shared::C_ModelProperty<bool> m_bool0860;                                     // 0x860, 0x1810F1C08
        shared::C_ModelProperty<bool> m_bool0890;                                     // 0x890
        /**
         * C_ModelArrayProperty<WUID, E_HandSlot, 2> (constructed by 0x1810F0064):
         * its table, then the weapon item in each hand, which its slot 1
         * (0x180686F30) reads as `this + 8 + 8 * hand`.
         */
        const void *m_pHandWeaponIdsTable;                                            // 0x8C0
        framework::WUID m_handWeaponIds[2];                                           // 0x8C8
        std::uint64_t m_handWeaponIds08D8;                                            // 0x8D8, 0 by default (0x1810F0064)
        std::uint64_t m_handWeaponIds08E0;                                            // 0x8E0, 0 by default
        shared::C_Signal m_handWeaponIdsElementSignal;                                // 0x8E8, dispatch 0x185666AF0
        shared::C_Signal m_handWeaponIdsSignal;                                       // 0x8F8, dispatch 0x185666A00
        std::byte m_padding0908[0x8];                                                 // 0x908
        S_HandSlotIntArrayProperty m_handInts0910;                                    // 0x910, 0x1810F0894
        S_HandSlotIntArrayProperty m_handInts0958;                                    // 0x958, 0x1810F0894
        shared::C_ModelProperty<bool> m_bool09A0;                                     // 0x9A0, 0x1810F1C08
        shared::C_ModelProperty<E_AnimActionMovementState::Type> m_animMovementState; // 0x9D0
        shared::C_ModelProperty<float> m_float0A00;                                   // 0xA00, 0x1810F1C38
        shared::C_ModelProperty<float> m_float0A30;                                   // 0xA30, 0x1810F1C38
        shared::C_ModelProperty<float> m_float0A60;                                   // 0xA60, 0x1810F1C38
        shared::C_ModelProperty<float> m_float0A90;                                   // 0xA90, 0x1810F1C38
        shared::C_ModelProperty<float> m_float0AC0;                                   // 0xAC0, custom default (0x1810F0980)
        shared::C_ModelRefProperty m_ref0AF0;                                         // 0xAF0
        /**
         * C_ModelSetProperty<S_RestrictedInputClass> (0x1810F1D14): its table,
         * an MSVC std::set head and count, a zeroed slot and the set signal.
         */
        const void *m_pRestrictedInputClassesTable;                                   // 0xB20
        void *m_pRestrictedInputClassesHead;                                          // 0xB28, 0x40-byte sentinel node
        std::uint64_t m_restrictedInputClassCount;                                    // 0xB30
        std::uint64_t m_restrictedInputClasses0B38;                                   // 0xB38, 0 by default
        shared::C_Signal m_restrictedInputClassesSignal;                              // 0xB40, dispatch 0x185666AE0
        std::byte m_padding0B50[0x8];                                                 // 0xB50

        /**
         * C_ModelArrayProperty<bool, E_GuardRequestScope::Type, 8>: its table,
         * the eight bools `C_ModelArrayPropertyBool::SetElement` (0x18093A3B0)
         * writes at `this + 8 + index`, then the per-element and array signals.
         */
        const void *m_pCombatRequestsTable;                                           // 0xB58
        bool m_combatRequests[8];                                                     // 0xB60
        std::uint64_t m_combatRequests0B68;                                           // 0xB68, not written by the model constructor
        std::uint64_t m_combatRequests0B70;                                           // 0xB70, 0 by default
        shared::C_Signal m_combatRequestsElementSignal;                               // 0xB78, dispatch 0x1855D4D00
        shared::C_Signal m_combatRequestsSignal;                                      // 0xB88, dispatch 0x185666A00
        std::byte m_padding0B98[0x8];                                                 // 0xB98

        shared::C_ModelProperty<std::uint32_t> m_uint0BA0;                            // 0xBA0
        // Eight free-standing model signals, then three more bool properties.
        shared::C_Signal m_signal0BD0;                                                // 0xBD0, dispatch 0x1855D4D30
        shared::C_Signal m_signal0BE0;                                                // 0xBE0, dispatch 0x1855D4D30
        shared::C_Signal m_signal0BF0;                                                // 0xBF0, dispatch 0x185666A40
        shared::C_Signal m_signal0C00;                                                // 0xC00, dispatch 0x185666A40
        shared::C_Signal m_signal0C10;                                                // 0xC10, dispatch 0x185666930
        shared::C_Signal m_signal0C20;                                                // 0xC20, dispatch 0x185666888
        shared::C_Signal m_signal0C30;                                                // 0xC30, dispatch 0x1856668A8
        shared::C_Signal m_signal0C40;                                                // 0xC40, dispatch 0x1856668A8
        shared::C_ModelProperty<bool> m_bool0C50;                                     // 0xC50, 0x1810F1C08
        shared::C_ModelProperty<bool> m_bool0C80;                                     // 0xC80, 0x1810F1C08
        shared::C_ModelProperty<bool> m_bool0CB0;                                     // 0xCB0, 0x1810F1C08
        C_CombatActor *m_pCombatActor;                                                // 0xCE0, the constructor's argument: the owning fighter
        std::uint64_t m_opponentHandle;                                               // 0xCE8, the opponent's weak handle, written by SetOpponent
        C_CombatTarget *m_pTarget;                                                    // 0xCF0
        C_CombatActor *m_pOpponent;                                                   // 0xCF8
        void *m_pIntSetHead;                                                          // 0xD00, MSVC std::set<int> head (_Buyheadnode)
        std::uint64_t m_intSetSize;                                                   // 0xD08

        static void AssertLayout() {
            static_assert(offsetof(C_CombatActorModel, m_fightingStanceUp) == 0x000, "C_CombatActorModel::m_fightingStanceUp offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_state) == 0x030, "C_CombatActorModel::m_state offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_combatActionType) == 0x060, "C_CombatActorModel::m_combatActionType offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_id0120) == 0x120, "C_CombatActorModel::m_id0120 offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_combatZone) == 0x0F0, "C_CombatActorModel::m_combatZone offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_previousCombatZone) == 0x180, "C_CombatActorModel::m_previousCombatZone offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_handSlot01B0) == 0x1B0, "C_CombatActorModel::m_handSlot01B0 offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_id0210) == 0x210, "C_CombatActorModel::m_id0210 offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_attackInputClass) == 0x240, "C_CombatActorModel::m_attackInputClass offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_attackInputActive) == 0x2A0, "C_CombatActorModel::m_attackInputActive offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_guardRequest) == 0x630, "C_CombatActorModel::m_guardRequest offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_id0270) == 0x270, "C_CombatActorModel::m_id0270 offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_ref0510) == 0x510, "C_CombatActorModel::m_ref0510 offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_id05D0) == 0x5D0, "C_CombatActorModel::m_id05D0 offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_handSlot0600) == 0x600, "C_CombatActorModel::m_handSlot0600 offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_blockMode) == 0x650, "C_CombatActorModel::m_blockMode offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_time06E0) == 0x6E0, "C_CombatActorModel::m_time06E0 offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_ref0800) == 0x800, "C_CombatActorModel::m_ref0800 offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_bool0890) == 0x890, "C_CombatActorModel::m_bool0890 offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_handWeaponIds) == 0x8C8, "C_CombatActorModel::m_handWeaponIds offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_animMovementState) == 0x9D0, "C_CombatActorModel::m_animMovementState offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_ref0AF0) == 0xAF0, "C_CombatActorModel::m_ref0AF0 offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_pCombatRequestsTable) == 0xB58, "C_CombatActorModel::m_pCombatRequestsTable offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_combatRequests) == 0xB60, "C_CombatActorModel::m_combatRequests offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_uint0BA0) == 0xBA0, "C_CombatActorModel::m_uint0BA0 offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_pTarget) == 0xCF0, "C_CombatActorModel::m_pTarget offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_pOpponent) == 0xCF8, "C_CombatActorModel::m_pOpponent offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_bool0830) == 0x830, "C_CombatActorModel::m_bool0830 offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_bool0860) == 0x860, "C_CombatActorModel::m_bool0860 offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_handWeaponIdsElementSignal) == 0x8E8, "C_CombatActorModel::m_handWeaponIdsElementSignal offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_handInts0910) == 0x910, "C_CombatActorModel::m_handInts0910 offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_handInts0958) == 0x958, "C_CombatActorModel::m_handInts0958 offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_bool09A0) == 0x9A0, "C_CombatActorModel::m_bool09A0 offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_float0A00) == 0xA00, "C_CombatActorModel::m_float0A00 offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_float0AC0) == 0xAC0, "C_CombatActorModel::m_float0AC0 offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_pRestrictedInputClassesTable) == 0xB20, "C_CombatActorModel::m_pRestrictedInputClassesTable offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_restrictedInputClassesSignal) == 0xB40, "C_CombatActorModel::m_restrictedInputClassesSignal offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_combatRequestsElementSignal) == 0xB78, "C_CombatActorModel::m_combatRequestsElementSignal offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_signal0BD0) == 0xBD0, "C_CombatActorModel::m_signal0BD0 offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_bool0C50) == 0xC50, "C_CombatActorModel::m_bool0C50 offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_pCombatActor) == 0xCE0, "C_CombatActorModel::m_pCombatActor offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_opponentHandle) == 0xCE8, "C_CombatActorModel::m_opponentHandle offset mismatch");
            static_assert(offsetof(C_CombatActorModel, m_pIntSetHead) == 0xD00, "C_CombatActorModel::m_pIntSetHead offset mismatch");
            static_assert(sizeof(S_HandSlotIntArrayProperty) == 0x48, "C_CombatActorModel::S_HandSlotIntArrayProperty size mismatch");
        }
    };

    // C_CombatActor::Init allocates exactly this many bytes for the model.
    static_assert(sizeof(C_CombatActorModel) == 0xD10, "C_CombatActorModel size mismatch");

    inline const C_CombatActorModel::FieldDescriptor *C_CombatActorModel::Fields() {
        using K = FieldKind;
        static const FieldDescriptor kFields[] = {
            {0x000, K::Bool, "stanceUp"},
            {0x030, K::State, "state"},
            {0x060, K::Int32, "combatActionType"},
            {0x090, K::Int32, "int@090"},
            {0x0C0, K::Int32, "int@0C0"},
            {0x0F0, K::Int32, "combatZone"},
            {0x120, K::Int32, "id@120"},
            {0x150, K::Int32, "int@150"},
            {0x180, K::Int32, "prevCombatZone"},
            {0x1B0, K::HandSlot, "hand@1B0"},
            {0x1E0, K::Float, "float@1E0"},
            {0x210, K::Int32, "id@210"},
            {0x240, K::Int32, "attackInputClass"},
            {0x270, K::Int32, "id@270"},
            {0x2A0, K::Bool, "attackInputActive"},
            {0x2D0, K::Bool, "guardReadingToStrike"},
            {0x300, K::Bool, "bool@300"},
            {0x330, K::Bool, "bool@330"},
            {0x360, K::Bool, "bool@360"},
            {0x390, K::Bool, "riposteRequested"},
            {0x3C0, K::Bool, "bool@3C0"},
            {0x3F0, K::Float, "float@3F0"},
            {0x420, K::Float, "float@420"},
            {0x450, K::Float, "float@450"},
            {0x480, K::Bool, "bool@480"},
            {0x4B0, K::Bool, "bool@4B0"},
            {0x4E0, K::Bool, "bool@4E0"},
            {0x510, K::Reference, "ref@510"},
            {0x540, K::Bool, "bool@540"},
            {0x570, K::Float, "float@570"},
            {0x5A0, K::Bool, "bool@5A0"},
            {0x5D0, K::Int32, "id@5D0"},
            {0x600, K::HandSlot, "hand@600"},
            {0x650, K::Byte, "blockMode"},
            {0x680, K::Bool, "bool@680"},
            {0x6B0, K::Reference, "ref@6B0"},
            {0x6E0, K::Time, "time@6E0"},
            {0x710, K::Time, "time@710"},
            {0x740, K::Reference, "ref@740"},
            {0x770, K::Reference, "ref@770"},
            {0x7A0, K::Reference, "ref@7A0"},
            {0x7D0, K::Reference, "ref@7D0"},
            {0x800, K::Reference, "ref@800"},
            {0x890, K::Bool, "bool@890"},
            {0x9D0, K::Byte, "animMoveState"},
            {0xAF0, K::Reference, "ref@AF0"},
            {0xBA0, K::UInt32, "uint@BA0"},
        };
        return kFields;
    }

    inline std::size_t C_CombatActorModel::FieldCount() {
        return 47; // the guard-request array is not a C_ModelProperty and is not listed
    }

    inline std::uint64_t C_CombatActorModel::ReadFieldBits(const FieldDescriptor &field) const {
        const auto *base = reinterpret_cast<const std::byte *>(this) + field.offset + 0x8;
        switch (field.kind) {
        case FieldKind::Bool:
        case FieldKind::Byte:
            return *reinterpret_cast<const std::uint8_t *>(base);
        case FieldKind::Int32:
        case FieldKind::UInt32:
        case FieldKind::Float:
        case FieldKind::State:
        case FieldKind::HandSlot:
            return *reinterpret_cast<const std::uint32_t *>(base);
        case FieldKind::Time:
        case FieldKind::Reference:
        default:
            return *reinterpret_cast<const std::uint64_t *>(base);
        }
    }

    inline std::int32_t C_CombatActorModel::GetIdField(std::size_t index) const {
        switch (index) {
        case 0: return m_id0120.GetValue();
        case 1: return m_previousCombatZone.GetValue();
        case 2: return m_id0210.GetValue();
        case 3: return m_attackInputClass.GetValue();
        case 4: return m_id0270.GetValue();
        case 5: return m_id05D0.GetValue();
        default: return -1;
        }
    }

    inline void C_CombatActorModel::SetIdFieldWithoutSignal(std::size_t index, std::int32_t value) {
        switch (index) {
        case 0: m_id0120.SetValueWithoutSignal(value); return;
        case 1: m_previousCombatZone.SetValueWithoutSignal(value); return;
        case 2: m_id0210.SetValueWithoutSignal(value); return;
        case 3: m_attackInputClass.SetValueWithoutSignal(value); return;
        case 4: m_id0270.SetValueWithoutSignal(value); return;
        case 5: m_id05D0.SetValueWithoutSignal(value); return;
        default: return;
        }
    }

    inline const void *C_CombatActorModel::GetReferenceTarget(std::size_t index) const {
        switch (index) {
        case 0: return m_ref0510.GetTarget();
        case 1: return m_ref06B0.GetTarget();
        case 2: return m_ref0740.GetTarget();
        case 3: return m_ref0770.GetTarget();
        case 4: return m_ref07A0.GetTarget();
        case 5: return m_ref07D0.GetTarget();
        case 6: return m_ref0800.GetTarget();
        case 7: return m_ref0AF0.GetTarget();
        default: return nullptr;
        }
    }
} // namespace SDK::wh::combatmodule
