#include <utils/safe_win32.h>

#include "animation_service.h"

#include "core/application.h"
#include "features/dice/dice_module.h"
#include "features/npc/npc.h"
#include "features/player/player.h"
#include "shared/features/player/station_animation.h"

#include "sdk/cryengine/3dengine/i_3d_engine.h"
#include "sdk/cryengine/action/i_actor_system.h"
#include "sdk/cryengine/animation/c_cgf_attachment.h"
#include "sdk/cryengine/animation/i_animation_set.h"
#include "sdk/cryengine/animation/i_attachment.h"
#include "sdk/cryengine/animation/i_attachment_manager.h"
#include "sdk/cryengine/animation/i_skeleton_anim.h"
#include "sdk/cryengine/entitysystem/c_render_proxy.h"
#include "sdk/cryengine/system/i_console.h"
#include "sdk/wh/entitymodule/c_player.h"

#include "shared/features/animation/animation_rpc.h"
#include "shared/game/animation_tags.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>
#include <utils/hashing.h>
#include <utils/string_utils.h>
#include <voice/client/voice_client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>

namespace KCDC::Features::Animation {
    namespace {
        constexpr const char *kLogger = "Animation";

        // The sockets the props hang on, one per prop slot. Sockets of their
        // own rather than the skeleton's `hand_right`: a body holding a sword
        // has that one bound already, and binding over it would release the
        // weapon's payload out from under the item system.
        constexpr const char *kPropSockets[Shared::Game::AnimationPlayback::kMaxProps] = {"mp_prop_0", "mp_prop_1"};

        // Kept by pointer by the engine's own MCM diagnostics, so it has to
        // outlive every call that states it.
        constexpr const char *kMovementLockReason = "ScriptAnimation";

        // How long a loop waits before asking again when the controller never
        // started its last pass, so a body that keeps refusing is not asked
        // every frame. A pass that was cut short is queued again at once.
        constexpr MafiaNet::Time kRefusedReplayDelayMs = 250;

        // `CAttachmentBONE::GetJointID` for a joint the skeleton does not have.
        constexpr std::uint32_t kNoJoint = 0xFFFF;

        // An expression waits in the queue for its scope as long as it takes
        // (`IAction::UpdatePending` times out only a non-negative queue time):
        // it is held state, so a face asked for while something else owns the
        // scope still shows once that ends.
        constexpr float kExpressionQueueTime = -1.0F;

        // The skeleton layer the `LipSync` scope owns (`kcd_male_controllerdefs`
        // `<LipSync layer="12">`), which `BasicActor.lua` points the game's own
        // lip-sync provider at through `TransitionQueueSettings.nAnimLayer`.
        constexpr std::uint32_t kLipSyncLayer = 12;

        // The provider's weight for that layer, `BasicActor.lua`'s
        // `playbackWeight`, which every actor class inherits unless it says
        // otherwise (`player.lua` says 1.0).
        constexpr float kTalkWeight = 0.75F;

        // The blend times the provider reads for every lip-sync clip it starts
        // (`FillCharAnimationParams`) and stops (`StopLipSync`).
        constexpr const char *kTalkStartTransitionCVar = "wh_actor_LipSyncStartTransitionTime";
        constexpr const char *kTalkStopTransitionCVar  = "wh_actor_LipSyncStopTransitionTime";

        // The user tokens this service starts talk clips under, apart from the
        // engine's own counter, which starts at 1 and counts up.
        constexpr std::uint32_t kTalkTokenBase = 0x4B000000;

        // The loop a voice chat talker's mouth plays. The envelope, not the
        // style, carries what they are saying, so the plainest one serves.
        constexpr Shared::Game::TalkStyle kVoiceTalkStyle = Shared::Game::TalkStyle::Neutral;

        // The speech envelope (full-scale RMS, `VoiceClient::GetSpeakerLevel`)
        // at which the mouth is fully open. Conversational speech sits around
        // 0.05-0.2 after the sender's gain; a shout clips here rather than
        // over-driving the layer.
        constexpr float kVoiceFullMouthLevel = 0.2F;

        /**
         * How far open a talker's mouth is for their current envelope. The
         * square root lifts quiet speech, which would otherwise barely part
         * the lips, the way loudness is heard rather than measured.
         */
        float MouthOpenness(float level) {
            return std::sqrt(std::clamp(level / kVoiceFullMouthLevel, 0.0F, 1.0F));
        }

        /** Mannequin's key for a name: the CRC-32 of its lowercase spelling (`CCrc32::ComputeLowercase`). */
        std::uint32_t LowercaseCrc(std::string_view name) {
            const std::string lower = Framework::Utils::StringUtils::ToLower(std::string(name));
            return Framework::Utils::Hashing::CalculateCRC32(lower.c_str(), lower.size());
        }

        SDK::IActionController *ControllerOf(SDK::wh::entitymodule::C_Actor *actor) {
            auto *animated = actor != nullptr ? actor->GetAnimatedCharacterRuntime() : nullptr;
            return animated != nullptr ? animated->GetActionController() : nullptr;
        }

        SDK::ICharacterInstance *CharacterOf(SDK::wh::entitymodule::C_Actor *actor) {
            auto *animated = actor != nullptr && actor->IsSettled() ? actor->GetAnimatedCharacterRuntime() : nullptr;
            return animated != nullptr && animated->GetActionController() != nullptr ? animated->GetBoundCharacter3P() : nullptr;
        }

