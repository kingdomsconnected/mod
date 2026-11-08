#pragma once

#include <networking/replication/network_entity.h>
#include <networking/rpc/rpc_identifier.h>

#include <cstdint>

namespace KCDC::Shared::Game {
    enum class StationActivity : std::uint8_t {
        None,
        Dice,
        Smithing,
        Alchemy,
        // At a grindstone; `guid` is the wheel's. Women cannot use one
        // (Grindstone.lua IsUsable), so no female body ever reports it.
        Sharpening
    };

    // Physical occupancy, independent of a recipe's inventory transaction.
    // A new observer receives this held state and starts its loop immediately.
    struct StationState {
        StationActivity activity = StationActivity::None;
        std::uint64_t guid       = 0;
        std::uint32_t revision   = 0;
        std::uint32_t match      = 0;
        std::uint8_t slot        = 0;
        bool working             = false;
        // Nonzero only while a native dice throw is running. Distinguishes
        // consecutive throws even if the idle between them was not sampled.
        std::uint32_t action = 0;

        bool operator==(const StationState &) const = default;
        bool Active() const {
            return activity != StationActivity::None;
        }
        bool IsSane() const {
            if (activity == StationActivity::None) {
                return guid == 0 && match == 0 && slot == 0 && !working && action == 0;
            }
            if (guid == 0) {
                return false;
            }
            if (activity == StationActivity::Dice) {
                return match != 0 && slot < 2 && !working;
            }
            if (activity == StationActivity::Sharpening) {
                return match == 0 && slot == 0 && !working && action == 0;
            }
            return (activity == StationActivity::Smithing || activity == StationActivity::Alchemy) && match == 0 && slot == 0 && action == 0;
        }
        bool SamePose(const StationState &other) const {
            return activity == other.activity && guid == other.guid && match == other.match && slot == other.slot && working == other.working && action == other.action;
        }
        static bool Newer(std::uint32_t next, std::uint32_t previous) {
            return next != previous && static_cast<std::uint32_t>(next - previous) < 0x80000000U;
        }
        static std::uint32_t Advance(std::uint32_t value) {
            return value + 1 != 0 ? value + 1 : 1;
        }
        void Serialize(Framework::Networking::Replication::FieldSerializer &fields) {
            auto kind = static_cast<std::uint8_t>(activity);
            fields.Field(kind);
            fields.Field(guid);
            fields.Field(revision);
            fields.Field(match);
            fields.Field(slot);
            fields.Field(working);
            fields.Field(action);
            if (!fields.Writing()) {
                activity = static_cast<StationActivity>(kind);
                if (!IsSane()) {
                    *this = {};
                }
            }
        }
    };
} // namespace KCDC::Shared::Game

namespace KCDC::Shared::RPC {
    struct PlayerStation {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::PlayerStation");
        Game::StationState state;
        std::uint32_t epoch       = 0;
        std::uint32_t incarnation = 0;
        bool valid                = true;
        void Serialize(MafiaNet::BitStream *bs, bool write) {
            auto kind = static_cast<std::uint8_t>(state.activity);
            valid     = bs->Serialize(write, epoch) && bs->Serialize(write, incarnation) && bs->Serialize(write, state.revision) && bs->Serialize(write, kind) && bs->Serialize(write, state.guid) && bs->Serialize(write, state.match) && bs->Serialize(write, state.slot)
                    && bs->Serialize(write, state.working) && bs->Serialize(write, state.action);
            if (!write) {
                state.activity = static_cast<Game::StationActivity>(kind);
            }
        }
        bool IsSane() const {
            return valid && state.revision != 0 && state.IsSane();
        }
    };
} // namespace KCDC::Shared::RPC
