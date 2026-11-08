#include "patrol_preview.h"
#include "core/application.h"
#include "features/door/door_service.h"
#include "features/navigation/navmesh.h"
#include "features/npc/npc.h"
#include "features/npc/npc_order.h"
#include "shared/game/clothing_presets.h"
#include "shared/game/cry_guid_text.h"
#include "shared/game/npc_roles.h"
#include <filesystem>
#include <future>
#include <utils/safe_win32.h>

namespace KCDC::Features::Patrol {
    struct PatrolPreview::State {
        struct Loaded {
            std::unique_ptr<Navigation::Navmesh> mesh;
            std::string error;
        };
        std::future<Loaded> loading;
        std::unique_ptr<Navigation::Navmesh> mesh;
        std::string meshLevel;
        std::string requestedLevel;
        std::string loadingLevel;
        std::optional<Shared::Game::PatrolRouteDefinition> pending;
        std::string role;
        std::unique_ptr<Npc::Npc> npc;
        Npc::NpcOrder order;
        std::string status;
        std::vector<glm::vec3> path;
        MafiaNet::Time lastReady                                        = 0;
        fu2::function<bool(const Navigation::EntityLink &) const> doors = [this](const Navigation::EntityLink &link) {
            if (link.kind != Navigation::TileCodec::kDoorLinkKind) {
                return true;
            }
            return npc && npc->CanOpenDoors(); // Preview can unlock local doors on approach.
        };

        Npc::NpcOrderContext Context() const {
            Npc::NpcOrderContext context;
            context.now            = MafiaNet::GetTime();
            context.position       = npc->position;
            context.hasSimulator   = true;
            context.meshReady      = mesh && mesh->IsReady();
            context.reportStatus   = npc->intentStatus;
            context.reportRevision = npc->statusRevision;
            context.intentRevision = npc->intentRevision;
            context.doorPassage    = [this](std::uint64_t guid) { return Core::gApplication->GetDoorService().PreparePreviewPassage(guid, npc->GetEntityId()); };
            return context;
        }
        void Publish() {
            npc->intent         = order.SimulatorIntent(npc->position);
            npc->intentRevision = Shared::Entities::NpcEntity::AdvanceRevision(npc->intentRevision);
            order.Published(npc->intentRevision);
        }
        void Load() {
            wchar_t executable[32768];
            const auto length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
            if (!length || length == std::size(executable)) {
                status = "Could not locate the game's navigation mesh.";
                pending.reset();
                return;
            }
            const auto root = std::filesystem::path(executable).parent_path().parent_path().parent_path();
            const auto pak  = root / "Data" / "Levels" / requestedLevel / "recast.pak";
            loadingLevel    = requestedLevel;
            loading         = std::async(std::launch::async, [pak, level = loadingLevel] {
                Loaded result;
                try {
                    result.mesh = std::make_unique<Navigation::Navmesh>();
                    if (!result.mesh->LoadPak(pak.string(), level, result.error)) {
                        result.mesh.reset();
                    }
                }
                catch (const std::exception &error) {
                    result.mesh.reset();
                    result.error = error.what();
                }
                return result;
            });
            status          = "Loading the level's navigation mesh...";
        }
    };

    PatrolPreview::PatrolPreview(): _state(std::make_unique<State>()) {}
    PatrolPreview::~PatrolPreview() {
        Stop();
    }
    bool PatrolPreview::Active() const {
        return _state->pending.has_value() || _state->npc != nullptr;
    }
    nlohmann::json PatrolPreview::Progress() const {
        return _state->npc ? _state->order.PatrolState() : nlohmann::json(nullptr);
    }
    const std::string &PatrolPreview::Status() const {
        return _state->status;
    }
    const std::vector<glm::vec3> &PatrolPreview::Path() const {
        return _state->path;
    }

    void PatrolPreview::Stop(bool worldGone) {
        _state->pending.reset();
        if (_state->npc) {
            _state->npc->StopLocalPreview(worldGone);
        }
        _state->npc.reset();
        _state->order = {};
        _state->path.clear();
        _state->status = "Preview stopped.";
        if (worldGone) {
            _state->mesh.reset();
            _state->meshLevel.clear();
        }
    }

