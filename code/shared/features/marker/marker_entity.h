#pragma once

#include "shared/features/marker/marker_shape.h"
#include "shared/features/player/player_entity.h"
#include "shared/game/marker_catalog.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <networking/replication/network_entity.h>

namespace KCDC::Shared::Entities {
    /**
     * A marker the server put in the world and left there.
     *
     * A replicated entity so the interest grid streams it like a prop: a zone
     * has to be there for whoever walks up to it, which a one-shot message
     * cannot do. Removing one is destroying the entity.
     *
     * `spin` and `bob` are played on each client's own clock rather than
     * replicated per frame. Nothing depends on two clients agreeing where in
     * its bob a marker is.
     */
    class MarkerEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::Marker";

        static constexpr std::uint16_t kNoMaterial = 0xFFFFU;

        static constexpr float kMinSize   = 0.25F;
        static constexpr float kMaxSize   = 64.0F;
        static constexpr float kMinHeight = 0.25F;
        static constexpr float kMaxHeight = 64.0F;

        /** Degrees per second. Past a couple of turns a second a marker reads as a flicker. */
        static constexpr float kMaxSpin = 720.0F;

        static constexpr float kMaxBob = 4.0F;

        static constexpr float kMinStreamRange = 50.0F;
        static constexpr float kMaxStreamRange = 400.0F;

        /**
         * Derived from the marker's extent rather than declared: neither a
         * decal nor a primitive has an authored view distance the way a
         * particle effect does, and what decides when it is worth streaming is
         * how much of the screen it covers.
         */
        static float StreamRangeFor(float size, float height) {
            return std::clamp(std::max(size, height) * 12.0F, kMinStreamRange, kMaxStreamRange);
        }

        static bool IsSaneSize(float size) {
            return std::isfinite(size) && size >= kMinSize && size <= kMaxSize;
        }

        static bool IsSaneHeight(float height) {
            return std::isfinite(height) && height >= kMinHeight && height <= kMaxHeight;
        }

        static bool IsSaneMotion(float spin, float bob) {
            return std::isfinite(spin) && std::abs(spin) <= kMaxSpin && std::isfinite(bob) && bob >= 0.0F && bob <= kMaxBob;
        }

        /** Row of `Shared::Game::g_MarkerMaterials`, so no material name crosses the wire. */
        std::uint16_t materialIndex = kNoMaterial;

        /** A `MarkerShape`, kept as its underlying type so the serializer takes it directly. */
        std::uint8_t shape = static_cast<std::uint8_t>(MarkerShape::Decal);

        /** Width in metres: a decal's projected width, a mesh's diameter. */
        float size = 4.0F;

        /** Height in metres. Ignored by `Decal`, `Sphere` and `Plane`. */
        float height = 1.0F;

        /** Degrees per second about the marker's own up axis; 0 stands still. */
        float spin = 0.0F;

        /** Metres the marker rises and falls from its resting height; 0 stands still. */
        float bob = 0.0F;

        /**
         * Packed 0xRRGGBBAA tint, or 0 for the material as it ships.
         *
         * The shipped material is shared by everything that names it, so a
         * tint is a private clone of it rather than a write to the original.
         */
        std::uint32_t color = 0;

        /**
         * Whether this marker carries an engine area that reports crossings.
         *
         * Opt-in: most markers are decoration, and one nobody listens to
         * should cost nothing but its draw.
         */
        bool trigger = false;

        MarkerShape GetShape() const {
            return IsMarkerShape(shape) ? static_cast<MarkerShape>(shape) : MarkerShape::Decal;
        }

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            SerializeBuild(fields);
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            SerializeBuild(fields);
        }

        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.FloatBits(position.x, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(position.y, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(position.z, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(rotation.x, -1.0F, 1.0F, HumanEntity::kRotationBits);
            fields.FloatBits(rotation.y, -1.0F, 1.0F, HumanEntity::kRotationBits);
            fields.FloatBits(rotation.z, -1.0F, 1.0F, HumanEntity::kRotationBits);
            fields.FloatBits(rotation.w, -1.0F, 1.0F, HumanEntity::kRotationBits);
        }

      private:
        /** Everything a receiver has to build the marker again for. */
        void SerializeBuild(Framework::Networking::Replication::FieldSerializer &fields) {
            fields.Field(materialIndex);
            fields.Field(shape);
            fields.Field(size);
            fields.Field(height);
            fields.Field(spin);
            fields.Field(bob);
            fields.Field(color);
            fields.Field(trigger);
        }
    };
} // namespace KCDC::Shared::Entities
