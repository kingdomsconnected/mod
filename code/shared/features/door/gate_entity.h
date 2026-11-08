#pragma once

#include "shared/features/player/player_entity.h"
#include "shared/game/gate_catalog.h"

#include <mafianet/virtual_world.h>
#include <networking/replication/network_entity.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace KCDC::Shared::Entities {
    /** Where a gate is in its cycle. Open always means passable. */
    enum class GateState : std::uint8_t { Closed, Opening, Open, Closing };

    inline bool IsGateMoving(GateState state) {
        return state == GateState::Opening || state == GateState::Closing;
    }

    /** The settled state a motion runs into. */
    inline GateState GateStateAfter(GateState state) {
        return state == GateState::Opening ? GateState::Open : state == GateState::Closing ? GateState::Closed : state;
    }

    /** How open a gate is `phase` of the way through one motion, from its asset's curve. */
    inline float GateMotionOpenness(const Shared::Game::GateAsset &asset, bool opening, float phase) {
        const auto &curve    = opening ? asset.openCurve : asset.closeCurve;
        const float position = std::clamp(phase, 0.0F, 1.0F) * static_cast<float>(Shared::Game::kGateCurveSamples - 1);
        const std::size_t at = std::min(static_cast<std::size_t>(position), Shared::Game::kGateCurveSamples - 2);
        return curve[at] + (curve[at + 1] - curve[at]) * (position - static_cast<float>(at));
    }

    /**
     * The first phase of a motion at which the gate stands `openness` open.
     *
     * What a motion reversed partway starts from: the other clip, at the
     * pose the gate is in. An opening motion looks for the first point at or
     * above it, a closing one at or below; a pose the clip never reaches
     * starts it from its end.
     */
    inline float GateMotionPhaseAt(const Shared::Game::GateAsset &asset, bool opening, float openness) {
        const auto &curve  = opening ? asset.openCurve : asset.closeCurve;
        const auto reached = [opening, openness](float value) {
            return opening ? value >= openness : value <= openness;
        };
        if (reached(curve[0])) {
            return 0.0F;
        }
        for (std::size_t at = 1; at < Shared::Game::kGateCurveSamples; ++at) {
            if (reached(curve[at])) {
                const float step   = curve[at] - curve[at - 1];
                const float weight = step != 0.0F ? std::clamp((openness - curve[at - 1]) / step, 0.0F, 1.0F) : 1.0F;
                return (static_cast<float>(at - 1) + weight) / static_cast<float>(Shared::Game::kGateCurveSamples - 1);
            }
        }
        return 1.0F;
    }

    /**
     * One animated gate -- a drawbridge or a portcullis -- as the server knows it.
     *
     * The server builds one for every gate the level places, from the compiled
     * gate catalog and nothing else: the asset, where it stands and how long
     * each clip runs are all facts of the shipped data, so no client measures
     * or reports anything. It is identified by the level's own EntityGuid,
     * which is the same on every machine.
     *
     * Every client that streams one in brings the animated gate into its
     * world and hides the static stand-in the level shows there otherwise, so
     * a gate is always present and in sync, the way a door is.
     *
     * The motion is never streamed. A cycle is the state it heads for plus
     * the server clock it started on, and every client plays that out locally.
     * `state` is also the slower guarantee: a client that missed
     * the change still converges on the right pose.
     *
     * `cycleStartMs` is the server's `MafiaNet::GetTimeUS() / 1000`, converted
     * through the peer's clock differential. It is back-dated when a gate is
     * reversed mid-cycle, so the new motion starts from where the gate is.
     */
    class GateEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::Gate";

        /** Castle architecture is visible far further off than a door is. */
        static constexpr float kStreamRange = 400.0F;

        std::uint64_t gateGuid  = 0;
        std::uint32_t levelHash = 0;

        /** Index into `Shared::Game::g_GateAssets`. Fixed for the gate's life. */
        std::uint16_t assetIndex = 0;

        /**
         * The virtual world this copy is, as the server set it: the global
         * one for the copy built at boot, else the one world a script or a
         * player there asked for. The replica base keeps its world on the
         * server only, and a player can hold several copies of one gate: the
         * global copy everyone sees, and every world's own copy when the
         * player is in the global world itself (Shared::Game::WorldCopyRank).
         */
        std::uint32_t copyWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL;

        /**
         * The level shows both gates open: each one's static stand-in is its
         * `_opened` mesh, the portcullis raised and the drawbridge lowered, and
         * both quests hold them that way until a sequence moves them.
         */
        GateState state = GateState::Open;

        /** Server clock at the start of the current motion, 0 when settled. */
        std::uint64_t cycleStartMs = 0;

        /** The asset this gate is, from the catalog every peer compiles. */
        const Shared::Game::GateAsset &Asset() const {
            return Shared::Game::g_GateAssets[assetIndex];
        }

        /** How long the motion now running takes, 0 when settled. */
        std::uint32_t CurrentCycleDurationMs() const {
            return state == GateState::Opening ? Asset().openDurationMs : state == GateState::Closing ? Asset().closeDurationMs : 0;
        }

        /** How far through the running motion the gate is `elapsedMs` after it started, 0 to 1. */
        float PhaseAfter(std::int64_t elapsedMs) const {
            const std::uint32_t duration = CurrentCycleDurationMs();
            return duration == 0 ? 1.0F : std::clamp(static_cast<float>(elapsedMs) / static_cast<float>(duration), 0.0F, 1.0F);
        }

        /** How open the gate is `elapsedMs` after its motion started, 0 shut to 1 open. */
        float OpennessAfter(std::int64_t elapsedMs) const {
            if (!IsGateMoving(state)) {
                return state == GateState::Open ? 1.0F : 0.0F;
            }
            return GateMotionOpenness(Asset(), state == GateState::Opening, PhaseAfter(elapsedMs));
        }

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(gateGuid);
            fields.Field(levelHash);
            fields.Field(assetIndex);
            fields.Field(copyWorld);
            SerializeCycle(fields);
            // Checked here, where it arrives, so nothing downstream indexes
            // the catalog with a value it does not hold.
            if (assetIndex >= Shared::Game::g_GateAssets.size()) {
                assetIndex = 0;
            }
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            SerializeCycle(fields);
        }

        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.FloatBits(position.x, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(position.y, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(position.z, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
        }

      private:
        void SerializeCycle(Framework::Networking::Replication::FieldSerializer &fields) {
            // Seeded from the current value: on the delta path an unchanged
            // field is not written back, so a bare local would read as Closed.
            std::uint8_t wireState = static_cast<std::uint8_t>(state);
            fields.Field(wireState);
            state = wireState <= static_cast<std::uint8_t>(GateState::Closing) ? static_cast<GateState>(wireState) : GateState::Open;
            fields.Field(cycleStartMs);
        }
    };
} // namespace KCDC::Shared::Entities
