#include <utils/safe_win32.h>

#include "player_action_service.h"

#include "features/dog/dog.h"

#include "sdk/cryengine/action/c_animated_character.h"
#include "sdk/cryengine/action/c_tag_definition.h"
#include "sdk/wh/animationmodule/c_callback_action.h"
#include "sdk/wh/entitymodule/c_player.h"

#include "shared/game/animation_catalog.h"
#include "shared/game/body_action_catalog.h"
#include "shared/features/player/player_station.h"
#include <utils/hashing.h>

#include <algorithm>

#include <core_modules.h>
#include <logging/logger.h>
#include <mafianet/get_time.h>
#include <networking/replication/replication_manager.h>

#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <cstring>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <MinHook.h>
#include <utils/hooking/hook_function.h>
#include <stdexcept>

#include "core/obfuscation.h"
#include "core/application.h"
#include "core/local_events.h"

#include "features/dice/dice_module.h"
#include "features/player/player.h"
#include "features/player/player_action_service.h"

#include "sdk/wh/playermodule/c_alchemy_anim_action.h"
#include "sdk/wh/playermodule/c_minigame.h"
#include "sdk/wh/playermodule/i_lock_picking_action.h"
#include "sdk/cryengine/3dengine/i_3d_engine.h"
#include "sdk/cryengine/action/i_action_controller.h"
#include "sdk/cryengine/action/i_mannequin_action.h"
#include "sdk/cryengine/animation/c_cgf_attachment.h"
#include "sdk/cryengine/animation/i_attachment.h"
#include "sdk/cryengine/animation/i_attachment_manager.h"
#include "sdk/cryengine/entitysystem/c_render_proxy.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_actor_model.h"
#include "sdk/wh/entitymodule/c_human.h"
#include "sdk/wh/entitymodule/c_inventory.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/entitymodule/c_item_attachment_manager.h"
#include "sdk/wh/entitymodule/e_stance_category.h"
#include "sdk/wh/entitymodule/s_item_class_guid.h"
#include "sdk/wh/rpgmodule/c_inventory_soul.h"
#include "sdk/wh/xgenaimodule/c_ai_object_manager.h"
#include "sdk/patterns.h"
#include "sdk/wh/entitymodule/c_action_actor.h"
#include "sdk/wh/entitymodule/c_dog_external_action_sync_anim_master.h"
#include "sdk/wh/entitymodule/e_actor_action_type.h"
#include "sdk/wh/framework/i_action.h"

#include "sdk/cryengine/animation/c_entity_attachment.h"
#include "sdk/cryengine/animation/i_attachment.h"
#include "sdk/cryengine/animation/i_attachment_manager.h"
#include "sdk/cryengine/animation/i_attachment_object.h"
#include "sdk/wh/animationmodule/c_procedural_clip_anim_event.h"

#include "sdk/cryengine/animation/cry_char_animation_params.h"
#include "sdk/cryengine/animation/i_animation_set.h"
#include "sdk/cryengine/animation/i_character_instance.h"
#include "sdk/cryengine/animation/i_skeleton_anim.h"
#include "sdk/wh/playermodule/c_human_sharpening_action_base.h"
#include "sdk/wh/playermodule/c_sharpening.h"

#include "shared/features/player/station_sharpening.h"
#include "shared/features/player/station_tools.h"
#include "shared/game/crafting_catalog.h"
#include "shared/game/cry_guid_text.h"


namespace KCDC::Features::Player {
    namespace {
        constexpr const char *kLogger = "BodyAction";

        /**
         * Every fragment the animation catalog lists under `prefix`, keyed by
         * the CRC-32 of its lowercase name as Mannequin keys it.
         */
        std::unordered_set<std::uint32_t> CatalogFragmentsStartingWith(std::string_view prefix) {
            std::unordered_set<std::uint32_t> crcs;
            for (const Shared::Game::AnimationEntry &entry : Shared::Game::g_Animations) {
                if (std::string_view(entry.fragment).starts_with(prefix)) {
                    crcs.insert(SDK::IAction::LowercaseCrc32(entry.fragment));
                }
            }
            return crcs;
        }

        /**
         * Whether a fragment is one the alchemy minigame plays on its player:
         * every `Alchemy*` fragment. The apothecary NPCs' `ApothecaryAlchemy*`
         * behaviours are not among them.
         */
        bool IsAlchemyFragment(std::uint32_t crc) {
            static const std::unordered_set<std::uint32_t> fragments = CatalogFragmentsStartingWith("Alchemy");
            return fragments.contains(crc);
        }

        /**
         * Whether a fragment is one the smithing minigame plays on its player:
         * the sixteen `Blacksmithing*` fragments `SMannequinBlacksmithParams`
         * resolves (0x181938C04), one per action selector of
         * `C_Blacksmithing::QueueAnimationAction` (0x180856F98). The NPC
         * smiths' `BlacksmithBehavior*` loops are not among them.
         */
        bool IsBlacksmithingFragment(std::uint32_t crc) {
            static const std::unordered_set<std::uint32_t> fragments = CatalogFragmentsStartingWith("Blacksmithing");
            return fragments.contains(crc);
        }
    } // namespace

    PlayerActionService &PlayerActionService::Get() {
        static PlayerActionService service;
        return service;
    }

    void PlayerActionService::Update() {
        auto *player   = SDK::wh::entitymodule::C_Player::GetInstance();
        auto *animated = player != nullptr ? player->GetAnimatedCharacterRuntime() : nullptr;
        _localController.store(animated != nullptr ? animated->GetActionController() : nullptr, std::memory_order_release);

        // Loops and interruptible butchering must stop when the owner stops,
        // including cancellation before the animation's normal end.
        std::vector<StationToolNotice> toolEvents;
        {
            std::lock_guard lock(_mutex);
            if (_heldAction != nullptr && _heldAction->HasEnded()) {
                ReleaseHeldAction();
                Shared::Game::BodyAction stop;
                stop.kind = Shared::Game::BodyAction::Kind::Stop;
                _captured = stop;
            }
            toolEvents.swap(_stationToolEvents);
        }

        // Handed over outside the lock: placing a tool calls into the engine.
        auto *replication = !toolEvents.empty() ? Framework::CoreModules::GetReplication() : nullptr;
        if (replication != nullptr) {
            replication->ForEach<KCDC::Features::Player::Human>([&](KCDC::Features::Player::Human *human) {
                if (human->IsOwner()) {
                    return;
                }
                for (const StationToolNotice &notice : toolEvents) {
                    if (notice.bodyEntityId == human->GetEntityId()) {
                        human->ApplyStationToolEvent(notice.action, notice.event);
                    }
                }
            });
        }
    }

    void PlayerActionService::NotifyStationToolEvent(SDK::EntityId bodyEntityId, const SDK::IAction *action, Shared::Game::StationToolEvent event) {
        std::lock_guard lock(_mutex);
        _stationToolEvents.push_back({bodyEntityId, action, event});
    }

    void PlayerActionService::Reset() {
        _localController.store(nullptr, std::memory_order_release);
        std::lock_guard lock(_mutex);
        ReleaseHeldAction();
        ReleaseCapturedAction();
        _pickingPlayback.clear();
        _pickingTracked.store(false, std::memory_order_release);
        _captured.reset();
        if (_diceThrow != nullptr) {
            _diceThrow->Release();
        }
        _diceThrow          = nullptr;
        _diceThrowRevision  = 0;
        _praisedDogEntityId = 0;
        for (const HeldMinigameAction &held : _minigameActions) {
            held.action->Release();
        }
        _minigameActions.clear();
        _lastMinigameActionId = 0;
        _stationToolEvents.clear();
    }

    void PlayerActionService::ReleaseHeldAction() {
        if (_heldAction != nullptr) {
            _heldAction->Release();
            _heldAction = nullptr;
        }
    }

    void PlayerActionService::ReleaseCapturedAction() {
        if (_capturedAction != nullptr) {
            _capturedAction->Release();
            _capturedAction = nullptr;
        }
    }

    void PlayerActionService::TrackPickingMotion(SDK::IActionController *controller, SDK::IAction &action, SDK::ISkeletonAnim &skeleton, const std::array<float, 3> &values) {
        std::lock_guard lock(_mutex);
        _pickingPlayback.insert_or_assign(controller, PickingPlayback {&action, &skeleton, values});
        _pickingTracked.store(true, std::memory_order_release);
        Framework::Logging::GetLogger(kLogger)->debug("Replaying pickup blend: height={:.3f} angle={:.3f} variation={:.3f}", values[0], values[1], values[2]);
    }

    void PlayerActionService::ForgetPickingMotion(SDK::IAction *action) {
        std::lock_guard lock(_mutex);
        std::erase_if(_pickingPlayback, [action](const auto &entry) {
            return entry.second.action == action;
        });
        _pickingTracked.store(!_pickingPlayback.empty(), std::memory_order_release);
    }

    void PlayerActionService::ApplyPickingMotion(SDK::IActionController *controller) {
        if (!_pickingTracked.load(std::memory_order_acquire)) {
            return;
        }
        std::lock_guard lock(_mutex);
        const auto found = _pickingPlayback.find(controller);
        if (found == _pickingPlayback.end() || found->second.action->GetStatus() != SDK::IAction::EStatus::Installed) {
            return;
        }
        // Controller::Update installs the blend space first. These are the
        // same three writes C_PickUpItemLogic makes on the owner's skeleton.
        auto &playback = found->second;
        playback.skeleton->SetDesiredMotionParam(SDK::eMotionParamID_UserParam0, playback.values[0]);
        playback.skeleton->SetDesiredMotionParam(SDK::eMotionParamID_UserParam1, playback.values[1]);
        playback.skeleton->SetDesiredMotionParam(SDK::eMotionParamID_BlendWeight, playback.values[2]);
    }

