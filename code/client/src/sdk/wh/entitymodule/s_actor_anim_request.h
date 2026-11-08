#pragma once

#include "../../patterns.h"
#include "../../types.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace SDK::wh::entitymodule {
    /**
     * Root of every actor action request. Slots one and two remain opaque;
     * slot three selects the C_ActionActor expansion that owns the request.
     */
    class S_ActionRequest {
      public:
        virtual ~S_ActionRequest()                    = default; // slot 0
        virtual void unk_fn0001()                     = 0;
        virtual void unk_fn0002()                     = 0;
        virtual std::uint8_t GetExpansionType() const = 0; // slot 3, +0x18

      private:
        std::byte m_callback0008[0x40]; // 0x08, native std::function; dtor 0x1808E2514 destroys it, its impl pointer is at +0x38
    };

    static_assert(sizeof(S_ActionRequest) == 0x48, "S_ActionRequest size mismatch");

    /** Shared animation-request state, constructor and destructor bounded. */
    class S_ActionAnimRequest: public S_ActionRequest {
      private:
        // Ctor 0x1808E254C.
        void *m_value0048;                 // 0x48, ctor zeroes
        std::uint32_t m_value0050;         // 0x50, ctor zeroes
        float m_value0054;                 // 0x54, ctor sets 1.0
        float m_values0058[3];             // 0x58, ctor zeroes
        std::byte m_padding0064[0x4];
        std::byte m_nativeState0068[0x40]; // 0x68, native std::function (impl at +0x38, zeroed by the ctor)
        std::byte m_nativeState00A8[0x40]; // 0xA8, native std::function (impl at +0x38, zeroed by the ctor)

        static void AssertLayout() {
            static_assert(offsetof(S_ActionAnimRequest, m_value0048) == 0x48, "S_ActionAnimRequest::m_value0048 offset mismatch");
            static_assert(offsetof(S_ActionAnimRequest, m_value0054) == 0x54, "S_ActionAnimRequest::m_value0054 offset mismatch");
            static_assert(offsetof(S_ActionAnimRequest, m_nativeState0068) == 0x68, "S_ActionAnimRequest::m_nativeState0068 offset mismatch");
        }
    };

    static_assert(sizeof(S_ActionAnimRequest) == 0xE8, "S_ActionAnimRequest size mismatch");

    /** Empty RTTI tag between animation requests and full-body derivatives. */
    class S_ActionFullBodyAnimRequest: public S_ActionAnimRequest {};

    static_assert(sizeof(S_ActionFullBodyAnimRequest) == 0xE8, "S_ActionFullBodyAnimRequest size mismatch");

    /**
     * Pickup-specialized request presented by the owning player's accepted
     * action. Observers copy only the bounded action type and two flags; the
     * shared native item owner at +0xF8 remains local to the accepted action.
     */
    class S_ActorAnimPickUpRequest: public S_ActionFullBodyAnimRequest {
      public:
        std::int32_t GetActionType() const {
            return m_actionType;
        }
        std::uint8_t GetRequestFlag0() const {
            return m_requestFlag0;
        }
        std::uint8_t GetRequestFlag1() const {
            return m_requestFlag1;
        }

      private:
        // Ctor S_ActorAnimItemHandlingRequestImpl<S_ActorAnimPickUpRequest> 0x1808CA0FC.
        std::uint64_t m_itemWuid00E8; // 0xE8, ctor sets qword_185322A98
        std::int32_t m_actionType;    // 0xF0, ctor sets 2
        std::uint8_t m_requestFlag0;  // 0xF4, ctor zeroes
        std::uint8_t m_requestFlag1;  // 0xF5, ctor zeroes
        std::byte m_padding00F6[0x2];
        std::byte m_sharedItem00F8[0x10]; // 0xF8, native shared_ptr pair, ctor zeroes
        std::int32_t m_fragment0108;  // 0x108, ctor sets g_actionFragmentGeneric
        std::uint32_t m_value010C;    // 0x10C, ctor zeroes
        std::uint8_t m_mode0110;      // 0x110, ctor sets 3
        std::byte m_padding0111[0x7];

        static void AssertLayout() {
            static_assert(offsetof(S_ActorAnimPickUpRequest, m_actionType) == 0xF0, "S_ActorAnimPickUpRequest::m_actionType offset mismatch");
            static_assert(offsetof(S_ActorAnimPickUpRequest, m_requestFlag0) == 0xF4, "S_ActorAnimPickUpRequest::m_requestFlag0 offset mismatch");
            static_assert(offsetof(S_ActorAnimPickUpRequest, m_requestFlag1) == 0xF5, "S_ActorAnimPickUpRequest::m_requestFlag1 offset mismatch");
            static_assert(offsetof(S_ActorAnimPickUpRequest, m_sharedItem00F8) == 0xF8, "S_ActorAnimPickUpRequest::m_sharedItem00F8 offset mismatch");
            static_assert(offsetof(S_ActorAnimPickUpRequest, m_fragment0108) == 0x108, "S_ActorAnimPickUpRequest::m_fragment0108 offset mismatch");
            static_assert(offsetof(S_ActorAnimPickUpRequest, m_mode0110) == 0x110, "S_ActorAnimPickUpRequest::m_mode0110 offset mismatch");
        }
    };

    static_assert(sizeof(S_ActorAnimPickUpRequest) == 0x118, "S_ActorAnimPickUpRequest size mismatch");

    /** Shared item-handling request that puts one exact WUID into a hand. */
    class S_ActorAnimPutItemInHandRequest: public S_ActionFullBodyAnimRequest {
      public:
        void SetItem(std::uint64_t itemWuid, std::int32_t hand) {
            m_hand     = hand;
            m_itemWuid = itemWuid;
        }

      private:
        // Ctors 0x1814CCF84 (in hand) and 0x1814A3F7C (in inventory).
        std::int32_t m_hand;          // 0xE8; constructor default 2 (automatic)
        std::byte m_padding00EC[0x4]; // never written; aligns the WUID
        std::uint64_t m_itemWuid;     // 0xF0, ctor sets the invalid WUID qword_185322AA0
        std::uint32_t m_context00F8;  // 0xF8, ctor sets g_actionFragmentGeneric
        std::uint32_t m_state00FC;    // 0xFC, ctor zeroes
        std::uint8_t m_mode0100;      // 0x100, ctor sets 3
        std::byte m_padding0101[0x7];

        static void AssertLayout() {
            static_assert(offsetof(S_ActorAnimPutItemInHandRequest, m_hand) == 0xE8, "S_ActorAnimPutItemInHandRequest::m_hand offset mismatch");
            static_assert(offsetof(S_ActorAnimPutItemInHandRequest, m_itemWuid) == 0xF0, "S_ActorAnimPutItemInHandRequest::m_itemWuid offset mismatch");
            static_assert(offsetof(S_ActorAnimPutItemInHandRequest, m_context00F8) == 0xF8, "S_ActorAnimPutItemInHandRequest::m_context00F8 offset mismatch");
            static_assert(offsetof(S_ActorAnimPutItemInHandRequest, m_state00FC) == 0xFC, "S_ActorAnimPutItemInHandRequest::m_state00FC offset mismatch");
            static_assert(offsetof(S_ActorAnimPutItemInHandRequest, m_mode0100) == 0x100, "S_ActorAnimPutItemInHandRequest::m_mode0100 offset mismatch");
        }
    };

    static_assert(sizeof(S_ActorAnimPutItemInHandRequest) == 0x108, "S_ActorAnimPutItemInHandRequest size mismatch");

    /** Shared item-handling request that visually returns a WUID to inventory. */
    class S_ActorAnimPutItemInInventoryRequest: public S_ActionFullBodyAnimRequest {
      public:
        void SetItem(std::uint64_t itemWuid, std::int32_t hand) {
            m_hand     = hand;
            m_itemWuid = itemWuid;
        }

      private:
        // Ctors 0x1814CCF84 (in hand) and 0x1814A3F7C (in inventory).
        std::int32_t m_hand;          // 0xE8; constructor default 2 (automatic)
        std::byte m_padding00EC[0x4]; // never written; aligns the WUID
        std::uint64_t m_itemWuid;     // 0xF0, ctor sets the invalid WUID qword_185322AA0
        std::uint32_t m_context00F8;  // 0xF8, ctor sets g_actionFragmentGeneric
        std::uint32_t m_state00FC;    // 0xFC, ctor zeroes
        std::uint8_t m_mode0100;      // 0x100, ctor sets 3
        std::byte m_padding0101[0x7];

        static void AssertLayout() {
            static_assert(offsetof(S_ActorAnimPutItemInInventoryRequest, m_hand) == 0xE8, "S_ActorAnimPutItemInInventoryRequest::m_hand offset mismatch");
            static_assert(offsetof(S_ActorAnimPutItemInInventoryRequest, m_itemWuid) == 0xF0, "S_ActorAnimPutItemInInventoryRequest::m_itemWuid offset mismatch");
            static_assert(offsetof(S_ActorAnimPutItemInInventoryRequest, m_context00F8) == 0xF8, "S_ActorAnimPutItemInInventoryRequest::m_context00F8 offset mismatch");
            static_assert(offsetof(S_ActorAnimPutItemInInventoryRequest, m_state00FC) == 0xFC, "S_ActorAnimPutItemInInventoryRequest::m_state00FC offset mismatch");
            static_assert(offsetof(S_ActorAnimPutItemInInventoryRequest, m_mode0100) == 0x100, "S_ActorAnimPutItemInInventoryRequest::m_mode0100 offset mismatch");
        }
    };

    static_assert(sizeof(S_ActorAnimPutItemInInventoryRequest) == 0x108, "S_ActorAnimPutItemInInventoryRequest size mismatch");

    /**
     * The request behind an animated weapon draw, from expansion slot 11.
     *
     * `m_handItems` is the pair the set brings out, indexed by hand, both
     * invalid until `BuildDrawHandPair` fills them.
     *
     * `m_hasQamSlot` must stay clear. The action's entry step runs its buff
     * lookup only when it is set, and that lookup dereferences
     * `C_InventorySoul::m_pPlayerInventorySoul` unchecked -- null on anything
     * that is not the player. The game's own builder always sets it.
     */
    class S_ActorAnimDrawRequest: public S_ActionFullBodyAnimRequest {
      public:
        static constexpr std::size_t kHandCount = 2;

        void SetHandItem(std::size_t hand, std::uint64_t itemWuid) {
            if (hand < kHandCount) {
                m_handItems[hand] = itemWuid;
            }
        }

        std::uint64_t *GetHandItems() {
            return m_handItems;
        }

      private:
        // Ctor 0x1808C7C5C.
        std::int32_t m_kind00E8; // 0xE8, constructor writes 3
        std::uint16_t m_state00EC; // 0xEC, constructor clears it
        std::byte m_padding00EE[0x2];
        void *m_values00F0[3];     // 0xF0, constructor clears them
        std::uint8_t m_state0108;  // 0x108, constructor clears it
        std::byte m_padding0109[0x7];
        std::uint64_t m_handItems[kHandCount]; // 0x110
        std::int32_t m_qamSlot;                // 0x120
        std::uint8_t m_hasQamSlot;             // 0x124, constructor clears it
        std::byte m_opaque0125[0x3];
        std::uint8_t m_state0128; // 0x128, constructor writes 1
        std::byte m_opaque0129[0x7];
        std::int32_t m_state0130; // 0x130
        std::int32_t m_state0134; // 0x134
        std::uint8_t m_state0138; // 0x138, constructor writes 3
        std::byte m_opaque0139[0x17];

        static void AssertLayout() {
            static_assert(offsetof(S_ActorAnimDrawRequest, m_kind00E8) == 0xE8, "S_ActorAnimDrawRequest::m_kind00E8 offset mismatch");
            static_assert(offsetof(S_ActorAnimDrawRequest, m_values00F0) == 0xF0, "S_ActorAnimDrawRequest::m_values00F0 offset mismatch");
            static_assert(offsetof(S_ActorAnimDrawRequest, m_state0108) == 0x108, "S_ActorAnimDrawRequest::m_state0108 offset mismatch");
            static_assert(offsetof(S_ActorAnimDrawRequest, m_handItems) == 0x110, "S_ActorAnimDrawRequest::m_handItems offset mismatch");
            static_assert(offsetof(S_ActorAnimDrawRequest, m_qamSlot) == 0x120, "S_ActorAnimDrawRequest::m_qamSlot offset mismatch");
            static_assert(offsetof(S_ActorAnimDrawRequest, m_hasQamSlot) == 0x124, "S_ActorAnimDrawRequest::m_hasQamSlot offset mismatch");
            static_assert(offsetof(S_ActorAnimDrawRequest, m_state0128) == 0x128, "S_ActorAnimDrawRequest::m_state0128 offset mismatch");
            static_assert(offsetof(S_ActorAnimDrawRequest, m_state0130) == 0x130, "S_ActorAnimDrawRequest::m_state0130 offset mismatch");
            static_assert(offsetof(S_ActorAnimDrawRequest, m_state0138) == 0x138, "S_ActorAnimDrawRequest::m_state0138 offset mismatch");
        }
    };

    static_assert(sizeof(S_ActorAnimDrawRequest) == 0x150, "S_ActorAnimDrawRequest size mismatch");

    /**
     * The holster twin, from expansion slot 12. It carries no hand pair.
     *
     * The factory 0x1808EBAFC allocates 0x108 bytes for the whole
     * `std::_Ref_count_obj2<S_ActorAnimItemHandlingRequestImpl<...>>`, whose
     * 0x10-byte count header leaves 0xF8 for the request. The fields below
     * are the ones the item-handling impl writes, as for the other requests.
     */
    class S_ActorAnimHolsterRequest: public S_ActionFullBodyAnimRequest {
      private:
        std::int32_t m_fragment00E8; // 0xE8, factory sets g_actionFragmentGeneric
        std::uint32_t m_value00EC;   // 0xEC, factory zeroes
        std::uint8_t m_mode00F0;     // 0xF0, factory sets 3
        std::byte m_padding00F1[0x7];

        static void AssertLayout() {
            static_assert(offsetof(S_ActorAnimHolsterRequest, m_fragment00E8) == 0xE8, "S_ActorAnimHolsterRequest::m_fragment00E8 offset mismatch");
            static_assert(offsetof(S_ActorAnimHolsterRequest, m_mode00F0) == 0xF0, "S_ActorAnimHolsterRequest::m_mode00F0 offset mismatch");
        }
    };

    static_assert(sizeof(S_ActorAnimHolsterRequest) == 0xF8, "S_ActorAnimHolsterRequest size mismatch");

    /** Generic actor-animation request with item presentation state. */
    class S_ActorAnimRequest: public S_ActionFullBodyAnimRequest {
      public:
        void SetItemHandlingPresentation(std::uint64_t itemWuid, std::int32_t actionType, std::uint8_t requestFlag0, std::uint8_t requestFlag1) {
            m_itemWuid     = itemWuid;
            m_actionType   = actionType;
            m_requestFlag0 = requestFlag0;
            m_requestFlag1 = requestFlag1;
        }

        std::int32_t GetActionType() const {
            return m_actionType;
        }
        std::uint8_t GetRequestFlag0() const {
            return m_requestFlag0;
        }
        std::uint8_t GetRequestFlag1() const {
            return m_requestFlag1;
        }

      private:
        std::uint64_t m_itemWuid;    // 0xE8
        std::int32_t m_actionType;   // 0xF0
        std::uint8_t m_requestFlag0; // 0xF4
        std::uint8_t m_requestFlag1; // 0xF5
        // Ctor 0x1808E29F8 (which writes the action type at 0xF0 as the 16-bit value 1).
        std::byte m_padding00F6[0x2];
        std::uint16_t m_state00F8;        // 0xF8, ctor zeroes
        std::byte m_padding00FA[0x2];
        std::uint32_t m_state00FC;        // 0xFC, ctor zeroes
        std::uint8_t m_state0100;         // 0x100, ctor zeroes
        std::byte m_padding0101[0x7];
        std::uint8_t m_state0108;         // 0x108, ctor zeroes
        std::byte m_padding0109[0x7];
        std::uint8_t m_state0110;         // 0x110, ctor zeroes
        std::byte m_padding0111[0x3];
        std::uint16_t m_state0114;        // 0x114, ctor zeroes
        std::byte m_padding0116[0x2];
        std::int32_t m_value0118;         // 0x118, ctor sets -1
        std::byte m_value011C[0x10];      // 0x11C, copied from the constant at 0x18409EF60
        std::uint32_t m_value012C;        // 0x12C, the constant's first dword again
        std::byte m_value0130[0x10];      // 0x130, the same constant
        std::uint32_t m_value0140;        // 0x140, the constant's first dword again
        std::byte m_padding0144[0x4];
        CryStringT<char> m_string0148;    // 0x148, ctor sets the empty string
        CryStringT<char> m_tagState;      // 0x150, ctor sets "AnyTagState"
        std::uint16_t m_state0158;        // 0x158, ctor sets 1
        std::uint8_t m_state015A;         // 0x15A, ctor zeroes
        std::byte m_padding015B[0x1];
        std::uint32_t m_state015C;        // 0x15C, ctor zeroes
        std::byte m_state0160[0x20];      // 0x160, built by 0x1808E2698
        void *m_values0180[4];            // 0x180, ctor zeroes; two shared_ptr pairs, dtor 0x1808E22A0 releases +0x188 and +0x198
        std::byte m_opaque01A0[0x4];
        std::uint8_t m_flag01A4;          // 0x1A4, ctor zeroes
        std::byte m_opaque01A5[0xB];
        std::uint8_t m_flag01B0;          // 0x1B0, ctor zeroes
        std::byte m_opaque01B1[0x3F];
        std::uint8_t m_flag01F0;          // 0x1F0, ctor zeroes
        std::byte m_padding01F1[0x7];

        static void AssertLayout() {
            static_assert(offsetof(S_ActorAnimRequest, m_itemWuid) == 0xE8, "S_ActorAnimRequest::m_itemWuid offset mismatch");
            static_assert(offsetof(S_ActorAnimRequest, m_actionType) == 0xF0, "S_ActorAnimRequest::m_actionType offset mismatch");
            static_assert(offsetof(S_ActorAnimRequest, m_requestFlag0) == 0xF4, "S_ActorAnimRequest::m_requestFlag0 offset mismatch");
            static_assert(offsetof(S_ActorAnimRequest, m_requestFlag1) == 0xF5, "S_ActorAnimRequest::m_requestFlag1 offset mismatch");
            static_assert(offsetof(S_ActorAnimRequest, m_value0118) == 0x118, "S_ActorAnimRequest::m_value0118 offset mismatch");
            static_assert(offsetof(S_ActorAnimRequest, m_string0148) == 0x148, "S_ActorAnimRequest::m_string0148 offset mismatch");
            static_assert(offsetof(S_ActorAnimRequest, m_tagState) == 0x150, "S_ActorAnimRequest::m_tagState offset mismatch");
            static_assert(offsetof(S_ActorAnimRequest, m_state0160) == 0x160, "S_ActorAnimRequest::m_state0160 offset mismatch");
            static_assert(offsetof(S_ActorAnimRequest, m_flag01B0) == 0x1B0, "S_ActorAnimRequest::m_flag01B0 offset mismatch");
            static_assert(offsetof(S_ActorAnimRequest, m_flag01F0) == 0x1F0, "S_ActorAnimRequest::m_flag01F0 offset mismatch");
        }
    };

    static_assert(sizeof(S_ActorAnimRequest) == 0x1F8, "S_ActorAnimRequest size mismatch");

    /** Constructor-default concrete actor-animation request. */
    class S_ActorAnimRequestImpl: public S_ActorAnimRequest {
      public:
        static std::shared_ptr<S_ActorAnimRequest> Create() {
            std::shared_ptr<S_ActorAnimRequest> result;
            hook::call<void>(gPatterns.CreateSharedActorAnimRequestImpl, &result);
            return result;
        }

      private:
        // CreateSharedActorAnimRequestImpl1808E2458.
        std::int32_t m_value01F8;  // 0x1F8, factory sets -1
        std::uint32_t m_value01FC; // 0x1FC, factory zeroes
        std::uint8_t m_flag0200;   // 0x200, factory zeroes
        std::byte m_padding0201[0x7];

        static void AssertLayout() {
            static_assert(offsetof(S_ActorAnimRequestImpl, m_value01F8) == 0x1F8, "S_ActorAnimRequestImpl::m_value01F8 offset mismatch");
            static_assert(offsetof(S_ActorAnimRequestImpl, m_flag0200) == 0x200, "S_ActorAnimRequestImpl::m_flag0200 offset mismatch");
        }
    };

    static_assert(sizeof(S_ActorAnimRequestImpl) == 0x208, "S_ActorAnimRequestImpl size mismatch");
} // namespace SDK::wh::entitymodule
