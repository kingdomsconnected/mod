#include "prop_editor_figure.h"

// First: these bring the networking headers, which must precede anything that includes <windows.h>.
#include "core/application.h"
#include "features/npc/npc.h"

#include "common/module_support.h"
#include "common/vector_math.h"
#include "shared/game/clothing_presets.h"
#include "shared/game/cry_guid_text.h"
#include "shared/game/npc_roles.h"

#include <mafianet/get_time.h>

namespace KCDC::Features::Prop {
    namespace {
        /** A body that has not stood up in this long is not going to. */
        constexpr MafiaNet::Time kFigureSpawnTimeoutMs = 15000;
    } // namespace

    struct EditorFigure::State {
        std::unique_ptr<Npc::Npc> npc;
        std::string role;
        SDK::Vec3 position {};
        float yawDegrees       = 0.0F;
        std::string status     = "No figure is standing.";
        MafiaNet::Time askedAt = 0;
        bool announced         = false;
    };

    EditorFigure::EditorFigure(): _state(std::make_unique<State>()) {}

    EditorFigure::~EditorFigure() {
        Remove();
    }

    bool EditorFigure::Active() const {
        return _state->npc != nullptr;
    }

    bool EditorFigure::Standing() const {
        return _state->npc != nullptr && _state->npc->PreviewReady();
    }

    const std::string &EditorFigure::Role() const {
        return _state->role;
    }

    const SDK::Vec3 &EditorFigure::Position() const {
        return _state->position;
    }

    float EditorFigure::YawDegrees() const {
        return _state->yawDegrees;
    }

    const std::string &EditorFigure::Status() const {
        return _state->status;
    }

    void EditorFigure::Remove(bool worldGone) {
        State &state = *_state;
        if (state.npc != nullptr) {
            state.npc->StopLocalPreview(worldGone);
            state.npc.reset();
            state.status = "The figure was taken away.";
        }
        state.announced = false;
    }

    bool EditorFigure::Place(const std::string &role, const SDK::Vec3 &position, float yawDegrees) {
        State &state = *_state;
        Remove();

        const Shared::Game::NpcRoleEntry *entry = Shared::Game::FindNpcRole(role);
        if (entry == nullptr || !KCDC::Common::IsFinite(position) || !std::isfinite(yawDegrees)) {
            state.status = "Choose a figure and a place to stand it.";
            return false;
        }

        auto npc        = std::make_unique<Npc::Npc>(true);
        npc->soulGuid   = entry->soulGuid;
        npc->actorClass = entry->actorClass;
        if (const Shared::Game::ClothingPresetEntry *outfit = Shared::Game::FindClothingPreset(entry->clothingPresetGuid)) {
            npc->equipment.reserve(outfit->itemGuids.size() * 32);
            for (const auto guid : outfit->itemGuids) {
                std::uint64_t low  = 0;
                std::uint64_t high = 0;
                if (!Shared::Game::CryGuidFromText(guid, low, high)) {
                    state.status = "That figure has an outfit the game cannot dress.";
                    return false;
                }
                Shared::RPC::ItemClassId {low, high}.AppendPackedText(npc->equipment);
            }
        }
        npc->position       = KCDC::Common::ToGlm(position);
        npc->rotation       = KCDC::Common::ToGlm(FigureRotation(yawDegrees));
        npc->transformReady = true;
        // The default intent is Hold: it stands where it was put, facing where it faces.
        npc->flags = Shared::Game::NpcFlags::Invulnerable;

        state.npc        = std::move(npc);
        state.role       = role;
        state.position   = position;
        state.yawDegrees = yawDegrees;
        state.askedAt    = MafiaNet::GetTime();
        state.announced  = false;
        state.status     = "Standing the figure up...";
        return true;
    }

    void EditorFigure::Update() {
        State &state = *_state;
        if (state.npc == nullptr) {
            return;
        }
        state.npc->Frame();

        const bool ready = state.npc->PreviewReady();
        if (state.npc->PreviewSpawnFailed() || (!ready && MafiaNet::GetTime() - state.askedAt > kFigureSpawnTimeoutMs)) {
            Remove();
            state.status = "The figure could not stand there. Try another figure or another place.";
            return;
        }
        if (ready) {
            state.askedAt = MafiaNet::GetTime();
            if (!state.announced) {
                state.announced = true;
                state.status    = "The figure is standing: " + state.role + ".";
            }
        }
    }
} // namespace KCDC::Features::Prop