    void PlayerActionService::NotifyQueued(SDK::IActionController *controller, SDK::IAction &action) {
        // `controller` is the native call's `this`, never null; with no local
        // player the compared pointer is null and nothing matches.
        //
        // Only the game's own actions: the mod's scripted animations queue
        // plain `TAction`s here too, and those already reach other players
        // through `HumanEntity::animation`.
        if (controller != _localController.load(std::memory_order_acquire)) {
            return;
        }
        const std::uint32_t fragmentId             = action.GetFragmentId();
        const std::uint32_t fragmentCrc            = controller->GetFragmentCRC(fragmentId);
        if (fragmentCrc == Framework::Utils::Hashing::CalculateCRC32("diceshake", 9)) {
            std::lock_guard lock(_mutex);
            if (_diceThrow != nullptr) {
                _diceThrow->Release();
            }
            action.AddRef();
            _diceThrow         = &action;
            _diceThrowRevision = Shared::Game::StationState::Advance(_diceThrowRevision);
            return;
        }
        // Every alchemy step queues a C_AlchemyAnimAction; its idles may be
        // plain callback actions, told apart by their Alchemy* fragment.
        // Every smithing step is a callback action its C_BlacksmithingAction
        // builds (0x180399464) at priority 5 with no flags, told apart by its
        // Blacksmithing* fragment. Lockpicking sets one fragment tag at most,
        // the lock's (0x180899B58); alchemy up to three, the table object and
        // its position; smithing two, the workpiece's and `forgeBag` or
        // `slaveTongs` (0x180399768).
        // A grindstone step is one of C_Sharpening's three actions, which
        // choose their fragment tag -- the blade's suffix -- only as they
        // initialise, and the wheel loop its fragment too: both are read
        // from what the action was built with instead of from its tags.
        const bool callback   = SDK::wh::animationmodule::C_CallbackAction::Is(action);
        const bool alchemy    = (SDK::wh::playermodule::C_AlchemyAnimAction::Is(action) || callback) && IsAlchemyFragment(fragmentCrc);
        const bool smithing   = callback && IsBlacksmithingFragment(fragmentCrc);
        const bool sharpening = SDK::wh::playermodule::C_HumanSharpeningActionBase::Is(action);
        if (SDK::wh::playermodule::I_LockPickingAction::Is(action) || alchemy || smithing || sharpening) {
            Shared::Game::MinigameActions::Action captured;
            captured.fragment = sharpening && SDK::wh::playermodule::C_HumanSharpeningActionBase::IsLoop(action) ? SDK::IAction::LowercaseCrc32(Shared::Game::kSharpeningLoopFragment) : fragmentCrc;
            captured.priority = static_cast<std::uint8_t>(std::clamp(action.GetPriority(), 0, 0xFF));
            // Read before the native Queue: still only what the constructor was given.
            captured.flags = static_cast<std::uint8_t>(action.GetFlags() & Shared::Game::MinigameActions::kReplayedFlags);
            const char *bladeSuffix = sharpening ? static_cast<const SDK::wh::playermodule::C_HumanSharpeningActionBase &>(action).GetBladeSuffix() : "";
            if (*bladeSuffix != '\0') {
                captured.tags[captured.tagCount++] = SDK::IAction::LowercaseCrc32(bladeSuffix);
            }
            else if (const SDK::CTagDefinition *tags = controller->GetFragmentTagDefinition(fragmentId)) {
                const std::uint8_t *state = action.GetFragmentTags().bits;
                for (std::uint32_t tag = 0; tag < tags->GetTagCount() && captured.tagCount < Shared::Game::MinigameActions::kMaxTags; ++tag) {
                    if (tags->IsSet(state, tag)) {
                        captured.tags[captured.tagCount++] = tags->GetTagCRC(tag);
                    }
                }
            }
            std::lock_guard lock(_mutex);
            captured.id = ++_lastMinigameActionId;
            action.AddRef(); // released once it has ended
            _minigameActions.push_back({captured, &action});
            return;
        }
        if (!SDK::wh::animationmodule::C_CallbackAction::Is(action)) {
            return;
        }
        const Shared::Game::BodyActionEntry *entry = Shared::Game::FindBodyAction(fragmentCrc);
        if (entry == nullptr) {
            return;
        }

        Shared::Game::BodyAction captured;
        captured.kind     = Shared::Game::BodyAction::Kind::Fragment;
        captured.fragment = fragmentCrc;
        captured.priority = static_cast<std::uint8_t>(std::clamp(action.GetPriority(), 0, 0xFF));

        // The option the game asked for rather than the variant it will pick:
        // the game's own transitions and interactions ask for option zero.
        const std::uint32_t option = action.GetOptionIndex();
        captured.option            = option < Shared::Game::BodyAction::kOptionRandom ? static_cast<std::uint8_t>(option) : Shared::Game::BodyAction::kOptionRandom;

        // Every tag the fragment was asked for, named rather than numbered:
        // the bits index this controller's own dictionary.
        if (const SDK::CTagDefinition *tags = controller->GetFragmentTagDefinition(fragmentId)) {
            const std::uint8_t *state = action.GetFragmentTags().bits;
            const std::uint32_t count = tags->GetTagCount();
            for (std::uint32_t tag = 0; tag < count; ++tag) {
                if (!tags->IsSet(state, tag)) {
                    continue;
                }
                if (captured.tagCount == Shared::Game::BodyAction::kMaxTags) {
                    Framework::Logging::GetLogger(kLogger)->warn("{} has more than {} tags; the rest are dropped", entry->fragment, Shared::Game::BodyAction::kMaxTags);
                    break;
                }
                captured.tags[captured.tagCount++] = tags->GetTagCRC(tag);
            }
        }

        std::lock_guard lock(_mutex);
        ReleaseHeldAction();
        ReleaseCapturedAction();
        // Held until taken: the seat's sit point is set on the action after
        // the game queues it, and a pickup's blend only once it installs.
        action.AddRef();
        _capturedAction = &action;
        if (entry->looped || fragmentCrc == Shared::Game::BodyAction::kButcheringFragment) {
            action.AddRef(); // released when it ends or is replaced
            _heldAction = &action;
        }
        _captured = captured;
    }

    void PlayerActionService::NotifyPraise(SDK::EntityId dogEntityId, std::uint32_t variant) {
        Shared::Game::BodyAction captured;
        captured.kind    = Shared::Game::BodyAction::Kind::Praise;
        captured.variant = variant;

        std::lock_guard lock(_mutex);
        ReleaseHeldAction();
        ReleaseCapturedAction();
        _praisedDogEntityId = dogEntityId;
        _captured           = captured;
    }

    std::uint32_t PlayerActionService::DiceThrow() {
        std::lock_guard lock(_mutex);
        if (_diceThrow == nullptr) {
            return 0;
        }
        if (_diceThrow->HasEnded()) {
            _diceThrow->Release();
            _diceThrow = nullptr;
            return 0;
        }
        return _diceThrowRevision;
    }

    Shared::Game::MinigameActions PlayerActionService::CaptureMinigameActions() {
        Shared::Game::MinigameActions state;
        std::lock_guard lock(_mutex);
        for (auto held = _minigameActions.begin(); held != _minigameActions.end();) {
            if (!held->action->HasEnded()) {
                ++held;
                continue;
            }
            held->action->Release();
            held = _minigameActions.erase(held);
        }
        // The newest that fit; one older than those is ended on receivers.
        const std::size_t first = _minigameActions.size() > Shared::Game::MinigameActions::kMaxActions ? _minigameActions.size() - Shared::Game::MinigameActions::kMaxActions : 0;
        for (std::size_t index = first; index < _minigameActions.size(); ++index) {
            const HeldMinigameAction &held = _minigameActions[index];
            state.actions[state.count++] = held.wire;
            float fraction               = 0.0F;
            if (held.wire.fragment == Shared::Game::MinigameActions::kTurnFragment && held.action->GetParam("TurnFraction", fraction)) {
                state.turnFraction = static_cast<std::uint8_t>(std::lround(std::clamp(fraction, 0.0F, 1.0F) * 255.0F));
            }
        }
        return state;
    }

    std::optional<Shared::Game::BodyAction> PlayerActionService::TakeLocalAction() {
        std::optional<Shared::Game::BodyAction> taken;
        SDK::EntityId dogEntityId = 0;
        {
            std::lock_guard lock(_mutex);
            if (_capturedAction != nullptr && _captured.has_value()) {
                // What the fragment's PositionAdjust clips align the body to.
                SDK::QuatT target {};
                _captured->hasTarget = _capturedAction->GetTargetPos(target);
                if (_captured->hasTarget) {
                    _captured->targetRotation = {target.q.x, target.q.y, target.q.z, target.q.w};
                    _captured->targetPosition = {target.t.x, target.t.y, target.t.z};
                }
            }
            if (_capturedAction != nullptr && _captured.has_value() && _captured->fragment != Shared::Game::BodyAction::kPickingFragment) {
                ReleaseCapturedAction();
            }
            if (_capturedAction != nullptr) {
                if (_capturedAction->GetStatus() == SDK::IAction::EStatus::Pending) {
                    return std::nullopt;
                }
                auto *player   = SDK::wh::entitymodule::C_Player::GetInstance();
                auto *animated = player != nullptr ? player->GetAnimatedCharacterRuntime() : nullptr;
                auto *skeleton = animated != nullptr ? animated->GetBoundSkeletonAnim3P() : nullptr;
                if (_capturedAction->GetStatus() == SDK::IAction::EStatus::Installed && _captured.has_value() && skeleton != nullptr) {
                    auto &values = _captured->pickingMotion;
                    _captured->hasPickingMotion =
                        skeleton->GetDesiredMotionParam(SDK::eMotionParamID_UserParam0, values[0]) && skeleton->GetDesiredMotionParam(SDK::eMotionParamID_UserParam1, values[1]) && skeleton->GetDesiredMotionParam(SDK::eMotionParamID_BlendWeight, values[2]);
                    if (_captured->hasPickingMotion) {
                        Framework::Logging::GetLogger(kLogger)->debug("Captured pickup blend: height={:.3f} angle={:.3f} variation={:.3f}", values[0], values[1], values[2]);
                    }
                    else {
                        // Specialized pickups can be ordinary clips, with no
                        // parametric sampler. Their fragment and tags suffice.
                        values = {};
                    }
                }
                else {
                    _captured.reset();
                }
                ReleaseCapturedAction();
            }
            taken       = _captured;
            dogEntityId = _praisedDogEntityId;
            _captured.reset();
            _praisedDogEntityId = 0;
        }
        if (!taken.has_value() || taken->kind != Shared::Game::BodyAction::Kind::Praise) {
            return taken;
        }

        // Only a praise of a dog this client runs is this player's to report;
        // one replayed here on somebody else's dog came back through the hook.
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication != nullptr) {
            replication->ForEach<KCDC::Features::Dog::Dog>([&](KCDC::Features::Dog::Dog *dog) {
                if (dog->IsOwner() && dog->GetEntityId() == dogEntityId) {
                    taken->partner = dog->GetNetworkID();
                }
            });
        }
        if (taken->partner == 0) {
            return std::nullopt;
        }
        return taken;
    }
} // namespace KCDC::Features::Player

