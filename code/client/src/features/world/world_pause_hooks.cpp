#include <utils/safe_win32.h>

#include <MinHook.h>
#include <logging/logger.h>

#include <utils/hooking/hook_function.h>

#include "sdk/cryengine/action/c_view.h"
#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/animation/i_default_skeleton.h"
#include "sdk/cryengine/animation/i_skeleton_pose.h"
#include "sdk/cryengine/audio/i_audio_system.h"
#include "sdk/patterns.h"
#include "sdk/wh/game/c_camera_manager.h"

#include "features/world/world_service.h"
#include "common/module_support.h"

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdint>

namespace KCDC::Features::World {
    namespace {
        using PauseGame_t               = void(__fastcall *)(SDK::IGameFramework *, bool, std::uint16_t, bool);
        PauseGame_t g_pauseGameOriginal = nullptr;

        using GetApsePauseRatio_t                       = float(__fastcall *)();
        GetApsePauseRatio_t g_getApsePauseRatioOriginal = nullptr;

        using ExecuteGlobalAudioTrigger_t                               = bool(__fastcall *)(void *, const SDK::AudioControlId *, bool);
        ExecuteGlobalAudioTrigger_t g_executeGlobalAudioTriggerOriginal = nullptr;

        using UpdateAudioListener_t                         = void(__fastcall *)(SDK::CView *, const SDK::CCamera &);
        UpdateAudioListener_t g_updateAudioListenerOriginal = nullptr;

        // The ATL triggers a menu sets the game's audio up with, per
        // Libs/GameAudio/default_controls.xml: audio_setup_menu starts
        // snapshot:/setups/setup_main_menu from C_UIMenu::PrepareFrontEnd
        // (0x180C05E6C) for the escape menu, audio_setup_inventory starts
        // setup_inventory from C_UIFullUIModeHelper::SetFullUIMode
        // (0x181F52540) for the inventory, map and journal, and
        // audio_setup_inventory_onepane starts setup_inventory_onepane.
        constexpr const char *kMenuAudioSetups[] = {"audio_setup_menu", "audio_setup_inventory", "audio_setup_inventory_onepane"};

        bool IsMenuAudioSetup(SDK::AudioControlId triggerId) {
            SDK::IAudioSystem *audio = SDK::IAudioSystem::GetInstance();
            if (audio == nullptr) {
                return false;
            }

            for (const char *name : kMenuAudioSetups) {
                SDK::AudioControlId setupId = SDK::kInvalidAudioControlId;
                if (audio->GetAudioTriggerId(name, setupId) && setupId == triggerId) {
                    return true;
                }
            }

            return false;
        }

        /**
         * Keeps the engine running while a session is live.
         *
         * A pause here is not a pause of the view: it disables ITimer, pauses
         * every entity timer, and pauses the audio system -- which takes
         * proximity voice with it, since voice plays through the game's own
         * FMOD. The mod's own tick was never the problem; PostUpdate runs
         * paused or not.
         *
         * Refusing the acquire needs no counters of its own. The native
         * returns early from a release when `counters[reason] == 0` or the
         * game is not paused, so the unmatched release that follows is a
         * no-op, and a `releaseAll` still reaches the engine.
         */
        void __fastcall PauseGame_Hook(SDK::IGameFramework *framework, bool pause, std::uint16_t reason, bool releaseAll) {
            if (pause && WorldService::IsSessionActive()) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Refused a game pause, reason {}", reason);
                return;
            }

            g_pauseGameOriginal(framework, pause, reason, releaseAll);
        }

        /**
         * Keeps the world at full speed behind the inventory, map and journal.
         *
         * Those screens never reach PauseGame. They slow the world instead:
         * `C_UIFullUIModeHelper::SetWorldSlowdown` (0x181F525B0) pushes a
         * "UISpeed" entry of 1/ratio into the named modifiers that multiply
         * into `t_GameScale`, and the ratio is `wh_ui_ApsePauseRatio`, 1000 by
         * default -- so the local body, every remote body and the world clock
         * crawl at a thousandth of real time while the page is up. Skip time,
         * fast travel and one more UI path build their entries from the same
         * getter.
         *
         * Answering 1 makes every one of those entries neutral while leaving
         * the screens' own bookkeeping untouched: the entry is still pushed
         * and removed, it just no longer scales anything. The cvar itself is
         * not written, so nothing of the session outlives it.
         */
        float __fastcall GetApsePauseRatio_Hook() {
            if (WorldService::IsSessionActive()) {
                return 1.0f;
            }

            return g_getApsePauseRatioOriginal();
        }