    void PatrolPreview::Start(const Shared::Game::PatrolRouteDefinition &route, const std::string &role, const std::string &level) {
        Stop();
        if (!route.IsSane() || !Shared::Game::FindNpcRole(role)) {
            _state->status = "Choose a valid route and preview NPC.";
            return;
        }
        _state->pending        = route;
        _state->role           = role;
        _state->requestedLevel = level;
        if (!_state->loading.valid() && (!_state->mesh || _state->meshLevel != level)) {
            _state->Load();
        }
    }

    void PatrolPreview::Update() {
        auto &state = *_state;
        if (state.loading.valid() && state.loading.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto loaded     = state.loading.get();
            state.mesh      = std::move(loaded.mesh);
            state.meshLevel = state.loadingLevel;
            if (state.pending && state.requestedLevel == state.meshLevel && !state.mesh) {
                state.pending.reset();
                state.status = "Preview cannot load navigation: " + loaded.error;
            }
        }
        if (state.pending && !state.loading.valid()) {
            if (!state.mesh || state.meshLevel != state.requestedLevel) {
                state.Load();
                return;
            }
            const auto *role      = Shared::Game::FindNpcRole(state.role);
            state.npc             = std::make_unique<Npc::Npc>(true);
            state.npc->soulGuid   = role->soulGuid;
            state.npc->actorClass = role->actorClass;
            if (const auto *outfit = Shared::Game::FindClothingPreset(role->clothingPresetGuid)) {
                state.npc->equipment.reserve(outfit->itemGuids.size() * 32);
                for (const auto guid : outfit->itemGuids) {
                    std::uint64_t low = 0, high = 0;
                    if (!Shared::Game::CryGuidFromText(guid, low, high)) {
                        Stop();
                        state.status = "The preview NPC has an invalid outfit.";
                        return;
                    }
                    Shared::RPC::ItemClassId {low, high}.AppendPackedText(state.npc->equipment);
                }
            }
            state.npc->position       = state.pending->points.front().position;
            state.npc->transformReady = true;
            state.npc->flags          = Shared::Game::NpcFlags::Invulnerable;
            state.order.Patrol(*state.pending, Npc::Pathfinding::Server, state.Context());
            state.pending.reset();
            state.lastReady = MafiaNet::GetTime();
            state.status  = "Spawning preview NPC...";
        }
        if (!state.npc) {
            return;
        }
        state.npc->Frame();
        const bool ready = state.npc->PreviewReady();
        if (state.npc->PreviewSpawnFailed() || (!ready && MafiaNet::GetTime() - state.lastReady > 15000)) {
            Stop();
            state.status = "The preview NPC could not spawn. Try another NPC or position.";
            return;
        }
        if (!ready) {
            return;
        }
        state.lastReady     = MafiaNet::GetTime();
        const auto context  = state.Context();
        const auto observed = state.order.Observe(context);
        const auto advanced = state.order.Advance(context, state.npc->position);
        if (state.order.NeedsPlan()) {
            Navigation::QueryOptions options;
            options.searchHeight   = state.order.Navigation().SearchHeight();
            options.maxSearchNodes = Npc::NpcOrders::kSearchNodes;
            options.linkGate       = &state.doors;
            state.order.Plan(*state.mesh, options, state.npc->position);
            state.path = state.order.Navigation().Path();
            state.order.Advance(context, state.npc->position);
            state.Publish();
        }
        else if (observed.republish || advanced.republish) {
            state.Publish();
        }
        for (const auto &event : state.order.TakePatrolEvents()) {
            if (event["finished"] == true) {
                const std::string outcome = event["status"];
                Stop();
                state.status = "Preview " + outcome + ".";
                return;
            }
            if (event["status"] != "reached") {
                state.status = "Waypoint " + std::to_string(event["waypointIndex"].get<int>() + 1) + ": " + event["status"].get<std::string>();
            }
        }
        if (observed.announce && *observed.announce == Shared::Game::NpcIntentStatus::Reached) {
            state.status = "Preview running.";
        }
    }
} // namespace KCDC::Features::Patrol
