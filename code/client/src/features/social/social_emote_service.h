#pragma once

#include "core/feature_registry.h"

#include "sdk/cryengine/action/c_animated_character.h"
#include "sdk/cryengine/action/i_mannequin_action.h"
#include "sdk/wh/entitymodule/c_actor.h"

#include "shared/features/social/social_emote_rpc.h"
#include "shared/features/social/social_emote_sequence.h"

#include <mafianet/get_time.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace KCDC::Features::Social {
    /**
     * Client owner for the emotes this peer plays and the ones it is told about.
     *
     * One Mannequin action is held per actor -- the local player's and one per
     * remote body -- and the tick is what starts, expires and releases them.
     * The RPC lands on the network thread, so relayed events are queued and
     * consumed here instead of being played where they arrive.
     */
    class EmoteService final: public KCDC::Core::IFeatureService {
      public:
        /**
         * Queues one emote on a body nobody is tracking, and forgets it.
         *
         * What an NPC's gesture is: the server says it happened, every client
         * that can see the body plays it once, and nothing reports its start or
         * its end because nothing is waiting for either. The managed path above
         * exists for the local player, whose emote has to be reported, and for
         * the remote players whose reports have to be matched up -- an NPC has
         * neither half.
         */
        static bool QueueEmoteOnActor(SDK::wh::entitymodule::C_Actor *actor, std::uint16_t emoteId);

        /** Registers the emote RPC. Call once at startup. */
        static void Install();

        // Thread-safe; zero cancels the current emote.
        void RequestLocal(std::uint16_t emoteId);
        void Update() override;
        void Reset() override;
        void ForgetActor(std::uint64_t networkId);

        /** Network thread: queues a relayed emote for the next tick. */
        void Receive(const Shared::RPC::Emote &event);

        // --- Local engine edges, raised by this feature's own hooks --------

        /**
         * A Mannequin action started. When it is the local player's own emote,
         * this is what takes the next sequence number and broadcasts its Start.
         */
        void NotifyActionEntered(SDK::IAction *action);

        /**
         * That action ended, failed, or was cancelled. Broadcasts the matching
         * Stop for the emote this peer reported, then releases the playback.
         */
        void NotifyActionExited(SDK::IAction *action);

      private:
        std::uint16_t ActionEntered(const SDK::IAction *action);
        // Defers release until Update, outside the native callback.
        void ActionExited(const SDK::IAction *action);

        struct Playback {
            SDK::IAction *action           = nullptr;
            std::uint16_t emoteId          = 0;
            bool ended                     = false;
            bool entered                   = false;
            MafiaNet::Time pendingDeadline = 0;
        };
        struct RemotePlayback {
            Playback playback;
            Shared::Game::EmoteSequence sequence;
        };

        void Release(Playback &playback, bool cancel);
        bool Play(Playback &playback, SDK::wh::entitymodule::C_Actor *actor, std::uint16_t id);



        /**
         * Hands the local body's horizontal movement to the animation while a
         * full-body emote is on it, and gives it back afterwards.
         *
         * A full-body fragment takes the FullBody scope, so the legs stop
         * playing a walk -- but nothing about that stops the entity, which is
         * still moved by the player's own request, and the bow slides. The
         * engine's answer is the movement control method: with the horizontal
         * component on `eMCM_Animation`, `CAnimatedCharacter::MergeMCM` takes
         * the clip's root motion instead of the request, and a one-shot with
         * no root translation leaves the body standing. That is exactly what
         * CryEngine's own `MovementControlMethod` procedural clip does, and
         * KCD2 keeps it -- no shipped fragment uses it, so the Animation slot
         * is ours alone. The vertical component is left unstated so gravity
         * still belongs to the game.
         */
        void UpdateLocalMovementLock();

        Playback _local;
        std::map<std::uint64_t, RemotePlayback> _remote;
        std::atomic<int> _command {-1};
        std::atomic<bool> _sessionActive {false};
        std::mutex _inboxMutex;
        std::vector<Shared::RPC::Emote> _inbox;
        bool _receiving = false; // protected by inbox mutex

        // What this peer last told everyone else it was playing, and the
        // generation counter that orders those reports.
        SDK::IAction *_reportedEmoteAction   = nullptr;
        std::uint64_t _nextEmoteSequence     = 1;
        std::uint64_t _reportedEmoteSequence = 0;
        std::uint16_t _reportedEmoteId       = 0;

        // The animated character this peer has taken the Animation MCM slot
        // on, or null while it holds none. Never written through unless the
        // game still hands out the same one.
        SDK::CAnimatedCharacter *_localMovementLock = nullptr;
    };
} // namespace KCDC::Features::Social
