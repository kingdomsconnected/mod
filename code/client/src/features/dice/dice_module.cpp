#include "features/dice/dice_module.h"

#include "core/application.h"
#include "features/audio/audio_range.h"
#include "features/npc/npc.h"
#include "features/player/player.h"
#include "sdk/cryengine/3dengine/i_stat_obj.h"
#include "sdk/cryengine/scaleform/i_flash_ui.h"
#include "sdk/cryengine/scaleform/c_flash_ui_element.h"
#include "sdk/cryengine/scaleform/i_flash_player.h"
#include <networking/replication/replication_manager.h>
#include "features/ui/ui_hud_notifications.h"
#include "common/module_support.h"

#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_link.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/entitysystem/s_entity_proximity_query.h"
#include "sdk/patterns.h"
#include "sdk/wh/entitymodule/c_human.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

#include <utils/hooking/hooking.h>

#include <cmath>
#include <cstring>
#include "features/player/player_action_service.h"
#include <limits>
#include <string>

namespace KCDC::Features::Dice {
    namespace {
        constexpr const char *kLogger = "Dice";

        /** How far from the point between the two players the table may stand. */
        constexpr float kTableSearchRadius = 6.0F;

        /** How long the opponent's body and the table may take to be there once the match is on. */
        constexpr MafiaNet::Time kStartTimeoutMs = 15000;

        /** How long a won match waits for the table to play the winning pass before it is closed anyway. */
        constexpr MafiaNet::Time kNativeEndGraceMs = 20000;

        /** `C_DiceBadge` type 13 at tier 3: the opponent seat may draw no badge. */
        constexpr std::uint16_t kNoBadge = 0x030D;

        /** `TransitionToEndingState` result for a seat that gave up (`RequestGiveUp`). */
        constexpr std::uint32_t kEndingGaveUp = 3;

        /**
         * The throw's player cues. `DiceShake` -- the only throw the native
         * game knows, and the one every player's body replays at its station
         * -- plays `minigame_dice_shake.caf`, whose animation events fire
         * these on the thrower's body. They are authored for the player
         * hearing himself: 2D events with a 65535 m cull, so a remote
         * player's throw rang out on every machine on the server. The range
         * keeps this machine's own throw, a metre or two from the camera, at
         * full volume (see `AudioRange::Gain`) and fades a remote one out
         * over the room it is played in.
         */
        constexpr float kThrowCueRange = 30.0F;
        constexpr KCDC::Features::Audio::AudioRange::TriggerRange kThrowCueRanges[] = {
            {"dice_shake_player", kThrowCueRange},
            {"dice_wood1_player", kThrowCueRange},
            {"dice_bowl_put_player", kThrowCueRange},
        };

        bool HasLink(const SDK::IEntityLink *links, const char *name) {
            for (const SDK::IEntityLink *link = links; link != nullptr; link = link->next) {
                if (link->name.m_pData != nullptr && std::strcmp(link->name.m_pData, name) == 0) {
                    return true;
                }
            }
            return false;
        }

        /**
         * The dice table nearest `anchor`: the entity whose links name both
         * seats and the cup, exactly what `InitializeDiceTableScene` resolves.
         * Both clients load the same level, so both find the same one.
         */
        SDK::EntityId FindTable(const SDK::Vec3 &anchor) {
            auto *entitySystem = SDK::IEntitySystem::GetInstance();
            if (entitySystem == nullptr) {
                return 0;
            }
            SDK::SEntityProximityQuery query;
            query.box.min = SDK::Vec3 {anchor.x - kTableSearchRadius, anchor.y - kTableSearchRadius, anchor.z - kTableSearchRadius};
            query.box.max = SDK::Vec3 {anchor.x + kTableSearchRadius, anchor.y + kTableSearchRadius, anchor.z + kTableSearchRadius};
            entitySystem->QueryProximity(query);

            SDK::EntityId best = 0;
            float bestDistance = std::numeric_limits<float>::max();
            for (int index = 0; index < query.Count(); ++index) {
                SDK::IEntity *entity = query.At(index);
                if (entity == nullptr || entity->IsGarbage()) {
                    continue;
                }
                const SDK::IEntityLink *links = entity->GetEntityLinks();
                if (!HasLink(links, "seat1") || !HasLink(links, "seat2") || !HasLink(links, "dice_cup")) {
                    continue;
                }
                const SDK::Vec3 position = entity->GetWorldPos();
                const float dx           = position.x - anchor.x;
                const float dy           = position.y - anchor.y;
                const float dz           = position.z - anchor.z;
                const float distance     = dx * dx + dy * dy + dz * dz;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best         = entity->GetId();
                }
            }
            return best;
        }

