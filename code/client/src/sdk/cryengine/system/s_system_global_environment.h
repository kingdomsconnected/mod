#pragma once

#include "../3dengine/i_3d_engine.h"
#include "../3dengine/i_particle_manager.h"
#include "../action/i_game_framework.h"
#include "../animation/i_character_manager.h"
#include "../audio/i_audio_system.h"
#include "../entitysystem/i_entity_system.h"
#include "../input/i_hardware_mouse.h"
#include "../input/i_input.h"
#include "../physics/c_physical_world.h"
#include "../renderer/i_renderer.h"
#include "../scaleform/i_scaleform_helper.h"
#include "../scriptsystem/i_script_system.h"
#include "i_console.h"
#include "i_cry_pak.h"
#include "i_system.h"
#include "i_timer.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * `gEnv`: CryEngine's table of subsystem pointers, one global instance
     * at 0x18492D800
     * that `CSystem`'s constructor keeps a pointer to (0x180A52DC1).
     *
     * Every `CEngineModule_*::Initialize` writes its subsystem straight into
     * it, and every native reads them back by absolute address, which is what
     * fixes each member below. The order is CryEngine's from before
     * p3DEngine left the table -- with pProfileLogSystem and pVisualLog still
     * in it -- checked slot by slot against each global's readers; the three
     * slots no published name fits stay unnamed.
     *
     * Everything past +0x190 stays opaque. The total
     * size is not proven: the last byte any code touches is +0x3D4 and the
     * next global starts at +0x440, so the modelled extent is a lower bound.
     */
    struct SSystemGlobalEnvironment {
        // Named in CryEngine's member order wherever the slot's readers
        // confirm it; an untyped interface is held as void * with its type
        // in the comment.
        void *pDialogSystem;                // 0x00, IDialogSystem; read by CLevelSystem's load and unload
        I3DEngine *p3DEngine;               // 0x08, the binary's own g_p3DEngine
        void *pNetwork;                     // 0x10, INetwork; CCryAction's connect and map commands
        void *pNetContext;                  // 0x18, INetContext
        void *pLobby;                       // 0x20, ICryLobby
        IScriptSystem *pScriptSystem;       // 0x28, CScriptSystem; read by C_LockBase::SetLocked
        CPhysicalWorld *pPhysicalWorld;     // 0x30, IPhysicalWorld in the header; this build's is CPhysicalWorld
        void *pFlowSystem;                  // 0x38, IFlowSystem; CCustomActionManager, CLevelSystem
        IInput *pInput;                     // 0x40
        void *pStatoscope;                  // 0x48, IStatoscope; CTimeDemoRecorder
        ICryPak *pCryPak;                   // 0x50
        void *pFileChangeMonitor;           // 0x58, IFileChangeMonitor
        void *pProfileLogSystem;            // 0x60, IProfileLogSystem
        IParticleManager *pParticleManager; // 0x68
        void *pOpticsManager;               // 0x70, IOpticsManager; CScriptBind_Entity::ParseLightParams
        void *pFrameProfileSystem;          // 0x78, IFrameProfileSystem; CCryAction pre and post update
        ITimer *pTimer;                     // 0x80, read by C_Dice::InitializeDiceSession (0x182E93D3B)
        void *pCryFont;                     // 0x88, ICryFont; CScriptBind_System::LoadFont
        IGameFramework *pGameFramework;     // 0x90
        void *pLocalMemoryUsage;            // 0x98, ILocalMemoryUsage; CSystem::Update
        IEntitySystem *pEntitySystem;       // 0xA0, CEngineModule_EntitySystem::Initialize
        IConsole *pConsole;                 // 0xA8
        void *m_unknown00B0;                // 0xB0, never read in this build
        IAudioSystem *pAudioSystem;         // 0xB8, CEngineModule_CrySoundSystem::Initialize (0x181174234)
        ISystem *pSystem;                   // 0xC0
        void *m_unknown00C8;                // 0xC8, read by CD3D9Renderer's window and projection paths
        ICharacterManager *pCharacterManager; // 0xD0, CEngineModule_CryAnimation::Initialize (0x18099B504)
        void *pAISystem;                    // 0xD8, IAISystem; CScriptBind_AI
        void *pLog;                         // 0xE0, ILog; CXConsole::AddCommand
        void *pCodeCheckpointMgr;           // 0xE8, ICodeCheckpointMgr; C_TestCheckPoint
        void *pMovieSystem;                 // 0xF0, IMovieSystem; CScriptBind_Movie, the track-view nodes
        void *pNameTable;                   // 0xF8, INameTable; CCryName
        void *pVisualLog;                   // 0x100, IVisualLog
        IRenderer *pRenderer;               // 0x108, copied by CFlowInspectorDefault's constructor (0x1803DF3AC)
        void *pAuxGeomRenderer;             // 0x110, IRenderAuxGeom
        IHardwareMouse *pHardwareMouse;     // 0x118
        void *pMaterialEffects;             // 0x120, IMaterialEffects; PlayHitMaterialEffect (0x180B24A0C)
        void *pJobManager;                  // 0x128, JobManager::IJobManager; skinning and SyncAllAnimations
        void *pOverloadSceneManager;        // 0x130, IOverloadSceneManager; CSystem::Update
        void *pFlashUI;                     // 0x138, IFlashUI; CScriptBind_UIAction
        void *pUIFramework;                 // 0x140, UIFramework::IUIFramework
        void *pServiceNetwork;              // 0x148, IServiceNetwork; CRemoteCommandClient/Server
        void *pRemoteCommandManager;        // 0x150, IRemoteCommandManager
        void *pDynamicResponseSystem;       // 0x158, DRS::IDynamicResponseSystem
        void *pThreadManager;               // 0x160, IThreadManager; CThreadManager::RegisterThirdPartyThreadImpl
        IScaleformHelper *pScaleformHelper; // 0x168; CFlashUIElement::GetBootStrapper, created by 0x1819DEAD4
        void *m_unknown0170;                // 0x170, written by 0x180E3C9E4
        void *m_pPlatformServices0178;      // 0x178, read by the achievement, DLC and optical-drive paths
        void *m_unknown0180;                // 0x180, read by CanStartJump and CProceduralClipJump
        void *pEngineLockBuffers;           // 0x188, wh::C_EngineLockBuffers; the binary's own wh::g_pEngineLockBuffers, set in CSystem::Init
        std::byte m_opaque0190[0x248];      // 0x190

        /** The one instance, resolved from the entity-system thunk in `patterns.cpp`. */
        static SSystemGlobalEnvironment &Get();

      private:
        static void AssertLayout() {
            static_assert(offsetof(SSystemGlobalEnvironment, pScriptSystem) == 0x28, "SSystemGlobalEnvironment::pScriptSystem offset mismatch");
            static_assert(offsetof(SSystemGlobalEnvironment, pPhysicalWorld) == 0x30, "SSystemGlobalEnvironment::pPhysicalWorld offset mismatch");
            static_assert(offsetof(SSystemGlobalEnvironment, pInput) == 0x40, "SSystemGlobalEnvironment::pInput offset mismatch");
            static_assert(offsetof(SSystemGlobalEnvironment, pCryPak) == 0x50, "SSystemGlobalEnvironment::pCryPak offset mismatch");
            static_assert(offsetof(SSystemGlobalEnvironment, pParticleManager) == 0x68, "SSystemGlobalEnvironment::pParticleManager offset mismatch");
            static_assert(offsetof(SSystemGlobalEnvironment, pTimer) == 0x80, "SSystemGlobalEnvironment::pTimer offset mismatch");
            static_assert(offsetof(SSystemGlobalEnvironment, pEntitySystem) == 0xA0, "SSystemGlobalEnvironment::pEntitySystem offset mismatch");
            static_assert(offsetof(SSystemGlobalEnvironment, pConsole) == 0xA8, "SSystemGlobalEnvironment::pConsole offset mismatch");
            static_assert(offsetof(SSystemGlobalEnvironment, pAudioSystem) == 0xB8, "SSystemGlobalEnvironment::pAudioSystem offset mismatch");
            static_assert(offsetof(SSystemGlobalEnvironment, pSystem) == 0xC0, "SSystemGlobalEnvironment::pSystem offset mismatch");
            static_assert(offsetof(SSystemGlobalEnvironment, pCharacterManager) == 0xD0, "SSystemGlobalEnvironment::pCharacterManager offset mismatch");
            static_assert(offsetof(SSystemGlobalEnvironment, pRenderer) == 0x108, "SSystemGlobalEnvironment::pRenderer offset mismatch");
            static_assert(offsetof(SSystemGlobalEnvironment, pHardwareMouse) == 0x118, "SSystemGlobalEnvironment::pHardwareMouse offset mismatch");
            static_assert(offsetof(SSystemGlobalEnvironment, p3DEngine) == 0x08, "SSystemGlobalEnvironment::p3DEngine offset mismatch");
            static_assert(offsetof(SSystemGlobalEnvironment, pGameFramework) == 0x90, "SSystemGlobalEnvironment::pGameFramework offset mismatch");
            static_assert(offsetof(SSystemGlobalEnvironment, pThreadManager) == 0x160, "SSystemGlobalEnvironment::pThreadManager offset mismatch");
            static_assert(offsetof(SSystemGlobalEnvironment, pScaleformHelper) == 0x168, "SSystemGlobalEnvironment::pScaleformHelper offset mismatch");
            static_assert(offsetof(SSystemGlobalEnvironment, pEngineLockBuffers) == 0x188, "SSystemGlobalEnvironment::pEngineLockBuffers offset mismatch");
        }
    };

    // A lower bound, not a proven size -- see above.
    static_assert(sizeof(SSystemGlobalEnvironment) == 0x3D8, "SSystemGlobalEnvironment modelled extent mismatch");
} // namespace SDK
