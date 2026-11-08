#include "siege_module.h"

#include "siege.h"

#include "core/application.h"
#include "features/audio/audio_range.h"
#include "features/audio/audio_service.h"
#include "features/camera/camera_view.h"
#include "features/player/player.h"
#include "features/player/player_teleport.h"
#include "features/prop/prop_ghost.h"
#include "features/ui/ui_action_hints.h"
#include "features/vfx/vfx_service.h"
#include "common/module_support.h"

#include "shared/features/siege/siege_ballistics.h"
#include "shared/features/vfx/vfx_effect_ref.h"
#include "shared/features/vfx/vfx_tuning.h"
#include "shared/game/vfx_catalog.h"

#include "sdk/cryengine/action/c_view.h"
#include "sdk/cryengine/entitysystem/c_render_proxy.h"
#include "sdk/cryengine/input/i_input.h"
#include "sdk/cryengine/physics/c_physical_world.h"
#include "sdk/cryengine/system/cry_name.h"
#include "sdk/cryengine/system/i_system.h"
#include "sdk/wh/entitymodule/c_body_part_data.h"
#include "sdk/wh/entitymodule/c_player.h"
#include "sdk/wh/rpgmodule/c_combat_soul.h"
#include "sdk/wh/rpgmodule/c_soul.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>

namespace KCDC::Features::Siege {
    namespace {
        constexpr const char *kLogger = "Siege";

        /** How long an impact effect is kept before its emitter is freed. */
        constexpr std::uint32_t kImpactEffectMs  = 8000;
        constexpr std::uint32_t kReleaseEffectMs = 5000;

        /** An impact already shown within this of the server's point is the same one. */
        constexpr float kSameImpactDistance = 15.0F;

        /** How many shown impacts and launched shots are remembered. */
        constexpr std::size_t kShownMemory    = 32;
        constexpr std::size_t kLaunchedMemory = 64;

        /** How fast a stone tumbles in flight, in radians a second; for the look of it only. */
        constexpr float kTumbleRate = 4.0F;

        /**
         * How far from the engine itself a player is offered its place, in
         * metres along the ground from its mesh's bounds: standing at a
         * trebuchet's frame or beside a cannon's carriage. The server allows a
         * press from 16 m of the base, which covers both.
         */
        constexpr float kPromptReach = 2.0F;

        /** How near a winch a player must stand to be offered it, in metres; the server lets go of a crewman past 4. */
        constexpr float kWinchReach = 2.0F;

        /** How often the prompt is chosen again, in milliseconds; the carry prompt's own pace. */
        constexpr std::uint64_t kPromptIntervalMs = 200;

        /** The speed of sound, in metres a second, and the shortest delay worth waiting out, in milliseconds. */
        constexpr float kSpeedOfSound          = 343.0F;
        constexpr std::uint64_t kMinSoundDelay = 30;

        /** A ball passing the listener this near, in metres, is heard going by -- once it has left the muzzle behind. */
        constexpr float kWhizDistance  = 15.0F;
        constexpr float kWhizAfterSecs = 0.3F;

        /** The shake: how long it lasts at its strongest, its frequency, and how random it is (CPlayer::CameraShake's own). */
        constexpr float kShakeSeconds   = 0.5F;
        constexpr float kShakeExtra     = 0.7F;
        constexpr float kShakeFrequency = 0.08F;
        constexpr float kShakeRandom    = 0.5F;

        /** The id the shake goes under, so a second blast replaces the first rather than stacking on it. */
        constexpr int kShakeId = 0x5E16;

        /** How often the operator's look is sent, and how far it must move first: a metre, and a little more far off. */
        constexpr std::uint64_t kAimIntervalMs = 100;
        constexpr float kAimMinMove            = 1.0F;
        constexpr float kAimMoveFraction       = 0.004F;

        /** How far the operator's look is traced, in metres, and how near the engine a look moves nothing. */
        constexpr float kAimRay      = 1500.0F;
        constexpr float kAimDeadZone = 6.0F;

        /** How long a chord of the arc the beam traces is, in seconds. */
        constexpr float kMarkerChord = 0.2F;

        /** How far the aim or the engine must move, in metres, before the beam is traced again. */
        constexpr float kMarkerMoved = 0.01F;

        /** How near the server's aim must come to this client's last look to have caught up, in metres, and how long it is waited for. */
        constexpr float kAimCaughtUp       = 1.0F;
        constexpr std::uint64_t kAimHoldMs = 1500;

        /** How many points of a sling's swing the beam follows before the stone flies. */
        constexpr std::size_t kBeamSlingPoints = 8;

        /** The beam: the game's own primitives, red and see-through. */
        constexpr const char *kBeamModel    = "objects/special/primitive_cylinder_noproxy.cgf";
        constexpr const char *kBeamEndModel = "objects/special/primitive_sphere_noproxy.cgf";
        constexpr KCDC::Features::Prop::GhostTint kBeamTint = KCDC::Features::Prop::PackGhostTint(255, 40, 30, 90);

        /** Its radius at the muzzle, how much it widens a metre, its widest, and the ball's radius where it lands, in metres. */
        constexpr float kBeamRadius    = 0.06F;
        constexpr float kBeamWidening  = 0.004F;
        constexpr float kBeamRadiusMax = 0.6F;
        constexpr float kBeamEndRadius = 0.8F;

        // The operator's keys. The aim is held, so it is an action of the
        // mode's own map rather than a hint: a hint fires on a press only.
        // Not exclusive -- an exclusive map takes the mouse look with it.
        // `keyboard` carries the mouse in CryEngine's input vocabulary.
        constexpr char kMapName[]    = "kcdc_siege";
        constexpr char kAimAction[]  = "kcdc_siege_aim";
        constexpr char kFilterName[] = "kcdc_siege_block";

        /** Any value does: nothing else holds a reason on this filter. */
        constexpr std::uint8_t kFilterReason = 0;

        constexpr char kMapXml[] = "<actionmap name=\"kcdc_siege\">"
                                   "<action name=\"kcdc_siege_aim\" onPress=\"1\" onRelease=\"1\" keyboard=\"mouse2\" xboxpad=\"xi_triggerl_btn\" pspad=\"pad_l2\"/>"
                                   "</actionmap>";