        /**
         * Puts back the bits a set of tags changed, and only while they still
         * hold what the tags left there: a group something else has written
         * since is that something's now. Running it twice changes nothing.
         */
        void RestoreTags(SDK::CTagState *tagState, const std::uint8_t *before, const std::uint8_t *after) {
            std::uint8_t state[SDK::CTagState::kStateBytes];
            std::memcpy(state, tagState->GetRawState(), sizeof(state));
            for (std::size_t i = 0; i < sizeof(state); ++i) {
                const std::uint8_t mask = before[i] ^ after[i];
                if ((state[i] & mask) == (after[i] & mask)) {
                    state[i] = static_cast<std::uint8_t>((state[i] & ~mask) | (before[i] & mask));
                }
            }
            tagState->SetRawState(state);
        }

        // --- The adopted actions' lifecycle slots ------------------------------
        //
        // Each runs the native `TAction` slot first, so the action is in the
        // state the engine expects, and then says what happened.

        void __fastcall AdoptedEnter(SDK::IAction *action) {
            AnimationService &service = AnimationService::Get();
            hook::this_call<void>(service.NativeSlot(SDK::IAction::VirtualSlot::Enter), action);
            service.OnActionEntered(action);
        }

        void __fastcall AdoptedExit(SDK::IAction *action) {
            AnimationService &service = AnimationService::Get();
            hook::this_call<void>(service.NativeSlot(SDK::IAction::VirtualSlot::Exit), action);
            service.OnActionExited(action);
        }

        void __fastcall AdoptedFail(SDK::IAction *action, SDK::IAction::EFailure failure) {
            AnimationService &service = AnimationService::Get();
            hook::this_call<void>(service.NativeSlot(SDK::IAction::VirtualSlot::Fail), action, failure);
            service.OnActionRefused(action);
        }

        // Called for a queued action every update before its status is read;
        // Finished here is its queue time running out, or a ForceFinish, and
        // the controller drops it from the queue right after.
        SDK::IAction::EStatus __fastcall AdoptedUpdatePending(SDK::IAction *action, float timePassed) {
            AnimationService &service     = AnimationService::Get();
            const SDK::IAction::EStatus status = hook::this_call<SDK::IAction::EStatus>(service.NativeSlot(SDK::IAction::VirtualSlot::UpdatePending), action, timePassed);
            if (status == SDK::IAction::EStatus::Finished) {
                service.OnActionRefused(action);
            }
            return status;
        }

        void __fastcall AdoptedInterruptedBeforeStart(SDK::IAction *action) {
            AnimationService &service = AnimationService::Get();
            hook::this_call<void>(service.NativeSlot(SDK::IAction::VirtualSlot::InterruptedBeforeStart), action);
            service.OnActionRefused(action);
        }

        void __fastcall AdoptedOnActionFinished(SDK::IAction *action) {
            AnimationService &service = AnimationService::Get();
            hook::this_call<void>(service.NativeSlot(SDK::IAction::VirtualSlot::OnActionFinished), action);
            service.OnActionCompletionPoint(action);
        }
    } // namespace

    AnimationService &AnimationService::Get() {
        static AnimationService service;
        return service;
    }