namespace KCDC::Features::Player {
    namespace {

        // The owner's game queues its actions to play at once.
        constexpr float kQueueTime = -1.0F;

        // `scribe_book`, the NPCTool the game's reading NPCs hold: item
        // manipulation type 45 (`book`), model book_01.cdf. The model has its
        // own Mannequin database (kcd_book_database.adb) with the reader's
        // fragment names, so it opens and closes in step with the hands.
        constexpr std::string_view kReadingBookClass = "4ce6d8d4-bee3-4b7d-87e8-00fffb587e0e";

        // The CRC-32 of each lowercase name, Mannequin's own key.
        constexpr std::uint32_t kReadingBookInCrc  = 0x30679F52; // ReadingBookIn
        constexpr std::uint32_t kReadingBookCrc    = 0x08DD1E20; // ReadingBook
        constexpr std::uint32_t kReadingBookOutCrc = 0xAD84BAC7; // ReadingBookOut
        constexpr std::uint32_t kBookTagCrc        = 0xCBE5A331; // book, the fragments' tag

        // The mode the native generic hand-item path asks the movement
        // state machine for, as ApplyHandItem does.
        constexpr std::uint8_t kReadingItemHandlingMode = 3;

        /**
         * Queues one of the reading fragments with its `book` tag, at the
         * priority `C_Player::PlayAnim` uses. Actions of equal priority wait
         * for the one installed before them, so ReadingBookIn plays out
         * before the loop takes over. The returned action carries our own
         * reference, or is null when this body's database has no such
         * fragment.
         */
        SDK::IAction *QueueReadingFragment(SDK::IActionController *controller, std::uint32_t fragmentCrc) {
            const std::uint32_t fragment = controller->FindFragment(fragmentCrc);
            if (fragment == SDK::CTagDefinition::kInvalidId) {
                Framework::Logging::GetLogger(kLogger)->warn("This body's animation database has no reading fragment {:08X}", fragmentCrc);
                return nullptr;
            }
            SDK::SFragTagState tags;
            if (const SDK::CTagDefinition *definition = controller->GetFragmentTagDefinition(fragment)) {
                const std::uint32_t book = definition->FindCRC(kBookTagCrc);
                if (book != SDK::CTagDefinition::kInvalidId) {
                    definition->SetTag(tags.bits, book, true);
                }
            }
            auto *action = SDK::TAction<SDK::SAnimationContext>::Create(fragment, tags, SDK::IAction::kPlayAnimPriority);
            if (action == nullptr) {
                return nullptr;
            }
            action->AddRef(); // ours; Queue takes its own
            controller->Queue(*action, kQueueTime);
            return action;
        }

        void ReleaseAction(SDK::IAction *&action, bool finish) {
            if (action == nullptr) {
                return;
            }
            if (finish && action->IsRunning()) {
                action->ForceFinish();
            }
            action->Release();
            action = nullptr;
        }

        Shared::Game::BodyStance ToBodyStance(SDK::wh::entitymodule::E_StanceCategory::Type category) {
            switch (category) {
            case SDK::wh::entitymodule::E_StanceCategory::Sitting: return Shared::Game::BodyStance::Sitting;
            case SDK::wh::entitymodule::E_StanceCategory::Lying: return Shared::Game::BodyStance::Lying;
            default:
                // Horse, cart, crouch and kneel have their own replication;
                // for this one the body is on its feet.
                return Shared::Game::BodyStance::Standing;
            }
        }

        // The seat's level EntityGUID: WUID -> AI object -> entity -> guid.
        // Zero for no seat, or one the level did not place.
        std::uint64_t SeatGuidOf(std::uint64_t seatWuid) {
            auto *objects      = SDK::wh::xgenaimodule::C_AIObjectManager::GetInstance();
            auto *entitySystem = SDK::IEntitySystem::GetInstance();
            if (seatWuid == SDK::wh::entitymodule::C_ActorModel::kNoStanceObject || objects == nullptr || entitySystem == nullptr) {
                return 0;
            }
            const SDK::EntityId entityId = objects->FindEntityIdByWuid(SDK::wh::framework::WUID {seatWuid});
            SDK::IEntity *seat           = entityId != 0 ? entitySystem->GetEntity(entityId) : nullptr;
            return seat != nullptr ? seat->GetGuid() : 0;
        }

        // The inverse, on the receiving machine.
        std::uint64_t SeatWuidOf(std::uint64_t seatGuid) {
            return seatGuid != 0 ? SDK::wh::xgenaimodule::FindWuidByEntityGuid(seatGuid).value : SDK::wh::entitymodule::C_ActorModel::kNoStanceObject;
        }

        // The owner's body was walked onto the lock and set down there
        // (0x18121F13C) before its first lockpicking action, as at the
        // alchemy table before its first alchemy one, and only some idles
        // carry a clip that aligns a body; drawn behind its owner, a copy
        // would kneel short of the lock. So it starts once it is drawn where
        // its owner is, or after this long.
        constexpr float kMinigameArrivalDistance           = 0.1F;
        constexpr MafiaNet::Time kMinigameArrivalTimeoutMs = 1000;

        /**
         * Queues one of the owner's minigame actions as its minigame built
         * it: that fragment with its tags, at that priority. A lockpicking
         * turn gets its fraction before it is queued, since its clip reads it
         * as it enters, and a smithing step or a grindstone walk-in its
         * alignment target. The returned action carries our own reference, or
         * is null when this body's database has no such fragment.
         */
        SDK::IAction *QueueMinigameAction(SDK::IActionController *controller, const Shared::Game::MinigameActions::Action &owned, float turnFraction, const std::optional<SDK::QuatT> &alignmentTarget) {
            static const std::uint32_t sharpeningIn = SDK::IAction::LowercaseCrc32(Shared::Game::kSharpeningInFragment);
            const std::uint32_t fragment = controller->FindFragment(owned.fragment);
            if (fragment == SDK::CTagDefinition::kInvalidId) {
                Framework::Logging::GetLogger(kLogger)->warn("This body's animation database has no minigame fragment {:08X}", owned.fragment);
                return nullptr;
            }
            SDK::SFragTagState tags;
            const SDK::CTagDefinition *definition = owned.tagCount != 0 ? controller->GetFragmentTagDefinition(fragment) : nullptr;
            for (std::uint8_t i = 0; definition != nullptr && i < owned.tagCount; ++i) {
                const std::uint32_t tag = definition->FindCRC(owned.tags[i]);
                if (tag != SDK::CTagDefinition::kInvalidId) {
                    definition->SetTag(tags.bits, tag, true);
                }
            }
            // With the owner's flags: an alchemy idle a step interrupts is
            // queued again, here as there, rather than left ended.
            auto *action = SDK::TAction<SDK::SAnimationContext>::Create(fragment, tags, owned.priority, owned.flags);
            if (action == nullptr) {
                return nullptr;
            }
            action->AddRef(); // ours; Queue takes its own
            if (owned.fragment == Shared::Game::MinigameActions::kTurnFragment) {
                action->SetParam("TurnFraction", turnFraction);
            }
            // Every smithing step the owner's game queued was handed the
            // station's alignment point as its TargetPos (0x18148A1F8), which
            // the fragments' PositionAdjustAlignBone clips align the body to
            // (0x180AB165C). Without it they align it to nothing. A
            // grindstone's walk-in is handed the wheel's (0x182EA5ED8), for
            // the same clip; its loop and walk-out align nothing.
            if (alignmentTarget.has_value() && (IsBlacksmithingFragment(owned.fragment) || owned.fragment == sharpeningIn)) {
                action->SetParam("TargetPos", *alignmentTarget);
            }
            controller->Queue(*action, kQueueTime);
            return action;
        }

        /**
         * Where `C_Blacksmithing` aligns its player: the world transform of
         * the entity the smithery links as `alignment`, which every smithing
         * action copies when it is made (0x1808571F8, through 0x180857178)
         * and hands its fragment as TargetPos. Empty when the smithery or its
         * link is not loaded here.
         */
        std::optional<SDK::QuatT> SmithingAlignment(std::uint64_t stationGuid) {
            auto *entities = SDK::IEntitySystem::GetInstance();
            auto *station  = entities != nullptr && stationGuid != 0 ? entities->GetEntity(entities->FindEntityByGuid(stationGuid)) : nullptr;
            if (station == nullptr) {
                return std::nullopt;
            }
            for (auto *link = station->GetEntityLinks(); link != nullptr; link = link->next) {
                if (link->name.m_pData == nullptr || std::strcmp(link->name.m_pData, "alignment") != 0) {
                    continue;
                }
                auto *alignment = entities->GetEntity(link->entityId);
                if (alignment == nullptr) {
                    return std::nullopt;
                }
                return SDK::QuatT {alignment->GetWorldRotation(), alignment->GetWorldPos()};
            }
            return std::nullopt;
        }