        /**
         * Apse renders its inventory/map scene kilometres outside the world.
         * Blocking its audio snapshot does not stop the listener following
         * that camera. Keep the local view's listener on the live player's
         * head while the UI camera is selected, through the same entity
         * transform the native sends to the audio system. This reaches both
         * world sounds and FMOD voice before either is distance-attenuated.
         */
        void __fastcall UpdateAudioListener_Hook(SDK::CView *view, const SDK::CCamera &camera) {
            SDK::wh::game::C_CameraManager *cameras = SDK::wh::game::C_CameraManager::GetInstance();
            if (WorldService::IsSessionActive() && cameras != nullptr && cameras->IsUICameraActive() && !cameras->IsDebugCameraActive()) {
                SDK::wh::entitymodule::C_Human *actor = KCDC::Common::LocalHumanActor();
                SDK::IEntity *listener                = view->GetAudioListenerEntity();
                SDK::IEntity *entity                  = actor != nullptr ? actor->GetEntity() : nullptr;
                SDK::ICharacterInstance *character    = entity != nullptr ? entity->GetCharacter(0) : nullptr;
                SDK::ISkeletonPose *pose              = character != nullptr ? character->GetISkeletonPose() : nullptr;
                if (pose != nullptr && listener != nullptr && view->GetLinkedEntityIdRaw() == actor->GetEntityId()) {
                    const auto &skeleton    = character->GetIDefaultSkeleton();
                    const std::int16_t head = skeleton.GetJointIDByBoneID(SDK::BONEID_HEAD);
                    if (head < 0 || static_cast<std::uint32_t>(head) >= skeleton.GetJointCount()) {
                        g_updateAudioListenerOriginal(view, camera);
                        return;
                    }

                    // MovementState::eyePosition can itself come from the UI
                    // camera. The body skeleton remains in the live world.
                    const SDK::Vec3 position = entity->GetSlotWorldTM(0).TransformPoint(pose->GetAbsJointByID(head).t);

                    // The first-person camera uses the actor's yaw followed
                    // by its view pitch about X. Read these live so a moving
                    // horse, teleport or hit cannot leave the listener behind.
                    const glm::vec3 forward  = glm::mat3_cast(KCDC::Common::ToGlm(actor->GetViewRotation()))[1];
                    const float yaw          = std::atan2(-forward.x, forward.y);
                    const glm::quat rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 0.0f, 1.0f)) * glm::angleAxis(actor->GetViewPitch(), glm::vec3(1.0f, 0.0f, 0.0f));
                    const glm::mat3 basis    = glm::mat3_cast(rotation);
                    SDK::Matrix34 transform {};
                    for (int row = 0; row < 3; ++row) {
                        for (int column = 0; column < 3; ++column) {
                            transform.m[row][column] = basis[column][row];
                        }
                    }
                    transform.m[0][3] = position.x;
                    transform.m[1][3] = position.y;
                    transform.m[2][3] = position.z;
                    listener->SetWorldTM(transform);
                    return;
                }
            }
            g_updateAudioListenerOriginal(view, camera);
        }

        /**
         * Keeps the world audible behind the escape menu and the inventory
         * pages.
         *
         * Alongside the camera move, a menu's setup trigger starts a Studio
         * snapshot that mixes the world away under a page the session keeps
         * running behind --
         * proximity voice with it, since voice mixes under the game's master
         * bus. The setup is refused at the one function every 2D trigger goes
         * through, and nothing else is: the menu's own UI sounds and music
         * are separate triggers. The stop that follows on leaving the menu
         * names a trigger with no active instance, so it stops nothing.
         */
        bool __fastcall ExecuteGlobalAudioTrigger_Hook(void *unused, const SDK::AudioControlId *triggerId, bool playOnlyOnce) {
            if (WorldService::IsSessionActive() && IsMenuAudioSetup(*triggerId)) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Refused menu audio setup {:#x}", *triggerId);
                return true;
            }

            return g_executeGlobalAudioTriggerOriginal(unused, triggerId, playOnlyOnce);
        }
    } // namespace
} // namespace KCDC::Features::World

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CCryAction__PauseGame), reinterpret_cast<LPVOID>(&KCDC::Features::World::PauseGame_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::World::g_pauseGameOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.GetApsePauseRatio), reinterpret_cast<LPVOID>(&KCDC::Features::World::GetApsePauseRatio_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::World::g_getApsePauseRatioOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CView__UpdateAudioListener), reinterpret_cast<LPVOID>(&KCDC::Features::World::UpdateAudioListener_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::World::g_updateAudioListenerOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.ExecuteGlobalAudioTrigger180558908), reinterpret_cast<LPVOID>(&KCDC::Features::World::ExecuteGlobalAudioTrigger_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::World::g_executeGlobalAudioTriggerOriginal));
    },
    "WorldPause");
