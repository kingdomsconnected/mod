#pragma once

#include "../../patterns.h"

#include "../shared/c_signal.h"
#include "c_actor.h"
#include "s_mannequin_human_params.h"
#include "s_item_transfer_descriptor.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Human;
    class C_HumanHandHelper;
    class C_Item;
    class C_ItemAttachmentManager;
    class C_ItemPickUpHelper;

    /** Constructor-proven ABI overlay for C_Human's derived runtime state. */
    struct HumanRuntimeStateLayout {
        wh::shared::C_Signal m_signal;        // 0x00
        C_ItemAttachmentManager *m_pItemAttachmentManager; // 0x10
        void *m_pState0018;                   // 0x18
        std::uint32_t m_state0020[3];         // 0x20
        std::byte m_embeddedState002C[0x1C];  // 0x2C, separately constructed
        std::int32_t m_state0048;             // 0x48, initialized to -3
        std::byte m_padding004C[0x4];
        std::int16_t m_state0050;             // 0x50, initialized to -1
        std::uint8_t m_state0052;             // 0x52
        std::byte m_padding0053[0x1];
        std::int32_t m_state0054;             // 0x54, initialized to -3
        std::byte m_padding0058[0x4];
        std::int16_t m_state005C;             // 0x5C, initialized to -1
        std::uint8_t m_state005E;             // 0x5E
        std::byte m_padding005F[0x1];
        C_Human *m_pHuman;                    // 0x60, self
        std::uint8_t m_state0068;             // 0x68, initialized to 2
        std::byte m_padding0069[0x7];
        std::uint64_t m_state0070;            // 0x70
        std::uint8_t m_firstPersonView;        // 0x78
        std::uint8_t m_viewMode0079;           // 0x79
        std::uint8_t m_state007A;              // 0x7A, initialized to 1
        std::byte m_padding007B[0x1];
        std::uint32_t m_state007C;             // 0x7C
        std::byte m_opaque0080[0x8];
    };

    static_assert(sizeof(HumanRuntimeStateLayout) == 0x88,
                  "HumanRuntimeStateLayout size mismatch");
    static_assert(offsetof(HumanRuntimeStateLayout, m_pItemAttachmentManager) == 0x10,
                  "HumanRuntimeStateLayout::m_pItemAttachmentManager offset mismatch");

    /**
     * Shared humanoid actor base for NPCs and players.
     *
     * C_Human's primary table at 0x183A3B398 contains exactly 283 slots:
     * all 275 C_Actor entries followed by eight opaque C_Human contracts.
     * Its scalar deleting destructor proves the complete 0xA60-byte size.
     * The derived state owns the shared 0xE0-byte C_ItemAttachmentManager at
     * +0x9D0. Human setup allocates the 0x100-byte C_ItemPickUpHelper stored at +0xA48;
     * The constructor allocates the 0xB0-byte C_HumanHandHelper stored at
     * +0xA58; RTTI resolves its four listener/interface bases exactly.
     * teardown releases both before destroying C_Actor. The local-view update
     * path writes the first-person-view boolean at +0xA38 and publishes the
     * same value through the `FirstPersonView` state; the adjacent byte remains
     * unnamed because its narrower mode semantics are not yet proven.
     */
    class C_Human : public C_Actor {
      public:
        ~C_Human() override = default;

        virtual void unk_fn0275() = 0; // first C_Human slot, +0x898
        virtual bool StartItemPickup(S_ItemTransferDescriptor *descriptor) = 0;
        virtual bool BeginItemTransfer(S_ItemTransferDescriptor *descriptor) = 0;
        virtual void unk_fn0278() = 0;
        virtual void unk_fn0279() = 0;
        virtual void unk_fn0280() = 0;
        /** Drops both live hand items; the dead action calls this at entry. */
        virtual void DropHandItems() = 0; // slot 281, 0x180E797A4
        virtual void unk_fn0282() = 0; // final C_Human slot, +0x8D0

        // SDK convenience accessor; this is not a recovered virtual name.
        bool IsFirstPersonView() const { return m_runtimeState.m_firstPersonView != 0; }

        HumanRuntimeStateLayout *GetHumanRuntimeState() { return &m_runtimeState; }
        const HumanRuntimeStateLayout *GetHumanRuntimeState() const { return &m_runtimeState; }
        // C_Actor slot 191, which C_Human overrides to hand back exactly this
        // field: m_runtimeState is at +0x9C0 and the manager at +0x10 into it,
        // so the engine returns C_Human+0x9D0 and so does this.
        C_ItemAttachmentManager *GetItemAttachmentManager() const override {
            return m_runtimeState.m_pItemAttachmentManager;
        }
        C_ItemPickUpHelper *GetItemPickUpHelper() const { return m_pItemPickUpHelper; }
        C_HumanHandHelper *GetHumanHandHelper() const { return m_pHumanHandHelper; }

        /**
         * Executes the proven central native drop worker and returns its exact
         * post-split world item.
         */
        bool InvokeDropInventoryItem(std::uint64_t itemWuid, std::uint32_t amount, std::uint64_t context, C_Item **droppedItem) {
            return hook::this_call<bool>(gPatterns.C_Human__DropInventoryItem, this, itemWuid, amount, context, droppedItem);
        }

      private:
        HumanRuntimeStateLayout m_runtimeState; // 0x9C0
        C_ItemPickUpHelper *m_pItemPickUpHelper; // 0xA48
        SMannequinHumanParams m_mannequinParams; // 0xA50, carries only its vtable
        C_HumanHandHelper *m_pHumanHandHelper; // 0xA58

        static void AssertLayout() {
            static_assert(offsetof(C_Human, m_runtimeState) == 0x9C0, "C_Human::m_runtimeState offset mismatch");
            static_assert(offsetof(C_Human, m_pItemPickUpHelper) == 0xA48, "C_Human::m_pItemPickUpHelper offset mismatch");
            static_assert(offsetof(C_Human, m_mannequinParams) == 0xA50, "C_Human::m_mannequinParams offset mismatch");
            static_assert(offsetof(C_Human, m_pHumanHandHelper) == 0xA58, "C_Human::m_pHumanHandHelper offset mismatch");
        }
    };

    static_assert(sizeof(C_Human) == 0xA60, "C_Human size mismatch");
} // namespace SDK::wh::entitymodule