        /**
         * Where `C_Sharpening` aligns its player as it walks in: the world
         * transform of the wheel's character slot 0, which the walk-in action
         * reads (`GetSlotWorldTM(0)`, 0x182EABCF4) and hands its fragment as
         * TargetPos. Empty when the wheel is not loaded here.
         */
        std::optional<SDK::QuatT> GrindstoneAlignment(std::uint64_t grindstoneGuid) {
            auto *entities = SDK::IEntitySystem::GetInstance();
            auto *wheel    = entities != nullptr && grindstoneGuid != 0 ? entities->GetEntity(entities->FindEntityByGuid(grindstoneGuid)) : nullptr;
            if (wheel == nullptr) {
                return std::nullopt;
            }
            SDK::Vec3 position, scale;
            SDK::Quat rotation;
            SDK::Matrix34ToTransform(wheel->GetSlotWorldTM(0), position, rotation, scale);
            return SDK::QuatT {rotation, position};
        }
    } // namespace

    // --- Owner capture -------------------------------------------------------

    void Human::CaptureBodyActions(SDK::wh::entitymodule::C_Actor *actor) {
        SDK::wh::entitymodule::C_ActorModel *model = actor != nullptr ? actor->GetActorModel() : nullptr;
        if (model != nullptr) {
            Shared::Game::BodyStanceState captured;
            captured.stance = ToBodyStance(model->GetStanceCategory());

            // The side only means something on what the body sits or lies on.
            if (captured.IsSeatedOrLying()) {
                const std::uint32_t side = model->GetStanceSide();
                captured.side            = side <= 0xFFU ? static_cast<std::uint8_t>(side) : 0;

                // Resolved only when the model's seat changes: the stance is
                // read every tick, the seat rarely moves.
                if (model->GetStanceObject() != _capturedSeatWuid) {
                    _capturedSeatWuid = model->GetStanceObject();
                    _capturedSeatGuid = SeatGuidOf(_capturedSeatWuid);
                }
                captured.seatGuid = _capturedSeatGuid;
            }

            // Set by the action system while a reading minigame's action
            // runs, from what C_Reading reports; the owner's own body gets
            // its reading idle from this and nothing else.
            if (model->IsActorStateActive(SDK::wh::entitymodule::E_ActorState::ReadingFromInventory)) {
                captured.reading = Shared::Game::BodyStanceState::kReadingFromInventory;
            }
            else if (model->IsActorStateActive(SDK::wh::entitymodule::E_ActorState::Reading)) {
                captured.reading = Shared::Game::BodyStanceState::kReading;
            }
            captured.holdingDecoy = model->IsActorStateActive(SDK::wh::entitymodule::E_ActorState::HoldingDecoy);
            bodyStance = captured;
        }

        if (std::optional<Shared::Game::BodyAction> action = PlayerActionService::Get().TakeLocalAction()) {
            action->revision = Shared::Game::BodyAction::Advance(bodyAction.revision);
            bodyAction       = *action;
        }
        minigameActions = PlayerActionService::Get().CaptureMinigameActions();
    }

    void Human::CaptureStation(SDK::wh::entitymodule::C_Actor *actor) {
        Shared::Game::StationState captured;
        if (actor != nullptr && soul.CanAct()) {
            captured = KCDC::Features::Dice::DiceModule::Get().Station();
            if (!captured.Active()) {
                for (const auto type : {SDK::wh::playermodule::E_MinigameType::Blacksmithing, SDK::wh::playermodule::E_MinigameType::Alchemy}) {
                    auto *minigame = SDK::wh::playermodule::C_Minigame::Of(actor, type);
                    auto *entities = SDK::IEntitySystem::GetInstance();
                    auto *table    = minigame != nullptr && minigame->IsActive() && entities != nullptr ? entities->GetEntity(minigame->GetStationEntityId()) : nullptr;
                    if (table == nullptr || table->GetGuid() == 0) {
                        continue;
                    }
                    captured.activity = type == SDK::wh::playermodule::E_MinigameType::Alchemy ? Shared::Game::StationActivity::Alchemy : Shared::Game::StationActivity::Smithing;
                    captured.guid     = table->GetGuid();
                    captured.working  = type == SDK::wh::playermodule::E_MinigameType::Alchemy || KCDC::Core::gApplication->GetSmithingService().IsWorking();
                    break;
                }
            }
            if (!captured.Active()) {
                CaptureSharpening(actor, captured);
            }
        }
        if (captured.activity != Shared::Game::StationActivity::Sharpening) {
            sharpening = {};
        }
        KCDC::Core::gApplication->GetLocalEvents().ObserveStation(captured, stateEpoch, soul.incarnation);
    }

    void Human::CaptureSharpening(SDK::wh::entitymodule::C_Actor *actor, Shared::Game::StationState &captured) {
        // The wheel is the one `SetGrindstone` stored, not the minigame's
        // shared interaction target, which sharpening never writes.
        auto *minigame = static_cast<SDK::wh::playermodule::C_Sharpening *>(SDK::wh::playermodule::C_Minigame::Of(actor, SDK::wh::playermodule::E_MinigameType::Sharpening));
        auto *entities = SDK::IEntitySystem::GetInstance();
        auto *wheel    = minigame != nullptr && minigame->IsActive() && entities != nullptr ? entities->GetEntity(minigame->GetGrindstoneEntityId()) : nullptr;
        if (wheel == nullptr || wheel->GetGuid() == 0) {
            return;
        }
        captured.activity        = Shared::Game::StationActivity::Sharpening;
        captured.guid            = wheel->GetGuid();
        sharpening.active        = true;
        sharpening.swordPosition = minigame->GetSwordPosition();
        sharpening.swordRotation = minigame->GetSwordRotation();
        sharpening.wheelSpeed    = minigame->GetWheelSpeed();
    }

    bool Human::HasStationPose() const {
        if (IsOwner() || !station.Active() || !soul.CanAct() || IsSeated() || KCDC::Features::Dice::DiceModule::Get().HoldsBody(_entityId)) {
            return false;
        }
        // The owner's alchemy and smithing steps are its minigame actions,
        // replayed as it queued them (ApplyMinigameActions); the station's
        // idle is for a body shown none, and for a woman, whose controller has
        // no Alchemy* or Blacksmithing* fragments.
        const bool replayedMinigame = station.activity == Shared::Game::StationActivity::Alchemy || station.activity == Shared::Game::StationActivity::Smithing || station.activity == Shared::Game::StationActivity::Sharpening;
        if (replayedMinigame && minigameActions.count != 0 && appearance.gender != Shared::Game::AppearanceGender::Female) {
            return false;
        }
        auto *entities = SDK::IEntitySystem::GetInstance();
        auto *actor    = ResolveActor();
        if (actor == nullptr || !actor->IsSettled() || actor->GetAnimatedCharacterRuntime() == nullptr || actor->GetAnimatedCharacterRuntime()->GetActionController() == nullptr) {
            return false;
        }
        auto *table = entities != nullptr ? entities->GetEntity(entities->FindEntityByGuid(station.guid)) : nullptr;
        if (table == nullptr) {
            return false;
        }
        if (station.activity != Shared::Game::StationActivity::Dice) {
            return true;
        }
        const char *name = station.slot == 0 ? "seat1" : "seat2";
        for (auto *link = table->GetEntityLinks(); link != nullptr; link = link->next) {
            if (link->name.m_pData != nullptr && std::strcmp(link->name.m_pData, name) == 0 && entities->GetEntity(link->entityId) != nullptr) {
                return true;
            }
        }
        return false;
    }

    bool Human::PlaceAtStation(SDK::IEntity *entity) const {
        if (!HasStationPose()) {
            return false;
        }
        if (station.activity != Shared::Game::StationActivity::Dice) {
            return false;
        }
        auto *entities   = SDK::IEntitySystem::GetInstance();
        auto *table      = entities->GetEntity(entities->FindEntityByGuid(station.guid));
        const char *name = station.slot == 0 ? "seat1" : "seat2";
        for (auto *link = table->GetEntityLinks(); link != nullptr; link = link->next) {
            if (link->name.m_pData == nullptr || std::strcmp(link->name.m_pData, name) != 0) {
                continue;
            }
            auto *seat = entities->GetEntity(link->entityId);
            if (seat == nullptr) {
                return false;
            }
            entity->SetPosRotScale(seat->GetWorldPos(), seat->GetWorldRotation(), SDK::Vec3 {1.0F, 1.0F, 1.0F}, SDK::ENTITY_XFORM_IGNORE_PHYSICS);
            return true;
        }
        return false;
    }

    // --- Applying to the copy ----------------------------------------------

    void Human::SuspendBodyActions(SDK::wh::entitymodule::C_Actor *actor) {
        if (actor == nullptr || !actor->IsSettled()) {
            return;
        }
        ReleaseAction(_bodyActionPlaying, true);
        if (_readingStage != ReadingStage::None) {
            DropReadingBook(actor);
        }
        for (auto &playing : _minigamePlaying) {
            ReleaseAction(playing.second, true);
        }
        _minigamePlaying.clear();
        _minigameWaitingSince = 0;
        ReleaseStationTools();
        ReleaseGrindstone();
        auto *attachments = static_cast<SDK::wh::entitymodule::C_Human *>(actor)->GetItemAttachmentManager();
        if (attachments != nullptr) {
            for (std::size_t hand = 0; hand < Shared::Entities::HumanEntity::kHandCount; ++hand) {
                if (_appliedHandWuid[hand] == 0) {
                    continue;
                }
                attachments->InvokeDetachItemById(SDK::wh::framework::WUID {_appliedHandWuid[hand]});
                _appliedHandWuid[hand] = 0;
            }
        }
        auto *model = actor->GetActorModel();
        if (model != nullptr && _appliedBodyStance.reading != 0) {
            model->SetActorState(static_cast<SDK::wh::entitymodule::E_ActorState::Type>(_appliedBodyStance.reading), false);
        }
        if (model != nullptr && _appliedBodyStance.IsSeatedOrLying()) {
            model->SetStance(SDK::wh::entitymodule::E_StanceCategory::Standing, 0, SDK::wh::entitymodule::C_ActorModel::kNoStanceObject);
        }
        // Discard one-shots received while the station owns this body.
        _bodyActionSeen            = true;
        _appliedBodyActionRevision = bodyAction.revision;
        _appliedBodyStance         = {};
    }

