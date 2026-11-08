#include "features/animation/animation_verbs.h"

#include "features/npc/npc_scripting.h"
#include "features/player/player_scripting.h"

#include "shared/game/animation_catalog.h"

#include <core_modules.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <string>
#include <utility>

namespace KCDC::Features::Animation {
    namespace {
        /** Whether this request's animation has yet to be announced as ended. A request with no fragment plays nothing that could end. */
        template <typename Entity>
        bool AwaitsEnd(const Entity &entity) {
            return entity.animation.active && !entity.animation.fragment.empty() && entity.animationEndAnnounced != entity.animation.revision;
        }

        /**
         * Tells a player's owner, the one peer sent neither replication
         * channel, what everyone else reads off the relay. A forced state
         * would fence off the soul and combat deltas it has in flight.
         */
        template <typename Message>
        void TellOwner(const Shared::Entities::HumanEntity &player, Message &message) {
            if (auto *net = Framework::CoreModules::GetNetworkPeer()) {
                net->SendRPC(message, MafiaNet::ToGuid(player.ownerGUID));
            }
        }

        /** The face `current` becomes with this expression, a fresh revision so a repeat replays. */
        Shared::Game::FaceState WithExpression(const Shared::Game::FaceState &current, std::string fragment, std::string tags) {
            Shared::Game::FaceState face = current;
            face.expressionRevision      = Shared::Game::FaceState::Advance(current.expressionRevision);
            face.expression              = std::move(fragment);
            face.tags                    = std::move(tags);
            return face;
        }
    } // namespace

