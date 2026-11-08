#pragma once

#include "shared/features/blip/blip_entity.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <mafianet/types.h>
#include <optional>
#include <string>
#include <vector>

namespace KCDC::Features::Blip {
    /** Everything a blip is drawn from, as a script or a command asks for it. */
    struct BlipLook {
        std::uint8_t icon  = Shared::Game::kDefaultBlipIcon;
        std::int32_t state = 0;
        std::string label;
        bool compass = true;
        bool map     = true;
    };

    /**
     * What the server can do with blips, in one place, so a script and a
     * command place one the same way.
     */
    class Blips {
      public:
        /** Every replicated blip, optionally narrowed to one virtual world. */
        static std::vector<Shared::Entities::BlipEntity *> Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld);

        /**
         * Creates a blip, or null when the position or the icon is wrong.
         * `viewer` makes it private to one connection from its first update;
         * the unassigned GUID shows it to the whole virtual world.
         */
        static Shared::Entities::BlipEntity *Place(const glm::vec3 &position, const BlipLook &look, std::uint32_t virtualWorld, MafiaNet::PeerGuid viewer);

        /** Restyles a live blip. An icon the game cannot draw leaves it as it was. */
        static void SetLook(Shared::Entities::BlipEntity *blip, const BlipLook &look);

        /** The label as it goes on the wire: cut to the entity's limit without splitting a character. */
        static std::string BoundLabel(const std::string &label);

        static void Destroy(Shared::Entities::BlipEntity *blip);
        static std::size_t DestroyAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld);
    };
} // namespace KCDC::Features::Blip