    bool Human::ApplyBodyActions(SDK::wh::entitymodule::C_Actor *actor) {
        SDK::wh::entitymodule::C_ActorModel *model = actor != nullptr && actor->IsSettled() ? actor->GetActorModel() : nullptr;
        if (model == nullptr) {
            return false;
        }
        if (IsSeated()) {
            _decoyReleased = true;
            ApplyHeldDecoy(actor);
            return false;
        }

        // ApplyActorStateTags reads state 24 into holdingDecoy. The native
        // throw fragments require it, including while the draw is queued.
        if (bodyStance.holdingDecoy != _appliedBodyStance.holdingDecoy) {
            model->SetActorState(SDK::wh::entitymodule::E_ActorState::HoldingDecoy, bodyStance.holdingDecoy);
        }

        // In the owner's order: its game queues a transition first and records
        // the stance only once that has played, and both arrive here on the
        // same reliable channel.
        if (!_bodyActionSeen) {
            _bodyActionSeen            = true;
            _appliedBodyActionRevision = bodyAction.revision;
        }
        else if (bodyAction.revision != _appliedBodyActionRevision) {
            _appliedBodyActionRevision = bodyAction.revision;
            if (bodyAction.IsSane()) {
                if (bodyAction.IsDecoy() && bodyAction.fragment == Shared::Game::BodyAction::kDecoyDraw) {
                    _decoyReleased = false;
                }
                PlayBodyAction(actor);
            }
        }

        // The model is all this sets. The body's own idle controller is
        // connected to it, and from the seat's animation list picks the idle
        // and adds the global tags that go with it (`sitting`, `lyingRight`);
        // the actor's per-frame sync keeps the model's stance only while those
        // tags stand. That is the whole chain the owner's game ran, so nothing
        // here writes a tag or plays an idle.
        if (!(bodyStance == _appliedBodyStance)) {
            // The same state the owner's action system set, through the same
            // writer; the body's tag update picks the reading idle from it.
            if (bodyStance.reading != _appliedBodyStance.reading) {
                if (_appliedBodyStance.reading != 0) {
                    model->SetActorState(static_cast<SDK::wh::entitymodule::E_ActorState::Type>(_appliedBodyStance.reading), false);
                }
                if (bodyStance.reading != 0) {
                    model->SetActorState(static_cast<SDK::wh::entitymodule::E_ActorState::Type>(bodyStance.reading), true);
                }
            }

            const auto category          = static_cast<SDK::wh::entitymodule::E_StanceCategory::Type>(bodyStance.stance);
            const std::uint64_t seatWuid = SeatWuidOf(bodyStance.seatGuid);
            if (bodyStance.IsSeatedOrLying() && seatWuid == SDK::wh::entitymodule::C_ActorModel::kNoStanceObject) {
                Framework::Logging::GetLogger(kLogger)->warn("'{}' rests on {:016X}, which this machine has no WUID for; the engine will not hold the stance", nickname, bodyStance.seatGuid);
            }
            model->SetStance(category, bodyStance.side, seatWuid);
            _appliedBodyStance = bodyStance;
            Framework::Logging::GetLogger(kLogger)->debug("Body of '{}' now in stance {} (side {}) on {:016X}, reading state {}", nickname, static_cast<int>(bodyStance.stance), bodyStance.side, bodyStance.seatGuid, bodyStance.reading);
        }

        bool acting = false;
        if (_bodyActionPlaying != nullptr) {
            acting = _bodyActionPlaying->IsRunning();
            if (!acting) {
                PlayerActionService::Get().ForgetPickingMotion(_bodyActionPlaying);
                _bodyActionPlaying->Release();
                _bodyActionPlaying = nullptr;
            }
        }
        const bool reading     = ApplyReading(actor);
        const bool minigame    = ApplyMinigameActions(actor);
        ApplySharpening(actor, minigame);
        ApplyHeldDecoy(actor);
        // Decoy fragments overlay the upper body. Keep ordinary crouched
        // locomotion underneath, including its player and stance tags.
        return (acting && !bodyAction.IsDecoy()) || reading || minigame || _appliedBodyStance.IsSeatedOrLying();
    }

    void Human::ReleaseHeldDecoy() {
        _decoyReleased = true;
        if (auto *actor = ResolveActor()) {
            ApplyHeldDecoy(actor);
        }
    }

