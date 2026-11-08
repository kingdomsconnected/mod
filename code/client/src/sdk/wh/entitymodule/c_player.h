#pragma once

#include "../../cryengine/action/i_game_framework_listener.h"
#include "../framework/i_action.h"
#include "../shared/c_signal.h"
#include "../xgenaimodule/behaviortree/e_trespass_level.h"
#include "c_cart_player_control.h"
#include "c_dog_interaction_manager.h"
#include "c_human.h"
#include "c_missile_weapon_player_controller.h"
#include "c_player_input.h"
#include "c_player_interactor.h"
#include "c_player_material_effects.h"
#include "c_player_movement_action.h"
#include "c_rider_player_control.h"
#include "e_combat_state.h"
#include "i_equipment_manager_listener.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_ChatFollowManager;
    class C_FrontCollision;
    class C_Horse;
    class C_PlayerCarryCorpse;
    class C_PlayerTenseCircumstance;
    class C_PlayerUnconscious;
    class C_RigidOnlyCollision;

    /**
     * Constructor-proven local-player state owning trespass/combat model
     * properties. The stripped source type name is intentionally not guessed.
     */
    struct PlayerLocalStateLayout {
        std::uint8_t m_sprintHeld; // 0x00 (C_Player+0xB20), set by C_PlayerInput::OnSprint; cleared too by PreUpdate with no movement input and by C_Player::MountHorse
        std::byte m_padding0001[0x7];
        void *m_pTrespassLevelPropertyVtable; // 0x08
        xgenaimodule::BehaviorTree::E_TrespassLevel::Type m_trespassLevel; // 0x10
        std::byte m_padding0011[0x7];
        wh::shared::C_Signal m_trespassLevelBinding; // 0x18
        std::byte m_opaque0028[0x8];
        std::uint8_t m_flyMode; // 0x30
        std::byte m_padding0031[0x3];
        std::uint32_t m_state0034[2];
        std::uint32_t m_state003C;
        void *m_pCombatStatePropertyVtable; // 0x40
        E_CombatState::Type m_combatState; // 0x48
        std::byte m_padding0049[0x7];
        wh::shared::C_Signal m_combatStateBinding; // 0x50
        std::byte m_opaque0060[0x8];

        bool IsFlyMode() const { return m_flyMode != 0; }

        /** Sprint key state: set on press and hold, cleared on release. */
        bool IsSprintHeld() const { return m_sprintHeld != 0; }
        void SetSprintHeld(bool held) { m_sprintHeld = static_cast<std::uint8_t>(held); }

        xgenaimodule::BehaviorTree::E_TrespassLevel::Type GetTrespassLevel() const {
            return m_trespassLevel;
        }
        E_CombatState::Type GetCombatState() const { return m_combatState; }
    };

    static_assert(sizeof(PlayerLocalStateLayout) == 0x68,
                  "PlayerLocalStateLayout size mismatch");

    /** Constructor-initialized state immediately preceding local properties. */
    struct PlayerAdviceStateLayout {
        static constexpr std::uint8_t FastTravelEnabledMask = 0x10;

        void *m_state0000; // 0x00
        void *m_state0008; // 0x08
        void *m_state0010; // 0x10
        void *m_state0018; // 0x18
        std::uint32_t m_state0020;
        std::uint8_t m_flags0024; // bit 0x10 serializes as fastTravelEnabled
        std::byte m_padding0025[0x3];
        float m_movementPace; // 0x28, C_Player+0xAE8; native slots 133/134
        std::byte m_padding002C[0xC];
        std::int64_t m_state0038; // initialized to INT64_MIN
        std::int64_t m_state0040; // initialized to -100000
        C_PlayerInteractor *m_pInteractor; // 0x48, C_Player+0xB08; allocated and constructed in C_Player::C_Player (0x180BC7B28)
        void *m_pOwnedState0050; // owned 0x28-byte non-polymorphic state
        std::uint32_t m_state0058;
        std::byte m_padding005C[0x4];

        bool IsFastTravelEnabled() const {
            return (m_flags0024 & FastTravelEnabledMask) != 0;
        }
    };

    struct PlayerTerminalStateLayout {
        wh::shared::C_Signal m_modelPropertyBinding; // 0x00
        void *m_state0010;
        std::byte m_opaque0018[0x14];
        std::uint32_t m_state002C; // initialized to 4
        void *m_state0030;
        void *m_state0038;
        std::byte m_opaque0040[0x8];
        void *m_state0048;
    };

    static_assert(offsetof(PlayerAdviceStateLayout, m_movementPace) == 0x28, "PlayerAdviceStateLayout movement pace offset mismatch");
    static_assert(offsetof(PlayerAdviceStateLayout, m_pInteractor) == 0x48, "PlayerAdviceStateLayout interactor offset mismatch");

    static_assert(sizeof(PlayerAdviceStateLayout) == 0x60,
                  "PlayerAdviceStateLayout size mismatch");
    static_assert(sizeof(PlayerTerminalStateLayout) == 0x50,
                  "PlayerTerminalStateLayout size mismatch");

    /**
     * Concrete local-player actor.
     *
     * C_Player's primary table at 0x183AA4B98 contains 289 slots: all 283
     * C_Human entries followed by six opaque C_Player contracts. The complete
     * object is 0xE00 bytes. IGameObjectView and IGameObjectProfileManager
     * secondary vptrs remain at +0x40/+0x48; I_EquipmentManagerListener and
     * IGameFrameworkListener are exact secondary bases at +0xA60/+0xA68.
     * The named GetFlyMode/SetFlyMode pair reads and writes the byte at +0xB50.
     * Named constructors further establish an inline C_PlayerMaterialEffects
     * block at +0xBF0 and C_MissileWeaponPlayerController at +0xD70. The
     * intervening owning pointers are typed only where their allocation paths
     * install an unambiguous class vtable. The owned 0x20-byte object at
     * +0xD50 caches animation-graph parameter identifiers for
     * player_armorload, player_hit_side, is_master_strike, and player_bushman;
     * its stripped source class name remains unknown.
     */
    class C_Player : public C_Human,
                     public I_EquipmentManagerListener,
                     public SDK::IGameFrameworkListener {
      public:
        bool UseConsumableItem(C_Item *item, std::int32_t amount, std::int32_t context, bool checkFood, std::int32_t saveSlot) {
            std::uint32_t result = 128;
            hook::this_call<void>(gPatterns.C_Player__UseItem, this, &result, item, amount, context, checkFood, saveSlot);
            return (result & 0x1FEU) == 0;
        }

        /** The horse this body's ride reference names (0x180A4F980), or null once it no longer resolves. */
        C_Horse *GetRiddenHorse() const {
            return hook::this_call<C_Horse *>(gPatterns.C_Player__GetRiddenHorse, this);
        }

        static constexpr const char *ActorClassName = "C_Player";
        static constexpr ActorClass ActorClassId = ActorClass_Player;

        ~C_Player() override = default;

        virtual void unk_fn0283() = 0; // first C_Player slot, +0x8D8
        virtual void unk_fn0284() = 0;
        virtual void SetTrespassLevel(
            xgenaimodule::BehaviorTree::E_TrespassLevel::Type trespassLevel) = 0;
        virtual void unk_fn0286() = 0;
        virtual void unk_fn0287() = 0;
        virtual void unk_fn0288() = 0; // final C_Player slot, +0x900

        static C_Player *GetInstance();

        PlayerLocalStateLayout *GetLocalState() { return &m_localState; }
        const PlayerLocalStateLayout *GetLocalState() const { return &m_localState; }
        PlayerAdviceStateLayout *GetAdviceState() { return &m_adviceState; }
        const PlayerAdviceStateLayout *GetAdviceState() const {
            return &m_adviceState;
        }
        bool IsFastTravelEnabled() const {
            return m_adviceState.IsFastTravelEnabled();
        }
        C_PlayerInput *GetPlayerInput() const { return m_pPlayerInput; }
        C_RiderPlayerControl *GetRiderPlayerControl() const {
            return m_pRiderPlayerControl;
        }
        C_PlayerInteractor *GetInteractor() const { return m_adviceState.m_pInteractor; }
        C_PlayerMaterialEffects *GetPlayerMaterialEffects() {
            return &m_playerMaterialEffects;
        }
        const C_PlayerMaterialEffects *GetPlayerMaterialEffects() const {
            return &m_playerMaterialEffects;
        }
        C_RigidOnlyCollision *GetRigidOnlyCollision() const {
            return m_pRigidOnlyCollision;
        }
        C_FrontCollision *GetFrontCollision() const {
            return m_pFrontCollision;
        }
        C_ChatFollowManager *GetChatFollowManager() const {
            return m_pChatFollowManager;
        }
        C_PlayerCarryCorpse *GetCarryCorpseController() const {
            return m_pPlayerCarryCorpse;
        }
        C_PlayerUnconscious *GetUnconsciousController() const {
            return m_pPlayerUnconscious;
        }
        C_PlayerTenseCircumstance *GetTenseCircumstanceController() const {
            return m_pTenseCircumstance;
        }
        C_CartPlayerControl *GetCartPlayerControl() const {
            return m_pCartPlayerControl;
        }
        C_MissileWeaponPlayerController *GetMissileWeaponController() {
            return &m_missileWeaponPlayerController;
        }
        const C_MissileWeaponPlayerController *GetMissileWeaponController() const {
            return &m_missileWeaponPlayerController;
        }
        PlayerTerminalStateLayout *GetTerminalState() {
            return &m_terminalState;
        }
        const PlayerTerminalStateLayout *GetTerminalState() const {
            return &m_terminalState;
        }

      private:
        static void AssertLayout() {
            static_assert(offsetof(C_Player, m_adviceState) == 0xAC0, "C_Player advice state offset mismatch");
            static_assert(offsetof(C_Player, m_pMovementAction) == 0xB90, "C_Player::m_pMovementAction offset mismatch");
            static_assert(offsetof(C_Player, m_pPlayAnimAction) == 0xBA0, "C_Player::m_pPlayAnimAction offset mismatch");
            static_assert(offsetof(C_Player, m_pEventSubscription0CB8) == 0xCB8, "C_Player::m_pEventSubscription0CB8 offset mismatch");
            static_assert(offsetof(C_Player, m_audioInCrouch) == 0xCC0, "C_Player::m_audioInCrouch offset mismatch");
            static_assert(offsetof(C_Player, m_audioStealthSkill) == 0xCC4, "C_Player::m_audioStealthSkill offset mismatch");
            static_assert(offsetof(C_Player, m_pHelmetActionHint) == 0xCC8, "C_Player::m_pHelmetActionHint offset mismatch");
            static_assert(offsetof(C_Player, m_cameraLimitReleaseYawSin) == 0xCD8, "C_Player::m_cameraLimitReleaseYawSin offset mismatch");
            static_assert(offsetof(C_Player, m_pDogInteractionManager) == 0xCE0, "C_Player::m_pDogInteractionManager offset mismatch");
            static_assert(offsetof(C_Player, m_pLadderStateListener) == 0xD18, "C_Player::m_pLadderStateListener offset mismatch");
            static_assert(offsetof(C_Player, m_pFallStateListener) == 0xD28, "C_Player::m_pFallStateListener offset mismatch");
            static_assert(offsetof(C_Player, m_pMinigameStateListener) == 0xD30, "C_Player::m_pMinigameStateListener offset mismatch");
            static_assert(offsetof(C_Player, m_pActorState10Listener) == 0xD40, "C_Player::m_pActorState10Listener offset mismatch");
            static_assert(offsetof(C_Player, m_pRiderPlayerControl) == 0xD68, "C_Player::m_pRiderPlayerControl offset mismatch");
        }

        wh::shared::C_Signal m_modelPropertyBindings[5]; // 0xA70
        PlayerAdviceStateLayout m_adviceState; // 0xAC0
        PlayerLocalStateLayout m_localState; // 0xB20; fly mode at 0xB50
        C_PlayerInput *m_pPlayerInput; // 0xB88
        C_PlayerMovementAction *m_pMovementAction; // 0xB90, created and queued by SetupProceduralAnimationContexts (0x18113A11C), released by ResetMannequinActionController
        void *m_pOwnedState0B98; // 0xB98, owned polymorphic object deleted through slot 0 in Release
        wh::framework::I_Action *m_pPlayAnimAction; // 0xBA0, the generic action PlayAnim queues
        std::byte m_embeddedState0BA8[0x48];
        C_PlayerMaterialEffects m_playerMaterialEffects; // 0xBF0
        C_RigidOnlyCollision *m_pRigidOnlyCollision;      // 0xCA8
        C_FrontCollision *m_pFrontCollision;              // 0xCB0
        void *m_pEventSubscription0CB8; // 0xCB8, event 21 subscription made in Init, removed in Release
        std::int32_t m_audioInCrouch;     // 0xCC0, bound to the audio parameter "player_in_crouch" in Init
        std::int32_t m_audioStealthSkill; // 0xCC4, bound to the audio parameter "player_stealth_skill" in Init
        void *m_pHelmetActionHint;        // 0xCC8, shown and hidden by RefreshHelmetActionHint through slots 2 and 3
        std::uint64_t m_cameraLimitReleaseState; // 0xCD0, cleared when the last camera limit is released
        float m_cameraLimitReleaseYawSin; // 0xCD8, the view yaw captured by SetCameraLimitsEnabled (0x180B66DFC)
        float m_cameraLimitReleaseYawCos; // 0xCDC, initialized to 1.0f
        C_DogInteractionManager *m_pDogInteractionManager; // 0xCE0, 0x28-byte allocation; AcquireSoul drives its stealth-kill subscription
        C_ChatFollowManager *m_pChatFollowManager;        // 0xCE8
        void *m_pOwnedState0CF0; // owned 0x98-byte non-polymorphic state
        std::byte m_state0CF8[0x20];
        void *m_pLadderStateListener; // 0xD18, actor-state listener: on Ladder (3) it counts the rigid-only and front collisions off
        C_PlayerCarryCorpse *m_pPlayerCarryCorpse;        // 0xD20
        void *m_pFallStateListener; // 0xD28, actor-state listener: on Fall (18) it takes save lock 11
        void *m_pMinigameStateListener; // 0xD30, actor-state listener: on Minigame (4) it switches the interactor's mode at +0x293
        C_CartPlayerControl *m_pCartPlayerControl;        // 0xD38, std::unique_ptr moved in from MakeCartPlayerControl in C_Player::Init (0x180A4C3B4, move 0x180A4C490)
        void *m_pActorState10Listener; // 0xD40, actor-state listener for state 10, also subscribed to the actor model and the combat actor
        C_PlayerUnconscious *m_pPlayerUnconscious;        // 0xD48
        void *m_pAnimationParameterCache;                 // 0xD50, owned 0x20-byte allocation
        void *m_pOwnedState0D58;
        C_PlayerTenseCircumstance *m_pTenseCircumstance; // 0xD60
        C_RiderPlayerControl *m_pRiderPlayerControl;     // 0xD68, allocated at 0x180A4C5D0
        C_MissileWeaponPlayerController m_missileWeaponPlayerController; // 0xD70
        PlayerTerminalStateLayout m_terminalState; // 0xDB0
    };

    static_assert(sizeof(C_Player) == 0xE00, "C_Player size mismatch");
} // namespace SDK::wh::entitymodule
