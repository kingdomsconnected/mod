#pragma once

#include "player_entity.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace KCDC::Shared::Game {
    /**
     * A smithery tool a smith takes into a hand: the smithery's own entity,
     * linked to it under the tool's name.
     *
     * `C_Blacksmithing::Start` (0x180B674A4) builds one
     * `C_BlacksmithingSlotToolAttachment` per tool: "hammer" for hand 0 and
     * "tongs" for hand 1 (0x180B678C8, 0x180B67820). The attachment resolves
     * the smithery's link of that name (0x180B67B40) and moves its item into
     * that hand through `CompleteItemTransfer` (0x18193F148), whose hand 0 is
     * `entity_right` and hand 1 `entity_left` (0x1840AC298).
     */
    enum class StationTool : std::uint8_t {
        Hammer,
        Tongs
    };

    /** One tool event a smithing fragment's `AnimEvent` clip raises. */
    struct StationToolEvent {
        StationTool tool = StationTool::Hammer;
        bool attach      = false;

        bool operator==(const StationToolEvent &) const = default;
    };

    /**
     * The tool events among the names `ParseBlacksmithingAnimationEvent`
     * (0x180857548) accepts -- codes 3 to 6 -- and nothing else: the
     * workpiece's events move an entity the owner's recipe spawns, which an
     * observer does not have.
     */
    inline std::optional<StationToolEvent> ParseStationToolEvent(std::string_view name) {
        if (name == "AttachTongs") {
            return StationToolEvent {StationTool::Tongs, true};
        }
        if (name == "DetachTongs") {
            return StationToolEvent {StationTool::Tongs, false};
        }
        if (name == "AttachHammer") {
            return StationToolEvent {StationTool::Hammer, true};
        }
        if (name == "DetachHammer") {
            return StationToolEvent {StationTool::Hammer, false};
        }
        return std::nullopt;
    }

    /** The hand a tool is carried in, as `HumanEntity` numbers hands. */
    constexpr std::size_t StationToolHand(StationTool tool) {
        return tool == StationTool::Hammer ? KCDC::Shared::Entities::HumanEntity::kRightHand : KCDC::Shared::Entities::HumanEntity::kLeftHand;
    }

    /** The smithery's link name for a tool. */
    constexpr const char *StationToolLink(StationTool tool) {
        return tool == StationTool::Hammer ? "hammer" : "tongs";
    }
} // namespace KCDC::Shared::Game
