#pragma once

#include "core/feature_registry.h"

#include "sdk/cryengine/action/c_animated_character.h"
#include "sdk/cryengine/action/c_tag_definition.h"
#include "sdk/cryengine/action/i_action_controller.h"
#include "sdk/cryengine/action/i_mannequin_action.h"
#include "sdk/cryengine/animation/i_character_instance.h"
#include "sdk/cryengine/animation/i_skeleton_anim.h"
#include "sdk/wh/entitymodule/c_actor.h"

#include "shared/features/animation/animation_playback.h"
#include "shared/features/animation/face_state.h"

#include <mafianet/get_time.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Animation {
    /**
     * Client owner of the animations the server makes bodies play.
     *
     * The request is state on the entity -- `HumanEntity::animation` for
     * everybody else's players, `NpcEntity::animation` for NPCs -- except for
     * the local player, whose request arrives as `PlayerAnimation` because an
     * entity's owner is sent no fields. Every tick compares each body's
     * request with what this machine last applied to it and, when the revision
     * moved, takes the old one off and puts the new one on.
     *
     * Putting one on is three independent things, each the engine's own
     * mechanism:
     *
     * - the fragment, queued as the same generic `TAction` the game's
     *   `C_Player::PlayAnim` builds;
     * - the global tags among the request's tags (`r_bucket`), written into the
     *   body's Mannequin context the way `PlayAnim`'s fallback does, and put
     *   back when the request ends;
     * - the props, each a bone socket of its own carrying a `CCGFAttachment`,
     *   built the way `CProceduralClipAttachProp` builds one.
     *
     * What happens to a queued fragment is never read back from its status.
     * Each action the service queues dispatches through a copy of the
     * `TAction` virtual table whose lifecycle slots report to the service --
     * `Enter`, `Exit`, `Fail`, `UpdatePending`, the unstarted interrupt and
     * `OnActionFinished` -- so the controller itself says when the action
     * started, reached its end, was cut short or was never installed, in the
     * update where it happened and for these actions only. A loop plays its
     * next pass from the completion point through `IAction::SetFragment`, the
     * controller's own requeue, and the one queued-action exit that calls no
     * virtual, `CActionController::Flush`, is detoured.
     *
     * The face is a second request on the same bodies, `face` beside
     * `animation`, and touches neither the body's action nor its props:
     *
     * - the expression, a fragment on the `FacialExpression` scope queued the
     *   same way at the same priority, which outranks both the game's own
     *   default face (priority 1) and the one it wears while speaking (3);
     * - the talking mouth, a looping facial clip started on the skeleton's
     *   `LipSync` layer the way `CLipSyncProvider_TransitionQueue` starts a
     *   voice line's clip there. The game's own lip-sync for a line it plays
     *   takes the layer over, and the loop comes back once the line is done.
     *
     * A remote player the voice chat is playing to this machine talks through
     * the same loop while no script holds their mouth. The framework's speech
     * envelope for that speaker scales the layer's blend weight, so the mouth
     * opens on syllables and closes between words and sentences instead of
     * chewing through silence at a constant depth.
     */
    class AnimationService final: public KCDC::Core::IFeatureService {
      public:
        static AnimationService &Get();

        /** Registers the owner's RPC. Call once at startup. */
        static void Install();

        void Update() override;
        void Reset() override;

        /** The level went away and took every body with it; forget them untouched. */
        void NativeContextChanged() override;

        /** Network thread: what the server wants the local player's own body to play. */
        void ReceiveLocal(const Shared::Game::AnimationPlayback &playback);

        /** Network thread: what the server wants the local player's own face to do. */
        void ReceiveLocalFace(const Shared::Game::FaceState &face);

        /**
         * The local body is being moved somewhere else. Whatever it plays is
         * cut now rather than left to drag the body until its clip ends; a
         * loop starts again where the body lands. Game thread only.
         */
        void InterruptLocal();

        // --- Engine edges -----------------------------------------------------
        //
        // Called from the adopted actions' virtual slots and the Flush detour,
        // on whichever thread runs that controller's update. Each takes only
        // the edge lock and never calls into the engine while holding it.

        /** The native `TAction` implementation of one slot. */
        std::uint64_t NativeSlot(SDK::IAction::VirtualSlot slot) const {
            return _nativeSlots[static_cast<std::size_t>(slot)];
        }

        void OnActionEntered(SDK::IAction *action);
        void OnActionExited(SDK::IAction *action);
        void OnActionRefused(SDK::IAction *action);
        void OnActionCompletionPoint(SDK::IAction *action);
        void OnControllerFlushed(SDK::IActionController *controller);

      private:
        /** Which lifecycle edge an adopted action reported. */
        enum class Edge : std::uint8_t {
            Started,     // Enter: the fragment began on its scope
            Finished,    // a one-shot reached its completion point
            Interrupted, // Exit without having finished: something cut it
            Refused,     // never started: Fail, a queue time run out, an unstarted interrupt, a flush
        };

        /**
         * One queued action as the edges see it: enough to route the edge to
         * its body, and to take a stance tag off in the very update the
         * action ends, without touching the service's bodies off-thread.
         */
        struct Ticket {
            std::uint64_t key      = 0;
            std::uint32_t revision = 0;
            std::uint64_t serial   = 0;

            SDK::IActionController *controller = nullptr;

            bool loop             = false;
            bool endsWithFragment = false;

            SDK::CTagState *tagState = nullptr; // null when the request sets no global tag
            std::uint8_t tagsBefore[SDK::CTagState::kStateBytes] {};
            std::uint8_t tagsAfter[SDK::CTagState::kStateBytes] {};

            bool started  = false;
            bool finished = false;
            bool ended    = false; // no further edge is reported
        };

        struct EdgeEvent {
            std::uint64_t key      = 0;
            std::uint32_t revision = 0;
            std::uint64_t serial   = 0;
            Edge edge              = Edge::Started;
        };

        /**
         * Global tags a request states on a body's Mannequin context, with the
         * state as it was before them and as they left it, so ending the
         * request undoes exactly their bits.
         */
        struct TagOverlay {
            std::vector<std::uint32_t> globalTags;
            SDK::CTagState *tagState = nullptr;
            bool captured            = false;
            std::uint8_t before[SDK::CTagState::kStateBytes] {};
            std::uint8_t after[SDK::CTagState::kStateBytes] {};
        };

        /** What this machine has put on one body's face. */
        struct Face {
            // The expression as last applied; its revision is what moves.
            std::uint32_t expressionRevision = 0;
            std::string expression;
            std::string tags;
            SDK::IAction *action = nullptr; // one reference held
            TagOverlay overlay;

            // The mouth: the style asked for, its clip on this body's
            // animation set (-1 when it has none) and the user token of the
            // pass this machine last started, 0 before the first.
            Shared::Game::TalkStyle talk = Shared::Game::TalkStyle::None;
            std::int32_t talkAnimationId = -1;
            std::uint32_t talkToken      = 0;

            // The `LipSync` layer's blend weight as this machine last wrote
            // it: 1.0 is the engine's own, which is what the game's lip-sync
            // plays its lines against. `fadingToken` is the pass that was
            // stopped while the weight was lowered, still blending out.
            float layerWeight         = 1.0F;
            std::uint32_t fadingToken = 0;
        };

        /** What this machine has put on one body. */
        struct Body {
            std::uint64_t key = 0; // the entity's network id

            // The actor and character the pieces below were applied to. A body
            // that answers with different ones has been rebuilt, and nothing
            // may be written through these any more.
            SDK::wh::entitymodule::C_Actor *actor = nullptr;
            SDK::EntityId entityId                = 0;
            SDK::ICharacterInstance *character    = nullptr;

            Shared::Game::AnimationPlayback playback;
            bool station = false;

            // The request that was already current when this machine first saw
            // the body; see `Drive`.
            std::uint32_t firstRevision = 0;

            // Whether this machine's body is the one that counts for the
            // request: the owner's for a player, the simulator's for an NPC. Only
            // it reports how the animation ended.
            bool authoritative              = false;
            std::uint32_t reportedRevision = 0;

            // The fragment, resolved against this body's controller.
            std::uint32_t fragmentId = SDK::CTagDefinition::kInvalidId;
            SDK::SFragTagState fragmentTags;
            int option               = 0;       // the fragment's option: 0, or a streamed clip's
            SDK::IAction *action     = nullptr; // one reference held; adopted
            std::uint64_t serial     = 0;       // which of this body's actions `action` is
            bool started             = false;   // `action` has entered its scope
            MafiaNet::Time replayAt  = 0;       // when a loop queues again
            bool fragmentPending     = false;   // still to be queued (again)

            // The request's global tags.
            TagOverlay overlay;

            // An `endsWithFragment` request whose fragment is over: its tags
            // are off and stay off.
            bool released = false;

            bool propAttached[Shared::Game::AnimationPlayback::kMaxProps] {};

            Face face;

            bool touched = false; // seen this tick
        };

        /** Whether the actor system still answers with the body these pieces were applied to. */
        static bool IsStillThere(const Body &body);

        /** `voiceSpeaker` is the peer whose voice chat moves this body's mouth, 0 for none. */
        void Drive(std::uint64_t key, SDK::wh::entitymodule::C_Actor *actor, const Shared::Game::AnimationPlayback &wanted, const Shared::Game::FaceState &wantedFace, bool local, bool authoritative, std::uint64_t voiceSpeaker, bool station = false);
        void Start(Body &body, bool fresh);

        /**
         * Takes everything this machine put on the body back off. `touchEngine`
         * is false for a body that is already gone, whose engine objects must
         * not be written through.
         */
        void Clear(Body &body, bool touchEngine);

        /** Forgets the body's action: no further edge of it is heard. */
        void DropAction(Body &body);

        void ResolveTags(Body &body, SDK::IActionController *controller);

        /**
         * Resolves a fragment and its tags against one body's controller: the
         * fragment's own tags into `fragmentTags`, the global ones into
         * `overlay`. The fragment is `kInvalidId` when the database has none.
         */
        static void ResolveRequest(SDK::IActionController *controller, const std::string &fragment, const std::string &tags, std::uint32_t &fragmentId, SDK::SFragTagState &fragmentTags, TagOverlay &overlay);
        static void ApplyGlobalTags(TagOverlay &overlay);
        static void RestoreGlobalTags(TagOverlay &overlay);
        void AttachProps(Body &body);
        void DetachProps(Body &body);
        void InvalidateDrawCache(Body &body);
        void QueueFragment(Body &body, SDK::IActionController *controller);
        void TickFragment(Body &body, SDK::IActionController *controller);

        /** Gives an action this service built the virtual table whose lifecycle slots report here. */
        void Adopt(SDK::IAction &action);

        /** Records one edge of an adopted action. The edge lock is held. */
        void PushEdge(const Ticket &ticket, Edge edge);

        /** Applies the edges the controllers reported since the last tick. */
        void DrainEdges();
        void ApplyEdge(Body &body, Edge edge);

        /** Tells the server how the request's animation ended, once, from the body that counts. */
        void Report(Body &body, Shared::Game::AnimationEndReason reason);

        // --- The face -----------------------------------------------------------

        void DriveFace(Body &body, const Shared::Game::FaceState &wanted, std::uint64_t voiceSpeaker);
        void StartExpression(Body &body, SDK::IActionController *controller);

        /**
         * Puts the mouth in the style asked for, and back on after the game's
         * own lip-sync has had the layer. `openness` in [0, 1] is the layer
         * weight the loop plays at: 1 for a scripted mouth, the speech
         * envelope for a voice one.
         */
        void TickTalk(Body &body, Shared::Game::TalkStyle wanted, float openness);

        /**
         * Writes the `LipSync` layer's blend weight: `openness` while this
         * machine's loop is all the layer plays, the engine's 1.0 once
         * anything else holds it. A stopped pass keeps the weight it left
         * with until it has blended out, so a mouth closed at the end of a
         * sentence does not spring open on its way out.
         */
        static void TickMouth(Face &face, SDK::ISkeletonAnim *skeleton, float openness);

        /** Takes the face off, with the same `touchEngine` meaning as `Clear`. */
        void ClearFace(Body &body, bool touchEngine);
        void ClearExpression(Body &body, bool touchEngine);
        void StopTalk(Body &body, bool touchEngine);

        /** Holds the local body still while a request that asks for it is on screen. */
        void UpdateMovementLock(const Body *local);

        std::map<std::uint64_t, Body> _bodies;
        std::uint64_t _localKey   = 0;
        std::uint64_t _nextSerial = 0;

        // The local player's request, as the server last sent it.
        Shared::Game::AnimationPlayback _local;
        Shared::Game::FaceState _localFace;
        std::uint32_t _nextTalkToken = 0;

        std::mutex _inboxMutex;
        std::optional<Shared::Game::AnimationPlayback> _inbox; // protected by the mutex
        std::optional<Shared::Game::FaceState> _faceInbox;     // protected by the mutex
        bool _receiving = false;                               // protected by the mutex

        // The adopted actions' virtual table: the RTTI locator, then
        // `TAction<SAnimationContext>`'s slots with the lifecycle ones
        // replaced. Built from the first action's own table, before any
        // adopted action exists, and constant after that.
        std::array<std::uint64_t, SDK::IAction::kTActionVirtualSlots + 1> _edgeTable {};
        std::array<std::uint64_t, SDK::IAction::kTActionVirtualSlots> _nativeSlots {};
        bool _edgeTableReady = false;

        std::mutex _edgeMutex;
        std::unordered_map<const SDK::IAction *, Ticket> _tickets; // protected by the edge mutex
        std::vector<EdgeEvent> _edges;                             // protected by the edge mutex

        // The animated character this machine has taken the Animation MCM slot
        // on, or null. Released only through the one the game still answers.
        SDK::CAnimatedCharacter *_movementLock = nullptr;
    };
} // namespace KCDC::Features::Animation