    void Human::ApplyHeldDecoy(SDK::wh::entitymodule::C_Actor *actor) {
        if (!bodyStance.holdingDecoy) {
            _decoyReleased = false;
        }
        const bool wanted = bodyStance.holdingDecoy && !_decoyReleased && soul.CanAct();
        if (!wanted && _heldDecoy == nullptr) {
            return;
        }
        auto *entity    = actor->GetEntity();
        auto *character = entity != nullptr ? entity->GetCharacter(0) : nullptr;
        auto *manager   = character != nullptr ? character->GetIAttachmentManager() : nullptr;
        // Keep a dedicated socket: native hand-item updates own entity_right
        // and can clear or replace its binding after our presentation tick.
        // Both native human CDFs bind entity_right to righthandslot, and the
        // native decoy bind resets its relative transform to identity.
        auto *socket = manager != nullptr ? manager->GetInterfaceByName(std::string_view(KCDC_HIDE("kcdc_held_pebble")).data()) : nullptr;
        if (socket == nullptr && wanted && manager != nullptr) {
            socket = manager->CreateAttachment(std::string_view(KCDC_HIDE("kcdc_held_pebble")).data(), SDK::CA_BONE, std::string_view(KCDC_HIDE("righthandslot")).data(), false);
            if (socket != nullptr) {
                socket->AlignJointAttachment();
                socket->SetAttRelativeDefault(SDK::QuatT {SDK::Quat {0.0F, 0.0F, 0.0F, 1.0F}, SDK::Vec3 {0.0F, 0.0F, 0.0F}});
            }
        }
        if (socket == nullptr) {
            _heldDecoy = nullptr;
            return;
        }
        // The socket owns its payload. Never clear a weapon or another native
        // binding that has replaced ours in the meantime.
        if (socket->GetIAttachmentObject() != _heldDecoy) {
            _heldDecoy = nullptr;
        }
        bool changed = false;
        if (!wanted && _heldDecoy != nullptr) {
            socket->ClearBinding(0);
            _heldDecoy = nullptr;
            changed    = true;
        }
        auto *engine = SDK::I3DEngine::GetInstance();
        if (wanted && _heldDecoy == nullptr && socket->GetIAttachmentObject() == nullptr && engine != nullptr) {
            // A held visual has no physics; release spawns the projectile.
            auto *mesh = engine->LoadStatObj(std::string_view(KCDC_HIDE("objects/quest_items/stone_throwing/pebble_decoy_a.cgf")).data(), nullptr, nullptr, false, 0);
            if (auto *attachment = mesh != nullptr ? SDK::CCGFAttachment::Create(mesh) : nullptr) {
                socket->AddBinding(attachment, nullptr, 0);
                socket->HideAttachment(0);
                _heldDecoy = attachment;
                changed    = true;
            }
        }
        auto *proxy = changed && entity != nullptr ? entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Render)) : nullptr;
        if (proxy != nullptr && engine != nullptr) {
            engine->OnObjectModified(SDK::CRenderProxy::FromProxy(proxy), 0);
        }
    }

    // --- Minigames ------------------------------------------------------------

    bool Human::ApplyMinigameActions(SDK::wh::entitymodule::C_Actor *actor) {
        const Shared::Game::MinigameActions wanted = minigameActions.IsSane() ? minigameActions : Shared::Game::MinigameActions {};
        const auto listed                          = [&wanted](std::uint8_t id) {
            return std::any_of(wanted.actions, wanted.actions + wanted.count, [id](const Shared::Game::MinigameActions::Action &action) {
                return action.id == id;
            });
        };
        const auto queued = [this](std::uint8_t id) {
            return std::any_of(_minigamePlaying.begin(), _minigamePlaying.end(), [id](const std::pair<Shared::Game::MinigameActions::Action, SDK::IAction *> &playing) {
                return playing.first.id == id;
            });
        };

        // What the owner's game ended, in whatever order, ends here too.
        for (auto playing = _minigamePlaying.begin(); playing != _minigamePlaying.end();) {
            if (listed(playing->first.id)) {
                ++playing;
                continue;
            }
            ReleaseAction(playing->second, true);
            playing = _minigamePlaying.erase(playing);
        }
        // The native returns its tools on the steps' own Detach events; a
        // smith whose steps all ended, or who left the smithery, holds none.
        if (_minigamePlaying.empty() || station.activity != Shared::Game::StationActivity::Smithing) {
            ReleaseStationTools();
        }

        auto *animated                     = actor->GetAnimatedCharacterRuntime();
        SDK::IActionController *controller = animated != nullptr ? animated->GetActionController() : nullptr;
        if (controller == nullptr) {
            return false;
        }

        if (_minigamePlaying.empty() && wanted.count != 0) {
            const MafiaNet::Time now = MafiaNet::GetTime();
            if (_minigameWaitingSince == 0) {
                _minigameWaitingSince = now;
            }
            const SDK::Vec3 at = actor->GetEntity()->GetWorldPos();
            if (glm::length(glm::vec2(position.x - at.x, position.y - at.y)) > kMinigameArrivalDistance && now - _minigameWaitingSince < kMinigameArrivalTimeoutMs) {
                return false;
            }
        }
        _minigameWaitingSince = 0;

        // What it queued since, in its order: the controller here picks what
        // plays from the same queue the owner's picked from.
        const float turnFraction = static_cast<float>(wanted.turnFraction) / 255.0F;
        std::optional<SDK::QuatT> alignmentTarget;
        bool alignmentTargetResolved = false;
        for (std::uint8_t i = 0; i < wanted.count; ++i) {
            if (queued(wanted.actions[i].id)) {
                continue;
            }
            // Resolved once a pass, and only when the pass queues something.
            if (!alignmentTargetResolved) {
                if (station.activity == Shared::Game::StationActivity::Smithing) {
                    alignmentTarget = SmithingAlignment(station.guid);
                }
                else if (station.activity == Shared::Game::StationActivity::Sharpening) {
                    alignmentTarget = GrindstoneAlignment(station.guid);
                }
                alignmentTargetResolved = true;
            }
            _minigamePlaying.emplace_back(wanted.actions[i], QueueMinigameAction(controller, wanted.actions[i], turnFraction, alignmentTarget));
        }

        bool running = false;
        for (const auto &[owned, action] : _minigamePlaying) {
            if (action == nullptr) {
                continue;
            }
            if (owned.fragment == Shared::Game::MinigameActions::kTurnFragment) {
                action->SetParam("TurnFraction", turnFraction);
            }
            running = running || action->IsRunning();
        }
        return running;
    }

    // --- Station tools ------------------------------------------------------

    namespace {
        SDK::IAttachmentManager *AttachmentsOf(SDK::wh::entitymodule::C_Actor *actor) {
            SDK::IEntity *entity               = actor != nullptr ? actor->GetEntity() : nullptr;
            SDK::ICharacterInstance *character = entity != nullptr ? entity->GetCharacter(0) : nullptr;
            return character != nullptr ? character->GetIAttachmentManager() : nullptr;
        }
    } // namespace

    void Human::ApplyStationToolEvent(const SDK::IAction *action, Shared::Game::StationToolEvent event) {
        const bool replayed = action != nullptr && std::any_of(_minigamePlaying.begin(), _minigamePlaying.end(), [action](const std::pair<Shared::Game::MinigameActions::Action, SDK::IAction *> &playing) {
            return playing.second == action;
        });
        if (!replayed || station.activity != Shared::Game::StationActivity::Smithing) {
            return;
        }
        const std::size_t hand = Shared::Game::StationToolHand(event.tool);
        if (!event.attach) {
            ReleaseStationTool(hand);
            return;
        }

        // The smithery's own tool, as the native's tool attachment resolves
        // it: the entity the smithery links under the tool's name
        // (0x180B67B40).
        auto *entities = SDK::IEntitySystem::GetInstance();
        auto *actor    = ResolveActor();
        auto *smithery = entities != nullptr ? entities->GetEntity(entities->FindEntityByGuid(station.guid)) : nullptr;
        if (actor == nullptr || !actor->IsSettled() || actor->GetEntity() == nullptr || smithery == nullptr) {
            return;
        }
        SDK::IEntity *tool = nullptr;
        for (auto *link = smithery->GetEntityLinks(); link != nullptr; link = link->next) {
            if (link->name.m_pData != nullptr && std::strcmp(link->name.m_pData, Shared::Game::StationToolLink(event.tool)) == 0) {
                tool = entities->GetEntity(link->entityId);
                break;
            }
        }
        if (tool == nullptr || _stationTools[hand].tool == tool->GetId()) {
            return;
        }
        // Whatever this hand held goes back first.
        ReleaseStationTool(hand);
        auto *attachments = AttachmentsOf(actor);
        if (attachments == nullptr) {
            return;
        }
        // Our own socket, on the bone the native hand attachment uses --
        // `entity_right` and `entity_left` sit on `righthandslot` and
        // `lefthandslot` in the human CDFs -- because the native hand-item
        // updates own those two and can clear or replace a binding on them.
        const bool right         = hand == Shared::Entities::HumanEntity::kRightHand;
        SDK::IAttachment *socket = attachments->GetInterfaceByName(right ? std::string_view(KCDC_HIDE("kcdc_station_tool_right")).data() : std::string_view(KCDC_HIDE("kcdc_station_tool_left")).data());
        if (socket == nullptr) {
            socket = right ? attachments->CreateAttachment(std::string_view(KCDC_HIDE("kcdc_station_tool_right")).data(), SDK::CA_BONE, std::string_view(KCDC_HIDE("righthandslot")).data(), false)
                           : attachments->CreateAttachment(std::string_view(KCDC_HIDE("kcdc_station_tool_left")).data(), SDK::CA_BONE, std::string_view(KCDC_HIDE("lefthandslot")).data(), false);
            if (socket == nullptr) {
                return;
            }
            socket->AlignJointAttachment();
            socket->SetAttRelativeDefault(SDK::QuatT {SDK::Quat {0.0F, 0.0F, 0.0F, 1.0F}, SDK::Vec3 {0.0F, 0.0F, 0.0F}});
        }

        StationToolHold hold;
        hold.tool     = tool->GetId();
        hold.rest     = tool->GetWorldTM();
        hold.physics  = tool->IsPhysicsEnabled();
        auto *payload = SDK::CEntityAttachment::Create(tool, actor->GetEntity());
        if (payload == nullptr) {
            return;
        }
        // Carried, it must not push the body that carries it, as the
        // engine's own entity clip takes collision off what it attaches
        // (CProceduralClipAttachEntity::OnEnter).
        if (hold.physics) {
            tool->EnablePhysics(false);
        }
        socket->AddBinding(payload, nullptr, 0);
        _stationTools[hand] = hold;
        Framework::Logging::GetLogger(kLogger)->debug("Body of '{}' takes the smithery's {} into hand {}", nickname, Shared::Game::StationToolLink(event.tool), hand);
    }

    void Human::ReleaseStationTool(std::size_t hand) {
        StationToolHold &hold = _stationTools[hand];
        if (hold.tool == 0) {
            return;
        }
        // Only our own binding of this tool comes off, and only from the body
        // the actor system still answers with: a body queued for despawn
        // still carries it, a rebuilt one never had the socket. Anything else
        // on the socket by now is not ours to clear.
        const bool right                     = hand == Shared::Entities::HumanEntity::kRightHand;
        SDK::IAttachmentManager *attachments = AttachmentsOf(ResolveActor());
        SDK::IAttachment *socket             = attachments == nullptr ? nullptr : attachments->GetInterfaceByName(right ? std::string_view(KCDC_HIDE("kcdc_station_tool_right")).data() : std::string_view(KCDC_HIDE("kcdc_station_tool_left")).data());
        SDK::IAttachmentObject *payload      = socket != nullptr ? socket->GetIAttachmentObject() : nullptr;
        if (payload != nullptr && payload->GetAttachmentType() == SDK::IAttachmentObject::EType::Entity && static_cast<SDK::CEntityAttachment *>(payload)->GetEntityId() == hold.tool) {
            socket->ClearBinding(0);
        }
        auto *entities = SDK::IEntitySystem::GetInstance();
        if (SDK::IEntity *tool = entities != nullptr ? entities->GetEntity(hold.tool) : nullptr) {
            tool->SetWorldTM(hold.rest);
            if (hold.physics) {
                tool->EnablePhysics(true);
            }
        }
        hold = {};
    }

    void Human::ReleaseStationTools() {
        for (std::size_t hand = 0; hand < Shared::Entities::HumanEntity::kHandCount; ++hand) {
            ReleaseStationTool(hand);
        }
    }

    // --- Grindstone ---------------------------------------------------------

    namespace {
        SDK::ISkeletonAnim *SkeletonOf(SDK::IEntity *entity) {
            SDK::ICharacterInstance *character = entity != nullptr ? entity->GetCharacter(0) : nullptr;
            return character != nullptr ? character->GetISkeletonAnim() : nullptr;
        }

        /**
         * The blade's suffix the owner's grindstone steps are tagged with,
         * named by this body's own database: the one tag of the first
         * SharpeningMinigame* action that carries one. Empty when none does.
         */
        std::string BladeSuffixOf(SDK::IActionController *controller, const Shared::Game::MinigameActions &actions) {
            static const std::unordered_set<std::uint32_t> fragments = CatalogFragmentsStartingWith("SharpeningMinigame");
            for (std::uint8_t i = 0; controller != nullptr && i < actions.count; ++i) {
                const Shared::Game::MinigameActions::Action &action = actions.actions[i];
                if (action.tagCount == 0 || !fragments.contains(action.fragment)) {
                    continue;
                }
                const SDK::CTagDefinition *definition = controller->GetFragmentTagDefinition(controller->FindFragment(action.fragment));
                const char *name                      = definition != nullptr ? definition->GetTagName(definition->FindCRC(action.tags[0])) : nullptr;
                if (name != nullptr) {
                    return name;
                }
            }
            return {};
        }
    } // namespace

    void Human::ApplySharpening(SDK::wh::entitymodule::C_Actor *actor, bool replaying) {
        // The wheel turns as the owner's turns for as long as the owner is at
        // it, whether or not this body has arrived to play the steps.
        if (station.activity != Shared::Game::StationActivity::Sharpening || !sharpening.active) {
            ReleaseGrindstone();
            return;
        }

        // The blade along and across the wheel is the loop's blend, which the
        // native writes as the actor's motion parameters 10 and 11
        // (0x182EB0AA8, 0x182EB0B30) every frame.
        SDK::ISkeletonAnim *skeleton = replaying ? SkeletonOf(actor->GetEntity()) : nullptr;
        if (skeleton != nullptr) {
            skeleton->SetDesiredMotionParam(SDK::eMotionParamID_BlendWeight, sharpening.swordPosition);
            skeleton->SetDesiredMotionParam(SDK::eMotionParamID_BlendWeight2, sharpening.swordRotation);
        }

        auto *entities                    = SDK::IEntitySystem::GetInstance();
        SDK::IEntity *wheel               = entities != nullptr ? entities->GetEntity(entities->FindEntityByGuid(station.guid)) : nullptr;
        SDK::ISkeletonAnim *wheelSkeleton = SkeletonOf(wheel);
        if (wheelSkeleton == nullptr) {
            ReleaseGrindstone();
            return;
        }
        if (wheel->GetId() != _grindstone) {
            ReleaseGrindstone();
            const Shared::Game::SharpeningStation *catalogued = Shared::Game::FindSharpeningStation(KCDC::Core::gApplication->GetCurrentLevelName(), station.guid);
            _grindstone                                       = wheel->GetId();
            _grindstoneSpeedMultiplier                        = catalogued != nullptr ? catalogued->speedMultiplier : 1.0F;
        }

        // The wheel's own clip for this blade, started once on its layer 0 as
        // `StartGrindstoneAnimation` (0x182EB1C10) starts it: looping, from
        // its first frame. Until a step names the blade, the wheel waits.
        if (_grindstoneClip < 0) {
            auto *animated                     = actor->GetAnimatedCharacterRuntime();
            SDK::IActionController *controller = animated != nullptr ? animated->GetActionController() : nullptr;
            const std::string suffix           = BladeSuffixOf(controller, minigameActions);
            SDK::IAnimationSet *clips          = suffix.empty() || wheel->GetCharacter(0) == nullptr ? nullptr : wheel->GetCharacter(0)->GetIAnimationSet();
            const std::int32_t clip            = clips != nullptr ? clips->GetAnimIDByName(Shared::Game::GrindstoneAnimationName(suffix).c_str()) : -1;
            if (clip < 0) {
                return;
            }
            const SDK::CryCharAnimationParams params(0, SDK::CA_LOOP_ANIMATION);
            if (!wheelSkeleton->StartAnimationById(clip, params)) {
                return;
            }
            wheelSkeleton->SetLayerNormalizedTime(0, 0.0F);
            _grindstoneClip = clip;
            Framework::Logging::GetLogger(kLogger)->debug("Body of '{}' turns the grindstone {:016X} for a '{}' blade", nickname, station.guid, suffix);
        }

        // Its speed and the blade's angle on it, as `SetGrindstoneAnimationSpeed`
        // (0x182EB0888) and the rotation setter write them on the owner's.
        wheelSkeleton->SetLayerPlaybackScale(0, Shared::Game::GrindstonePlaybackScale(sharpening.wheelSpeed, _grindstoneSpeedMultiplier));
        wheelSkeleton->SetDesiredMotionParam(SDK::eMotionParamID_BlendWeight2, sharpening.swordRotation);
    }

    void Human::ReleaseGrindstone() {
        if (_grindstone == 0) {
            return;
        }
        // Left on its clip and stilled, as the native leaves a wheel whose
        // speed has run down (`UpdateWheelSpeed`).
        auto *entities = SDK::IEntitySystem::GetInstance();
        if (SDK::ISkeletonAnim *wheelSkeleton = _grindstoneClip >= 0 ? SkeletonOf(entities != nullptr ? entities->GetEntity(_grindstone) : nullptr) : nullptr) {
            wheelSkeleton->SetLayerPlaybackScale(0, 0.0F);
        }
        _grindstone                = 0;
        _grindstoneClip            = -1;
        _grindstoneSpeedMultiplier = 0.0F;
    }

    // --- Reading -------------------------------------------------------------

    bool Human::ApplyReading(SDK::wh::entitymodule::C_Actor *actor) {
        // A book on a stand stays on its stand, a world object every machine
        // already has; the actor state is all that travels for it.
        const bool wanted = bodyStance.reading == Shared::Game::BodyStanceState::kReadingFromInventory;
        if (!wanted && _readingStage == ReadingStage::None) {
            return false;
        }

        auto *human                        = static_cast<SDK::wh::entitymodule::C_Human *>(actor);
        auto *attachments                  = human->GetItemAttachmentManager();
        auto *animated                     = actor->GetAnimatedCharacterRuntime();
        SDK::IActionController *controller = animated != nullptr ? animated->GetActionController() : nullptr;
        if (attachments == nullptr || controller == nullptr) {
            return false;
        }

        switch (_readingStage) {
        case ReadingStage::None: {
            auto *runtime   = human->GetInventorySoulRuntime();
            auto *inventory = runtime != nullptr && runtime->IsInitialisedFor(human->GetSoul()) ? runtime->GetInventory() : nullptr;
            SDK::wh::entitymodule::S_ItemClassGUID bookClass {};
            if (inventory == nullptr || !Shared::Game::CryGuidFromText(kReadingBookClass, bookClass.m_low, bookClass.m_high)) {
                return false;
            }
            SDK::wh::entitymodule::C_Item *book = inventory->InvokeFindItemByClassRaw(bookClass);
            if (book == nullptr) {
                inventory->CreateItems(bookClass, 1, false);
                book = inventory->InvokeFindItemByClassRaw(bookClass);
            }
            if (book == nullptr) {
                Framework::Logging::GetLogger(kLogger)->error("Could not give '{}' a book to read", nickname);
                return false;
            }

            // The generic hand-item route: AttachItemById -> AttachItem ->
            // AttachItemToHand spawns the book's entity on the right hand and
            // sets the hand's manipulation tag (r_book).
            //
            // Its QueueEnslaveHandItem binds nothing for a book. The slave's
            // database comes from the Mannequin preview files, and the loader
            // (0x1812C2580) stops a file's list at the first entry missing a
            // database or skeleton; kcd_malePreview.xml's 24th, LongSword,
            // has neither, and both book entries come after it. So for every
            // male body in the game, the NPCs' included, the SlaveObject half
            // of ReadingBookIn and ReadingBookOut never plays: the book rides
            // in the hand and the body reads it.
            const bool accepted = attachments->InvokeRequestItemHandling(kReadingItemHandlingMode);
            const bool attached = accepted && attachments->InvokeAttachItemById(SDK::wh::framework::WUID {book->GetId()}, SDK::wh::entitymodule::C_ItemAttachmentManager::E_AttachHand::Right);
            attachments->InvokeFinishItemHandling();
            if (!attached) {
                Framework::Logging::GetLogger(kLogger)->warn("'{}' would not take the reading book in hand (item handling {})", nickname, accepted ? "accepted" : "refused");
                return false;
            }
            _readingBookWuid = book->GetId();
            _readingIntro    = QueueReadingFragment(controller, kReadingBookInCrc);
            _readingAction   = QueueReadingFragment(controller, kReadingBookCrc);
            _readingStage    = ReadingStage::Reading;
            Framework::Logging::GetLogger(kLogger)->debug("'{}' takes a book to read", nickname);
            return true;
        }

        case ReadingStage::Reading: {
            if (wanted) {
                return true;
            }
            ReleaseAction(_readingIntro, true);
            ReleaseAction(_readingAction, true);
            _readingAction = QueueReadingFragment(controller, kReadingBookOutCrc);
            _readingStage  = ReadingStage::Closing;
            return true;
        }

        case ReadingStage::Closing: {
            if (_readingAction != nullptr && !_readingAction->HasEnded()) {
                return true;
            }
            ReleaseAction(_readingAction, false);
            DropReadingBook(actor);
            return false;
        }
        }
        return false;
    }

    void Human::DropReadingBook(SDK::wh::entitymodule::C_Actor *actor) {
        auto *human       = static_cast<SDK::wh::entitymodule::C_Human *>(actor);
        auto *attachments = human->GetItemAttachmentManager();
        if (attachments != nullptr && _readingBookWuid != 0) {
            attachments->InvokeDetachItemById(SDK::wh::framework::WUID {_readingBookWuid});
        }

        // The book belongs to the reading, not to the body: it goes, so the
        // next reading creates its own and nothing else ever finds it.
        auto *runtime   = human->GetInventorySoulRuntime();
        auto *inventory = runtime != nullptr && runtime->IsInitialisedFor(human->GetSoul()) ? runtime->GetInventory() : nullptr;
        SDK::wh::entitymodule::S_ItemClassGUID bookClass {};
        if (inventory != nullptr && Shared::Game::CryGuidFromText(kReadingBookClass, bookClass.m_low, bookClass.m_high)) {
            inventory->RemoveItemsByClass(bookClass, 1);
        }

        ReleaseAction(_readingIntro, true);
        ReleaseAction(_readingAction, true);
        _readingBookWuid = 0;
        _readingStage    = ReadingStage::None;
        Framework::Logging::GetLogger(kLogger)->debug("'{}' puts the book away", nickname);
    }

    void Human::PlayBodyAction(SDK::wh::entitymodule::C_Actor *actor) {
        // Whatever is playing gives way, as it does in the owner's game: a new
        // action replaces it, and a stop ends it.
        if (_bodyActionPlaying != nullptr) {
            PlayerActionService::Get().ForgetPickingMotion(_bodyActionPlaying);
            if (_bodyActionPlaying->IsRunning()) {
                _bodyActionPlaying->ForceFinish();
            }
            _bodyActionPlaying->Release();
            _bodyActionPlaying = nullptr;
        }

        switch (bodyAction.kind) {
        case Shared::Game::BodyAction::Kind::Fragment: PlayBodyActionFragment(actor); return;
        case Shared::Game::BodyAction::Kind::Praise: PlayPraise(actor); return;
        case Shared::Game::BodyAction::Kind::Stop:
        case Shared::Game::BodyAction::Kind::None: return;
        }
    }

    void Human::PlayBodyActionFragment(SDK::wh::entitymodule::C_Actor *actor) {
        auto *animated                     = actor->GetAnimatedCharacterRuntime();
        SDK::IActionController *controller = animated != nullptr ? animated->GetActionController() : nullptr;
        if (controller == nullptr) {
            return;
        }

        const std::uint32_t fragment = controller->FindFragment(bodyAction.fragment);
        if (fragment == SDK::CTagDefinition::kInvalidId) {
            Framework::Logging::GetLogger(kLogger)->warn("This body's animation database has no fragment {:08X}", bodyAction.fragment);
            return;
        }

        // Each tag is resolved against this body's own dictionary for the
        // fragment, which is where the owner's came from.
        SDK::SFragTagState tags;
        if (const SDK::CTagDefinition *definition = controller->GetFragmentTagDefinition(fragment)) {
            for (std::uint8_t i = 0; i < bodyAction.tagCount; ++i) {
                const std::uint32_t tag = definition->FindCRC(bodyAction.tags[i]);
                if (tag == SDK::CTagDefinition::kInvalidId) {
                    Framework::Logging::GetLogger(kLogger)->warn("This body's animation database has no tag {:08X} for fragment {:08X}", bodyAction.tags[i], bodyAction.fragment);
                    continue;
                }
                definition->SetTag(tags.bits, tag, true);
            }
        }

        // The same generic action at the priority the owner's game queued it
        // at, so it displaces locomotion as it did there.
        auto *action = SDK::TAction<SDK::SAnimationContext>::Create(fragment, tags, bodyAction.priority);
        if (action == nullptr) {
            return;
        }
        action->AddRef(); // ours; Queue takes its own
        action->SetOptionIndex(bodyAction.option == Shared::Game::BodyAction::kOptionRandom ? SDK::IAction::kOptionRandom : bodyAction.option);
        // The owner's alignment target: the fragment's PositionAdjust clips
        // slide this body onto the same seat point they slid the owner's.
        if (bodyAction.hasTarget) {
            const auto &rotation = bodyAction.targetRotation;
            const auto &position = bodyAction.targetPosition;
            action->SetParam("TargetPos", SDK::QuatT {SDK::Quat {rotation[0], rotation[1], rotation[2], rotation[3]}, SDK::Vec3 {position[0], position[1], position[2]}});
        }
        if (bodyAction.hasPickingMotion) {
            if (auto *skeleton = animated->GetBoundSkeletonAnim3P()) {
                PlayerActionService::Get().TrackPickingMotion(controller, *action, *skeleton, bodyAction.pickingMotion);
            }
        }
        controller->Queue(*action, kQueueTime);
        _bodyActionPlaying = action;
        Framework::Logging::GetLogger(kLogger)->debug("Body of '{}' plays {:08X} with {} tag(s) at priority {}", nickname, bodyAction.fragment, bodyAction.tagCount, bodyAction.priority);
    }

    void Human::PlayPraise(SDK::wh::entitymodule::C_Actor *actor) {
        // The praise is the dog's paired animation, so it is the receiver's
        // copy of that dog that plays it, with this body as the partner.
        KCDC::Features::Dog::Dog *dog = nullptr;
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<KCDC::Features::Dog::Dog>([&](KCDC::Features::Dog::Dog *candidate) {
                if (candidate->GetNetworkID() == bodyAction.partner) {
                    dog = candidate;
                }
            });
        }
        SDK::IEntity *entity = actor->GetEntity();
        if (dog == nullptr || entity == nullptr) {
            Framework::Logging::GetLogger(kLogger)->warn("'{}' praised dog {}, which has no body here", nickname, bodyAction.partner);
            return;
        }
        if (!dog->Praise(entity->GetId(), bodyAction.variant)) {
            Framework::Logging::GetLogger(kLogger)->warn("Dog {} refused the praise from '{}'", bodyAction.partner, nickname);
            return;
        }
        Framework::Logging::GetLogger(kLogger)->debug("Body of '{}' praises dog {} (variant {})", nickname, bodyAction.partner, bodyAction.variant);
    }

    void Human::ForgetBodyActions() {
        _heldDecoy     = nullptr; // The old character owns and releases the binding.
        _decoyReleased = false;
        if (_bodyActionPlaying != nullptr) {
            PlayerActionService::Get().ForgetPickingMotion(_bodyActionPlaying);
            _bodyActionPlaying->Release();
            _bodyActionPlaying = nullptr;
        }

        // The reading book went with the old body; a reading still under way
        // starts again on the new one.
        ReleaseAction(_readingIntro, false);
        ReleaseAction(_readingAction, false);
        _readingBookWuid = 0;
        _readingStage    = ReadingStage::None;

        // So does a minigame: the new body is handed the owner's again.
        for (auto &playing : _minigamePlaying) {
            ReleaseAction(playing.second, false);
        }
        _minigamePlaying.clear();
        _minigameWaitingSince = 0;
        // The tools are the smithery's and outlive the body: back where they
        // rest. The grindstone too outlives it, and stops.
        ReleaseStationTools();
        ReleaseGrindstone();

        // A new body starts standing, and a stance the owner still holds is
        // applied to it again; an action already played is not.
        _appliedBodyStance         = {};
        _appliedBodyActionRevision = bodyAction.revision;
    }
} // namespace KCDC::Features::Player