    void AnimationService::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        net->RegisterRPC<Shared::RPC::PlayerAnimation>([](const Shared::RPC::PlayerAnimation &message, MafiaNet::Packet *) {
            if (message.IsSane()) {
                Get().ReceiveLocal(message.playback);
            }
        });
        net->RegisterRPC<Shared::RPC::PlayerFace>([](const Shared::RPC::PlayerFace &message, MafiaNet::Packet *) {
            if (message.IsSane()) {
                Get().ReceiveLocalFace(message.face);
            }
        });
    }

    void AnimationService::ReceiveLocal(const Shared::Game::AnimationPlayback &playback) {
        std::lock_guard lock(_inboxMutex);
        if (_receiving) {
            _inbox = playback;
        }
    }

    void AnimationService::ReceiveLocalFace(const Shared::Game::FaceState &face) {
        std::lock_guard lock(_inboxMutex);
        if (_receiving) {
            _faceInbox = face;
        }
    }

    void AnimationService::Update() {
        {
            std::lock_guard lock(_inboxMutex);
            _receiving = true;
            if (_inbox.has_value()) {
                _local = std::move(*_inbox);
                _inbox.reset();
            }
            if (_faceInbox.has_value()) {
                _localFace = std::move(*_faceInbox);
                _faceInbox.reset();
            }
        }

        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }

        // Before the bodies are driven, so a tag an action's end took off is
        // not stated again this tick.
        DrainEdges();

        for (auto &[key, body] : _bodies) {
            body.touched = false;
        }

        _localKey = 0;
        replication->ForEach<KCDC::Features::Player::Human>([&](KCDC::Features::Player::Human *human) {
            if (human->IsOwner()) {
                _localKey = human->GetNetworkID();
                Drive(_localKey, SDK::wh::entitymodule::C_Player::GetInstance(), _local, _localFace, true, true, 0);
            }
            else {
                // The voice speaker is the owning peer, as the nametag's talk icon keys it.
                const bool station = human->HasStationPose();
                // Native dice owns the participant's opponent completely.
                // Suspend scripted full-body playback here too.
                const bool nativeDice = KCDC::Features::Dice::DiceModule::Get().HoldsBody(human->GetEntityId());
                const auto playback   = station ? Shared::Game::StationAnimation(human->station) : (nativeDice ? Shared::Game::AnimationPlayback {} : human->animation);
                Drive(human->GetNetworkID(), human->ResolveActor(), playback, human->face, false, false, static_cast<std::uint64_t>(human->ownerGUID), station || nativeDice);
            }
        });
        replication->ForEach<KCDC::Features::Npc::Npc>([&](KCDC::Features::Npc::Npc *npc) { Drive(npc->GetNetworkID(), npc->ResolveActor(), npc->animation, npc->face, false, npc->IsSimulating(), 0); });

        // A body whose entity went away this tick. Its actor may well outlive
        // it -- an adopted NPC is the level's own body -- so it is cleaned up
        // whenever the actor system still answers with the same one.
        for (auto it = _bodies.begin(); it != _bodies.end();) {
            if (it->second.touched) {
                ++it;
                continue;
            }
            const bool there = IsStillThere(it->second);
            Clear(it->second, there);
            ClearFace(it->second, there);
            it = _bodies.erase(it);
        }

        const auto local = _localKey != 0 ? _bodies.find(_localKey) : _bodies.end();
        UpdateMovementLock(local != _bodies.end() ? &local->second : nullptr);
    }

    bool AnimationService::IsStillThere(const Body &body) {
        auto *actorSystem = SDK::IActorSystem::GetInstance();
        // The actor may already be gone, so it is compared, never read, until
        // the actor system has vouched for it.
        return body.actor != nullptr && actorSystem != nullptr && actorSystem->GetActor(body.entityId) == body.actor && CharacterOf(body.actor) == body.character;
    }

    void AnimationService::Drive(std::uint64_t key, SDK::wh::entitymodule::C_Actor *actor, const Shared::Game::AnimationPlayback &wanted, const Shared::Game::FaceState &wantedFace, bool local, bool authoritative, std::uint64_t voiceSpeaker, bool station) {
        const auto [it, created] = _bodies.try_emplace(key);
        Body &body               = it->second;
        body.touched             = true;
        body.authoritative       = authoritative;
        if (created) {
            body.key = key;
            // What was already current when this machine first saw the body
            // is not news: a one-shot someone else watched a minute ago is not
            // replayed for whoever streams in now. The local player has no
            // such history -- the server only ever tells it something new.
            body.firstRevision = local ? 0 : wanted.revision;
        }

        SDK::ICharacterInstance *character = CharacterOf(actor);
        if (character == nullptr) {
            actor = nullptr;
        }
        if (body.actor != actor || body.character != character) {
            // Rebuilt or gone: nothing already applied can be written through
            // the old pointers, and everything is applied again to the new body.
            // A request already current when a body is rebuilt -- a respawn --
            // is not news to the new body either, so a one-shot it was still
            // playing is over.
            if (body.actor != nullptr) {
                body.firstRevision = wanted.revision;
                if (body.playback.active && !body.playback.loop && body.action != nullptr) {
                    Report(body, Shared::Game::AnimationEndReason::Interrupted);
                }
            }
            Clear(body, false);
            ClearFace(body, false);
            body.actor     = actor;
            body.entityId  = actor != nullptr ? actor->GetEntityId() : 0;
            body.character = character;
            body.playback  = {};
        }
        if (actor == nullptr) {
            return;
        }

        if (wanted.revision != body.playback.revision || station != body.station) {
            body.station = station;
            Clear(body, true);
            body.playback = wanted;
            if (station && wanted.revision == body.firstRevision && !wanted.loop) {
                // A newly streamed observer starts seated; it must not
                // replay a throw whose beginning it never received.
                body.playback.fragment = "DiceIdle";
                body.playback.loop     = true;
                for (auto &prop : body.playback.props) {
                    prop = {};
                }
            }
            Framework::Logging::GetLogger(kLogger)->debug("Body {} request {}: '{}' [{}]{}{}", key, wanted.revision, wanted.fragment, wanted.tags, wanted.active ? "" : " (stop)", wanted.loop ? " loop" : "");
            if (body.playback.active) {
                Start(body, wanted.revision == body.firstRevision);
            }
        }
        if (body.playback.active && !body.released) {
            ApplyGlobalTags(body.overlay);
            TickFragment(body, ControllerOf(actor));
        }
        DriveFace(body, wantedFace, voiceSpeaker);
    }

    void AnimationService::Start(Body &body, bool fresh) {
        // A request that ends with its fragment is over for anybody who did
        // not see the fragment start: none of it applies.
        if (fresh && body.playback.endsWithFragment) {
            body.released = true;
            return;
        }
        SDK::IActionController *controller = ControllerOf(body.actor);
        ResolveTags(body, controller);
        body.option = 0;
        if (!body.playback.clip.empty() && body.fragmentId != SDK::CTagDefinition::kInvalidId && !KCDC::Core::gApplication->GetStreamedAssetService().PrepareClip(body.character, body.playback.clip, body.option)) {
            // A clip this body's level was not built with -- published after the level loaded --
            // has no option to play under, and any other one would be the wrong clip.
            body.fragmentId = SDK::CTagDefinition::kInvalidId;
            Report(body, Shared::Game::AnimationEndReason::Refused);
        }
        if (body.playback.endsWithFragment && body.fragmentId == SDK::CTagDefinition::kInvalidId) {
            // Its tags are only there for the fragment; without one they would
            // hold the body in a stance nothing takes it out of.
            body.released = true;
            Report(body, Shared::Game::AnimationEndReason::Refused);
            return;
        }
        ApplyGlobalTags(body.overlay);
        AttachProps(body);
        // A fragment that ends by itself is only played for a request made
        // while this machine was watching; a loop is played whenever.
        body.fragmentPending = body.fragmentId != SDK::CTagDefinition::kInvalidId && (!fresh || body.playback.loop);
        body.replayAt        = 0;
    }

    void AnimationService::DropAction(Body &body) {
        if (body.action == nullptr) {
            return;
        }
        {
            std::lock_guard lock(_edgeMutex);
            _tickets.erase(body.action);
        }
        body.action->Release();
        body.action  = nullptr;
        body.started = false;
    }

    void AnimationService::Clear(Body &body, bool touchEngine) {
        if (body.action != nullptr) {
            SDK::IAction *action = body.action;
            action->AddRef(); // kept alive across the drop, for the finish below
            DropAction(body);
            // Ends it now: an installed action is blended off on the
            // controller's next update, a queued one is dropped from the queue.
            if (touchEngine && action->IsRunning()) {
                action->ForceFinish();
            }
            action->Release();
        }
        if (touchEngine) {
            RestoreGlobalTags(body.overlay);
            DetachProps(body);
        }
        body.fragmentId      = SDK::CTagDefinition::kInvalidId;
        body.fragmentTags    = {};
        body.option          = 0;
        body.fragmentPending = false;
        body.overlay         = {};
        body.released        = false;
        std::fill(std::begin(body.propAttached), std::end(body.propAttached), false);
    }

    void AnimationService::InterruptLocal() {
        const auto it = _localKey != 0 ? _bodies.find(_localKey) : _bodies.end();
        if (it == _bodies.end() || it->second.action == nullptr || !it->second.action->IsRunning()) {
            return;
        }
        Body &body         = it->second;
        const bool pending = body.action->GetStatus() == SDK::IAction::EStatus::Pending;
        body.action->ForceFinish();
        if (pending) {
            // The controller drops a finished queued action without a word
            // when it has no scope to tick it on, so its end is settled here.
            ApplyEdge(body, Edge::Interrupted);
        }
        // A playing one ends through its own Exit on the controller's next
        // update, and that edge decides what comes next.
        body.started = false;
        UpdateMovementLock(&body);
    }

    void AnimationService::ResolveTags(Body &body, SDK::IActionController *controller) {
        ResolveRequest(controller, body.playback.fragment, body.playback.tags, body.fragmentId, body.fragmentTags, body.overlay);
    }

    void AnimationService::ResolveRequest(SDK::IActionController *controller, const std::string &fragment, const std::string &tags, std::uint32_t &fragmentId, SDK::SFragTagState &fragmentTags, TagOverlay &overlay) {
        SDK::SAnimationContext *context = controller != nullptr ? controller->GetContext() : nullptr;
        overlay.tagState                = context != nullptr ? &context->GetTagState() : nullptr;

        fragmentId = SDK::CTagDefinition::kInvalidId;
        if (!fragment.empty() && controller != nullptr) {
            fragmentId = controller->FindFragment(LowercaseCrc(fragment));
            if (fragmentId == SDK::CTagDefinition::kInvalidId) {
                Framework::Logging::GetLogger(kLogger)->warn("This body's animation database has no fragment '{}'", fragment);
            }
        }

        // `C_Player::PlayAnim`'s order: the fragment's own dictionary first,
        // the controller's global one for whatever it does not know.
        const SDK::CTagDefinition *ownTags    = fragmentId != SDK::CTagDefinition::kInvalidId ? controller->GetFragmentTagDefinition(fragmentId) : nullptr;
        const SDK::CTagDefinition *globalTags = overlay.tagState != nullptr ? overlay.tagState->GetDefinition() : nullptr;
        Shared::Game::ForEachAnimationTag(tags, [&](std::string_view token) {
            const std::uint32_t crc = LowercaseCrc(token);
            const std::uint32_t own = ownTags != nullptr ? ownTags->FindCRC(crc) : SDK::CTagDefinition::kInvalidId;
            if (own != SDK::CTagDefinition::kInvalidId) {
                ownTags->SetTag(fragmentTags.bits, own, true);
                return true;
            }
            const std::uint32_t global = globalTags != nullptr ? globalTags->FindCRC(crc) : SDK::CTagDefinition::kInvalidId;
            if (global != SDK::CTagDefinition::kInvalidId) {
                overlay.globalTags.push_back(global);
                return true;
            }
            Framework::Logging::GetLogger(kLogger)->warn("This body's animation database has no tag '{}'{}{}", token, fragment.empty() ? "" : " for fragment ", fragment);
            return true;
        });
    }

    void AnimationService::ApplyGlobalTags(TagOverlay &overlay) {
        if (overlay.tagState == nullptr || overlay.globalTags.empty() || overlay.tagState->GetDefinition() == nullptr) {
            return;
        }
        const SDK::CTagDefinition *definition = overlay.tagState->GetDefinition();
        std::uint8_t state[SDK::CTagState::kStateBytes];
        std::memcpy(state, overlay.tagState->GetRawState(), sizeof(state));
        const bool first = !overlay.captured;
        if (first) {
            std::memcpy(overlay.before, state, sizeof(state));
        }
        for (const std::uint32_t tag : overlay.globalTags) {
            definition->SetTag(state, tag, true);
        }
        if (first) {
            std::memcpy(overlay.after, state, sizeof(state));
            overlay.captured = true;
        }
        // Stated again every tick rather than once: a body's own tag pass
        // rewrites the groups it owns, and the hand groups are among them on a
        // body that holds a real item.
        if (std::memcmp(state, overlay.tagState->GetRawState(), sizeof(state)) != 0) {
            overlay.tagState->SetRawState(state);
        }
    }

    void AnimationService::RestoreGlobalTags(TagOverlay &overlay) {
        if (overlay.tagState != nullptr && overlay.captured) {
            RestoreTags(overlay.tagState, overlay.before, overlay.after);
        }
        std::fill(std::begin(overlay.before), std::end(overlay.before), std::uint8_t {0});
        std::fill(std::begin(overlay.after), std::end(overlay.after), std::uint8_t {0});
        overlay.captured = false;
    }

    void AnimationService::AttachProps(Body &body) {
        SDK::IAttachmentManager *manager = body.character->GetIAttachmentManager();
        SDK::I3DEngine *engine           = SDK::I3DEngine::GetInstance();
        if (manager == nullptr || engine == nullptr) {
            return;
        }
        bool changed = false;
        for (std::size_t i = 0; i < Shared::Game::AnimationPlayback::kMaxProps; ++i) {
            const Shared::Game::AnimationProp &prop = body.playback.props[i];
            if (!prop.IsSet()) {
                continue;
            }
            // The socket is made once per character and rebound after that,
            // the way the game's own swap, `CScriptBind_Entity::SetAttachmentCGF`,
            // reuses a socket that already exists.
            SDK::IAttachment *socket = manager->GetInterfaceByName(kPropSockets[i]);
            if (socket == nullptr) {
                socket = manager->CreateAttachment(kPropSockets[i], SDK::CA_BONE, prop.joint.c_str(), false);
            }
            else {
                socket->SetJointName(prop.joint.c_str());
            }
            if (socket == nullptr) {
                continue;
            }
            socket->AlignJointAttachment();
            if (socket->GetJointID() == kNoJoint) {
                Framework::Logging::GetLogger(kLogger)->warn("This skeleton has no joint '{}' to hold '{}'", prop.joint, prop.model);
                continue;
            }
            SDK::IStatObj *mesh = engine->LoadStatObj(prop.model.c_str(), nullptr, nullptr, false, 0);
            const glm::quat &rotation = prop.rotation;
            const glm::vec3 &position = prop.position;
            SDK::QuatT offset;
            offset.q = SDK::Quat {rotation.x, rotation.y, rotation.z, rotation.w};
            offset.t = SDK::Vec3 {position.x, position.y, position.z};
            socket->SetAttRelativeDefault(offset);

            SDK::CCGFAttachment *attachment = mesh != nullptr ? SDK::CCGFAttachment::Create(mesh) : nullptr;
            if (attachment == nullptr) {
                Framework::Logging::GetLogger(kLogger)->warn("The game would not load prop model '{}'", prop.model);
                continue;
            }
            // Releases whatever the socket held, then shows it: the same two
            // calls `SetAttachmentCGF` makes.
            socket->AddBinding(attachment, nullptr, 0);
            socket->HideAttachment(0);
            body.propAttached[i] = true;
            changed              = true;
        }
        if (changed) {
            InvalidateDrawCache(body);
        }
    }

    void AnimationService::InvalidateDrawCache(Body &body) {
        // The body's rigid attachments are drawn into its render proxy's
        // permanent render objects, built the first time a mesh is bound and
        // kept: without this the socket goes on drawing the first prop it ever
        // held, whatever it holds now.
        SDK::IEntity *entity     = body.actor != nullptr ? body.actor->GetEntity() : nullptr;
        SDK::IEntityProxy *proxy = entity != nullptr ? entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Render)) : nullptr;
        SDK::I3DEngine *engine   = SDK::I3DEngine::GetInstance();
        if (proxy != nullptr && engine != nullptr) {
            engine->OnObjectModified(SDK::CRenderProxy::FromProxy(proxy), 0);
        }
    }

    void AnimationService::DetachProps(Body &body) {
        SDK::IAttachmentManager *manager = body.character != nullptr ? body.character->GetIAttachmentManager() : nullptr;
        bool changed                     = false;
        for (std::size_t i = 0; i < Shared::Game::AnimationPlayback::kMaxProps; ++i) {
            // Emptied rather than removed, so the next prop rebinds it;
            // clearing the binding releases the payload and with it the mesh.
            SDK::IAttachment *socket = body.propAttached[i] && manager != nullptr ? manager->GetInterfaceByName(kPropSockets[i]) : nullptr;
            if (socket != nullptr) {
                socket->ClearBinding(0);
                changed = true;
            }
            body.propAttached[i] = false;
        }
        if (changed) {
            InvalidateDrawCache(body);
        }
    }

    void AnimationService::Adopt(SDK::IAction &action) {
        if (!_edgeTableReady) {
            // Every action this service builds is a `TAction<SAnimationContext>`,
            // so the first one's table is the class's.
            const std::uint64_t *native = action.GetVirtualTable();
            _edgeTable[0]               = native[-1]; // the RTTI complete object locator
            for (std::size_t slot = 0; slot < SDK::IAction::kTActionVirtualSlots; ++slot) {
                _nativeSlots[slot]    = native[slot];
                _edgeTable[slot + 1] = native[slot];
            }
            const auto replace = [&](SDK::IAction::VirtualSlot slot, auto *function) {
                _edgeTable[static_cast<std::size_t>(slot) + 1] = reinterpret_cast<std::uint64_t>(function);
            };
            replace(SDK::IAction::VirtualSlot::Enter, &AdoptedEnter);
            replace(SDK::IAction::VirtualSlot::Fail, &AdoptedFail);
            replace(SDK::IAction::VirtualSlot::Exit, &AdoptedExit);
            replace(SDK::IAction::VirtualSlot::UpdatePending, &AdoptedUpdatePending);
            replace(SDK::IAction::VirtualSlot::InterruptedBeforeStart, &AdoptedInterruptedBeforeStart);
            replace(SDK::IAction::VirtualSlot::OnActionFinished, &AdoptedOnActionFinished);
            _edgeTableReady = true;
        }
        action.SetVirtualTable(_edgeTable.data() + 1);
    }

    void AnimationService::QueueFragment(Body &body, SDK::IActionController *controller) {
        body.fragmentPending = false;
        auto *action         = SDK::TAction<SDK::SAnimationContext>::Create(body.fragmentId, body.fragmentTags, SDK::IAction::kPlayAnimPriority);
        if (action == nullptr) {
            return;
        }
        Adopt(*action);
        action->AddRef();          // ours; Queue takes its own
        action->SetOptionIndex(body.option); // the same variant on every machine
        body.action  = action;
        body.serial  = ++_nextSerial;
        body.started = false;

        Ticket ticket;
        ticket.key              = body.key;
        ticket.revision         = body.playback.revision;
        ticket.serial           = body.serial;
        ticket.controller       = controller;
        ticket.loop             = body.playback.loop;
        ticket.endsWithFragment = body.playback.endsWithFragment;
        if (body.overlay.captured) {
            ticket.tagState = body.overlay.tagState;
            std::memcpy(ticket.tagsBefore, body.overlay.before, sizeof(ticket.tagsBefore));
            std::memcpy(ticket.tagsAfter, body.overlay.after, sizeof(ticket.tagsAfter));
        }
        {
            std::lock_guard lock(_edgeMutex);
            _tickets[action] = ticket;
        }
        controller->Queue(*action, 0.5F);
    }

    void AnimationService::TickFragment(Body &body, SDK::IActionController *controller) {
        // Only a pass still to be queued is looked at. Whatever happens to one
        // already queued arrives as an edge.
        if (controller != nullptr && body.action == nullptr && body.fragmentPending && MafiaNet::GetTime() >= body.replayAt) {
            QueueFragment(body, controller);
        }
    }

    // --- Engine edges ----------------------------------------------------------

    void AnimationService::PushEdge(const Ticket &ticket, Edge edge) {
        _edges.push_back({ticket.key, ticket.revision, ticket.serial, edge});
    }

    void AnimationService::OnActionEntered(SDK::IAction *action) {
        std::lock_guard lock(_edgeMutex);
        const auto it = _tickets.find(action);
        if (it == _tickets.end() || it->second.ended || it->second.started) {
            return;
        }
        it->second.started = true;
        PushEdge(it->second, Edge::Started);
    }

    void AnimationService::OnActionExited(SDK::IAction *action) {
        std::lock_guard lock(_edgeMutex);
        const auto it = _tickets.find(action);
        if (it == _tickets.end() || it->second.ended) {
            return;
        }
        Ticket &ticket = it->second;
        ticket.ended   = true;
        // A one-shot leaves its scope after its completion point too; that
        // end has been reported already.
        if (ticket.finished) {
            return;
        }
        if (ticket.endsWithFragment && ticket.tagState != nullptr) {
            RestoreTags(ticket.tagState, ticket.tagsBefore, ticket.tagsAfter);
        }
        PushEdge(ticket, Edge::Interrupted);
    }

    void AnimationService::OnActionRefused(SDK::IAction *action) {
        std::lock_guard lock(_edgeMutex);
        const auto it = _tickets.find(action);
        if (it == _tickets.end() || it->second.ended) {
            return;
        }
        Ticket &ticket = it->second;
        ticket.ended   = true;
        if (ticket.endsWithFragment && ticket.tagState != nullptr) {
            RestoreTags(ticket.tagState, ticket.tagsBefore, ticket.tagsAfter);
        }
        PushEdge(ticket, ticket.started ? Edge::Interrupted : Edge::Refused);
    }

    void AnimationService::OnActionCompletionPoint(SDK::IAction *action) {
        {
            std::lock_guard lock(_edgeMutex);
            const auto it = _tickets.find(action);
            if (it == _tickets.end() || it->second.ended) {
                return;
            }
            Ticket &ticket = it->second;
            if (!ticket.loop) {
                ticket.finished = true;
                // In this very update, before the controller blends the body
                // into whatever follows: the stance tag is what would pick the
                // seated idle again.
                if (ticket.endsWithFragment && ticket.tagState != nullptr) {
                    RestoreTags(ticket.tagState, ticket.tagsBefore, ticket.tagsAfter);
                }
                PushEdge(ticket, Edge::Finished);
                return;
            }
        }
        // A loop's next pass, from the completion point and through the
        // controller's own requeue: the same action installs itself again and
        // blends into its own start, with no Exit and no gap.
        action->Replay();
    }

    void AnimationService::OnControllerFlushed(SDK::IActionController *controller) {
        // Everything installed has already ended through Exit or the unstarted
        // interrupt; what is left is what sat in the queue, which the flush
        // dropped without a word.
        std::lock_guard lock(_edgeMutex);
        for (auto &[action, ticket] : _tickets) {
            if (ticket.controller != controller || ticket.ended || action->GetStatus() != SDK::IAction::EStatus::None) {
                continue;
            }
            ticket.ended = true;
            PushEdge(ticket, ticket.started ? Edge::Interrupted : Edge::Refused);
        }
    }

    void AnimationService::DrainEdges() {
        std::vector<EdgeEvent> edges;
        {
            std::lock_guard lock(_edgeMutex);
            edges.swap(_edges);
        }
        for (const EdgeEvent &event : edges) {
            const auto it = _bodies.find(event.key);
            // Anything about an action this body has since let go of -- a new
            // request, a rebuilt body -- is history.
            if (it == _bodies.end() || it->second.action == nullptr || it->second.serial != event.serial || it->second.playback.revision != event.revision) {
                continue;
            }
            ApplyEdge(it->second, event.edge);
        }
    }

    void AnimationService::ApplyEdge(Body &body, Edge edge) {
        if (edge == Edge::Started) {
            body.started = true;
            return;
        }
        DropAction(body);
        const bool loop = body.playback.loop;
        if (loop && edge != Edge::Finished) {
            // Cut short: straight back on. Never started: again in a moment.
            body.fragmentPending = true;
            body.replayAt        = MafiaNet::GetTime() + (edge == Edge::Refused ? kRefusedReplayDelayMs : 0);
            return;
        }
        if (body.playback.endsWithFragment) {
            // The edge took the tags off already; this settles a tick that may
            // have stated them again on another thread in between.
            RestoreGlobalTags(body.overlay);
            body.released = true;
        }
        Report(body, edge == Edge::Finished ? Shared::Game::AnimationEndReason::Finished : edge == Edge::Interrupted ? Shared::Game::AnimationEndReason::Interrupted : Shared::Game::AnimationEndReason::Refused);
    }

    void AnimationService::Report(Body &body, Shared::Game::AnimationEndReason reason) {
        if (!body.authoritative || body.playback.loop || body.reportedRevision == body.playback.revision) {
            return;
        }
        body.reportedRevision = body.playback.revision;
        auto *net             = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        Shared::RPC::AnimationEnded report;
        report.entityNetworkId = body.key;
        report.revision        = body.playback.revision;
        report.reason          = reason;
        net->BroadcastRPC(report, MafiaNet::Priority::Medium, MafiaNet::Reliability::ReliableOrdered);
    }

    // --- The face --------------------------------------------------------------

    void AnimationService::DriveFace(Body &body, const Shared::Game::FaceState &wanted, std::uint64_t voiceSpeaker) {
        Face &face = body.face;
        if (wanted.expressionRevision != face.expressionRevision) {
            ClearExpression(body, true);
            face.expressionRevision = wanted.expressionRevision;
            face.expression         = wanted.expression;
            face.tags               = wanted.tags;
            if (!face.expression.empty()) {
                StartExpression(body, ControllerOf(body.actor));
            }
        }
        if (!face.expression.empty()) {
            ApplyGlobalTags(face.overlay);
        }

        // A scripted mouth is the scripter's and wins. Without one, a talker
        // this machine is hearing moves theirs: `IsSpeakerTalking` holds across
        // the pauses between words, so the loop runs through a sentence and
        // only its weight follows the speech; out of range, muted or outside
        // the audible set reads as silent, and the mouth is the game's again.
        Shared::Game::TalkStyle talk = wanted.talk;
        float openness               = 1.0F;
        if (talk == Shared::Game::TalkStyle::None && voiceSpeaker != 0) {
            const Framework::Voice::VoiceClient *voice = Framework::CoreModules::GetVoiceClient();
            if (voice != nullptr && voice->IsSpeakerTalking(voiceSpeaker)) {
                talk     = kVoiceTalkStyle;
                openness = MouthOpenness(voice->GetSpeakerLevel(voiceSpeaker));
            }
        }
        TickTalk(body, talk, openness);
    }

    void AnimationService::StartExpression(Body &body, SDK::IActionController *controller) {
        Face &face               = body.face;
        std::uint32_t fragmentId = SDK::CTagDefinition::kInvalidId;
        SDK::SFragTagState fragmentTags;
        // The global tags are stated by `DriveFace`, this tick and every one after.
        ResolveRequest(controller, face.expression, face.tags, fragmentId, fragmentTags, face.overlay);
        if (fragmentId == SDK::CTagDefinition::kInvalidId) {
            return;
        }
        // Played whenever it is current, fresh or not: a face someone streams
        // in on is the face that body has now, and a gesture replayed for
        // them costs nothing. It ends on its own terms -- the `FE_*` loops
        // hold, the `ADLG_FA_*` gestures finish -- and the game's own face
        // comes back from under it either way.
        auto *action = SDK::TAction<SDK::SAnimationContext>::Create(fragmentId, fragmentTags, SDK::IAction::kPlayAnimPriority);
        if (action == nullptr) {
            return;
        }
        action->AddRef(); // ours; Queue takes its own
        action->SetOptionIndex(0);
        face.action = action;
        controller->Queue(*action, kExpressionQueueTime);
        Framework::Logging::GetLogger(kLogger)->debug("Body {} expression {}: '{}' [{}]", body.key, face.expressionRevision, face.expression, face.tags);
    }

    void AnimationService::TickTalk(Body &body, Shared::Game::TalkStyle wanted, float openness) {
        Face &face = body.face;
        if (wanted != face.talk) {
            StopTalk(body, true);
            face.talk                 = wanted;
            const char *clip          = Shared::Game::TalkStyleClip(wanted);
            SDK::IAnimationSet *clips = clip != nullptr ? body.character->GetIAnimationSet() : nullptr;
            face.talkAnimationId      = clips != nullptr ? clips->GetAnimIDByName(clip) : -1;
            if (clip != nullptr && face.talkAnimationId < 0) {
                Framework::Logging::GetLogger(kLogger)->warn("This body's animation set has no talk clip '{}'", clip);
            }
        }
        // Nothing of ours on the layer and its weight the engine's: the
        // common case, which costs no engine call at all.
        if (face.talkAnimationId < 0 && face.layerWeight == 1.0F) {
            return;
        }
        SDK::ISkeletonAnim *skeleton = body.character->GetISkeletonAnim();
        if (skeleton == nullptr) {
            return;
        }
        TickMouth(face, skeleton, openness);
        if (face.talkAnimationId < 0 || (face.talkToken != 0 && skeleton->FindAnimInFIFO(face.talkToken, static_cast<int>(kLipSyncLayer)) != nullptr)) {
            return;
        }
        // Ours is not playing. Whatever else holds the layer is the game's
        // own lip-sync for a line this body speaks, which knows the words;
        // the loop comes back once that has blended out.
        if (skeleton->GetNumAnimsInFIFO(kLipSyncLayer) > 0) {
            return;
        }
        SDK::CryCharAnimationParams params(static_cast<std::int32_t>(kLipSyncLayer), SDK::CA_LOOP_ANIMATION | SDK::CA_ALLOW_ANIM_RESTART);
        params.m_fTransTime      = SDK::IConsole::GetFloatCVar(kTalkStartTransitionCVar, 0.0F);
        params.m_fPlaybackWeight = kTalkWeight;
        params.m_nUserToken      = kTalkTokenBase | (++_nextTalkToken & 0x00FFFFFF);
        face.talkToken           = skeleton->StartAnimationById(face.talkAnimationId, params) ? params.m_nUserToken : 0;
    }

    void AnimationService::TickMouth(Face &face, SDK::ISkeletonAnim *skeleton, float openness) {
        const int layerAnims = skeleton->GetNumAnimsInFIFO(kLipSyncLayer);
        const bool playing   = face.talkToken != 0 && skeleton->FindAnimInFIFO(face.talkToken, static_cast<int>(kLipSyncLayer)) != nullptr;
        const bool fading    = face.fadingToken != 0 && face.fadingToken != face.talkToken && skeleton->FindAnimInFIFO(face.fadingToken, static_cast<int>(kLipSyncLayer)) != nullptr;
        const int ownAnims   = (playing ? 1 : 0) + (fading ? 1 : 0);

        // Anything but our own passes alone on the layer -- an empty layer,
        // or a line of the game's own lip-sync coming in -- plays at the
        // engine's weight. A talker picking up again before the last pass
        // has faded keeps following the envelope through the overlap.
        float weight = 1.0F;
        if (ownAnims != 0 && ownAnims == layerAnims) {
            weight = playing ? openness : face.layerWeight;
        }

        if (weight != face.layerWeight) {
            skeleton->SetLayerBlendWeight(static_cast<std::int32_t>(kLipSyncLayer), weight);
            face.layerWeight = weight;
        }
        if (face.layerWeight == 1.0F || !fading) {
            face.fadingToken = 0;
        }
    }

    void AnimationService::StopTalk(Body &body, bool touchEngine) {
        Face &face                   = body.face;
        SDK::ISkeletonAnim *skeleton = touchEngine && face.talkToken != 0 && body.character != nullptr ? body.character->GetISkeletonAnim() : nullptr;
        // Only while ours is all the layer plays: a line of the game's own
        // lip-sync blending in over it is already taking the layer, and
        // stopping the layer would cut that line's mouth.
        if (skeleton != nullptr && skeleton->GetNumAnimsInFIFO(kLipSyncLayer) == 1 && skeleton->FindAnimInFIFO(face.talkToken, static_cast<int>(kLipSyncLayer)) != nullptr) {
            skeleton->StopAnimationInLayer(static_cast<std::int32_t>(kLipSyncLayer), SDK::IConsole::GetFloatCVar(kTalkStopTransitionCVar, 0.0F));
        }
        // A lowered weight stays with the pass on its way out; `TickMouth`
        // puts the engine's back once the layer has let go of it.
        if (face.layerWeight != 1.0F) {
            face.fadingToken = face.talkToken;
        }
        face.talk            = Shared::Game::TalkStyle::None;
        face.talkAnimationId = -1;
        face.talkToken       = 0;
    }

    void AnimationService::ClearExpression(Body &body, bool touchEngine) {
        Face &face = body.face;
        if (face.action != nullptr) {
            if (touchEngine && face.action->IsRunning()) {
                face.action->ForceFinish();
            }
            face.action->Release();
            face.action = nullptr;
        }
        if (touchEngine) {
            RestoreGlobalTags(face.overlay);
        }
        face.overlay = {};
    }

    void AnimationService::ClearFace(Body &body, bool touchEngine) {
        ClearExpression(body, touchEngine);
        StopTalk(body, touchEngine);
        // Nobody is left to wait out the blend: the engine's weight goes back
        // now, before the record of having changed it is forgotten.
        SDK::ISkeletonAnim *skeleton = touchEngine && body.face.layerWeight != 1.0F && body.character != nullptr ? body.character->GetISkeletonAnim() : nullptr;
        if (skeleton != nullptr) {
            skeleton->SetLayerBlendWeight(static_cast<std::int32_t>(kLipSyncLayer), 1.0F);
        }
        // Nothing is applied any more, so the next tick puts the current face
        // on again from the start.
        body.face = {};
    }

    void AnimationService::UpdateMovementLock(const Body *local) {
        const bool wanted              = local != nullptr && local->playback.active && local->playback.lockMovement && local->started;
        SDK::CAnimatedCharacter *owner = local != nullptr && local->actor != nullptr ? local->actor->GetAnimatedCharacterRuntime() : nullptr;
        SDK::CAnimatedCharacter *want  = wanted ? owner : nullptr;
        if (want == _movementLock) {
            return;
        }
        // The engine's own answer to a clip that owns the body, as the emote
        // service uses it: horizontal movement from the clip's root motion,
        // vertical left to the game so gravity still applies.
        auto *player  = SDK::wh::entitymodule::C_Player::GetInstance();
        auto *current = player != nullptr ? player->GetAnimatedCharacterRuntime() : nullptr;
        if (_movementLock != nullptr && _movementLock == current) {
            _movementLock->SetMovementControlMethods(SDK::eMCMSlot_Animation, SDK::eMCM_Undefined, SDK::eMCM_Undefined, kMovementLockReason);
        }
        _movementLock = want;
        if (_movementLock != nullptr) {
            _movementLock->SetMovementControlMethods(SDK::eMCMSlot_Animation, SDK::eMCM_Animation, SDK::eMCM_Undefined, kMovementLockReason);
        }
    }

    void AnimationService::Reset() {
        {
            std::lock_guard lock(_inboxMutex);
            _receiving = false;
            _inbox.reset();
        }
        _local     = {};
        _localFace = {};
        {
            std::lock_guard lock(_inboxMutex);
            _faceInbox.reset();
        }
        for (auto &[key, body] : _bodies) {
            const bool there = IsStillThere(body);
            Clear(body, there);
            ClearFace(body, there);
        }
        _bodies.clear();
        _localKey = 0;
        {
            std::lock_guard lock(_edgeMutex);
            _edges.clear();
        }
        UpdateMovementLock(nullptr);
    }

    void AnimationService::NativeContextChanged() {
        for (auto &[key, body] : _bodies) {
            Clear(body, false);
            ClearFace(body, false);
        }
        _bodies.clear();
        _localKey     = 0;
        _movementLock = nullptr;
        std::lock_guard lock(_edgeMutex);
        _edges.clear();
    }
} // namespace KCDC::Features::Animation
