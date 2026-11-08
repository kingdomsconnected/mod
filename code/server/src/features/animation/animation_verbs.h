#pragma once

#include "shared/features/animation/animation_playback.h"
#include "shared/features/animation/animation_rpc.h"
#include "shared/features/animation/face_state.h"
#include "shared/features/npc/npc_entity.h"
#include "shared/features/player/player_entity.h"

#include <mafianet/types.h>

#include <string>

namespace KCDC::Features::Animation {
    /**
     * Everything the server does to what a body is playing.
     *
     * Stateless on purpose: the request lives on the entity it is about, so
     * streaming, a late join and an NPC's change of simulator all read it
     * from where it already is. What differs between a player and an NPC is
     * only who has to be told directly -- a player's owner through its own
     * RPC, an NPC's simulator through the forced state -- and that is the
     * whole of what the two halves below do differently.
     *
     * Every request's end is announced to scripts exactly once, whichever
     * way it came: the body's own client reports a one-shot that finished,
     * was cut short or never started, and the server announces the requests
     * it ends itself -- a stop, a replacement, an NPC that nobody is
     * simulating. A loop ends only the second way.
     */
    class Animations final {
      public:
        /** Registers the end report. Call once at startup. */
        static void Install();

        /**
         * Makes this body play `request` on every client, replacing whatever
         * it played before. The revision and `active` are the server's to set;
         * whatever the caller put there is overwritten.
         */
        static bool Play(Shared::Entities::HumanEntity *player, Shared::Game::AnimationPlayback request);
        static bool Play(Shared::Entities::NpcEntity *npc, Shared::Game::AnimationPlayback request);

        /**
         * Ends what this body is playing, and takes its props away. With
         * `standUp`, a request that held the body in a sitting or lying
         * `Stance` tag is followed out by the game's own transition back to
         * standing rather than dropped straight into the standing idle.
         */
        static bool Stop(Shared::Entities::HumanEntity *player, bool standUp);
        static bool Stop(Shared::Entities::NpcEntity *npc, bool standUp);

        /**
         * Puts a fragment on this body's `FacialExpression` scope, with tags
         * read the way `Play` reads them, on every client. An empty fragment
         * hands the face back to the game. A new call replaces the last one
         * and replays it even when it names the same fragment.
         */
        static bool SetExpression(Shared::Entities::HumanEntity *player, std::string fragment, std::string tags);
        static bool SetExpression(Shared::Entities::NpcEntity *npc, std::string fragment, std::string tags);

        /** Moves this body's mouth in `style` until told `None`, on every client. */
        static bool SetTalking(Shared::Entities::HumanEntity *player, Shared::Game::TalkStyle style);
        static bool SetTalking(Shared::Entities::NpcEntity *npc, Shared::Game::TalkStyle style);

        /**
         * The NPC's simulator changed. The new one does not replay a one-shot
         * it did not see start, so one still awaiting its end has ended.
         */
        static void SimulatorChanged(Shared::Entities::NpcEntity *npc);

      private:
        static void Publish(Shared::Entities::HumanEntity *player);
        static void Publish(Shared::Entities::NpcEntity *npc);
        static void PublishFace(Shared::Entities::HumanEntity *player);

        /** What replaces `current` on a stop: nothing, or its stance's way out. */
        static Shared::Game::AnimationPlayback StopRequest(const Shared::Game::AnimationPlayback &current, bool standUp);

        static void ReportEnded(const Shared::RPC::AnimationEnded &report, MafiaNet::PeerGuid sender);

        static void Announce(Shared::Entities::HumanEntity *player, const Shared::Game::AnimationPlayback &request, Shared::Game::AnimationEndReason reason);
        static void Announce(Shared::Entities::NpcEntity *npc, const Shared::Game::AnimationPlayback &request, Shared::Game::AnimationEndReason reason);

        /** An NPC nobody simulates plays nothing: its one-shot ends where it was asked for. */
        static void AnnounceUnplayable(Shared::Entities::NpcEntity *npc);
    };
} // namespace KCDC::Features::Animation