        /**
         * What the game must not answer while the player works an engine:
         * the attack and the guard, which fire and lay it; the torch, which
         * loads it; the use key, which steps away from it. The names are the
         * game's own (`keybindSuperactions.xml`).
         */
        constexpr char kFilterXml[] = "<actionfilter name=\"kcdc_siege_block\" type=\"actionFail\">"
                                      "<action name=\"attack_primary\"/>"
                                      "<action name=\"attack_primary_mouse\"/>"
                                      "<action name=\"prepare_to_attack\"/>"
                                      "<action name=\"attack_abort\"/>"
                                      "<action name=\"attack_abort_mouse\"/>"
                                      "<action name=\"attack_stop\"/>"
                                      "<action name=\"block\"/>"
                                      "<action name=\"opponent_lock\"/>"
                                      "<action name=\"combat_unlock_mouse\"/>"
                                      "<action name=\"combat_zone_mouse_x\"/>"
                                      "<action name=\"combat_zone_mouse_y\"/>"
                                      "<action name=\"sword_position\"/>"
                                      "<action name=\"sword_rotation\"/>"
                                      "<action name=\"stealth_kill_attack\"/>"
                                      "<action name=\"stealth_kill_silent_attack\"/>"
                                      "<action name=\"bow_primary\"/>"
                                      "<action name=\"bow_secondary\"/>"
                                      "<action name=\"crossbow_execute\"/>"
                                      "<action name=\"crossbow_prepare\"/>"
                                      "<action name=\"gun_execute\"/>"
                                      "<action name=\"gun_prepare\"/>"
                                      "<action name=\"toggle_torch\"/>"
                                      "<action name=\"use\"/>"
                                      "<action name=\"use_other\"/>"
                                      "<action name=\"talk\"/>"
                                      "</actionfilter>";

        /** `EActionActivationMode`: a press and a release. */
        constexpr int kOnPress   = 1;
        constexpr int kOnRelease = 2;

        /** Where the listener is: the camera, else the local player. */
        std::optional<glm::vec3> Listener() {
            KCDC::Features::Camera::ViewPose view;
            if (KCDC::Features::Camera::ReadViewPose(view)) {
                return KCDC::Common::ToGlm(view.position);
            }
            if (SDK::IEntity *player = KCDC::Common::LocalPlayer()) {
                return KCDC::Common::ToGlm(player->GetWorldPos());
            }
            return std::nullopt;
        }

        /** The soul of the player a network id names, the local one included; null for nobody. */
        SDK::wh::rpgmodule::I_Soul *SoulOfPlayer(std::uint64_t playerNetworkId) {
            auto *human = KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(playerNetworkId);
            if (human == nullptr) {
                return nullptr;
            }
            auto *actor = human->ResolveBody();
            return actor != nullptr ? actor->GetSoul() : nullptr;
        }

        std::uint64_t LocalNetworkId() {
            auto *human = KCDC::Features::Player::Human::FindLocal();
            return human != nullptr ? human->GetNetworkID() : 0;
        }

        /** The first thing a straight line from `from` by `step` meets, skipping up to two colliders. */
        std::optional<glm::vec3> Trace(const glm::vec3 &from, const glm::vec3 &step, SDK::IPhysicalEntity *skipA, SDK::IPhysicalEntity *skipB) {
            SDK::CPhysicalWorld *physics = SDK::CPhysicalWorld::GetInstance();
            if (physics == nullptr || glm::dot(step, step) <= 1e-6F) {
                return std::nullopt;
            }
            SDK::IPhysicalEntity *skip[2] = {nullptr, nullptr};
            int skipCount                 = 0;
            for (SDK::IPhysicalEntity *entity : {skipA, skipB}) {
                if (entity != nullptr) {
                    skip[skipCount++] = entity;
                }
            }
            SDK::SRayHit hit {};
            if (physics->RayWorldIntersection(KCDC::Common::ToNative(from), KCDC::Common::ToNative(step), SDK::CPhysicalWorld::kGroundObjectTypes, SDK::CPhysicalWorld::kGroundRayFlags, &hit, 1, skip, skipCount) > 0) {
                return KCDC::Common::ToGlm(hit.point);
            }
            return std::nullopt;
        }

        SDK::IPhysicalEntity *LocalPlayerPhysics() {
            SDK::IEntity *player = KCDC::Common::LocalPlayer();
            return player != nullptr ? player->GetPhysics() : nullptr;
        }
    } // namespace

    SiegeModule &SiegeModule::Get() {
        static SiegeModule instance;
        return instance;
    }

    bool SiegeModule::ExtendGameAudio(SDK::XmlNodeRef &document) {
        // One range per trigger; two engines sharing a sound get the longer.
        std::vector<KCDC::Features::Audio::AudioRange::TriggerRange> ranges;
        const auto add = [&ranges](const char *trigger, float range) {
            if (trigger[0] == '\0' || range <= 0.0F) {
                return;
            }
            for (KCDC::Features::Audio::AudioRange::TriggerRange &known : ranges) {
                if (std::strcmp(known.trigger, trigger) == 0) {
                    known.range = std::max(known.range, range);
                    return;
                }
            }
            ranges.push_back(KCDC::Features::Audio::AudioRange::TriggerRange {trigger, range});
        };
        for (const Shared::Game::SiegeEngineAsset &asset : Shared::Game::g_SiegeEngineAssets) {
            add(asset.releaseSound, asset.releaseSoundRange);
            add(asset.impactSound, asset.impactSoundRange);
            add(asset.fuseSound, asset.fuseSoundRange);
            add(asset.whizSound, asset.whizSoundRange);
            add(asset.wreckSound, asset.wreckSoundRange);
        }
        return KCDC::Features::Audio::AudioRange::Get().Limit(document, ranges);
    }

