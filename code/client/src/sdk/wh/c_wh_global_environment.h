#pragma once

#include "combatmodule/c_combat_module.h"
#include "entitymodule/c_entity_actions.h"
#include "entitymodule/c_entity_module.h"
#include "framework/i_ui_apse.h"
#include "framework/i_ui_item_transfer.h"
#include "framework/i_ui_shop.h"
#include "game/dlc/i_dlc_manager.h"
#include "questmodule/c_quest_module.h"
#include "rpgmodule/i_soul_system.h"
#include "shopmodule/c_shop_module.h"
#include "xgenaimodule/c_xgen_ai_module.h"

#include "../patterns.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh {
    namespace rpgmodule {
        class C_Calendar;
        class C_RPGModule;
    } // namespace rpgmodule
    namespace environmentmodule {
        class C_EnvironmentModule;
    }
    namespace dialogmodule {
        class C_DialogModule;
    }
    namespace game {
        class C_CameraManager;
    }
    namespace guimodule {
        class C_GUIModule;
        class C_UIStateBlockers;
    } // namespace guimodule
    namespace playermodule {
        class C_PlayerModule;
    }
    /** Typed layout of Warhorse's process-global module registry. */
    class C_GlobalEnvironment final {
      public:
        static C_GlobalEnvironment *GetInstance() {
            return *reinterpret_cast<C_GlobalEnvironment **>(gPatterns.WHGlobalEnvironment__Instance);
        }

        rpgmodule::I_SoulSystem *GetSoulSystem() const {
            return m_pSoulSystem;
        }
        combatmodule::C_CombatModule *GetCombatModule() const {
            return m_pCombatModule;
        }
        rpgmodule::C_Calendar *GetCalendar() const {
            return m_pCalendar;
        }

        rpgmodule::C_RPGModule *GetRPGModule() const {
            return m_pRPGModule;
        }

        environmentmodule::C_EnvironmentModule *GetEnvironmentModule() const {
            return m_pEnvironmentModule;
        }
        /** The input action names; see `entitymodule::C_EntityActions`. */
        entitymodule::C_EntityActions *GetEntityActions() const {
            return m_pEntityActions;
        }
        game::C_CameraManager *GetCameraManager() const {
            return m_pCameraManager;
        }
        game::dlc::I_DLCManager *GetDLCManager() const {
            return m_pDLCManager;
        }
        dialogmodule::C_DialogModule *GetDialogModule() const {
            return m_pDialogModule;
        }

        guimodule::C_GUIModule *GetGUIModule() const {
            return m_pGUIModule;
        }

        guimodule::C_UIStateBlockers *GetUIStateBlockers() const {
            return m_pUIStateBlockers;
        }

        playermodule::C_PlayerModule *GetPlayerModule() const {
            return m_pPlayerModule;
        }

        questmodule::C_QuestModule *GetQuestModule() const {
            return m_pQuestModule;
        }

        /** The shops of the loaded world and their deals; see `shopmodule::C_ShopModule`. */
        shopmodule::C_ShopModule *GetShopModule() const {
            return m_pShopModule;
        }

        /**
         * The trade screen, as an interface; see `framework::I_UIShop`. Set by
         * the screen's own init (0x180C41220) and cleared by its teardown, so
         * null outside the loaded game.
         */
        framework::I_UIShop *GetUIShop() const {
            return m_pUIShop;
        }

        framework::I_UIItemTransfer *GetUIItemTransfer() const {
            return m_pUIItemTransfer;
        }

        /** The pause screen, as an interface; see `framework::I_UIApse`. Null before the GUI has built it. */
        framework::I_UIApse *GetUIApse() const {
            return m_pUIApse;
        }

        xgenaimodule::C_XGenAIModule *GetXGenAIModule() const {
            return m_pXGenAIModule;
        }

      private:
        // Slots without a getter are typed from their writers: the Warhorse global setup 0x180C103AC, C_Game's init 0x1806034B4, the singleton ctor (which zeroes every slot) and each subsystem ctor that stores itself.
        void *m_pWarhorseGlobal;  // 0x00, qword_185168B38 (S_GameSessionLatch lives at its +0xB8), stored by 0x180C103AC
        void *m_pGameFramework;   // 0x08, CCryAction, stored by its ctor 0x181865C34
        void *m_pGameFramework10; // 0x10, the same CCryAction, stored alongside
        void *m_pGame;            // 0x18, wh::game::C_Game, stored by its init
        void *m_pGameCVars;       // 0x20, wh::S_WHGameCVars (0x968 bytes, zero-filled), destroyed by the teardown 0x18257FC04
        void *m_pState0028;       // 0x28, a one-byte allocation
        // 0x30, stored by the entity module's init (0x1810645B8) at 0x1810646CA
        // and read by C_PlayerInput's constructor at 0x18079A059.
        entitymodule::C_EntityActions *m_pEntityActions;
        game::C_CameraManager *m_pCameraManager; // 0x38
        void *m_pObject0040;                // 0x40, 0x280-byte object built by 0x180603BF0 in C_Game's init
        void *m_pObject0048;                // 0x48, 0x160-byte object built by 0x180603BD8 in C_Game's init
        void *m_pPlayerProfileManager;      // 0x50, framework::C_PlayerProfileWHManager
        void *m_pObject0058;                // 0x58, 0x20-byte object built by 0x180C1065C
        void *m_pObject0060;                // 0x60, 0x478-byte object built by 0x180C10248; it constructs a framework::C_DefaultEESymbolResolver
        void *m_pObject0068;                // 0x68, no writer found; the teardown destroys it through vtable slot 5
        void *m_pLODAreaManager;            // 0x70, framework::C_LODAreaManager
        void *m_pVisibilityAreaManager;     // 0x78, framework::C_VisibilityAreaManager
        // 0x80, read by 0x1839C9EC4, 0x182579154, 0x1825BD718 and 0x180B41500.
        game::dlc::I_DLCManager *m_pDLCManager;
        void *m_pGameField0088;    // 0x88, copied from C_Game +0xB0 by its init
        void *m_pObject0090;       // 0x90, 0x230-byte object built by 0x180C1056C (the framework's reflection examples)
        std::uint64_t m_state0098; // 0x98, zeroed by the singleton ctor; no reader or writer found
        void *m_pObject00A0;       // 0xA0, read by CScriptBindGame::ShowOverlayTutorial; no writer found
        void *m_pAnsel;            // 0xA8, wh::game::C_Ansel, only when the system enables it
        void *m_pPhotomodeManager; // 0xB0, wh::game::C_PhotomodeManager
        void *m_pModulesManager;   // 0xB8, framework::C_ModulesManager, which the modules' messages go through
        void *m_pVector00C0;       // 0xC0, a heap std::vector the teardown frees
        void *m_pGameField00C8;    // 0xC8, copied from C_Game +0xA8
        void *m_pGameField00D0;    // 0xD0, copied from C_Game +0xB8
        guimodule::C_UIStateBlockers *m_pUIStateBlockers; // 0xD8
        entitymodule::C_EntityModule *m_pEntityModule; // 0xE0, built by 0x180BE8968 in the module construction 0x180C9DA44
        guimodule::C_GUIModule *m_pGUIModule; // 0xE8
        void *m_pAnimationModule; // 0xF0, animationmodule::C_AnimationModule, built by 0x180D24284
        combatmodule::C_CombatModule *m_pCombatModule; // 0xF8
        void *m_pSoundModule;     // 0x100, soundmodule::C_SoundModule, built by 0x181810530
        void *m_pMusicModule;     // 0x108, musicmodule::C_MusicModule, built by 0x18120FF38
        // 0x110, read as the shop module by C_Shop::OpenForKeeper (0x180C406D8)
        // and by the shop's own init.
        shopmodule::C_ShopModule *m_pShopModule;
        questmodule::C_QuestModule *m_pQuestModule; // 0x118
        // The concept module. Its resource registry sits two hops inside it,
        // and the mod reaches that through the game's own accessor rather
        // than through this field, so the type is left untyped here.
        void *m_pConceptModule;                                       // 0x120
        playermodule::C_PlayerModule *m_pPlayerModule;                // 0x128
        rpgmodule::C_RPGModule *m_pRPGModule;                         // 0x130, installed at 0x180C9DA44
        environmentmodule::C_EnvironmentModule *m_pEnvironmentModule; // 0x138
        void *m_pUtilsModule;     // 0x140, utilsmodule::C_UtilsModule
        void *m_pDatabaseModule;  // 0x148, databasemodule::C_DatabaseModule's second base: stored 0x10 past the object 0x18192BBB4 builds
        // 0x150, installed by the module construction at 0x180C9DA44 alongside
        // every other module named in this class.
        dialogmodule::C_DialogModule *m_pDialogModule;
        void *m_pXBehaviorModule; // 0x158, xbehaviormodule::C_XBehaviorModule
        xgenaimodule::C_XGenAIModule *m_pXGenAIModule; // 0x160, built by 0x181889578 and stored at 0x180C9E073
        std::uint64_t m_state0168; // 0x168, zeroed by the singleton ctor; C_StealItemsEffect::Apply takes its address
        void *m_pEngine0170;       // 0x170, an engine interface called through slots +0x318 and +0x330 (C_ClothingSystem's ctor)
        void *m_p3DEngine;         // 0x178, C3DEngine, stored by 0x180A66278 and cleared by ~C3DEngine
        void *m_pActorSystem;      // 0x180, CActorSystem, stored by its ctor
        void *m_pItemSystem;       // 0x188, CItemSystem, stored by its ctor
        void *m_pEntitySystem;     // 0x190, CEntitySystem, stored by 0x180D25458
        void *m_pAISystem;         // 0x198, CAISystem, stored by 0x180BB7DE4
        void *m_pGameRules;        // 0x1A0, CGameRules, stored by 0x1808E95B8
        rpgmodule::C_Calendar *m_pCalendar;             // 0x1A8
        framework::I_UIApse *m_pUIApse;                 // 0x1B0, read by 0x1811A072C
        framework::I_UIShop *m_pUIShop;                 // 0x1B8
        framework::I_UIItemTransfer *m_pUIItemTransfer; // 0x1C0
        void *m_pUIItemSelection; // 0x1C8, the item-selection screen, stored by its init 0x180C3BCF8
        rpgmodule::I_SoulSystem *m_pSoulSystem; // 0x1D0
        void *m_pFrameworkObject01D8; // 0x1D8, what the CCryAction at +0x10 returns from vtable +0x308, cached by C_Game's init
        void *m_pGameCache;           // 0x1E0, wh::game::CGameCache
        void *m_pPros;                // 0x1E8, framework::C_Pros, built by 0x180C103AC

        static void AssertLayout() {
            static_assert(offsetof(C_GlobalEnvironment, m_pCombatModule) == 0xF8, "C_GlobalEnvironment::m_pCombatModule offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pGame) == 0x18, "C_GlobalEnvironment::m_pGame offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pPlayerProfileManager) == 0x50, "C_GlobalEnvironment::m_pPlayerProfileManager offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pModulesManager) == 0xB8, "C_GlobalEnvironment::m_pModulesManager offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_p3DEngine) == 0x178, "C_GlobalEnvironment::m_p3DEngine offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pGameRules) == 0x1A0, "C_GlobalEnvironment::m_pGameRules offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pUIItemSelection) == 0x1C8, "C_GlobalEnvironment::m_pUIItemSelection offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pPros) == 0x1E8, "C_GlobalEnvironment::m_pPros offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pEntityActions) == 0x30, "C_GlobalEnvironment::m_pEntityActions offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pCameraManager) == 0x38, "C_GlobalEnvironment::m_pCameraManager offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pDLCManager) == 0x80, "C_GlobalEnvironment::m_pDLCManager offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pUIStateBlockers) == 0xD8, "C_GlobalEnvironment::m_pUIStateBlockers offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pGUIModule) == 0xE8, "C_GlobalEnvironment::m_pGUIModule offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pEntityModule) == 0xE0, "C_GlobalEnvironment::m_pEntityModule offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pAnimationModule) == 0xF0, "C_GlobalEnvironment::m_pAnimationModule offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pDatabaseModule) == 0x148, "C_GlobalEnvironment::m_pDatabaseModule offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pXGenAIModule) == 0x160, "C_GlobalEnvironment::m_pXGenAIModule offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pShopModule) == 0x110, "C_GlobalEnvironment::m_pShopModule offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pQuestModule) == 0x118, "C_GlobalEnvironment::m_pQuestModule offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pConceptModule) == 0x120, "C_GlobalEnvironment::m_pConceptModule offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pPlayerModule) == 0x128, "C_GlobalEnvironment::m_pPlayerModule offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pRPGModule) == 0x130, "C_GlobalEnvironment::m_pRPGModule offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pEnvironmentModule) == 0x138, "C_GlobalEnvironment::m_pEnvironmentModule offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pDialogModule) == 0x150, "C_GlobalEnvironment::m_pDialogModule offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pCalendar) == 0x1A8, "C_GlobalEnvironment::m_pCalendar offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pUIApse) == 0x1B0, "C_GlobalEnvironment::m_pUIApse offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pUIShop) == 0x1B8, "C_GlobalEnvironment::m_pUIShop offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pUIItemTransfer) == 0x1C0, "C_GlobalEnvironment::m_pUIItemTransfer offset mismatch");
            static_assert(offsetof(C_GlobalEnvironment, m_pSoulSystem) == 0x1D0, "C_GlobalEnvironment::m_pSoulSystem offset mismatch");
        }
    };

    static_assert(sizeof(C_GlobalEnvironment) == 0x1F0, "C_GlobalEnvironment size mismatch");
} // namespace SDK::wh
