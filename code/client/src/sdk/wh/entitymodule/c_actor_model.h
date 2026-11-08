#pragma once

#include "../../patterns.h"
#include "../../types.h"
#include "e_actor_state.h"
#include "../framework/wuid.h"
#include "../shared/c_signal.h"
#include "../shared/s_model_value.h"
#include "e_stance_category.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Per-actor model state consumed by humanoid item-handling transitions.
     *
     * C_Actor constructs this 0x408-byte object and stores it at +0x990.
     * The native context query at 0x180F48C10 reads a 32-bit entry from
     * +0x170 and treats bit zero as the enabled flag. The bound below covers
     * every context in the native torch context initializer (maximum 19).
     */
    class C_ActorModel {
      public:
        static constexpr std::size_t kItemHandlingContextCount = 20;

        virtual void unk_fn0000()                                                = 0;
        virtual void unk_fn0001()                                                = 0;

        /**
         * Whether the body is crouched, as the engine's own crouch action set it.
         *
         * A signalled bool at `+0x8`: `C_ActorActionCrouch::TryEnterCrouch`
         * sets it through slot 3 on entering the crouch and
         * `C_ActorActionCrouch::Exit` clears it. Slot 2 is a COMDAT-folded
         * byte getter shared by many vtables. The setter is at 0x1809CD624.
         *
         * The CryAction `SMovementState::stance` is no substitute: KCD2's
         * movement controller only ever writes `STANCE_RELAXED` into it.
         */
        virtual bool IsCrouching() const = 0; // slot 2, +0x10

        /** Raises the property's signal on a change; the crouch action is its writer. */
        virtual void SetCrouching(bool crouching) = 0; // slot 3, +0x18

        virtual void unk_fn0004()                                                = 0;
        virtual void unk_fn0005()                                                = 0;
        virtual void unk_fn0006()                                                = 0;
        virtual void unk_fn0007()                                                = 0;
        virtual void unk_fn0008()                                                = 0;
        virtual void unk_fn0009()                                                = 0;
        virtual void unk_fn0010()                                                = 0;
        virtual void unk_fn0011()                                                = 0;
        virtual void unk_fn0012()                                                = 0;
        virtual void unk_fn0013()                                                = 0;
        virtual void unk_fn0014()                                                = 0;
        virtual void unk_fn0015()                                                = 0;
        virtual void unk_fn0016()                                                = 0;
        virtual void unk_fn0017()                                                = 0;
        virtual void unk_fn0018()                                                = 0;
        virtual void unk_fn0019()                                                = 0;
        virtual void unk_fn0020()                                                = 0;
        virtual void unk_fn0021()                                                = 0;
        virtual void unk_fn0022()                                                = 0;
        virtual void unk_fn0023()                                                = 0;
        virtual void unk_fn0024()                                                = 0;
        virtual void unk_fn0025()                                                = 0;
        virtual void unk_fn0026()                                                = 0;
        virtual void unk_fn0027()                                                = 0;
        virtual void unk_fn0028()                                                = 0;
        virtual void unk_fn0029()                                                = 0;
        virtual void unk_fn0030()                                                = 0;
        virtual void unk_fn0031()                                                = 0;
        virtual void unk_fn0032()                                                = 0;
        virtual void unk_fn0033()                                                = 0;
        virtual void unk_fn0034()                                                = 0;
        virtual void unk_fn0035()                                                = 0;
        virtual void unk_fn0036()                                                = 0;
        virtual void unk_fn0037()                                                = 0;
        virtual void unk_fn0038()                                                = 0;
        virtual void unk_fn0039()                                                = 0;
        virtual void unk_fn0040()                                                = 0;
        virtual void unk_fn0041()                                                = 0;
        virtual void unk_fn0042()                                                = 0;
        virtual void unk_fn0043()                                                = 0;
        virtual void unk_fn0044()                                                = 0;
        virtual void unk_fn0045()                                                = 0;
        virtual void unk_fn0046()                                                = 0;
        virtual void unk_fn0047()                                                = 0;
        virtual void unk_fn0048()                                                = 0;
        virtual void unk_fn0049()                                                = 0;
        virtual void unk_fn0050()                                                = 0;
        virtual void unk_fn0051()                                                = 0;
        virtual void unk_fn0052()                                                = 0;
        virtual void unk_fn0053()                                                = 0;
        virtual void unk_fn0054()                                                = 0;
        virtual void unk_fn0055()                                                = 0;
        virtual void unk_fn0056()                                                = 0;
        virtual void unk_fn0057()                                                = 0;
        virtual void unk_fn0058()                                                = 0;
        virtual void unk_fn0059()                                                = 0;
        virtual void unk_fn0060()                                                = 0;
        virtual void unk_fn0061()                                                = 0;
        virtual std::uint32_t GetItemHandlingContext(std::uint8_t context) const = 0; // slot 62, +0x1F0

        virtual void unk_fn0063() = 0;
        virtual void unk_fn0064() = 0;
        virtual void unk_fn0065() = 0;
        virtual void unk_fn0066() = 0;
        virtual void unk_fn0067() = 0;

        /**
         * Puts the body in a stance, on a side of an object.
         *
         * Slot 68 (0x180951108). Three signalled properties: the category at
         * `+0x80`, the object's WUID at `+0x98` and the side at `+0xE0`, each
         * raising its signal only when it changes. The actor's idle-state
         * object is connected to those signals
         * (`C_ActorIdleState::ConnectModelSignals1808DDEA8`) and re-selects
         * the body's idle from them: for sitting or lying, the fragment and the
         * global tags the object's own animation list gives, which it adds to
         * the tag state itself (`AddIdleTags181481448`).
         *
         * The object is not optional for a stance to hold. With
         * `kNoStanceObject` (`qword_18533A478`) the idle falls back to the
         * class default, which has a fragment for sitting but no tags, and the
         * actor's per-frame `SyncModelStanceFromTags18075B774` stands a body
         * with no Stance tag straight back up.
         *
         * The player's own transitions reach it through the Mannequin
         * procedural clip `C_ProceduralClipChangeStance::OnEnter180952474`
         * inside `SitDown`, `StandUp` and `LieDown`, with the seat's WUID.
         */
        virtual void SetStance(E_StanceCategory::Type category, std::uint32_t side, std::uint64_t stanceObject) = 0; // slot 68, +0x220

        virtual void unk_fn0069() = 0;
        virtual void unk_fn0070() = 0;
        virtual void unk_fn0071() = 0;
        virtual void unk_fn0072() = 0;
        virtual void unk_fn0073() = 0;
        virtual void unk_fn0074() = 0;
        virtual void unk_fn0075() = 0;
        virtual void unk_fn0076() = 0;
        virtual void unk_fn0077() = 0;
        virtual void unk_fn0078() = 0;
        virtual void unk_fn0079() = 0;
        virtual void unk_fn0080() = 0;
        virtual void unk_fn0081() = 0;
        virtual void unk_fn0082() = 0;

        /**
         * Whether one of the engine's own actor states is set on this body.
         *
         * Slot 83 (0x180645888). Reads the signalled bitset at `+0x370`,
         * inline when the property still has its default reader and through
         * its vtable otherwise. No bounds check, so the argument must be a
         * real `E_ActorState`.
         *
         * This is the engine's answer to what a body is doing, and it is worth
         * preferring to anything the mod tracks alongside: `Horse` is set for
         * as long as the engine considers a body mounted, and
         * `C_RiderExpansion::CanMount` reads this very set to decide whether
         * one may mount at all.
         */
        virtual bool IsActorStateActive(E_ActorState::Type state) const = 0; // slot 83

        /**
         * Sets or clears one actor state (`C_ModelBitsetProperty::Set`,
         * 0x1808324D4). A state already as asked is left alone; a change
         * raises the set's signal.
         *
         * This is the engine's own writer. The action system calls it for
         * whatever states an entering or leaving actor action reports
         * (`0x1809CCFD4`, with `C_ActorActionMinigame` reporting the
         * minigame's own, so `reading` and `readingFromInventory` for
         * `C_Reading`), and sprinting and jumping call it directly. It
         * writes no Mannequin tag: `C_Actor::ApplyActorStateTags` (slot 239,
         * 0x180644860) polls this set from the actor's per-frame tag update
         * and sets the global tags that go with `crouch`, `reading`,
         * `readingFromInventory` and a few others, so a state set here shows
         * on the body one frame later.
         */
        void SetActorState(E_ActorState::Type state, bool active) {
            hook::this_call<void>(gPatterns.C_ModelBitsetProperty_E_ActorState__Set, &m_actorStates, static_cast<std::uint8_t>(state), active);
        }

        bool IsItemHandlingContextEnabled(std::uint8_t context) const {
            return context < kItemHandlingContextCount && (GetItemHandlingContext(context) & 1U) != 0;
        }

        /**
         * The object this actor is interacting with, or zero for none.
         *
         * C_Actor::StartInteractiveActionByName writes it before dispatching
         * state event 103 and C_Actor::EndInteractiveAction clears it, so it
         * is live rather than a record of the last one. It is the engine's
         * own answer to whether a body is busy, which is why nothing here
         * tracks interactive actions itself.
         */
        SDK::EntityId GetInteractionTargetEntityId() const {
            return m_interactionTargetEntityId;
        }

        /** The engine's "no object", the WUID `SetStance` is given for a stance on nothing. */
        static constexpr std::uint64_t kNoStanceObject = framework::WUID::kInvalid;

        E_StanceCategory::Type GetStanceCategory() const {
            return m_stanceCategory;
        }

        /**
         * The WUID of what the body sits or lies on, or `kNoStanceObject`.
         *
         * This is what the idle lookup (`FindStanceIdleFragment180951088`)
         * resolves to the seat's own animation list, which is the only source
         * of the stance's global tags (`sitting`, `lyingRight`): the class
         * default carries none, and a body whose Stance tag is empty is stood
         * back up by `C_Actor::SyncModelStanceFromTags18075B774`.
         */
        std::uint64_t GetStanceObject() const {
            return m_stanceObject;
        }

        /** The side of the stance object the body took, in the stance-side numbering the idle lookup takes. */
        std::uint32_t GetStanceSide() const {
            return m_stanceSide;
        }

      private:
        // Laid out by the constructor 0x1803F01DC: a run of compact
        // value-and-signal properties, then the vtable-carrying ones. Each
        // signal's dispatch is a static of its own kind.
        wh::shared::S_ModelValue<bool> m_flag0008;            // 0x008
        wh::shared::S_ModelValue<bool> m_flag0020;            // 0x020
        wh::shared::S_ModelValue<bool> m_flag0038;            // 0x038
        wh::shared::S_ModelValue<std::int32_t> m_value0050;   // 0x050, -1 by default
        wh::shared::S_ModelValue<bool> m_flag0068;            // 0x068
        E_StanceCategory::Type m_stanceCategory;              // 0x080, 1 by default
        std::byte m_padding0084[0x4];
        wh::shared::C_Signal m_stanceCategorySignal;          // 0x088
        std::uint64_t m_stanceObject;                         // 0x098
        wh::shared::C_Signal m_stanceObjectSignal;            // 0x0A0
        wh::shared::S_ModelValue<bool> m_flag00B0;            // 0x0B0
        wh::shared::S_ModelValue<bool> m_flag00C8;            // 0x0C8
        std::uint32_t m_stanceSide;                           // 0x0E0
        std::byte m_padding00E4[0x4];
        wh::shared::C_Signal m_stanceSideSignal;              // 0x0E8
        wh::shared::S_ModelValue<bool> m_flag00F8;            // 0x0F8
        wh::shared::S_ModelValue<bool> m_flag0110;            // 0x110
        wh::shared::S_ModelValue<bool> m_flag0128;            // 0x128
        wh::shared::S_ModelValue<bool> m_flag0140;            // 0x140
        wh::shared::S_ModelValue<std::uint32_t> m_value0158;  // 0x158
        std::uint32_t m_itemHandlingContexts[kItemHandlingContextCount]; // 0x170
        wh::shared::C_Signal m_itemHandlingContextsSignal;    // 0x1C0
        std::byte m_value01D0[0x38];                          // 0x1D0, zeroed by the constructor
        wh::shared::C_Signal m_value01D0Signal;               // 0x208
        std::byte m_sprintDisabled[0x20]; // 0x218, C_ModelContextProperty<E_SprintDisabledContext, bool, 0>
        std::byte m_fallDisabled[0x20];   // 0x238, C_ModelContextProperty<E_FallDisabledContext, bool, 0>
        std::byte m_handSlotItems[0x50];  // 0x258, C_ModelArrayProperty<WUID, E_HandSlot, 2>, its two signals at 0x280 and 0x290
        std::byte m_minigameState[0x40];  // 0x2A8, C_ModelProperty<S_MinigameState>
        const void *m_pInteractionTargetVTable;    // 0x2E8, C_ModelProperty<unsigned int>
        SDK::EntityId m_interactionTargetEntityId; // 0x2F0
        std::byte m_padding02F4[0x4];
        wh::shared::C_Signal m_interactionTargetSignal; // 0x2F8
        std::byte m_opaque0308[0x8];
        const void *m_pVisorStateVTable;  // 0x310, C_ModelProperty<E_ActorVisorState::Type>
        std::uint8_t m_visorState;        // 0x318
        std::byte m_padding0319[0x7];
        wh::shared::C_Signal m_visorStateSignal; // 0x320
        std::byte m_opaque0330[0x8];
        wh::shared::S_ModelValue<std::uint32_t> m_value0338; // 0x338
        wh::shared::C_Signal m_signal0350; // 0x350
        wh::shared::C_Signal m_signal0360; // 0x360
        // C_ModelBitsetProperty<E_ActorState, std::bitset<45>>: vtable, the 45
        // bits at +0x378, a word set to -1 at +0x380 and its signal at +0x388.
        std::byte m_actorStates[0x8]; // 0x370
        std::uint64_t m_actorStateBits;   // 0x378
        std::uint64_t m_actorStateState;  // 0x380
        wh::shared::C_Signal m_actorStatesSignal; // 0x388
        std::byte m_movementRestriction[0x30]; // 0x398, C_ModelContextProperty<E_MovementRestrictionScope, bool, 0>
        void *m_pActor;                   // 0x3C8, the actor this model belongs to
        SDK::EntityId m_actorEntityId;    // 0x3D0, copied from the actor's +0x30
        std::byte m_padding03D4[0x4];
        // C_ActorModelUnstanceSignals: its vtable, two signals and this model.
        const void *m_pUnstanceSignalsVTable; // 0x3D8
        wh::shared::C_Signal m_unstanceSignalA; // 0x3E0
        wh::shared::C_Signal m_unstanceSignalB; // 0x3F0
        C_ActorModel *m_pUnstanceSignalsModel;  // 0x400

        static void AssertLayout() {
            static_assert(offsetof(C_ActorModel, m_stanceCategory) == 0x80, "C_ActorModel::m_stanceCategory offset mismatch");
            static_assert(offsetof(C_ActorModel, m_stanceObject) == 0x98, "C_ActorModel::m_stanceObject offset mismatch");
            static_assert(offsetof(C_ActorModel, m_stanceSide) == 0xE0, "C_ActorModel::m_stanceSide offset mismatch");
            static_assert(offsetof(C_ActorModel, m_itemHandlingContexts) == 0x170, "C_ActorModel::m_itemHandlingContexts offset mismatch");
            static_assert(offsetof(C_ActorModel, m_interactionTargetEntityId) == 0x2F0, "C_ActorModel::m_interactionTargetEntityId offset mismatch");
            static_assert(offsetof(C_ActorModel, m_actorStates) == 0x370, "C_ActorModel::m_actorStates offset mismatch");
            static_assert(offsetof(C_ActorModel, m_value0050) == 0x50, "C_ActorModel::m_value0050 offset mismatch");
            static_assert(offsetof(C_ActorModel, m_flag00F8) == 0xF8, "C_ActorModel::m_flag00F8 offset mismatch");
            static_assert(offsetof(C_ActorModel, m_itemHandlingContextsSignal) == 0x1C0, "C_ActorModel::m_itemHandlingContextsSignal offset mismatch");
            static_assert(offsetof(C_ActorModel, m_sprintDisabled) == 0x218, "C_ActorModel::m_sprintDisabled offset mismatch");
            static_assert(offsetof(C_ActorModel, m_handSlotItems) == 0x258, "C_ActorModel::m_handSlotItems offset mismatch");
            static_assert(offsetof(C_ActorModel, m_minigameState) == 0x2A8, "C_ActorModel::m_minigameState offset mismatch");
            static_assert(offsetof(C_ActorModel, m_visorState) == 0x318, "C_ActorModel::m_visorState offset mismatch");
            static_assert(offsetof(C_ActorModel, m_value0338) == 0x338, "C_ActorModel::m_value0338 offset mismatch");
            static_assert(offsetof(C_ActorModel, m_movementRestriction) == 0x398, "C_ActorModel::m_movementRestriction offset mismatch");
            static_assert(offsetof(C_ActorModel, m_pActor) == 0x3C8, "C_ActorModel::m_pActor offset mismatch");
            static_assert(offsetof(C_ActorModel, m_pUnstanceSignalsModel) == 0x400, "C_ActorModel::m_pUnstanceSignalsModel offset mismatch");
        }
    };

    static_assert(sizeof(C_ActorModel) == 0x408, "C_ActorModel size mismatch");
} // namespace SDK::wh::entitymodule
