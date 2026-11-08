#pragma once

#include "shared/features/player/player_entity.h"
#include "shared/game/blip_icon_catalog.h"

#include <cstddef>
#include <cstdint>
#include <mafianet/string.h>
#include <networking/replication/network_entity.h>
#include <string>

namespace KCDC::Shared::Entities {
    /**
     * A point the server put on players' compasses and map screens.
     *
     * A replicated entity for the same reason a marker is one -- whoever joins
     * later has to get it, which a one-shot message cannot do -- but streamed
     * the opposite way: a blip is for finding something far away, so it is
     * never culled by range. It reaches every player in its virtual world, or
     * exactly one player when it is private to them. Removing one is
     * destroying the entity.
     *
     * The client writes each one into the same registry its own scripts use,
     * so a server blip is drawn exactly like a local one.
     */
    class BlipEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::Blip";

        /** Map-screen names are one line under the cursor; past this they are cut. */
        static constexpr std::size_t kMaxLabelLength = 64;

        /** Row of `Shared::Game::kBlipIcons` by its type value, so no icon name crosses the wire. */
        std::uint8_t icon = Shared::Game::kDefaultBlipIcon;

        /** Forwarded to the compass movie as-is; 0 is what a fresh game mark carries. */
        std::int32_t state = 0;

        /** The blip's name on the map screen; empty keeps the name of its icon type. */
        std::string label;

        bool compass = true;
        bool map     = true;

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            SerializeLook(fields);
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            SerializeLook(fields);
        }

        /** The position alone: a blip has no facing and never moves on its own. */
        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.FloatBits(position.x, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(position.y, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(position.z, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
        }

      private:
        /** Everything a receiver draws the blip from. */
        void SerializeLook(Framework::Networking::Replication::FieldSerializer &fields) {
            fields.Field(icon);
            fields.Field(state);
            fields.Field(compass);
            fields.Field(map);

            MafiaNet::RakString wireLabel(label.c_str());
            fields.Field(wireLabel);
            if (!fields.Writing()) {
                label = wireLabel.C_String();
            }
        }
    };
} // namespace KCDC::Shared::Entities
