#pragma once

#include "shared/features/marker/marker_entity.h"
#include "shared/features/marker/marker_shape.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <mafianet/types.h>
#include <optional>
#include <vector>

namespace KCDC::Features::Marker {
    /**
     * What the server can do with markers, in one place, so a script and a
     * command place one the same way and raise the same events.
     *
     * There is no one-shot counterpart the way particle effects have `burst`:
     * a marker that vanishes for anyone who was not watching is not a marker.
     */
    class Markers {
      public:
        /** Registers the crossing RPC. Call once at startup. */
        static void Install();

        /** Every replicated marker, optionally narrowed to one virtual world. */
        static std::vector<Shared::Entities::MarkerEntity *> Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld);

        /** Creates a replicated marker, or null when the row, the shape, the pose or the extent is wrong. */
        static Shared::Entities::MarkerEntity *Place(std::size_t materialIndex, Shared::Entities::MarkerShape shape, const glm::vec3 &position, const glm::quat &rotation, float size, float height, float spin, float bob, std::uint32_t color, bool trigger, std::uint32_t virtualWorld);

        /** Resizes a live marker. Not a rebuild on the clients, but it does move the streaming range. */
        static void SetExtent(Shared::Entities::MarkerEntity *marker, float size, float height);

        /** Retints a live marker, which rebuilds it: a material is read once. */
        static void SetColor(Shared::Entities::MarkerEntity *marker, std::uint32_t color);

        /** Changes how a live marker moves. Ignored by the projected shape, which has nowhere to move to. */
        static void SetMotion(Shared::Entities::MarkerEntity *marker, float spin, float bob);

        static void Destroy(Shared::Entities::MarkerEntity *marker);
        static std::size_t DestroyAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld);
    };
} // namespace KCDC::Features::Marker
