#pragma once

#include "../../patterns.h"
#include "../../types.h"
#include "../entitymodule/s_item_class_guid.h"
#include "../shared/c_signal.h"
#include "alchemy_item_type.h"
#include "c_alchemy_action_director.h"
#include "c_alchemy_resource.h"
#include "c_minigame.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::playermodule {
    /**
     * Factory 0x1809F0234 allocates 0x6D8 for kind 3 before constructor
     * 0x1816E9570. The action director is typed in place: its
     * game-owned containers must never be copied or destroyed by the mod.
     */
    class C_Alchemy: public SDK::wh::playermodule::C_Minigame {
      public:
        bool Start(SDK::EntityId station) {
            return hook::this_call<bool>(SDK::gPatterns.C_Alchemy__Start, this, station);
        }
        void SelectObject(SDK::wh::playermodule::AlchemyItemType target) {
            hook::this_call<void>(SDK::gPatterns.C_Alchemy__SelectObject, this, target);
        }
        /**
         * Mirror the accepted back callback 0x181FFEC90 without retaining
         * its erased callable. The factory's nontrivial eight-byte action
         * reference uses a hidden result in RDX. Queue consumes its copied
         * reference; the factory reference remains ours to release. The
         * original smart-reference template spelling is not yet known.
         */
        bool QueueReturnAction() {
            if (!CanSelectObject() || HasInteractionAction()) {
                return false;
            }
            SDK::wh::framework::I_Action *factory = nullptr;
            hook::this_call<void>(SDK::gPatterns.C_Alchemy__CreateReturnAction, this, &factory, GetHeldObjectKind());
            if (factory == nullptr) {
                return false;
            }
            factory->AddRef();
            SDK::wh::framework::I_Action *queued = factory;
            hook::this_call<void>(SDK::gPatterns.C_Alchemy__QueueAction, this, &queued);
            factory->Release();
            return HasInteractionAction();
        }
        /**
         * Whether the table holds the player's camera limits (+0x308). An
         * action raises it (0x1816A3EC0) and the idle action's shutdown
         * drops it (0x180A96740), each writing the byte and then telling its
         * change signal, which runs SetCameraLimitsEnabled (0x180B66A60) on
         * the minigame's actor while the table is open: that moves the
         * actor's reference-counted limits.
         */
        bool AreCameraLimitsRequested() const {
            return m_cameraLimitsEnabled;
        }
        /** Raises or drops the request as the game's own actions do. */
        void SetCameraLimitsRequested(bool requested) {
            if (m_cameraLimitsEnabled != requested) {
                m_cameraLimitsEnabled = requested;
                m_cameraLimitsChanged.EmitBool(requested);
            }
        }
        void OnEntryActionFinished() {
            hook::this_call<void>(SDK::gPatterns.C_Alchemy__OnEntryActionFinished, this);
        }
        bool OwnsInteractionAction(const SDK::wh::framework::I_Action *action) const {
            return m_actionDirector.GetInteractionAction() == action;
        }
        bool HasInteractionAction() const {
            return m_actionDirector.HasInteractionAction();
        }
        SDK::wh::framework::I_Action *GetInteractionAction() {
            return m_actionDirector.GetInteractionAction();
        }
        bool CanSelectObject() const {
            return m_state == kInteractiveState;
        }
        SDK::wh::playermodule::AlchemyItemType GetHeldObjectKind() const {
            return m_heldObjectKind != SDK::wh::playermodule::AlchemyItemType::None ? m_heldObjectKind : m_secondaryHeldObjectKind;
        }
        const SDK::wh::entitymodule::S_ItemClassGUID *GetAssignedHerb(SDK::wh::playermodule::AlchemyItemType target) const {
            const auto index = static_cast<std::uint32_t>(target) - static_cast<std::uint32_t>(SDK::wh::playermodule::AlchemyItemType::Herb1);
            return index < 3 ? &m_assignedHerbs[index] : nullptr;
        }
        std::uint64_t GetAssignedHerbId(SDK::wh::playermodule::AlchemyItemType target) const {
            const auto index = static_cast<std::uint32_t>(target) - static_cast<std::uint32_t>(SDK::wh::playermodule::AlchemyItemType::Herb1);
            return index < 3 ? m_assignedHerbIds[index] : 0;
        }
        std::uint64_t GetAssignedIngredientId(SDK::wh::playermodule::AlchemyItemType target) const {
            if (target >= SDK::wh::playermodule::AlchemyItemType::Herb1 && target <= SDK::wh::playermodule::AlchemyItemType::Herb3) {
                return GetAssignedHerbId(target);
            }
            const auto index = static_cast<std::uint32_t>(target) - static_cast<std::uint32_t>(SDK::wh::playermodule::AlchemyItemType::SpecialIngredient1);
            return index < 3 ? m_assignedSpecialIds[index] : 0;
        }
        std::uint8_t GetNativeState() const {
            return m_state;
        }
        std::uint32_t GetSelectedObjectKind() const {
            return m_selectedObjectKind;
        }
        float GetHourglassRemaining() const {
            return m_hourglassRemaining;
        }
        SDK::EntityId GetObjectEntityId(SDK::wh::playermodule::AlchemyItemType kind) const {
            const auto index = static_cast<std::uint32_t>(kind);
            return index < static_cast<std::uint32_t>(SDK::wh::playermodule::AlchemyItemType::None) ? m_objectEntityIds[index] : 0;
        }
        float GetHeat() const {
            return m_heat;
        }
        bool IsPresentationFadingOut() const {
            return m_state == kFinishingState || m_state == kLeavingState;
        }
        float GetPresentationIntensity() const {
            return m_presentationIntensity;
        }
        void SetPresentationIntensity(float value) {
            m_presentationIntensity = value;
        }
        /** Read the native vector in place; its storage uses the game heap. */
        const SDK::StdVector<SDK::wh::playermodule::C_AlchemyResource *> *GetResources(SDK::wh::playermodule::AlchemyItemType kind) const {
            const auto index = static_cast<std::uint32_t>(kind);
            return index < static_cast<std::uint32_t>(SDK::wh::playermodule::AlchemyItemType::None) ? &m_resources[index] : nullptr;
        }

      private:
        // C_Alchemy::SelectObject at 0x1815FC4A2 accepts state 2 only.
        static constexpr std::uint8_t kInteractiveState = 2;
        // Pot effect Update at 0x180738E17 fades for these native states.
        static constexpr std::uint8_t kFinishingState = 6;
        static constexpr std::uint8_t kLeavingState   = 7;
        SDK::wh::shared::C_Signal m_signal0068;                         // 0x68; ctor 0x1816E9570 dispatch unk_185669440.
        SDK::wh::playermodule::C_AlchemyActionDirector m_actionDirector; // 0x78.
        // 0x108..0x668 is the state block built by 0x1816E9A68 and reset by 0x1816E9C58.
        std::uint64_t m_assignedHerbIds[3];                              // 0x108; accepted Take callback 0x1808D4306.
        SDK::wh::entitymodule::S_ItemClassGUID m_assignedHerbs[3];       // 0x120; factory 0x1808D2165 indexes by herb slot.
        SDK::EntityId m_objectEntityIds[21];                             // 0x150; pot effect resolves Pot at 0x168.
        std::uint32_t m_objectValues01A4[21];                            // 0x1A4; reset 0x1816E9C58 zeroes alongside the entity ids.
        std::uint64_t m_objectHandles01F8[21];                           // 0x1F8; ctor 0x1816E9A68 seeds each from sub_1804FD80C() + 3.
        SDK::wh::playermodule::AlchemyItemType m_heldObjectKind;          // 0x2A0; getter 0x1808D237C; reset to None.
        SDK::wh::playermodule::AlchemyItemType m_secondaryHeldObjectKind; // 0x2A4; same getter's None fallback.
        SDK::wh::shared::C_Signal m_heldObjectChanged;                    // 0x2A8; ctor subscribes RefreshHeldItemControls 0x1806C67B8.
        std::uint64_t m_assignedSpecialIds[3]; // 0x2B8; accepted Special Attach at 0x182E19669.
        std::byte m_opaque02D0[0x30];
        std::uint8_t m_state; // 0x300; E_AlchemyState::Type
        std::byte m_padding0301[0x3]; // reset sub_1816E9C58 writes +0x300 and +0x304 only
        std::uint32_t m_selectedObjectKind; // 0x304; reset to 21 (None).
        bool m_cameraLimitsEnabled;                       // 0x308; reset 0; signal drives SetCameraLimitsEnabled 0x180B66A60.
        std::byte m_padding0309[0x7];
        SDK::wh::shared::C_Signal m_cameraLimitsChanged;  // 0x310
        float m_heat; // 0x320; native setter 0x1807391E0; reset 0.01f.
        std::byte m_padding0324[0x4];
        SDK::wh::shared::C_Signal m_heatChanged; // 0x328; setter broadcasts old/new floats.
        float m_hourglassRemaining; // 0x338, normally 0..1
        std::byte m_padding033C[0x4];
        SDK::wh::shared::C_Signal m_hourglassChanged; // 0x340
        std::uint32_t m_value0350;                    // 0x350; reset 0.
        std::byte m_padding0354[0x4];
        SDK::wh::shared::C_Signal m_signal0358;       // 0x358
        std::uint32_t m_value0368;                    // 0x368; reset 0.
        std::byte m_padding036C[0x4];
        SDK::wh::shared::C_Signal m_signal0370;       // 0x370
        std::uint32_t m_value0380;                    // 0x380; reset 0.
        std::byte m_padding0384[0x4];
        SDK::wh::shared::C_Signal m_signal0388;       // 0x388
        bool m_flag0398;                              // 0x398; reset 0.
        std::byte m_padding0399[0x7];
        SDK::wh::shared::C_Signal m_signal03A0;       // 0x3A0
        bool m_flag03B0;                              // 0x3B0; reset 0.
        std::byte m_padding03B1[0x7];
        SDK::wh::shared::C_Signal m_signal03B8;       // 0x3B8
        bool m_flag03C8;                              // 0x3C8; reset 0.
        std::byte m_padding03C9[0x7];
        SDK::wh::shared::C_Signal m_signal03D0;       // 0x3D0
        bool m_tableUserAssignments;                  // 0x3E0; reset 0; signal drives SetTableUserAssignments 0x1809F16A8.
        std::byte m_padding03E1[0x7];
        SDK::wh::shared::C_Signal m_tableUserAssignmentsChanged; // 0x3E8
        bool m_inputEnabled;                          // 0x3F8; reset 1; signal drives SetInputEnabled 0x182E18E58.
        std::byte m_padding03F9[0x7];
        SDK::wh::shared::C_Signal m_inputEnabledChanged; // 0x400
        std::uint32_t m_value0410;                    // 0x410; reset 0; signal drives sub_1809F1F2C.
        std::byte m_padding0414[0x4];
        SDK::wh::shared::C_Signal m_signal0418;       // 0x418
        bool m_potOnFire; // 0x428; heat predicate 0x1807391C4.
        bool m_potMoving; // 0x429; same predicate excludes movement.
        std::byte m_padding042A[0x6];
        SDK::wh::shared::C_Signal m_potStateChanged;  // 0x430
        std::uint8_t m_boiling; // 0x440; pot effect Update 0x180738CBC.
        std::byte m_padding0441[0x7];
        SDK::wh::shared::C_Signal m_boilingChanged; // 0x448; changed byte notification.
        float m_presentationIntensity; // 0x458; pot effect Update visual suffix.
        std::uint32_t m_objectKind045C; // 0x45C; reset to 21 (None).
        SDK::wh::shared::C_Signal m_signal0460;       // 0x460
        SDK::StdVector<SDK::wh::playermodule::C_AlchemyResource *> m_resources[21]; // 0x470; Reset visits all 21 groups.
        std::uint32_t m_value0668;                    // 0x668; reset 0.
        std::byte m_opaque066C[0xC];
        std::uint32_t m_value0678;                    // 0x678; ctor 0x1816E9570 writes 0.
        std::byte m_padding067C[0x4];
        void *m_moduleObject0680;                     // 0x680; ctor copies (*(GameModules+0xE0))+0xC8.
        std::uint32_t m_capacity0688;                 // 0x688; ctor writes 21.
        std::byte m_padding068C[0x4];
        void *m_ptr0690;                              // 0x690; ctor 0.
        void *m_ptr0698;                              // 0x698; ctor 0.
        std::uint16_t m_value06A0;                    // 0x6A0; ctor 0.
        bool m_flag06A2;                              // 0x6A2; ctor 1.
        std::byte m_padding06A3[0x5];
        void *m_ptr06A8;                              // 0x6A8; ctor 0.
        void *m_ptr06B0;                              // 0x6B0; ctor 0.
        void *m_ptr06B8;                              // 0x6B8; ctor 0.
        SDK::wh::playermodule::C_Alchemy *m_pOwner;   // 0x6C0; ctor writes this.
        void *m_ptr06C8;                              // 0x6C8; ctor 0.
        std::uint32_t m_value06D0;                    // 0x6D0; ctor 0.
        std::byte m_padding06D4[0x4];

        static void AssertLayout() {
            static_assert(offsetof(C_Alchemy, m_actionDirector) == 0x78);
            static_assert(offsetof(C_Alchemy, m_assignedHerbIds) == 0x108);
            static_assert(offsetof(C_Alchemy, m_assignedHerbs) == 0x120);
            static_assert(offsetof(C_Alchemy, m_objectEntityIds) == 0x150);
            static_assert(offsetof(C_Alchemy, m_heldObjectKind) == 0x2A0);
            static_assert(offsetof(C_Alchemy, m_secondaryHeldObjectKind) == 0x2A4);
            static_assert(offsetof(C_Alchemy, m_assignedSpecialIds) == 0x2B8);
            static_assert(offsetof(C_Alchemy, m_state) == 0x300);
            static_assert(offsetof(C_Alchemy, m_selectedObjectKind) == 0x304);
            static_assert(offsetof(C_Alchemy, m_heat) == 0x320);
            static_assert(offsetof(C_Alchemy, m_heatChanged) == 0x328);
            static_assert(offsetof(C_Alchemy, m_hourglassRemaining) == 0x338);
            static_assert(offsetof(C_Alchemy, m_potOnFire) == 0x428);
            static_assert(offsetof(C_Alchemy, m_potMoving) == 0x429);
            static_assert(offsetof(C_Alchemy, m_boiling) == 0x440);
            static_assert(offsetof(C_Alchemy, m_boilingChanged) == 0x448);
            static_assert(offsetof(C_Alchemy, m_presentationIntensity) == 0x458);
            static_assert(offsetof(C_Alchemy, m_resources) == 0x470);
            static_assert(offsetof(C_Alchemy, m_signal0068) == 0x68);
            static_assert(offsetof(C_Alchemy, m_objectHandles01F8) == 0x1F8);
            static_assert(offsetof(C_Alchemy, m_heldObjectChanged) == 0x2A8);
            static_assert(offsetof(C_Alchemy, m_cameraLimitsEnabled) == 0x308);
            static_assert(offsetof(C_Alchemy, m_cameraLimitsChanged) == 0x310);
            static_assert(offsetof(C_Alchemy, m_hourglassChanged) == 0x340);
            static_assert(offsetof(C_Alchemy, m_tableUserAssignmentsChanged) == 0x3E8);
            static_assert(offsetof(C_Alchemy, m_inputEnabledChanged) == 0x400);
            static_assert(offsetof(C_Alchemy, m_potStateChanged) == 0x430);
            static_assert(offsetof(C_Alchemy, m_signal0460) == 0x460);
            static_assert(offsetof(C_Alchemy, m_value0668) == 0x668);
            static_assert(offsetof(C_Alchemy, m_pOwner) == 0x6C0);
        }
    };

    static_assert(sizeof(C_Alchemy) == 0x6D8);
} // namespace SDK::wh::playermodule
