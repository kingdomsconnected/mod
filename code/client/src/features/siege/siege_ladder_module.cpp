#include <utils/safe_win32.h>

#include "siege_ladder_module.h"

#include "siege_ladder.h"

#include "core/application.h"

#include "features/ui/ui_action_hints.h"
#include "common/module_support.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <cmath>
#include <string>

namespace KCDC::Features::Siege {
    namespace {
        constexpr const char *kLogger = "SiegeLadder";

        /** How often the nearest ladder is chosen again. */
        constexpr std::uint64_t kPromptIntervalMs = 200;

        /** The prompts are offered a little inside the server's reach, so one pressed at its edge is not refused. */
        constexpr float kPushPromptReach  = 1.8F;
        constexpr float kRaisePromptReach = 1.5F;
        constexpr float kPromptHeight     = 2.0F;

        /** The push is held, as the battle prefab's trigger is. */
        constexpr float kPushHoldSeconds = 0.8F;

        bool Reaches(const glm::vec3 &from, const glm::vec3 &point, float reach) {
            return glm::length(glm::vec2(from - point)) <= reach && std::abs(from.z - point.z) <= kPromptHeight;
        }
    } // namespace

    SiegeLadderModule &SiegeLadderModule::Get() {
        static SiegeLadderModule instance;
        return instance;
    }

    void SiegeLadderModule::NativeContextChanged() {
        _nativeContextLost = true;
    }

    void SiegeLadderModule::Reset() {
        // The replicas take their own entities with them; ActionHints takes
        // every hint down with the session itself.
        _prompt          = 0;
        _promptLadder    = 0;
        _nextPromptCheck = 0;
    }

    void SiegeLadderModule::SendUse(std::uint64_t ladderNetworkId, Shared::RPC::SiegeLadderAction action) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        Shared::RPC::SiegeLadderUse use;
        use.ladderNetworkId = ladderNetworkId;
        use.action          = action;
        net->BroadcastRPC(use, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        Framework::Logging::GetLogger(kLogger)->info("Asked to {} ladder {}", action == Shared::RPC::SiegeLadderAction::Push ? "push" : "raise", ladderNetworkId);
    }

    void SiegeLadderModule::HidePrompt() {
        if (_prompt != 0) {
            KCDC::Features::UI::ActionHints::Get().Remove(_prompt);
        }
        _prompt       = 0;
        _promptLadder = 0;
    }

    void SiegeLadderModule::UpdatePrompt(std::uint64_t now) {
        if (now < _nextPromptCheck) {
            return;
        }
        _nextPromptCheck = now + kPromptIntervalMs;

        SDK::IEntity *body                    = KCDC::Common::LocalPlayer();
        // A climber is offered nothing, and every ladder keeps its climb: the
        // walk at a ladder's top is where a climb ends, and taking the climb
        // away there would take it from under them.
        const SDK::EntityId localActor = KCDC::Common::LocalActorEntityId();
        const bool climbing            = localActor != 0 && KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetPlayerService().LadderActionFor(localActor) != nullptr;
        SiegeLadder *nearest                  = nullptr;
        Shared::RPC::SiegeLadderAction action = Shared::RPC::SiegeLadderAction::Push;
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            const glm::vec3 from = body != nullptr ? KCDC::Common::ToGlm(body->GetWorldPos()) : glm::vec3(0.0F);
            float best           = 1e9F;
            replication->ForEach<SiegeLadder>([&](SiegeLadder *ladder) {
                bool atTop = false;
                if (body != nullptr && !climbing && ladder->usable && ladder->GetEntityId() != 0) {
                    if (ladder->state == Shared::Entities::SiegeLadderState::Standing) {
                        const glm::vec3 pushFrom = ladder->PushFrom();
                        const float distance     = glm::distance(from, pushFrom);
                        if (Reaches(from, pushFrom, kPushPromptReach)) {
                            atTop = true;
                            if (distance < best) {
                                best    = distance;
                                nearest = ladder;
                                action  = Shared::RPC::SiegeLadderAction::Push;
                            }
                        }
                    }
                    else if (ladder->state == Shared::Entities::SiegeLadderState::Down && Reaches(from, ladder->position, kRaisePromptReach)) {
                        const float distance = glm::distance(from, ladder->position);
                        if (distance < best) {
                            best    = distance;
                            nearest = ladder;
                            action  = Shared::RPC::SiegeLadderAction::Raise;
                        }
                    }
                }
                // The use key at the top is the push's, not the climb's.
                ladder->SetClimbOffered(!atTop);
            });
        }

        const std::uint64_t ladderId = nearest != nullptr ? nearest->GetNetworkID() : 0;
        if (ladderId == _promptLadder && action == _promptAction && (_prompt != 0) == (ladderId != 0)) {
            return;
        }
        HidePrompt();
        if (ladderId == 0) {
            return;
        }

        KCDC::Features::UI::ActionHints::Options options;
        options.key = "primary_action";
        if (action == Shared::RPC::SiegeLadderAction::Push) {
            options.mode         = KCDC::Features::UI::ActionHints::Mode::Hold;
            options.holdDuration = kPushHoldSeconds;
            options.text         = KCDC::Common::GameText("@push_ladder", "Knock down ladder");
        }
        else {
            options.text = KCDC::Common::GameText("@use_ladder", "Raise ladder");
        }
        _prompt       = KCDC::Features::UI::ActionHints::Get().Create({}, options, [this, ladderId, action](std::uint32_t) {
            SendUse(ladderId, action);
        });
        _promptLadder = _prompt != 0 ? ladderId : 0;
        _promptAction = action;
    }

    void SiegeLadderModule::Update() {
        if (_nativeContextLost.exchange(false)) {
            if (auto *replication = Framework::CoreModules::GetReplication()) {
                replication->ForEach<SiegeLadder>([](SiegeLadder *ladder) {
                    ladder->Unbind();
                });
            }
        }

        const std::uint64_t now     = KCDC::Common::NowMs();
        std::int64_t offset         = 0;
        const bool serverClockKnown = KCDC::Common::ServerClockDifferential(offset);

        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<SiegeLadder>([&](SiegeLadder *ladder) {
                ladder->Frame(now, offset, serverClockKnown);
            });
        }
        UpdatePrompt(now);
    }
} // namespace KCDC::Features::Siege
