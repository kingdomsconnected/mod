#pragma once

#include "shared/streamed_assets/streamed_asset_policy.h"
#include "shared/game/vfx_catalog.h"

#include <networking/replication/network_entity.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace KCDC::Shared::Game {
    /**
     * The particle effect something plays: a row of `g_VfxEffects`, or an effect out of a library the
     * server streams, `kcdc_<resource>_<library>.<group>.<effect>`, by name.
     *
     * The catalog row keeps catalog effects off the wire as an index. A streamed effect has no row to
     * point at, so its name crosses instead; FindEffect loads its library by the part before the first
     * dot, like any of the game's.
     */
    struct VfxEffectRef {
        static constexpr std::uint16_t kNoIndex = 0xFFFFU;

        std::uint16_t index = kNoIndex;

        /** A streamed effect's name; empty for a catalog row. */
        std::string streamed;

        static VfxEffectRef Catalog(std::size_t row) {
            VfxEffectRef effect;
            effect.index = static_cast<std::uint16_t>(row);
            return effect;
        }

        static VfxEffectRef Streamed(std::string name) {
            VfxEffectRef effect;
            effect.streamed = std::move(name);
            return effect;
        }

        bool IsStreamed() const {
            return !streamed.empty();
        }

        /**
         * What the effect is: its catalog row, or for a streamed one an entry over its own name --
         * views into this object, so read them while it lives. A streamed library declares no culling
         * distance the server could read, so it gets none (the stream floor) and counts as continuous,
         * which a burst's duration bounds anyway. Nullopt for a row off the catalog or a name of the
         * wrong shape.
         */
        std::optional<VfxEffectEntry> Entry() const {
            if (streamed.empty()) {
                const VfxEffectEntry *row = VfxEffectAt(index);
                return row != nullptr ? std::optional<VfxEffectEntry>(*row) : std::nullopt;
            }
            if (!KCDC::Shared::StreamedAssets::Policy::IsStreamedEffectName(streamed)) {
                return std::nullopt;
            }
            const std::string_view name    = streamed;
            const std::size_t dot          = name.find('.');
            const std::string_view rest    = name.substr(dot + 1);
            const std::size_t groupEnd     = rest.find('.');
            const std::string_view library = name.substr(0, dot);
            const std::string_view group   = groupEnd == std::string_view::npos ? std::string_view {} : rest.substr(0, groupEnd);
            return VfxEffectEntry {name, library, group, 0.0F, true};
        }

        bool operator==(const VfxEffectRef &other) const = default;

        void Serialize(Framework::Networking::Replication::FieldSerializer &fields) {
            fields.Field(index);
            fields.Field(streamed);
        }
    };
} // namespace KCDC::Shared::Game
