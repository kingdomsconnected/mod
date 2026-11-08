#pragma once

#include <string_view>

namespace KCDC::Shared::Game {
    /**
     * One of a prefab entity's `Properties`, as the prefab spells it. A key
     * inside a nested table is written `Table.key` -- `Bed.esReadingQuality`,
     * `Click.Angle.fAngleTolerance` -- for that table of the entity's own
     * `Properties`.
     */
    struct PrefabProperty {
        std::string_view key;
        std::string_view value;
    };
} // namespace KCDC::Shared::Game