namespace KCDC::Features::Player {
    namespace {
        using Queue_t         = void(__fastcall *)(SDK::IActionController *, SDK::IAction *, float);
        Queue_t s_queueNative = nullptr;
        using Update_t          = void(__fastcall *)(SDK::IActionController *, float);
        Update_t s_updateNative = nullptr;

        void __fastcall UpdateController(SDK::IActionController *controller, float frameTime) {
            s_updateNative(controller, frameTime);
            PlayerActionService::Get().ApplyPickingMotion(controller);
        }

        using QueueAction_t               = bool(__fastcall *)(void *, SDK::wh::framework::I_Action **);
        QueueAction_t s_queueActionNative = nullptr;

        // Every Mannequin action on every actor passes here, which is the one
        // place the local player's transitions and interactions -- queued
        // deep inside the engine's own actions -- can be seen with the tags
        // that fit them to their object.
        void __fastcall QueueAction(SDK::IActionController *controller, SDK::IAction *action, float queueTime) {
            // `Queue` takes the action by reference, so it is never null.
            PlayerActionService::Get().NotifyQueued(controller, *action);
            s_queueNative(controller, action, queueTime);
        }

        // The dog's praise is one paired action `C_Dog::Bond` posts on the
        // animal's own action queue and forgets; nothing on the dog or the
        // player says afterwards that it happened. The moment it is posted is
        // the one moment its rolled variant can be read.
        bool __fastcall QueueActorAction(void *queue, SDK::wh::framework::I_Action **action) {
            // Read before the call: the queue takes the handle over, and the
            // reference the caller held may be gone by the time it returns.
            SDK::wh::framework::I_Action *posted = action != nullptr ? *action : nullptr;
            const bool praise                    = posted != nullptr && posted->GetActionType() == SDK::wh::entitymodule::E_ActorActionType::DogSyncAnimMaster;
            const std::uint32_t variant          = praise ? static_cast<SDK::wh::entitymodule::C_DogExternalActionSyncAnimMaster *>(posted)->GetVariant() : 0;

            const bool accepted = s_queueActionNative(queue, action);
            if (!praise || !accepted) {
                return accepted;
            }

            auto *dog    = static_cast<SDK::wh::entitymodule::C_ActionActor *>(queue)->GetActor();
            auto *entity = dog != nullptr ? dog->GetEntity() : nullptr;
            if (entity != nullptr) {
                PlayerActionService::Get().NotifyPraise(entity->GetId(), variant);
            }
            return accepted;
        }

