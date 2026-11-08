#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "core/application.h"
#include "features/npc/npc_locomotion_service.h"

#include "sdk/cryengine/action/c_animated_character.h"
#include "sdk/cryengine/action/c_tag_definition.h"
#include "sdk/patterns.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_anim_action_ai_movement.h"
#include "sdk/wh/entitymodule/c_horse.h"
#include "sdk/wh/entitymodule/c_player.h"

namespace KCDC::Features::Npc {
    namespace {
        using SelectState_t                 = std::uint32_t(__fastcall *)(SDK::wh::entitymodule::CAnimActionAIMovement *);
        SelectState_t g_selectStateOriginal = nullptr;

        std::uint32_t __fastcall SelectState_Hook(SDK::wh::entitymodule::CAnimActionAIMovement *action) {
            // Networked AI actors still use this selector. Native players
            // use their own movement action and never publish AI overrides.
            auto *actor = action != nullptr ? action->GetOwnerActor() : nullptr;
            if (actor != nullptr) {
                const auto override = KCDC::Core::gApplication != nullptr ? KCDC::Core::gApplication->GetNpcLocomotionService().GetLocomotionOverride(actor->GetEntityId()) : KCDC::Features::Npc::LocomotionOverride {};
                if (override.state != 0) {
                    return override.state;
                }
            }

            return g_selectStateOriginal(action);
        }

        using SetDesiredLocalLocation_t                             = void(__fastcall *)(SDK::CAnimatedCharacter *, SDK::ISkeletonAnim *, const SDK::QuatT &, float);
        SetDesiredLocalLocation_t g_setDesiredLocalLocationOriginal = nullptr;

        void __fastcall SetDesiredLocalLocation_Hook(SDK::CAnimatedCharacter *character, SDK::ISkeletonAnim *skeleton, const SDK::QuatT &desiredLocalLocation, float frameTime) {
            const auto override = KCDC::Core::gApplication != nullptr ? KCDC::Core::gApplication->GetNpcLocomotionService().GetLocomotionOverride(character->GetEntityId()) : KCDC::Features::Npc::LocomotionOverride {};
            if (override.state == 0) {
                g_setDesiredLocalLocationOriginal(character, skeleton, desiredLocalLocation, frameTime);
                return;
            }

            // Supply animation motion before native smoothing. This does not
            // alter the physical request suppressed by NoMovementOverride.
            const glm::vec3 velocity(override.velocityX, override.velocityY, override.velocityZ);
            const glm::quat facing(override.rotationW, override.rotationX, override.rotationY, override.rotationZ);
            const glm::vec3 localDelta = (glm::inverse(facing) * velocity) * frameTime;
            const SDK::QuatT motion {{0.0F, 0.0F, 0.0F, 1.0F}, {localDelta.x, localDelta.y, localDelta.z}};
            g_setDesiredLocalLocationOriginal(character, skeleton, motion, frameTime);
        }

        void ApplyOverride(SDK::wh::entitymodule::C_Actor *actor) {
            const auto override = KCDC::Core::gApplication != nullptr ? KCDC::Core::gApplication->GetNpcLocomotionService().GetLocomotionOverride(actor->GetEntityId()) : KCDC::Features::Npc::LocomotionOverride {};
            if (override.state == 0) {
                return;
            }

            auto *tagState = actor->GetTagState();
            if (tagState == nullptr) {
                return;
            }

            const auto unpack = [](std::uint8_t tag) -> std::uint32_t {
                return tag == KCDC::Shared::Game::kNoAnimationTag ? SDK::CTagDefinition::kInvalidId : static_cast<std::uint32_t>(tag);
            };

            // The whole state first, for the bodies that send one: it carries
            // the groups nothing else speaks for. The three below are restated
            // over it, so a stale pace inside it never outranks the pace the
            // body is being drawn at.
            if (override.hasTagState) {
                tagState->SetRawState(override.tagState);
            }

            // Replace tags after the native pass rebuilds them.
            tagState->SetGroupTag(actor->GetAnimationTagId(SDK::wh::entitymodule::C_Actor::ANIM_TAG_GROUP_MOVE_SPEED), unpack(override.moveSpeedTag));
            tagState->SetGroupTag(actor->GetAnimationTagId(SDK::wh::entitymodule::C_Actor::ANIM_TAG_GROUP_MOVE_DIR), unpack(override.moveDirTag));

            // Unknown stance must preserve the native tag; clearing it would
            // restart the locomotion fragment every frame.
            if (override.stanceTag != KCDC::Shared::Game::kNoAnimationTag) {
                tagState->SetGroupTag(actor->GetAnimationTagId(SDK::wh::entitymodule::C_Actor::ANIM_TAG_GROUP_STANCE), unpack(override.stanceTag));
            }

            if (auto *animatedCharacter = actor->GetAnimatedCharacterRuntime()) {
                // Network interpolation owns the transform.
                animatedCharacter->SetNoMovementOverride(true);

                SDK::SCharacterMoveRequest request {};
                request.type     = SDK::eCMT_Normal;
                request.velocity = SDK::Vec3 {override.velocityX, override.velocityY, override.velocityZ};
                request.rotation = SDK::Quat {0.0F, 0.0F, 0.0F, 1.0F};
                animatedCharacter->AddMovement(request);
            }
        }

        using UpdateMannequinTags_t                = void(__fastcall *)(SDK::wh::entitymodule::C_Actor *, void *, void *);
        UpdateMannequinTags_t g_updateTagsOriginal = nullptr;

        void __fastcall UpdateMannequinTags_Hook(SDK::wh::entitymodule::C_Actor *actor, void *a2, void *a3) {
            g_updateTagsOriginal(actor, a2, a3);
            // Players and horses derive tags from native movement requests.
            if (actor == nullptr || actor->GetActorClass() == SDK::wh::entitymodule::C_Horse::ActorClassId || actor->GetActorClass() == SDK::wh::entitymodule::C_Player::ActorClassId) {
                return;
            }

            ApplyOverride(actor);
        }
    } // namespace
} // namespace KCDC::Features::Npc

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CAnimActionAIMovement__SelectState), reinterpret_cast<LPVOID>(&KCDC::Features::Npc::SelectState_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Npc::g_selectStateOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CAnimatedCharacter__SetDesiredLocalLocation), reinterpret_cast<LPVOID>(&KCDC::Features::Npc::SetDesiredLocalLocation_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Npc::g_setDesiredLocalLocationOriginal));

        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Actor__UpdateMannequinTags), reinterpret_cast<LPVOID>(&KCDC::Features::Npc::UpdateMannequinTags_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Npc::g_updateTagsOriginal));
    },
    "Locomotion");