    void Animations::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        net->RegisterRPC<Shared::RPC::AnimationEnded>([](const Shared::RPC::AnimationEnded &report, MafiaNet::Packet *packet) {
            if (packet != nullptr && report.IsSane()) {
                ReportEnded(report, MafiaNet::ToPeerGuid(packet->guid));
            }
        });
    }

    bool Animations::Play(Shared::Entities::HumanEntity *player, Shared::Game::AnimationPlayback request) {
        if (player == nullptr) {
            return false;
        }
        request.active   = true;
        request.revision = Shared::Game::AnimationPlayback::Advance(player->animation.revision);
        if (!request.IsSane()) {
            return false;
        }
        const bool announce                             = AwaitsEnd(*player);
        const Shared::Game::AnimationPlayback previous = std::exchange(player->animation, std::move(request));
        Publish(player);
        // After the new request is out, so a handler that plays again from
        // here replaces this one rather than being overwritten by it.
        if (announce) {
            Announce(player, previous, Shared::Game::AnimationEndReason::Replaced);
        }
        return true;
    }

    bool Animations::Play(Shared::Entities::NpcEntity *npc, Shared::Game::AnimationPlayback request) {
        if (npc == nullptr) {
            return false;
        }
        request.active   = true;
        request.revision = Shared::Game::AnimationPlayback::Advance(npc->animation.revision);
        if (!request.IsSane()) {
            return false;
        }
        const bool announce                             = AwaitsEnd(*npc);
        const Shared::Game::AnimationPlayback previous = std::exchange(npc->animation, std::move(request));
        Publish(npc);
        if (announce) {
            Announce(npc, previous, Shared::Game::AnimationEndReason::Replaced);
        }
        AnnounceUnplayable(npc);
        return true;
    }

    Shared::Game::AnimationPlayback Animations::StopRequest(const Shared::Game::AnimationPlayback &current, bool standUp) {
        Shared::Game::AnimationPlayback next;
        next.revision = Shared::Game::AnimationPlayback::Advance(current.revision);
        const Shared::Game::AnimationStanceExitEntry *exit = standUp && current.active ? Shared::Game::FindAnimationStanceExit(current.tags) : nullptr;
        if (exit != nullptr) {
            // The stance tag stays on through the transition, which is what
            // selects the variant out of that stance, and comes off with it.
            next.active           = true;
            next.fragment         = exit->fragment;
            next.tags             = exit->stanceTag;
            next.lockMovement     = true;
            next.endsWithFragment = true;
        }
        return next;
    }

    bool Animations::Stop(Shared::Entities::HumanEntity *player, bool standUp) {
        if (player == nullptr) {
            return false;
        }
        const bool announce                             = AwaitsEnd(*player);
        const Shared::Game::AnimationPlayback previous = std::exchange(player->animation, StopRequest(player->animation, standUp));
        Publish(player);
        if (announce) {
            Announce(player, previous, Shared::Game::AnimationEndReason::Stopped);
        }
        return true;
    }

    bool Animations::Stop(Shared::Entities::NpcEntity *npc, bool standUp) {
        if (npc == nullptr) {
            return false;
        }
        const bool announce                             = AwaitsEnd(*npc);
        const Shared::Game::AnimationPlayback previous = std::exchange(npc->animation, StopRequest(npc->animation, standUp));
        Publish(npc);
        if (announce) {
            Announce(npc, previous, Shared::Game::AnimationEndReason::Stopped);
        }
        AnnounceUnplayable(npc);
        return true;
    }

    bool Animations::SetExpression(Shared::Entities::HumanEntity *player, std::string fragment, std::string tags) {
        if (player == nullptr) {
            return false;
        }
        Shared::Game::FaceState face = WithExpression(player->face, std::move(fragment), std::move(tags));
        if (!face.IsSane()) {
            return false;
        }
        player->face = std::move(face);
        PublishFace(player);
        return true;
    }

    bool Animations::SetExpression(Shared::Entities::NpcEntity *npc, std::string fragment, std::string tags) {
        if (npc == nullptr) {
            return false;
        }
        Shared::Game::FaceState face = WithExpression(npc->face, std::move(fragment), std::move(tags));
        if (!face.IsSane()) {
            return false;
        }
        npc->face = std::move(face);
        // The same forced state that carries `animation` to its simulator.
        Publish(npc);
        return true;
    }

    bool Animations::SetTalking(Shared::Entities::HumanEntity *player, Shared::Game::TalkStyle style) {
        if (player == nullptr || style >= Shared::Game::TalkStyle::Count) {
            return false;
        }
        // Nothing to tell anyone: a chat handler may well call this per message.
        if (player->face.talk == style) {
            return true;
        }
        player->face.talk = style;
        PublishFace(player);
        return true;
    }

    bool Animations::SetTalking(Shared::Entities::NpcEntity *npc, Shared::Game::TalkStyle style) {
        if (npc == nullptr || style >= Shared::Game::TalkStyle::Count) {
            return false;
        }
        if (npc->face.talk == style) {
            return true;
        }
        npc->face.talk = style;
        Publish(npc);
        return true;
    }

    void Animations::SimulatorChanged(Shared::Entities::NpcEntity *npc) {
        if (npc == nullptr || !AwaitsEnd(*npc) || npc->animation.loop) {
            return;
        }
        // The previous simulator's report, if one is still on its way, comes
        // from a peer that no longer runs the body and is refused.
        Announce(npc, npc->animation, Shared::Game::AnimationEndReason::Interrupted);
    }

    void Animations::AnnounceUnplayable(Shared::Entities::NpcEntity *npc) {
        if (npc->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID && !npc->animation.loop && AwaitsEnd(*npc)) {
            Announce(npc, npc->animation, Shared::Game::AnimationEndReason::Refused);
        }
    }

    void Animations::ReportEnded(const Shared::RPC::AnimationEnded &report, MafiaNet::PeerGuid sender) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }
        // Only the body that counts may say how its request ended, and only
        // about the request still current: anything older has already been
        // announced as replaced or stopped.
        if (auto *player = replication->GetEntity<Shared::Entities::HumanEntity>(report.entityNetworkId)) {
            if (player->ownerGUID == sender && player->animation.revision == report.revision && !player->animation.loop && AwaitsEnd(*player)) {
                Announce(player, player->animation, report.reason);
            }
            return;
        }
        if (auto *npc = replication->GetEntity<Shared::Entities::NpcEntity>(report.entityNetworkId)) {
            if (npc->ownerGUID == sender && npc->animation.revision == report.revision && !npc->animation.loop && AwaitsEnd(*npc)) {
                Announce(npc, npc->animation, report.reason);
            }
        }
    }

    void Animations::Announce(Shared::Entities::HumanEntity *player, const Shared::Game::AnimationPlayback &request, Shared::Game::AnimationEndReason reason) {
        player->animationEndAnnounced = request.revision;
        Scripting::Events::PlayerAnimationEnded(player->GetNetworkID(), request.fragment, reason);
    }

    void Animations::Announce(Shared::Entities::NpcEntity *npc, const Shared::Game::AnimationPlayback &request, Shared::Game::AnimationEndReason reason) {
        npc->animationEndAnnounced = request.revision;
        Scripting::Events::NpcAnimationEnded(npc->GetNetworkID(), request.fragment, reason);
    }

    void Animations::Publish(Shared::Entities::HumanEntity *player) {
        Shared::RPC::PlayerAnimation message;
        message.playback = player->animation;
        TellOwner(*player, message);
    }

    void Animations::PublishFace(Shared::Entities::HumanEntity *player) {
        Shared::RPC::PlayerFace message;
        message.face = player->face;
        TellOwner(*player, message);
    }

    void Animations::Publish(Shared::Entities::NpcEntity *npc) {
        // The field channel reaches observers; the simulator is withheld from
        // it and hears the write only here, which also fences off its echo of
        // the request this one replaces.
        npc->ForceState();
    }
} // namespace KCDC::Features::Animation