        using AnimEventEnter_t                  = void(__fastcall *)(SDK::wh::animationmodule::C_ProceduralClipAnimEvent *, float, float, const SDK::wh::animationmodule::S_ProcClipcAnimEventParams *);
        AnimEventEnter_t s_animEventEnterNative = nullptr;

        // A smithing fragment's `AnimEvent` clips raise the tool events on the
        // body's `AnimEventContext` signal, which only the native smithing
        // action listens to; on a body that replays the steps nothing does.
        // Every body's clips pass here, so only a tool event goes further, as
        // the body and the action whose fragment raised it.
        void __fastcall AnimEventEnter(SDK::wh::animationmodule::C_ProceduralClipAnimEvent *clip, float blendTime, float duration, const SDK::wh::animationmodule::S_ProcClipcAnimEventParams *params) {
            s_animEventEnterNative(clip, blendTime, duration, params);
            const char *name = params != nullptr ? params->GetEventName() : nullptr;
            if (name == nullptr) {
                return;
            }
            const std::optional<Shared::Game::StationToolEvent> event = Shared::Game::ParseStationToolEvent(name);
            SDK::IEntity *body                                        = clip->GetEntity();
            if (event.has_value() && body != nullptr) {
                PlayerActionService::Get().NotifyStationToolEvent(body->GetId(), clip->GetAction(), *event);
            }
        }
    } // namespace
} // namespace KCDC::Features::Player

static InitFunction initActionHooks(
    []() {
        const auto queue       = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CActionController__Queue), reinterpret_cast<LPVOID>(&KCDC::Features::Player::QueueAction), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_queueNative));
        const auto queueAction = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActionActor__QueueAction), reinterpret_cast<LPVOID>(&KCDC::Features::Player::QueueActorAction), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_queueActionNative));
        const auto update      = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CActionController__Update), reinterpret_cast<LPVOID>(&KCDC::Features::Player::UpdateController), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_updateNative));
        const auto animEvent   = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ProceduralClipAnimEvent__OnEnter), reinterpret_cast<LPVOID>(&KCDC::Features::Player::AnimEventEnter), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_animEventEnterNative));
        if (queue != MH_OK || queueAction != MH_OK || update != MH_OK || animEvent != MH_OK) {
            throw std::runtime_error("Could not install the body action hooks");
        }
    },
    "PlayerAction");