    void SiegeModule::DrawFar(SDK::EntityId entityId) {
        SDK::IEntity *entity = KCDC::Common::ResolveEntity(entityId);
        SDK::IEntityProxy *proxy = entity != nullptr ? entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Render)) : nullptr;
        if (proxy != nullptr) {
            // The most the engine takes: the node is drawn out to a hundred
            // times what its size alone would earn it.
            SDK::CRenderProxy::FromProxy(proxy)->SetViewDistRatio(255);
        }
    }

    void SiegeModule::PlayEffect(const char *name, const glm::vec3 &point, std::uint32_t durationMs) {
        if (name == nullptr || name[0] == '\0') {
            return;
        }
        const std::size_t row = Shared::Game::FindVfxEffectIndex(name);
        if (row == Shared::Game::kNoVfxEffect) {
            return;
        }
        KCDC::Core::gApplication->GetVfxService().Spawn(Shared::Game::VfxEffectRef::Catalog(row), point, glm::quat(1.0F, 0.0F, 0.0F, 0.0F), Shared::Game::VfxTuning {}, durationMs);
    }

    void SiegeModule::PlaySound(const char *trigger, const glm::vec3 &point) {
        if (trigger == nullptr || trigger[0] == '\0') {
            return;
        }
        const std::optional<glm::vec3> listener = Listener();
        const std::uint64_t delay               = listener ? static_cast<std::uint64_t>(glm::distance(*listener, point) / kSpeedOfSound * 1000.0F) : 0;
        if (delay < kMinSoundDelay) {
            KCDC::Features::Audio::AudioService::PlayAt(trigger, point);
            return;
        }
        _sounds.push_back(DelayedSound {KCDC::Common::NowMs() + delay, trigger, point});
    }

    void SiegeModule::PlayDueSounds(std::uint64_t now) {
        // By index: nothing played here queues another, but the list is
        // erased from as it goes.
        for (std::size_t index = 0; index < _sounds.size();) {
            if (now >= _sounds[index].dueMs) {
                KCDC::Features::Audio::AudioService::PlayAt(_sounds[index].trigger, _sounds[index].point);
                _sounds.erase(_sounds.begin() + static_cast<std::ptrdiff_t>(index));
                continue;
            }
            ++index;
        }
    }

    void SiegeModule::Shake(const glm::vec3 &point, float strength, float range) {
        if (!(strength > 0.0F) || !(range > 0.0F)) {
            return;
        }
        const std::optional<glm::vec3> listener = Listener();
        if (!listener) {
            return;
        }
        const float distance = glm::distance(*listener, point);
        if (distance >= range) {
            return;
        }
        // `actor:CameraShake` is a no-op in this game (C_Actor slot 63 is
        // nullsub_1); the view's own shake is what CPlayer::CameraShake ends
        // in, so it is called with what that builds. `wh_c_CameraShake`
        // still gates the whole of it in CView::Update.
        SDK::CView *view = KCDC::Features::Camera::ActiveView();
        if (view == nullptr) {
            return;
        }
        const float closeness = 1.0F - distance / range;
        const float degrees = std::min(90.0F, strength * closeness * closeness);
        const float radians   = glm::radians(degrees);
        const float shift   = 0.05F * strength * closeness * closeness;
        view->SetViewShake(SDK::Ang3 {radians * 0.15F, radians * 1.15F, radians * 0.05F}, SDK::Vec3 {shift, 0.0F, shift}, kShakeSeconds + kShakeExtra * closeness, kShakeFrequency, kShakeRandom, kShakeId);
    }

    void SiegeModule::InstallRPCs() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        _rpcsInstalled = true;

        net->RegisterRPC<Shared::RPC::SiegeShot>([](const Shared::RPC::SiegeShot &shot, MafiaNet::Packet *) {
            if (shot.IsSane()) {
                SiegeModule &module = SiegeModule::Get();
                std::scoped_lock lock(module._inboxMutex);
                module._shotInbox.push_back(shot);
            }
        });
        net->RegisterRPC<Shared::RPC::SiegeImpact>([](const Shared::RPC::SiegeImpact &impact, MafiaNet::Packet *) {
            if (impact.IsSane()) {
                SiegeModule &module = SiegeModule::Get();
                std::scoped_lock lock(module._inboxMutex);
                module._impactInbox.push_back(impact);
            }
        });
    }

    void SiegeModule::SendUse(std::uint64_t engineNetworkId, Shared::RPC::SiegeUseAction action) {
        Shared::RPC::SiegeUse use;
        use.engineNetworkId = engineNetworkId;
        use.action          = action;
        if (auto *net = Framework::CoreModules::GetNetworkPeer()) {
            net->BroadcastRPC(use, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
    }

    void SiegeModule::UpdatePrompt(std::uint64_t now) {
        if (now < _nextPromptCheck) {
            return;
        }
        _nextPromptCheck = now + kPromptIntervalMs;

        // The game's own prompt on whatever the player looks at takes the
        // same key, so this one stands down while it is up -- and while the
        // player already works an engine, whose keys are its own.
        SDK::wh::entitymodule::C_Player *player = SDK::wh::entitymodule::C_Player::GetInstance();
        SDK::IEntity *body                      = KCDC::Common::LocalPlayer();
        const bool gameOffersPrompt             = player != nullptr && player->GetInteractor()->GetTargetEntityId() != 0;

        SiegeEngine *nearest               = nullptr;
        Shared::RPC::SiegeUseAction action = Shared::RPC::SiegeUseAction::Operate;
        if (body != nullptr && !gameOffersPrompt && _role == Role::None) {
            const glm::vec3 from = KCDC::Common::ToGlm(body->GetWorldPos());
            float best           = kPromptReach;
            if (auto *replication = Framework::CoreModules::GetReplication()) {
                replication->ForEach<SiegeEngine>([&](SiegeEngine *engine) {
                    if (!engine->usable || engine->GetEntityId() == 0 || engine->state == Shared::Entities::SiegeState::Wrecked) {
                        return;
                    }
                    // A free winch the player stands at wins over the place:
                    // that is what someone walking up to the drum wants.
                    if (engine->crewRequired > 0) {
                        for (std::size_t place = 0; place < engine->CrewPlaces(); ++place) {
                            const float distance = glm::length(glm::vec2(from - engine->CrewPlacePosition(place)));
                            if (engine->crewNetworkIds[place] == 0 && distance <= kWinchReach && distance < best) {
                                best    = distance;
                                nearest = engine;
                                action  = Shared::RPC::SiegeUseAction::Winch;
                            }
                        }
                    }
                    const float distance = engine->GroundDistanceTo(from);
                    if (engine->operatorNetworkId == 0 && distance <= best && !(nearest == engine && action == Shared::RPC::SiegeUseAction::Winch)) {
                        best    = distance;
                        nearest = engine;
                        action  = Shared::RPC::SiegeUseAction::Operate;
                    }
                });
            }
        }

        const std::uint64_t engineId = nearest != nullptr ? nearest->GetNetworkID() : 0;
        if (engineId == _promptEngine && action == _promptAction && (_prompt != 0) == (engineId != 0)) {
            return;
        }
        HidePrompt();
        if (engineId == 0) {
            return;
        }

        KCDC::Features::UI::ActionHints::Options options;
        options.key  = "primary_action";
        // The game has a word for using a thing, but none for a winch.
        options.text  = action == Shared::RPC::SiegeUseAction::Winch ? std::string("Man the winch") : KCDC::Common::GameText("@ui_hud_use_item", "Use");
        _prompt      = KCDC::Features::UI::ActionHints::Get().Create({}, options, [this, engineId, action](std::uint32_t) {
            SendUse(engineId, action);
        });
        _promptEngine = _prompt != 0 ? engineId : 0;
        _promptAction = action;
    }

    void SiegeModule::HidePrompt() {
        if (_prompt != 0) {
            KCDC::Features::UI::ActionHints::Get().Remove(_prompt);
        }
        _prompt       = 0;
        _promptEngine = 0;
    }

    bool SiegeModule::AcquireInput() {
        SDK::ISystem *system            = SDK::ISystem::GetInstance();
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();
        SDK::IInput *input              = SDK::IInput::GetInstance();
        if (system == nullptr || manager == nullptr || input == nullptr || !input->IsEventPostingEnabled()) {
            return false;
        }
        if (manager->GetActionMap(kMapName) != nullptr) {
            Framework::Logging::GetLogger(kLogger)->warn("The siege action map is already owned; the aim key stays the game's");
            return false;
        }

        SDK::XmlNodeRef node = system->LoadXmlFromBuffer(kMapXml, sizeof(kMapXml) - 1, false, false);
        if (!node.IsValid()) {
            return false;
        }
        SDK::IActionMap *map = manager->CreateActionMap(kMapName);
        if (map == nullptr) {
            return false;
        }
        if (!map->LoadFromXML(node) || !manager->AddExtraActionListener(this, kMapName)) {
            manager->RemoveActionMap(kMapName);
            return false;
        }

        _inputManager = manager;
        _inputMap     = map;
        _listening    = true;
        manager->EnableActionMap(kMapName, true);

        // The map adds the aim; the filter takes the game's own attack,
        // guard, torch and use away, so a click fires the engine rather than
        // swinging too. `Enable` releases whatever they were holding.
        _filter = manager->FindOrCreateActionFilter(kFilterName, kFilterXml, sizeof(kFilterXml) - 1);
        if (_filter != nullptr) {
            _filter->Enable(kFilterReason);
        }
        else {
            Framework::Logging::GetLogger(kLogger)->warn("Working the engine without the action filter: its keys also reach the player's hands");
        }
        return true;
    }

    void SiegeModule::ReleaseInput() {
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();

        // Only while it is still the manager this module took: a level change
        // replaces it, and the old one is freed.
        if (manager != nullptr && manager == _inputManager) {
            if (_filter != nullptr && manager->GetActionFilter(kFilterName) == _filter) {
                _filter->Disable(kFilterReason, true);
            }
            if (_inputMap != nullptr && manager->GetActionMap(kMapName) == _inputMap) {
                manager->EnableActionMap(kMapName, false);
                if (_listening) {
                    manager->RemoveExtraActionListener(this, kMapName);
                }
                manager->RemoveActionMap(kMapName);
            }
        }
        _inputManager = nullptr;
        _inputMap     = nullptr;
        _filter       = nullptr;
        _listening    = false;
        _aimHeld      = false;
    }

    void SiegeModule::OnAction(const SDK::CCryName &action, int activationMode, float) {
        if (!_listening || std::strcmp(action.c_str(), kAimAction) != 0) {
            return;
        }
        // Latched: this runs inside the engine's input dispatch.
        if (activationMode == kOnPress) {
            _aimHeld = true;
        }
        else if (activationMode == kOnRelease) {
            _aimHeld = false;
        }
    }

    void SiegeModule::EnterRole(Role role, SiegeEngine &engine) {
        _role       = role;
        _roleEngine = engine.GetNetworkID();
        _aimSent.reset();
        _nextAimAt = 0;
        HidePrompt();

        const std::uint64_t engineId = _roleEngine;
        KCDC::Features::UI::ActionHints &hints = KCDC::Features::UI::ActionHints::Get();
        KCDC::Features::UI::ActionHints::Options leave;
        leave.key  = "primary_action";
        leave.text = KCDC::Common::GameText("@ui_hud_stop_use", "Stop");
        _leaveHint = hints.Create({}, leave, [this, engineId](std::uint32_t) {
            SendUse(engineId, Shared::RPC::SiegeUseAction::Leave);
        });

        if (role == Role::Crew) {
            // Stood at the winch, facing the drum the way the game's own
            // pullers do; the server's pull holds him there while it winds.
            const int place = engine.CrewPlaceOf(LocalNetworkId());
            if (place >= 0) {
                const Shared::Game::SiegeCrewPlace &crew = engine.Asset().crew[static_cast<std::size_t>(place)];
                KCDC::Features::Player::TeleportRequest request;
                request.position    = KCDC::Common::ToNative(engine.CrewPlacePosition(static_cast<std::size_t>(place)));
                request.rotation    = KCDC::Common::ToNative(glm::normalize(engine.rotation) * glm::angleAxis(glm::radians(crew.yawDeg), glm::vec3(0.0F, 0.0F, 1.0F)));
                request.hasRotation = true;
                request.label       = "Siege winch";
                KCDC::Core::gApplication->GetPlayerTeleporter().QueueTeleport(request);
            }
            Framework::Logging::GetLogger(kLogger)->info("Manning winch {} of {} {}", place, engine.Asset().name, engineId);
            return;
        }

        AcquireInput();
        KCDC::Features::UI::ActionHints::Options fire;
        fire.key  = "attack_primary";
        fire.text = _fireText = KCDC::Common::GameText("@ui_use_fire", "Fire");
        _fireHint             = hints.Create({}, fire, [this, engineId](std::uint32_t) {
            SendUse(engineId, Shared::RPC::SiegeUseAction::Fire);
        });
        // Shown only: the aim is held, and the mode's own map reads the hold.
        // Neither the aim nor the load has a word in the game's own text.
        KCDC::Features::UI::ActionHints::Options aim;
        aim.key  = "block";
        aim.text = "Aim";
        _aimHint = hints.Create({}, aim, [](std::uint32_t) {});
        KCDC::Features::UI::ActionHints::Options load;
        load.key  = "torch";
        load.text = "Load";
        _loadHint = hints.Create({}, load, [this, engineId](std::uint32_t) {
            SendUse(engineId, Shared::RPC::SiegeUseAction::Load);
        });
        Framework::Logging::GetLogger(kLogger)->info("Operating {} {}", engine.Asset().name, engineId);
    }

    void SiegeModule::LeaveRole() {
        KCDC::Features::UI::ActionHints &hints = KCDC::Features::UI::ActionHints::Get();
        for (std::uint32_t *hint : {&_fireHint, &_aimHint, &_loadHint, &_leaveHint}) {
            if (*hint != 0) {
                hints.Remove(*hint);
            }
            *hint = 0;
        }
        ReleaseInput();
        HideMarker();
        if (_marker != 0 && KCDC::Core::gApplication) {
            KCDC::Core::gApplication->GetPropGhostService().Destroy(_marker);
        }
        _marker       = 0;
        _markerTraced = false;
        _localAim.reset();
        _aimReleasedAt = 0;
        if (_role != Role::None) {
            Framework::Logging::GetLogger(kLogger)->info("No longer working engine {}", _roleEngine);
        }
        _role       = Role::None;
        _roleEngine = 0;
        _aimSent.reset();
    }

    void SiegeModule::UpdateOperatorHints(SiegeEngine &engine, std::uint64_t now, std::int64_t serverClockOffset, bool serverClockKnown) {
        KCDC::Features::UI::ActionHints &hints = KCDC::Features::UI::ActionHints::Get();
        const bool ready                       = engine.state == Shared::Entities::SiegeState::Ready;
        if (_fireHint != 0) {
            hints.SetEnabled(_fireHint, ready);
            // While it cannot fire, why not and how far along it is; once it
            // can, how far it would throw, read off the mark.
            // One load from the arm coming down to the stone in the sling: the
            // winding and the laying are two steps of it, not two loads.
            const float draw  = static_cast<float>(engine.StepDurationMs(Shared::Game::SiegeStep::Draw));
            const float lay   = static_cast<float>(engine.StepDurationMs(Shared::Game::SiegeStep::Load));
            const float phase = engine.StepPhase(now, serverClockOffset, serverClockKnown);
            const float done  = engine.state == Shared::Entities::SiegeState::Drawing ? phase * draw : draw + phase * lay;
            const int percent = draw + lay > 0.0F ? static_cast<int>(done / (draw + lay) * 100.0F) : 0;
            std::string text  = _fireText;
            switch (engine.state) {
            case Shared::Entities::SiegeState::Idle: text += engine.ammo == 0 ? " (nothing left to throw)" : " (not loaded)"; break;
            case Shared::Entities::SiegeState::Drawing:
            case Shared::Entities::SiegeState::Loading:
                text += engine.stepStartMs == 0 ? " (waiting for the crew, " + std::to_string(percent) + "%)" : " (loading " + std::to_string(percent) + "%)";
                break;
            case Shared::Entities::SiegeState::Firing: text += " (firing)"; break;
            case Shared::Entities::SiegeState::Wrecked: text += " (wrecked)"; break;
            case Shared::Entities::SiegeState::Ready:
                if (_markerPoint) {
                    text += " (" + std::to_string(static_cast<int>(std::round(glm::length(glm::vec2(*_markerPoint - engine.position))))) + " m)";
                }
                break;
            }
            hints.SetText(_fireHint, text);
        }
        if (_loadHint != 0) {
            hints.SetEnabled(_loadHint, engine.state == Shared::Entities::SiegeState::Idle && engine.ammo != 0);
        }
        if (_aimHint != 0) {
            hints.SetEnabled(_aimHint, engine.state != Shared::Entities::SiegeState::Firing);
        }
    }

    void SiegeModule::UpdateAim(SiegeEngine &engine, std::uint64_t now) {
        if (!_aimHeld || engine.state == Shared::Entities::SiegeState::Firing) {
            if (_localAim) {
                // Kept until the server's aim has caught up with the last look,
                // or long enough that it must have and did not take it.
                if (_aimReleasedAt == 0) {
                    _aimReleasedAt = now;
                }
                if ((engine.aimed && glm::distance(engine.aimTarget, *_localAim) < kAimCaughtUp) || now - _aimReleasedAt > kAimHoldMs) {
                    _localAim.reset();
                }
            }
            return;
        }
        _aimReleasedAt = 0;

        KCDC::Features::Camera::ViewPose view;
        if (!KCDC::Features::Camera::ReadViewPose(view)) {
            return;
        }
        const glm::vec3 from          = KCDC::Common::ToGlm(view.position);
        const glm::vec3 forward       = glm::normalize(KCDC::Common::ToGlm(view.forward));
        std::optional<glm::vec3> look = Trace(from, forward * kAimRay, engine.GetPhysics(), LocalPlayerPhysics());
        if (!look) {
            // The sky: the horizon that way, at the engine's own height, which
            // is walked in to what the engine can reach.
            const glm::vec2 flat(forward.x, forward.y);
            if (glm::dot(flat, flat) < 1e-4F) {
                return;
            }
            const glm::vec2 ahead = glm::vec2(engine.position) + glm::normalize(flat) * engine.Asset().maxRange;
            look                  = glm::vec3(ahead.x, ahead.y, engine.position.z);
        }
        // Looking at the engine itself lays nothing.
        if (glm::length(glm::vec2(*look - engine.position)) < kAimDeadZone) {
            return;
        }
        // Laid here at once, the way the server will lay it.
        if (const std::optional<glm::vec3> laid = Shared::Game::Siege::ReachableAlong(engine.Asset(), engine.position, *look)) {
            _localAim = laid;
        }

        if (now < _nextAimAt) {
            return;
        }
        if (_aimSent && glm::distance(*_aimSent, *look) < kAimMinMove + kAimMoveFraction * glm::distance(engine.position, *look)) {
            return;
        }
        _nextAimAt = now + kAimIntervalMs;
        _aimSent   = *look;

        Shared::RPC::SiegeAim aim;
        aim.engineNetworkId = engine.GetNetworkID();
        aim.target          = *look;
        if (aim.IsSane()) {
            if (auto *net = Framework::CoreModules::GetNetworkPeer()) {
                net->BroadcastRPC(aim, MafiaNet::Priority::High, MafiaNet::Reliability::UnreliableSequenced);
            }
        }
    }

    void SiegeModule::UpdateMarker(SiegeEngine &engine) {
        const Shared::Game::SiegeEngineAsset &asset = engine.Asset();

        // Laid where this client last looked while it waits on the server,
        // else where the server laid it. Nothing while the shot is on its way.
        std::optional<glm::vec3> target = _localAim;
        if (!target && engine.aimed) {
            target = engine.aimTarget;
        }
        // Traced again only when what it is traced from has moved: the solve
        // and the arc's rays are the same answer while the aim holds still.
        const bool firing  = engine.state == Shared::Entities::SiegeState::Firing;
        const bool sameAim = _markerTarget.has_value() == target.has_value() && (!target || glm::distance(*_markerTarget, *target) < kMarkerMoved);
        if (_markerTraced && sameAim && _markerEngine == engine.GetNetworkID() && _markerFiring == firing && _markerGuide == engine.aimGuide && glm::distance(_markerFrom, engine.position) < kMarkerMoved) {
            KCDC::Features::Prop::PropGhostService &ghosts = KCDC::Core::gApplication->GetPropGhostService();
            if (_marker == 0 || ghosts.IsAlive(_marker)) {
                return;
            }
        }
        _markerTraced = true;
        _markerEngine = engine.GetNetworkID();
        _markerFiring = firing;
        _markerGuide  = engine.aimGuide;
        _markerTarget = target;
        _markerFrom   = engine.position;

        Shared::Game::Siege::Launch launch;
        const glm::quat facing = target ? Shared::Game::Siege::FacingRotation(engine.position, *target) : glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
        if (!target || firing || Shared::Game::Siege::SolveLaunch(asset, engine.position, facing, *target, launch) != Shared::Game::Siege::AimResult::Ok) {
            HideMarker();
            return;
        }

        // The path, as points: first out of the sling, where the engine has
        // one, from where the stone lies in it to where it is let go.
        std::vector<glm::vec3> &path = _markerPath;
        path.clear();
        if (!asset.sling.empty()) {
            const std::size_t last = asset.sling.size() - 1;
            for (std::size_t index = 0; index < kBeamSlingPoints; ++index) {
                const Shared::Game::SiegePoint &at = asset.sling[last * index / (kBeamSlingPoints - 1)];
                path.push_back(engine.position + facing * KCDC::Features::Siege::ToGlm(at));
            }
        }
        else {
            path.push_back(launch.origin);
        }

        // Then the arc, traced through this world to where it meets it.
        glm::vec3 from      = launch.origin;
        float landedSeconds = 0.0F;
        std::optional<glm::vec3> landing;
        for (float seconds = kMarkerChord; seconds <= Shared::Game::Siege::kMaxFlightSeconds && !landing; seconds += kMarkerChord) {
            const glm::vec3 to = Shared::Game::Siege::PositionAt(launch.origin, launch.velocity, seconds);
            landing            = Trace(from, to - from, engine.GetPhysics(), LocalPlayerPhysics());
            landedSeconds      = seconds;
            from               = to;
        }
        if (!landing) {
            HideMarker();
            return;
        }
        _markerPoint = landing;

        const std::size_t cylinders = KCDC::Features::Prop::PropGhostService::kMaxPartsPerGhost - 1;
        const std::size_t arc       = cylinders - (path.size() - 1);
        for (std::size_t index = 1; index <= arc; ++index) {
            const float seconds = landedSeconds * static_cast<float>(index) / static_cast<float>(arc);
            path.push_back(index == arc ? *landing : Shared::Game::Siege::PositionAt(launch.origin, launch.velocity, seconds));
        }

        // Each slice a cylinder from one point to the next, widening with the
        // distance as a light's beam does. `primitive_cylinder_noproxy` is two
        // metres tall up its +Z from its base, one metre round. The ghost
        // stands at the world's origin, so a part's offset is where it is.
        std::vector<KCDC::Features::Prop::GhostPart> &parts = _markerParts;
        parts.clear();
        float travelled = 0.0F;
        for (std::size_t index = 0; index + 1 < path.size(); ++index) {
            const glm::vec3 span = path[index + 1] - path[index];
            const float length   = std::max(glm::length(span), 1e-3F);
            const glm::vec3 along = span / length;
            const glm::vec3 up(0.0F, 0.0F, 1.0F);
            const glm::vec3 axis = glm::cross(up, along);
            const float turn     = std::acos(std::clamp(glm::dot(up, along), -1.0F, 1.0F));
            const glm::quat lie  = glm::dot(axis, axis) > 1e-8F ? glm::angleAxis(turn, glm::normalize(axis)) : (along.z >= 0.0F ? glm::quat(1.0F, 0.0F, 0.0F, 0.0F) : glm::angleAxis(glm::pi<float>(), glm::vec3(1.0F, 0.0F, 0.0F)));
            const float radius   = std::min(kBeamRadiusMax, kBeamRadius + kBeamWidening * travelled);

            KCDC::Features::Prop::GhostPart part;
            part.model    = kBeamModel;
            part.offset   = KCDC::Common::ToNative(path[index]);
            part.rotation = KCDC::Common::ToNative(lie);
            part.stretch  = SDK::Vec3 {radius, radius, length * 0.5F};
            parts.push_back(part);
            travelled += length;
        }
        KCDC::Features::Prop::GhostPart ball;
        ball.model  = kBeamEndModel;
        ball.offset = KCDC::Common::ToNative(*landing);
        ball.scale  = kBeamEndRadius;
        parts.push_back(ball);

        // The landing is still worked out without the guide: the Fire hint
        // says how far the shot goes either way.
        if (!engine.aimGuide) {
            const std::optional<glm::vec3> landed = _markerPoint;
            HideMarker();
            _markerPoint = landed;
            return;
        }

        KCDC::Features::Prop::PropGhostService &ghosts = KCDC::Core::gApplication->GetPropGhostService();
        if (_marker != 0 && !ghosts.IsAlive(_marker)) {
            _marker = 0;
        }
        if (_marker == 0) {
            _marker = ghosts.Create(parts, kBeamTint, SDK::Vec3 {0.0F, 0.0F, 0.0F}, SDK::Quat {0.0F, 0.0F, 0.0F, 1.0F}, 1.0F);
            _markerShown = _marker != 0;
            return;
        }
        ghosts.SetParts(_marker, parts);
        if (!_markerShown) {
            ghosts.SetHidden(_marker, false);
            _markerShown = true;
        }
    }

    void SiegeModule::HideMarker() {
        if (_marker != 0 && _markerShown && KCDC::Core::gApplication) {
            KCDC::Core::gApplication->GetPropGhostService().SetHidden(_marker, true);
        }
        _markerShown = false;
        _markerPoint.reset();
    }

    void SiegeModule::UpdateRole(std::uint64_t now, std::int64_t serverClockOffset, bool serverClockKnown) {
        // What the engines say the local player holds, the operator's place
        // first: the server is the one that hands either out.
        const std::uint64_t me = LocalNetworkId();
        Role role              = Role::None;
        SiegeEngine *held      = nullptr;
        if (me != 0) {
            if (auto *replication = Framework::CoreModules::GetReplication()) {
                replication->ForEach<SiegeEngine>([&](SiegeEngine *engine) {
                    if (engine->operatorNetworkId == me) {
                        role = Role::Operator;
                        held = engine;
                    }
                    else if (role == Role::None && engine->CrewPlaceOf(me) >= 0) {
                        role = Role::Crew;
                        held = engine;
                    }
                });
            }
        }

        if (role != _role || (held != nullptr && held->GetNetworkID() != _roleEngine)) {
            LeaveRole();
            if (held != nullptr) {
                EnterRole(role, *held);
            }
        }
        if (_role != Role::Operator || held == nullptr) {
            return;
        }
        UpdateAim(*held, now);
        UpdateMarker(*held);
        UpdateOperatorHints(*held, now, serverClockOffset, serverClockKnown);
    }

    void SiegeModule::NativeContextChanged() {
        _nativeContextLost = true;
    }

    void SiegeModule::Reset() {
        // The session's replicas take their own entities with them; the
        // stones and the beam's ghost are this module's. Removing a hint
        // ActionHints already dropped with the session is harmless.
        ClearFlights(true);
        _shown.clear();
        _launched.clear();
        _sounds.clear();
        LeaveRole();
        HidePrompt();
        _nextPromptCheck = 0;
        std::scoped_lock lock(_inboxMutex);
        _shotInbox.clear();
        _impactInbox.clear();
    }

    void SiegeModule::ClearFlights(bool removeEntities) {
        if (removeEntities) {
            for (const Flight &flight : _flights) {
                KCDC::Core::gApplication->GetPropService().QueueDespawn(flight.stoneId);
            }
        }
        _flights.clear();
    }

    SiegeModule::Flight *SiegeModule::FindFlight(std::uint32_t shotId) {
        for (Flight &flight : _flights) {
            if (flight.shot.shotId == shotId) {
                return &flight;
            }
        }
        return nullptr;
    }

    bool SiegeModule::HasLaunched(std::uint32_t shotId) const {
        return std::find(_launched.begin(), _launched.end(), shotId) != _launched.end();
    }

    void SiegeModule::Launch(const Shared::RPC::SiegeShot &shot, std::uint64_t now) {
        Flight flight;
        flight.shot = shot;

        // The flight is timed off the server clock it left on, so a stone that
        // arrives late is drawn where it has got to rather than starting over.
        std::int64_t offset  = 0;
        flight.launchLocalMs = KCDC::Common::ServerClockDifferential(offset) ? static_cast<std::int64_t>(shot.launchMs) - offset : static_cast<std::int64_t>(now);
        flight.joinedSeconds = std::max(0.0F, static_cast<float>(static_cast<std::int64_t>(now) - flight.launchLocalMs) / 1000.0F);
        flight.lastPoint     = shot.origin;

        const Shared::Game::SiegeEngineAsset &asset = *Shared::Game::SiegeEngineAssetAt(shot.assetIndex);
        KCDC::Common::Spawn::PropSpawnRequest request;
        request.model   = asset.projectileModel;
        request.physics = KCDC::Common::Spawn::PropPhysics::None;
        request.world   = KCDC::Common::Spawn::WorldPlacement {KCDC::Common::ToNative(shot.origin), SDK::Quat {0.0F, 0.0F, 0.0F, 1.0F}};
        flight.stoneId  = KCDC::Core::gApplication->GetPropSpawner().SpawnNow(request).prop.entityId;
        DrawFar(flight.stoneId);

        // The muzzle's flash, boom and kick only for a shot seen leaving.
        if (flight.joinedSeconds < 1.0F) {
            PlayEffect(asset.releaseEffect, shot.origin, kReleaseEffectMs);
            PlaySound(asset.releaseSound, shot.origin);
            Shake(shot.origin, asset.releaseShake, asset.releaseShakeRange);
            if (auto *engine = KCDC::Common::FindReplicaById<SiegeEngine>(shot.engineNetworkId)) {
                engine->Kick(now);
            }
        }

        Framework::Logging::GetLogger(kLogger)->info("Shot {} launched from {:.1f}, {:.1f}, {:.1f} at {:.1f} m/s, {:.2f}s in{}", shot.shotId, shot.origin.x, shot.origin.y, shot.origin.z, glm::length(shot.velocity), flight.joinedSeconds, shot.report ? "; this client reports it" : "");
        _flights.push_back(flight);
        _launched.push_back(shot.shotId);
        if (_launched.size() > kLaunchedMemory) {
            _launched.pop_front();
        }
    }

    void SiegeModule::LaunchDueShots(std::uint64_t now, std::int64_t serverClockOffset, bool serverClockKnown) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr || !serverClockKnown) {
            return;
        }
        replication->ForEach<SiegeEngine>([&](SiegeEngine *engine) {
            if (engine->shotId == 0 || HasLaunched(engine->shotId)) {
                return;
            }
            const std::int64_t launchLocal = static_cast<std::int64_t>(engine->shotLaunchMs) - serverClockOffset;
            const float age                = static_cast<float>(static_cast<std::int64_t>(now) - launchLocal) / 1000.0F;
            // Not yet let go, or long since come down.
            if (age < 0.0F || age > Shared::Game::Siege::kMaxFlightSeconds) {
                return;
            }
            Shared::RPC::SiegeShot shot;
            shot.shotId          = engine->shotId;
            shot.engineNetworkId = engine->GetNetworkID();
            shot.assetIndex      = engine->assetIndex;
            shot.origin          = engine->shotOrigin;
            shot.velocity        = engine->shotVelocity;
            shot.launchMs        = engine->shotLaunchMs;
            shot.report          = false;
            if (shot.IsSane()) {
                Launch(shot, now);
            }
        });
    }

    void SiegeModule::ShowImpact(std::uint32_t shotId, std::uint8_t assetIndex, const glm::vec3 &point, bool struckEngine) {
        const Shared::Game::SiegeEngineAsset &asset = *Shared::Game::SiegeEngineAssetAt(assetIndex);
        // Only the server knows a stone struck an engine, so its debris comes
        // with the verdict even where the blast itself was already shown.
        if (struckEngine) {
            PlayEffect(asset.wreckEffect, point, kImpactEffectMs);
            PlaySound(asset.wreckSound, point);
        }
        for (const Shown &shown : _shown) {
            if (shown.shotId == shotId && glm::distance(shown.point, point) <= kSameImpactDistance) {
                return;
            }
        }
        PlayEffect(asset.impactEffect, point, kImpactEffectMs);
        PlaySound(asset.impactSound, point);
        Shake(point, asset.impactShake, asset.impactShakeRange);

        if (_shown.size() >= kShownMemory) {
            _shown.erase(_shown.begin());
        }
        _shown.push_back(Shown {shotId, point});
    }

    void SiegeModule::ReportImpact(std::uint32_t shotId, const glm::vec3 &point) {
        Shared::RPC::SiegeImpactReport report;
        report.shotId = shotId;
        report.point  = point;
        if (auto *net = Framework::CoreModules::GetNetworkPeer()) {
            net->BroadcastRPC(report, MafiaNet::Priority::High, MafiaNet::Reliability::Reliable);
        }
    }

    void SiegeModule::Land(Flight &flight, const glm::vec3 &point, float seconds, std::uint64_t now) {
        // Gone at its impact, shattered in the blast: left where it struck, a
        // branch or a roof edge would hold it hanging in the air.
        flight.landed    = true;
        flight.landedAt  = point;
        flight.clearAtMs = now;
        // Down before this machine was there to see it: no blast replayed.
        if (seconds + 0.5F < flight.joinedSeconds) {
            return;
        }
        ShowImpact(flight.shot.shotId, flight.shot.assetIndex, point, false);
    }

    void SiegeModule::Fly(std::uint64_t now) {
        const std::optional<glm::vec3> listener = Listener();

        for (Flight &flight : _flights) {
            if (flight.landed) {
                continue;
            }

            const float seconds = std::max(0.0F, static_cast<float>(static_cast<std::int64_t>(now) - flight.launchLocalMs) / 1000.0F);
            // A stone that never met anything has left the world this machine
            // has streamed in. It goes quietly: an impact here would be in
            // mid-air, and the server lands the shot on its own.
            if (seconds > Shared::Game::Siege::kMaxFlightSeconds) {
                flight.landed    = true;
                flight.clearAtMs = now;
                continue;
            }
            const glm::vec3 point = Shared::Game::Siege::PositionAt(flight.shot.origin, flight.shot.velocity, seconds);

            // The step since the last frame, traced against what a player
            // would walk into. The engine that threw it is passed through: the
            // stone leaves from inside its frame.
            const glm::vec3 step = point - flight.lastPoint;
            SDK::IPhysicalEntity *engineCollider = nullptr;
            if (auto *engine = KCDC::Common::FindReplicaById<SiegeEngine>(flight.shot.engineNetworkId)) {
                engineCollider = engine->GetPhysics();
            }
            if (const std::optional<glm::vec3> hit = Trace(flight.lastPoint, step, engineCollider, nullptr)) {
                const float along   = glm::length(step) > 1e-4F ? glm::distance(flight.lastPoint, *hit) / glm::length(step) : 0.0F;
                const float hitTime = flight.lastSeconds + (seconds - flight.lastSeconds) * std::clamp(along, 0.0F, 1.0F);
                Land(flight, *hit, hitTime, now);
                Framework::Logging::GetLogger(kLogger)->info("Shot {} hit the world at {:.1f}, {:.1f}, {:.1f} after {:.2f}s", flight.shot.shotId, hit->x, hit->y, hit->z, hitTime);
                if (flight.shot.report) {
                    ReportImpact(flight.shot.shotId, *hit);
                }
                continue;
            }

            flight.lastPoint   = point;
            flight.lastSeconds = seconds;
            if (SDK::IEntity *stone = KCDC::Common::ResolveEntity(flight.stoneId)) {
                const glm::vec3 velocity = Shared::Game::Siege::VelocityAt(flight.shot.velocity, seconds);
                const glm::vec3 axis     = glm::cross(velocity, glm::vec3(0.0F, 0.0F, 1.0F));
                const glm::quat tumble   = glm::dot(axis, axis) > 1e-6F ? glm::angleAxis(seconds * kTumbleRate, glm::normalize(axis)) : glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
                stone->SetPosRotScale(KCDC::Common::ToNative(point), KCDC::Common::ToNative(tumble), SDK::Vec3 {1.0F, 1.0F, 1.0F});
            }

            // A ball going past: heard as it goes by, not at the gun.
            const char *whiz = Shared::Game::SiegeEngineAssetAt(flight.shot.assetIndex)->whizSound;
            if (!flight.whizzed && whiz[0] != '\0' && listener && seconds > kWhizAfterSecs && glm::distance(*listener, point) < kWhizDistance) {
                flight.whizzed = true;
                KCDC::Features::Audio::AudioService::PlayAt(whiz, point);
            }
        }

        // Stones that have lain long enough are cleared away.
        _flights.erase(std::remove_if(_flights.begin(), _flights.end(), [now](const Flight &flight) {
            if (flight.landed && now >= flight.clearAtMs) {
                KCDC::Core::gApplication->GetPropService().QueueDespawn(flight.stoneId);
                return true;
            }
            return false;
        }), _flights.end());
    }

    void SiegeModule::HurtLocalPlayer(const Shared::RPC::SiegeImpact &impact) {
        SDK::IEntity *player             = KCDC::Common::LocalPlayer();
        SDK::wh::rpgmodule::C_Soul *soul = KCDC::Common::LocalSoul();
        if (player == nullptr || soul == nullptr || soul->IsDead()) {
            return;
        }
        const float distance = glm::distance(KCDC::Common::ToGlm(player->GetWorldPos()), impact.point);
        const float share    = Shared::Game::Siege::DamageFalloff(distance, impact.damageRadius);
        if (share <= 0.0F || impact.damage <= 0.0F) {
            return;
        }
        auto *combat = soul->GetCombatSoul();
        if (combat == nullptr) {
            return;
        }

        // The game's own damage, with the player who fired as the attacker,
        // so the local damage hook reports it to the server as a blow from
        // them. Stamina first, as the binding's descriptor reads; the default
        // body part, as the binding builds when none is named.
        const SDK::wh::entitymodule::C_BodyPartData bodyPart;
        combat->TakeDamage(0.0F, impact.damage * share, SoulOfPlayer(impact.attackerNetworkId), false, bodyPart);
        Framework::Logging::GetLogger(kLogger)->info("Shot {} hurt the local player for {:.1f} at {:.1f} m", impact.shotId, impact.damage * share, distance);
    }

    void SiegeModule::Settle(const Shared::RPC::SiegeImpact &impact, std::uint64_t now) {
        if (Flight *flight = FindFlight(impact.shotId); flight != nullptr && !flight->landed) {
            Land(*flight, impact.point, Shared::Game::Siege::kMaxFlightSeconds, now);
        }

        // Also for a stone this machine saw land somewhere else, or never saw
        // at all: the impact is shown where it really was, once.
        ShowImpact(impact.shotId, impact.assetIndex, impact.point, impact.struckEngine);
        HurtLocalPlayer(impact);
    }

    void SiegeModule::DrainInbox(std::uint64_t now) {
        std::vector<Shared::RPC::SiegeShot> shots;
        std::vector<Shared::RPC::SiegeImpact> impacts;
        {
            std::scoped_lock lock(_inboxMutex);
            shots.swap(_shotInbox);
            impacts.swap(_impactInbox);
        }
        for (const Shared::RPC::SiegeShot &shot : shots) {
            // Already flying from the engine's own copy: the message only says
            // whether this machine is the one to report where it comes down.
            if (Flight *flight = FindFlight(shot.shotId)) {
                flight->shot.report = flight->shot.report || shot.report;
                if (shot.report && flight->landed) {
                    ReportImpact(shot.shotId, flight->landedAt);
                }
                continue;
            }
            if (!HasLaunched(shot.shotId)) {
                Launch(shot, now);
            }
        }
        for (const Shared::RPC::SiegeImpact &impact : impacts) {
            Settle(impact, now);
        }
    }

    void SiegeModule::Update() {
        if (!_rpcsInstalled) {
            InstallRPCs();
        }

        // The level took every entity with it: nothing native is touched.
        // The role is left whole -- hints, keys and the beam's ghost, which
        // the ghost service has already dropped -- so UpdateRole takes it up
        // again against the new level's action map.
        if (_nativeContextLost.exchange(false)) {
            ClearFlights(false);
            LeaveRole();
            if (auto *replication = Framework::CoreModules::GetReplication()) {
                replication->ForEach<SiegeEngine>([](SiegeEngine *engine) {
                    engine->Unbind();
                });
            }
        }

        const std::uint64_t now  = KCDC::Common::NowMs();
        const float frameSeconds = _lastFrameMs != 0 ? std::clamp(static_cast<float>(now - std::min(now, _lastFrameMs)) / 1000.0F, 0.0F, 0.25F) : 0.0F;
        _lastFrameMs             = now;

        std::int64_t offset         = 0;
        const bool serverClockKnown = KCDC::Common::ServerClockDifferential(offset);

        DrainInbox(now);
        LaunchDueShots(now, offset, serverClockKnown);

        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<SiegeEngine>([&](SiegeEngine *engine) {
                engine->Frame(now, offset, serverClockKnown, frameSeconds);
            });
        }

        Fly(now);
        PlayDueSounds(now);
        UpdateRole(now, offset, serverClockKnown);
        UpdatePrompt(now);
    }
} // namespace KCDC::Features::Siege