        std::string Describe(Shared::Game::Dice::EndReason reason, bool won, bool lost, std::uint32_t mine, std::uint32_t theirs) {
            const std::string score = " (" + std::to_string(mine) + " : " + std::to_string(theirs) + ")";
            switch (reason) {
            case Shared::Game::Dice::EndReason::Won: return std::string(won ? "You won the dice game" : "You lost the dice game") + score + ".";
            case Shared::Game::Dice::EndReason::GaveUp: return std::string(won ? "Your opponent gave up the dice game" : "You gave up the dice game") + score + ".";
            case Shared::Game::Dice::EndReason::Left: return won ? "Your opponent left; the dice game is yours." : "The dice game is over.";
            case Shared::Game::Dice::EndReason::TimedOut: return std::string(lost ? "You took too long; the dice game is lost" : "Your opponent took too long; the dice game is yours") + score + ".";
            case Shared::Game::Dice::EndReason::Failed: return "The dice game could not go on.";
            case Shared::Game::Dice::EndReason::Stopped: return "The dice game was called off.";
            }
            return "The dice game is over.";
        }
    } // namespace

    DiceModule::DiceModule() {
        for (std::size_t i = 0; i < Shared::Game::g_PropModels.size(); ++i) {
            Shared::Game::Dice::SceneProp prop;
            prop.model = static_cast<std::uint32_t>(i + 1);
            if (prop.IsSane(0) || prop.IsSane(12)) {
                _sceneModels.push_back(i);
            }
        }
    }

    DiceModule &DiceModule::Get() {
        static DiceModule instance;
        return instance;
    }

    bool DiceModule::ExtendGameAudio(SDK::XmlNodeRef &document) {
        return KCDC::Features::Audio::AudioRange::Get().Limit(document, kThrowCueRanges);
    }

    void DiceModule::Update() {
        if (!_rpcsInstalled) {
            InstallRPCs();
        }

        // The level took the table and both bodies with it: nothing native is
        // touched, and the server hears that this side cannot go on.
        const bool contextLost = _nativeContextLost.exchange(false);
        if (contextLost) {
            ClearScenes(false);
        }
        if (contextLost && _phase != Phase::Idle) {
            if (!_serverEnded) {
                Leave(Shared::Game::Dice::EndReason::Failed);
            }
            Clear();
            return;
        }

        UpdateScenes();

        switch (_phase) {
        case Phase::Idle: break;

        case Phase::Starting:
            if (_serverEnded) {
                Clear();
                break;
            }
            if (!TryStart() && _phase == Phase::Starting && MafiaNet::GetTime() >= _startDeadline) {
                Framework::Logging::GetLogger(kLogger)->warn("Match {}: no dice table or opponent body in time", _matchId);
                UI::Hud::ShowNotification("There is no dice table here to play at.");
                Leave(Shared::Game::Dice::EndReason::Failed);
                Clear();
            }
            break;

        case Phase::Seated:
            if (_serverEnded && !_nativeEnded && (_endReason != Shared::Game::Dice::EndReason::Won || MafiaNet::GetTime() >= _endDeadline)) {
                ForceNativeEnd();
                break;
            }
            ApplyCommits();
            break;
        }
    }

    Shared::Game::Dice::Scene DiceModule::CaptureScene() const {
        Shared::Game::Dice::Scene result;
        auto *model    = Model();
        auto *entities = SDK::IEntitySystem::GetInstance();
        if (_phase != Phase::Seated || _localSeat != 0 || model == nullptr || entities == nullptr) {
            return result;
        }
        auto *table = entities->GetEntity(_dice->GetTableEntityId());
        if (table == nullptr) {
            return result;
        }
        const auto tablePosition = table->GetWorldPos();
        result.match       = _matchId;
        const auto capture = [&](SDK::EntityId id, std::size_t slot) {
            auto *entity = entities->GetEntity(id);
            if (entity == nullptr || entity->IsHidden() || entity->IsGarbage()) {
                return;
            }
            const auto position = entity->GetWorldPos();
            // Native unused dice can remain unhidden at the world origin.
            // Omit them using the same five-metre boundary as Scene::Near.
            const float dx = position.x - tablePosition.x, dy = position.y - tablePosition.y, dz = position.z - tablePosition.z;
            if (dx * dx + dy * dy + dz * dz > 25.0F) {
                return;
            }
            auto *mesh = entity->GetStatObj(static_cast<int>(0x80000000u));
            if (mesh == nullptr || mesh->GetFilePath() == nullptr) {
                return;
            }
            auto &prop = result.props[slot];
            for (const auto index : _sceneModels) {
                const auto *asset = Shared::Game::PropModelAt(index);
                // Item geometry preserves "Objects/" capitalization; the
                // generated catalog uses lowercase paths.
                if (asset != nullptr && _stricmp(asset->path.data(), mesh->GetFilePath()) == 0) {
                    prop.model = static_cast<std::uint32_t>(index + 1);
                    break;
                }
            }
            if (prop.model == 0) {
                return;
            }
            const auto rotation = entity->GetWorldRotation();
            const auto scale    = entity->GetScale();
            prop.position       = {position.x, position.y, position.z};
            prop.rotation       = {rotation.x, rotation.y, rotation.z, rotation.w};
            prop.scale          = {scale.x, scale.y, scale.z};
            if (!prop.IsSane(slot)) {
                prop = {};
            }
        };
        for (int seat = 0; seat < 2; ++seat) {
            auto *dice = _dice->GetDieSet(seat);
            if (dice == nullptr) {
                continue;
            }
            for (std::size_t i = 0; i < std::min<std::size_t>(dice->GetDieCount(), 6); ++i) {
                if (const auto *die = dice->GetDie(i)) {
                    capture(die->GetEntityId(), seat * 6 + i);
                }
            }
        }
        capture(_dice->GetCupEntityId(), 12);
        return result;
    }

    void DiceModule::ApplyScoreNames() const {
        if (_phase != Phase::Seated || (_names[0].empty() && _names[1].empty())) {
            return;
        }
        auto *ui      = SDK::IFlashUI::GetInstance();
        auto *element = ui != nullptr ? static_cast<SDK::CFlashUIElement *>(ui->GetUIElementByInstanceStr("hud")) : nullptr;
        auto *movie   = element != nullptr ? element->GetLoadedFlashPlayer() : nullptr;
        if (movie == nullptr) {
            return;
        }
        // hud.gfx fc_showDiceScore creates score under bl.diceScoreContainer.
        // It writes playerName into playerLabel and @ui_dice_opponent into
        // NPCLabel. Both are plain text fields in score.inside.
        if (!_names[_localSeat].empty()) {
            movie->SetVariable("_root.bl.diceScoreContainer.score.inside.playerLabel.text", SDK::SFlashVarValue::String(_names[_localSeat].c_str()));
        }
        if (!_names[_localSeat ^ 1].empty()) {
            movie->SetVariable("_root.bl.diceScoreContainer.score.inside.NPCLabel.text", SDK::SFlashVarValue::String(_names[_localSeat ^ 1].c_str()));
        }
    }

    void DiceModule::ClearScene(ObserverScene &scene, bool removeEntities) {
        if (removeEntities && KCDC::Core::gApplication != nullptr) {
            for (auto id : scene.entities) {
                if (id != 0) {
                    KCDC::Core::gApplication->GetPropSpawner().DespawnNow(id);
                }
            }
        }
        scene = {};
    }

    void DiceModule::ClearScenes(bool removeEntities) {
        for (auto &[id, scene] : _scenes) {
            ClearScene(scene, removeEntities);
        }
        _scenes.clear();
    }

    void DiceModule::UpdateScenes() {
        auto *replication = Framework::CoreModules::GetReplication();
        auto *entities    = SDK::IEntitySystem::GetInstance();
        if (replication == nullptr || entities == nullptr || KCDC::Core::gApplication == nullptr) {
            return;
        }
        for (auto &[id, scene] : _scenes) {
            scene.touched = false;
        }
        const auto now  = MafiaNet::GetTime();
        int spawnBudget = 2;
        replication->ForEach<KCDC::Features::Player::Human>([&](KCDC::Features::Player::Human *human) {
            if (human->IsOwner()) {
                if (Station().activity == Shared::Game::StationActivity::Dice && _localSeat == 0) {
                    KCDC::Core::gApplication->GetLocalEvents().ObserveDiceScene(CaptureScene(), human->stateEpoch, human->soul.incarnation);
                }
                return;
            }
            const auto &source = human->diceScene;
            if (source.match == 0 || source.match == _matchId || source.match != human->station.match || human->station.activity != Shared::Game::StationActivity::Dice || human->station.slot != 0 || !source.IsSane()) {
                return;
            }
            if (entities->GetEntityByGuid(human->station.guid) == nullptr) {
                return;
            }
            auto &scene   = _scenes[human->GetNetworkID()];
            scene.touched = true;
            if (scene.target.match != source.match) {
                ClearScene(scene, true);
                scene.touched = true;
            }
            if (scene.target.revision != source.revision) {
                scene.previous  = scene.target;
                scene.target    = source;
                scene.sampledAt = now;
                // Continue from the displayed pose when packets arrive unevenly.
                for (std::size_t i = 0; i < scene.entities.size(); ++i) {
                    if (auto *entity = entities->GetEntity(scene.entities[i])) {
                        const auto p                     = entity->GetWorldPos();
                        const auto q                     = entity->GetWorldRotation();
                        scene.previous.props[i].position = {p.x, p.y, p.z};
                        scene.previous.props[i].rotation = {q.x, q.y, q.z, q.w};
                    }
                }
            }
            for (std::size_t order = 0; order < scene.entities.size(); ++order) {
                // Spawn the cup first, and keep its smoothing short so it
                // follows the throwing hand without another full sample of lag.
                const std::size_t i = order == 0 ? 12 : order - 1;
                const float alpha = std::min(1.0F, static_cast<float>(now - scene.sampledAt) / (i == 12 ? 25.0F : 50.0F));
                const auto &prop = scene.target.props[i];
                auto *entity     = entities->GetEntity(scene.entities[i]);
                if (scene.models[i] != prop.model || (entity != nullptr && entity->IsGarbage())) {
                    if (entity != nullptr) {
                        KCDC::Core::gApplication->GetPropSpawner().DespawnNow(scene.entities[i]);
                    }
                    scene.entities[i] = 0;
                    scene.models[i]   = 0;
                    entity            = nullptr;
                }
                if (prop.model == 0) {
                    continue;
                }
                SDK::Vec3 position {prop.position[0], prop.position[1], prop.position[2]};
                SDK::Quat rotation {prop.rotation[0], prop.rotation[1], prop.rotation[2], prop.rotation[3]};
                const auto &previous = scene.previous.props[i];
                if (entity != nullptr && previous.model == prop.model) {
                    position = SDK::Vec3 {previous.position[0] + (position.x - previous.position[0]) * alpha, previous.position[1] + (position.y - previous.position[1]) * alpha, previous.position[2] + (position.z - previous.position[2]) * alpha};
                    const glm::quat a(previous.rotation[3], previous.rotation[0], previous.rotation[1], previous.rotation[2]);
                    const glm::quat b(rotation.w, rotation.x, rotation.y, rotation.z);
                    const auto q = glm::normalize(glm::slerp(a, b, alpha));
                    rotation     = SDK::Quat {q.x, q.y, q.z, q.w};
                }
                if (entity == nullptr && spawnBudget > 0) {
                    --spawnBudget;
                    KCDC::Common::Spawn::PropSpawnRequest request;
                    request.model     = Shared::Game::PropModelAt(prop.model - 1)->path;
                    request.physics   = KCDC::Common::Spawn::PropPhysics::None;
                    request.world     = KCDC::Common::Spawn::WorldPlacement {position, rotation};
                    scene.entities[i] = KCDC::Core::gApplication->GetPropSpawner().SpawnNow(request).prop.entityId;
                    scene.models[i]   = prop.model;
                    entity            = entities->GetEntity(scene.entities[i]);
                }
                if (entity != nullptr) {
                    entity->SetPosRotScale(position, rotation, SDK::Vec3 {prop.scale[0], prop.scale[1], prop.scale[2]}, SDK::ENTITY_XFORM_IGNORE_PHYSICS);
                }
            }
        });
        for (auto it = _scenes.begin(); it != _scenes.end();) {
            if (it->second.touched) {
                ++it;
                continue;
            }
            ClearScene(it->second, true);
            it = _scenes.erase(it);
        }
    }

    void DiceModule::Reset() {
        ClearScenes(!_nativeContextLost.load());
        Clear();
        _nativeContextLost = false;
    }

    void DiceModule::NativeContextChanged() {
        _nativeContextLost = true;
    }

    Shared::Game::StationState DiceModule::Station() const {
        Shared::Game::StationState result;
        if (_phase != Phase::Seated || _dice == nullptr || _dice->GetPhase() < SDK::wh::playermodule::E_DicePhase::Seated || _dice->GetPhase() >= SDK::wh::playermodule::E_DicePhase::Exiting) {
            return result;
        }
        auto *entities = SDK::IEntitySystem::GetInstance();
        auto *table    = entities != nullptr ? entities->GetEntity(_dice->GetTableEntityId()) : nullptr;
        if (table == nullptr || table->GetGuid() == 0) {
            return result;
        }
        result.activity = Shared::Game::StationActivity::Dice;
        result.guid     = table->GetGuid();
        result.match    = _matchId;
        result.slot     = _localSeat;
        result.action   = KCDC::Features::Player::PlayerActionService::Get().DiceThrow();
        return result;
    }

    bool DiceModule::HoldsBody(SDK::EntityId entityId) const {
        return _phase == Phase::Seated && entityId != 0 && entityId == _opponentEntityId;
    }

    void DiceModule::ForgetBody(SDK::EntityId entityId) {
        if (!HoldsBody(entityId)) {
            return;
        }
        // The table holds the body as its opponent actor; it is closed while
        // that actor still exists. If the other player left, the server has
        // already ended the match and this is ignored.
        Framework::Logging::GetLogger(kLogger)->info("Match {}: the opponent's body is going; closing the table", _matchId);
        Leave(Shared::Game::Dice::EndReason::Failed);
        hook::this_call<void>(SDK::gPatterns.C_Dice__Finish, _dice);
        Clear();
    }

    void DiceModule::OnTableSceneInitialized(SDK::wh::playermodule::C_Dice *dice) {
        if (_phase != Phase::Seated || dice != _dice || _localSeat == 0) {
            return;
        }
        // Seat 0 of the server sits on `seat1` on every machine. This player is
        // the server's second seat but the game's seat 0, so the table's sides
        // are exchanged and both bodies snapped again.
        dice->SwapSides();
        dice->ResetDiceTableTransforms();
    }

    void DiceModule::OnNavigationCameraUpdated(SDK::wh::playermodule::C_DieSet *dice, const SDK::Vec3 &position, SDK::Quat &rotation) {
        SDK::wh::playermodule::C_DiceGameModel *model = Model();
        if (_phase != Phase::Seated || model == nullptr || _cameraInitialized) {
            return;
        }
        if (dice->GetPlayer() == model->GetPlayer(0)) {
            _cameraInitialized = true;
        }
        else if (dice->GetPlayer() == model->GetPlayer(1)) {
            // The native updates the position for both seats, but only writes
            // a rotation on the local turn. The opponent may throw first here.
            const SDK::Vec3 &center = dice->GetCenter();
            const SDK::Vec3 direction {center.x - position.x, center.y - position.y, center.z - position.z};
            rotation           = SDK::QuatFromAngles(SDK::Ang3 {std::atan2(direction.z, std::hypot(direction.x, direction.y)), 0.0F, std::atan2(-direction.x, direction.y)});
            _cameraInitialized = true;
        }
    }

    std::vector<std::int32_t> *DiceModule::TakeThrow(SDK::wh::playermodule::C_DiceGamePlayerModel *player, bool selectedOnly) {
        SDK::wh::playermodule::C_DiceGameModel *model = Model();
        if (_phase != Phase::Seated || selectedOnly || model == nullptr) {
            return nullptr;
        }
        const std::int32_t seat = player == model->GetPlayer(0) ? 0 : (player == model->GetPlayer(1) ? 1 : -1);
        if (seat < 0) {
            return nullptr;
        }
        std::deque<std::vector<std::int32_t>> &queue = _deals[static_cast<std::size_t>(seat)];
        if (queue.empty()) {
            Framework::Logging::GetLogger(kLogger)->error("Match {}: seat {} throws with no deal from the server; the table is out of step", _matchId, seat);
            Leave(Shared::Game::Dice::EndReason::Failed);
            return nullptr;
        }
        _lent = std::move(queue.front());
        queue.pop_front();
        return &_lent;
    }

    SDK::wh::playermodule::C_DiceGamePlayerModel *DiceModule::RedirectThrow(SDK::wh::playermodule::I_DiceGameModelDelegate *delegate, SDK::wh::playermodule::C_DiceGamePlayerModel *player) {
        SDK::wh::playermodule::C_DiceGameModel *model = Model();
        if (_phase != Phase::Seated || model == nullptr || delegate != model->GetDelegate() || _firstThrowRedirected) {
            return player;
        }
        // The opening throw is the server's to give: the game always hands it
        // to whichever seat its own draw picked.
        _firstThrowRedirected = true;
        return model->GetPlayer(EngineSeat(_firstSeat));
    }

    std::optional<bool> DiceModule::AnswerForAI(SDK::wh::playermodule::C_DiceGameModel *model, SDK::wh::playermodule::C_DiceGamePlayerModel *player, SDK::wh::playermodule::I_DiceGameModelState *state, bool *handled) const {
        if (_phase != Phase::Seated || model == nullptr || model != Model() || state == nullptr) {
            return std::nullopt;
        }
        // PlayerTurn falls through to an automatic pass when no AI claims it,
        // so the seat's turn is claimed and nothing is done: the other
        // player's commit is what moves it. Every other state runs its own
        // course, as it would for a human seat.
        *handled = state->IsPlayerTurn() && state->GetPlayer() == player;
        return *handled;
    }

    bool DiceModule::AllowsHold(SDK::wh::playermodule::C_Dice *dice) const {
        return dice != _dice || _phase != Phase::Seated || (!_movePending && _commits.empty());
    }

    bool DiceModule::AllowsMove(SDK::wh::playermodule::C_Dice *dice, Shared::Game::Dice::MoveKind kind) {
        if (_applyingLocal || dice != _dice || _phase != Phase::Seated) {
            return true;
        }
        if (_serverEnded || _movePending || !_commits.empty()) {
            return false;
        }
        SDK::wh::playermodule::C_DiceGameModel *model = Model();
        if (model == nullptr) {
            return true;
        }
        // Only ever the local seat's own turn: a key reaching the handler at
        // any other moment would act on the opponent's seat.
        SDK::wh::playermodule::I_DiceGameModelState *state = model->GetState();
        if (state == nullptr || !state->IsPlayerTurn() || state->GetPlayer() != model->GetPlayer(0)) {
            return false;
        }

        std::vector<std::uint8_t> faces;
        const std::uint8_t held                       = LocalHeldMask(faces);
        const Shared::Game::Dice::Evaluation selection = Shared::Game::Dice::Evaluate(faces, false, false);
        if (kind == Shared::Game::Dice::MoveKind::Continue && !selection.scores) {
            // The game refuses this itself, with its own feedback.
            return true;
        }
        if (kind == Shared::Game::Dice::MoveKind::Pass && held != 0 && !selection.scores) {
            UI::Hud::ShowNotification("To pass, keep only dice that score, or none.");
            return false;
        }

        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return false;
        }
        Shared::RPC::DiceMove move;
        move.matchId   = _matchId;
        move.moveIndex = _commitsReceived;
        move.kind      = static_cast<std::uint8_t>(kind);
        move.heldMask  = held;
        net->BroadcastRPC(move, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        _movePending = true;
        _pendingMove = _commitsReceived;
        return false;
    }

    void DiceModule::OnNativeEnding(SDK::wh::playermodule::I_DiceGameModelDelegate *delegate, SDK::wh::playermodule::C_DiceGamePlayerModel *player, std::uint32_t result) {
        SDK::wh::playermodule::C_DiceGameModel *model = Model();
        if (_phase != Phase::Seated || model == nullptr || delegate != model->GetDelegate()) {
            return;
        }
        _nativeEnded = true;
        // Holding Esc in play gives up through the game's own state; the
        // server is told, and decides what it means.
        if (!_serverEnded && result == kEndingGaveUp && player == model->GetPlayer(0)) {
            Leave(Shared::Game::Dice::EndReason::GaveUp);
        }
    }

    void DiceModule::OnNativeFinished(SDK::wh::playermodule::C_Dice *dice) {
        if (_phase != Phase::Seated || dice != _dice) {
            return;
        }
        // Closed before the server ended it: the player walked away from the
        // table, which is giving up.
        if (!_serverEnded) {
            Leave(Shared::Game::Dice::EndReason::GaveUp);
        }
        Framework::Logging::GetLogger(kLogger)->info("Match {}: the table closed", _matchId);
        Clear();
    }

    bool DiceModule::RefusesBadge() const {
        return _phase != Phase::Idle;
    }

    bool DiceModule::SilencesBark(const SDK::wh::entitymodule::C_Actor *actor) const {
        SDK::wh::playermodule::C_DiceGameModel *model = Model();
        return _phase == Phase::Seated && !_opponentIsNpc && model != nullptr && actor != nullptr && model->GetPlayer(1) != nullptr && actor == model->GetPlayer(1)->GetActor();
    }

    void DiceModule::InstallRPCs() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        _rpcsInstalled = true;

        net->RegisterRPC<Shared::RPC::DiceNames>([this](const Shared::RPC::DiceNames &names, MafiaNet::Packet *) {
            if (names.IsSane() && names.matchId == _matchId) {
                _names = names.names;
            }
        });
        net->RegisterRPC<Shared::RPC::DiceMatchStart>([](const Shared::RPC::DiceMatchStart &start, MafiaNet::Packet *) {
            if (start.IsSane()) {
                DiceModule::Get().OnMatchStart(start);
            }
        });
        net->RegisterRPC<Shared::RPC::DiceCommit>([](const Shared::RPC::DiceCommit &commit, MafiaNet::Packet *) {
            if (commit.IsSane()) {
                DiceModule::Get().OnCommit(commit);
            }
        });
        net->RegisterRPC<Shared::RPC::DiceRefuse>([](const Shared::RPC::DiceRefuse &refuse, MafiaNet::Packet *) {
            if (refuse.IsSane()) {
                DiceModule::Get().OnRefuse(refuse);
            }
        });
        net->RegisterRPC<Shared::RPC::DiceMatchEnd>([](const Shared::RPC::DiceMatchEnd &end, MafiaNet::Packet *) {
            if (end.IsSane()) {
                DiceModule::Get().OnMatchEnd(end);
            }
        });
    }

    void DiceModule::OnMatchStart(const Shared::RPC::DiceMatchStart &start) {
        // The server starts a match only for players who are not in one; a
        // table still open here is a leftover, closed natively first.
        if (_phase == Phase::Seated && _dice != nullptr) {
            hook::this_call<void>(SDK::gPatterns.C_Dice__Finish, _dice);
        }
        Clear();

        _names             = start.names;
        _matchId           = start.matchId;
        _localSeat         = start.localSeat;
        _firstSeat         = start.firstSeat;
        _opponentNetworkId = start.opponentNetworkId;
        _targetScore       = start.targetScore;
        _anchor            = SDK::Vec3 {start.anchorX, start.anchorY, start.anchorZ};
        _startDeadline     = MafiaNet::GetTime() + kStartTimeoutMs;
        _phase             = Phase::Starting;
        QueueDeals(start.deals);

        Framework::Logging::GetLogger(kLogger)->info("Match {}: seat {} of 2 against networkId {}, to {}, seat {} throws first", _matchId, _localSeat, _opponentNetworkId, _targetScore, _firstSeat);
        TryStart();
    }

    bool DiceModule::TryStart() {
        SDK::wh::entitymodule::C_Human *local = KCDC::Common::LocalHumanActor();
        if (local == nullptr) {
            return false;
        }
        // The opponent is another player's copy or one of the server's NPCs;
        // a network id names one entity, so the two cannot be confused.
        SDK::EntityId opponentEntityId = 0;
        std::string opponentName;
        bool opponentIsNpc = false;
        if (auto *opponent = KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(_opponentNetworkId)) {
            if (opponent->ResolveActor() != nullptr) {
                opponentEntityId = opponent->GetEntityId();
                opponentName     = opponent->nickname;
            }
        }
        else if (auto *npc = KCDC::Common::FindReplicaById<KCDC::Features::Npc::Npc>(_opponentNetworkId)) {
            // Seated only once its body is dressed and animated: the table
            // plays interactive actions on it from its first frame.
            if (npc->alive && npc->IsBodyReady()) {
                opponentEntityId = npc->GetEntityId();
                opponentName     = npc->name;
                opponentIsNpc    = true;
            }
        }
        if (opponentEntityId == 0) {
            return false;
        }
        const SDK::EntityId table = FindTable(_anchor);
        if (table == 0) {
            return false;
        }
        SDK::wh::playermodule::C_Dice *dice = SDK::wh::playermodule::C_Dice::Of(local, true);
        if (dice == nullptr || dice->GetPhase() != SDK::wh::playermodule::E_DicePhase::Idle) {
            return false;
        }

        // Ours before the native runs, so the detours it reaches know it.
        _dice             = dice;
        _opponentEntityId = opponentEntityId;
        _opponentIsNpc    = opponentIsNpc;
        _phase            = Phase::Seated;
        if (!dice->StartGameWithTargetScore(table, _opponentEntityId, _targetScore, kNoBadge, true)) {
            Framework::Logging::GetLogger(kLogger)->error("Match {}: the game would not start a dice game against entity {:#x} on table {:#x}", _matchId, _opponentEntityId, table);
            UI::Hud::ShowNotification("The dice game could not start here.");
            Leave(Shared::Game::Dice::EndReason::Failed);
            Clear();
            return false;
        }
        Framework::Logging::GetLogger(kLogger)->info("Match {}: seated at table {:#x} against {} '{}' (entity {:#x})", _matchId, table, _opponentIsNpc ? "NPC" : "player", opponentName, _opponentEntityId);
        return true;
    }

    void DiceModule::OnCommit(const Shared::RPC::DiceCommit &commit) {
        if (_phase == Phase::Idle || commit.matchId != _matchId) {
            return;
        }
        if (commit.moveIndex != _commitsReceived) {
            Framework::Logging::GetLogger(kLogger)->error("Match {}: commit {} arrived where {} was due", _matchId, commit.moveIndex, _commitsReceived);
            Leave(Shared::Game::Dice::EndReason::Failed);
            return;
        }
        ++_commitsReceived;
        QueueDeals(commit.deals);
        _commits.push_back(commit);
    }

    void DiceModule::OnRefuse(const Shared::RPC::DiceRefuse &refuse) {
        if (refuse.matchId != _matchId || !_movePending || refuse.moveIndex != _pendingMove) {
            return;
        }
        _movePending = false;
        UI::Hud::ShowNotification("That move does not count here.");
    }

    void DiceModule::OnMatchEnd(const Shared::RPC::DiceMatchEnd &end) {
        if (_phase == Phase::Idle || end.matchId != _matchId) {
            return;
        }
        _serverEnded = true;
        _endReason   = static_cast<Shared::Game::Dice::EndReason>(end.reason);
        _winnerSeat  = end.winnerSeat;
        _endDeadline = MafiaNet::GetTime() + kNativeEndGraceMs;

        const bool won              = end.winnerSeat == _localSeat;
        const bool lost             = end.winnerSeat != Shared::Game::Dice::kNoSeat && !won;
        const std::uint32_t mine    = _localSeat == 0 ? end.total0 : end.total1;
        const std::uint32_t theirs  = _localSeat == 0 ? end.total1 : end.total0;
        UI::Hud::ShowNotification(Describe(_endReason, won, lost, mine, theirs));
        Framework::Logging::GetLogger(kLogger)->info("Match {} over: reason {}, seat {} won, {} : {}", _matchId, end.reason, end.winnerSeat, end.total0, end.total1);
    }

    void DiceModule::ApplyCommits() {
        SDK::wh::playermodule::C_DiceGameModel *model = Model();
        if (model == nullptr || _commits.empty() || _dice->GetPhase() != SDK::wh::playermodule::E_DicePhase::Playing) {
            return;
        }
        const Shared::RPC::DiceCommit &commit                 = _commits.front();
        const std::int32_t seat                               = EngineSeat(commit.seat);
        SDK::wh::playermodule::C_DiceGamePlayerModel *player  = model->GetPlayer(seat);
        SDK::wh::playermodule::I_DiceGameModelState *state    = model->GetState();
        if (player == nullptr || state == nullptr || !state->IsPlayerTurn() || state->GetPlayer() != player) {
            return;
        }
        const auto kind = static_cast<Shared::Game::Dice::MoveKind>(commit.kind);

        if (seat == 0) {
            // The press that was held back, played now through the key
            // handler itself, over exactly the dice the server scored.
            std::vector<std::uint8_t> faces;
            if (LocalHeldMask(faces) != commit.heldMask) {
                Framework::Logging::GetLogger(kLogger)->error("Match {}: the held dice changed under commit {}", _matchId, commit.moveIndex);
                Leave(Shared::Game::Dice::EndReason::Failed);
                _commits.clear();
                return;
            }
            _applyingLocal = true;
            hook::this_call<void>(kind == Shared::Game::Dice::MoveKind::Continue ? SDK::gPatterns.C_Dice__OnScoreRequested : SDK::gPatterns.C_Dice__OnPassRequested, _dice);
            _applyingLocal = false;
            _movePending   = false;
            _commits.pop_front();
            return;
        }

        // The opponent's move, one hold per turn of the state machine: each
        // hold queues Holding, which hands back to PlayerTurn on its own.
        if (!_replayStarted) {
            _replayHolds   = commit.heldMask;
            _replayStarted = true;
        }
        while (_replayHolds != 0) {
            std::uint32_t index = 0;
            while ((_replayHolds >> index & 1) == 0) {
                ++index;
            }
            _replayHolds = static_cast<std::uint8_t>(_replayHolds & ~(1u << index));
            for (std::size_t slot = 0; slot < player->GetDieCount(); ++slot) {
                const SDK::wh::playermodule::C_DieModel *die = player->GetDie(slot);
                if (die != nullptr && die->GetIndex() == index && die->GetState() == SDK::wh::playermodule::E_DieState::Free) {
                    model->RequestSetDieHeld(index, true);
                    return;
                }
            }
        }

        if (kind == Shared::Game::Dice::MoveKind::Continue) {
            model->RequestScoreSelectedDice();
        }
        else {
            state->RequestPass(model->GetDelegate(), false);
        }
        _replayStarted = false;
        _commits.pop_front();
    }

    void DiceModule::ForceNativeEnd() {
        _nativeEnded = true;
        if (_dice == nullptr) {
            Clear();
            return;
        }
        const SDK::wh::playermodule::E_DicePhase phase = _dice->GetPhase();
        SDK::wh::playermodule::C_DiceGameModel *model  = Model();
        if (phase == SDK::wh::playermodule::E_DicePhase::Playing && model != nullptr && model->GetState() != nullptr) {
            // Ended through the game's own states, so the table shows the
            // result and closes the way it always does.
            SDK::wh::playermodule::I_DiceGameModelState *state = model->GetState();
            if (_winnerSeat != Shared::Game::Dice::kNoSeat) {
                state->EndWithWin(model->GetDelegate(), model->GetPlayer(EngineSeat(_winnerSeat)));
            }
            else {
                state->RequestGiveUp(model->GetDelegate(), model->GetPlayer(0));
            }
            return;
        }
        if (phase != SDK::wh::playermodule::E_DicePhase::Exiting && phase != SDK::wh::playermodule::E_DicePhase::Finishing) {
            hook::this_call<void>(SDK::gPatterns.C_Dice__Finish, _dice);
            Clear();
        }
    }

    void DiceModule::QueueDeals(const std::vector<Shared::RPC::DiceDeal> &deals) {
        for (const Shared::RPC::DiceDeal &deal : deals) {
            _deals[static_cast<std::size_t>(EngineSeat(deal.seat))].emplace_back(deal.faces.begin(), deal.faces.end());
        }
    }

    void DiceModule::Leave(Shared::Game::Dice::EndReason reason) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (_leftSent || _matchId == 0 || net == nullptr) {
            return;
        }
        _leftSent = true;
        Shared::RPC::DiceLeave leave;
        leave.matchId = _matchId;
        leave.reason  = static_cast<std::uint8_t>(reason);
        net->BroadcastRPC(leave, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void DiceModule::Clear() {
        _names                = {};
        _phase                = Phase::Idle;
        _matchId              = 0;
        _localSeat            = 0;
        _firstSeat            = 0;
        _opponentNetworkId    = 0;
        _opponentEntityId     = 0;
        _opponentIsNpc        = false;
        _targetScore          = 0;
        _anchor               = {};
        _dice                 = nullptr;
        _startDeadline        = 0;
        _deals[0].clear();
        _deals[1].clear();
        _lent.clear();
        _commits.clear();
        _commitsReceived      = 0;
        _replayHolds          = 0;
        _replayStarted        = false;
        _movePending          = false;
        _pendingMove          = 0;
        _applyingLocal        = false;
        _firstThrowRedirected = false;
        _cameraInitialized = false;
        _serverEnded          = false;
        _endReason            = Shared::Game::Dice::EndReason::Stopped;
        _winnerSeat           = Shared::Game::Dice::kNoSeat;
        _endDeadline          = 0;
        _nativeEnded          = false;
        _leftSent             = false;
    }

    SDK::wh::playermodule::C_DiceGameModel *DiceModule::Model() const {
        return _dice != nullptr ? _dice->GetGameModel() : nullptr;
    }

    std::uint8_t DiceModule::LocalHeldMask(std::vector<std::uint8_t> &faces) const {
        SDK::wh::playermodule::C_DiceGameModel *model = Model();
        SDK::wh::playermodule::C_DiceGamePlayerModel *player = model != nullptr ? model->GetPlayer(0) : nullptr;
        std::uint8_t mask                              = 0;
        if (player == nullptr) {
            return mask;
        }
        for (std::size_t slot = 0; slot < player->GetDieCount(); ++slot) {
            const SDK::wh::playermodule::C_DieModel *die = player->GetDie(slot);
            if (die != nullptr && die->GetState() == SDK::wh::playermodule::E_DieState::Held && die->GetIndex() < Shared::Game::Dice::kDiceCount) {
                mask = static_cast<std::uint8_t>(mask | 1u << die->GetIndex());
                faces.push_back(static_cast<std::uint8_t>(die->GetFaceValue()));
            }
        }
        return mask;
    }
} // namespace KCDC::Features::Dice
