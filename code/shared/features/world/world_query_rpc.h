#pragma once

#include "shared/features/world/world_level_hash.h"

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Shared::RPC {
    /** One pair of RPCs carries all three questions: the round trip, the request book and the timeout are the same for each. */
    enum class WorldQueryKind : std::uint8_t {
        Raycast,
        Ground,
        Proximity
    };

    /**
     * Which of CryPhysics' object-type and ray-flag pairs a trace uses.
     *
     * One field rather than two on purpose: the pair is what makes a ray mean
     * something, and a mismatched one fails quietly -- `rwi_any_hit` at the
     * wrong pierceability clears a whole wall through one window. Only the
     * three combinations the game itself traces with can be named.
     * `SDK::CPhysicalWorld` carries the constants and the evidence.
     */
    enum class WorldRayMode : std::uint8_t {
        /** "Is the world in the way": static geometry, props and doors block; actors cannot. */
        Cover,
        /** "What is under this ray": everything a ray can hit, actors included. */
        Anything,
        /** "What could be stood on": the ground probe's own pair, which sees only walkable surfaces. */
        Ground
    };

    /**
     * What a trace met, as the collider it stopped on says. A tree or a rock
     * is not an entity, so for most of the level this, `model` and
     * `material` are all a hit has to say what it is.
     */
    enum class WorldHitKind : std::uint8_t {
        /** The heightmap. */
        Terrain,
        /** Anything the entity system owns: doors, props, NPCs, players, horses. */
        Entity,
        /** A vegetation instance: trees, bushes, plants, and the boulders a level paints as vegetation. */
        Vegetation,
        /** A placed static mesh: walls, houses, rocks and cliffs placed one by one. */
        Brush,
        /** Static geometry that is none of the above, or whose source could not be told. */
        Static
    };

    /** A receiver must never size an allocation from a number a peer chose. */
    inline constexpr std::size_t kMaxWorldQueryHits       = 8;
    inline constexpr std::size_t kMaxWorldQueryEntities   = 64;
    inline constexpr std::size_t kMaxWorldQueryTextLength = 64;
    /** Mesh and material names are paths into the game's data, longer than any entity name. */
    inline constexpr std::size_t kMaxWorldQueryPathLength = 256;

    /** A ray longer than the streamed world is a bug, not a question. */
    inline constexpr float kMaxWorldQueryRayLength = 4096.0F;

    /** Past this the proximity box covers more than a client has streamed in. */
    inline constexpr float kMaxWorldQueryRadius = 256.0F;

    /** How far above and below a ground probe may reach from the point it is given. */
    inline constexpr float kMaxWorldQueryProbe = 512.0F;

    namespace WorldQueryDetail {
        template <std::size_t Limit>
        bool SerializeBoundedString(MafiaNet::BitStream *bs, bool write, std::string &value) {
            if (write && value.size() > Limit) {
                return false;
            }

            std::uint16_t length = write ? static_cast<std::uint16_t>(value.size()) : 0;
            if (!bs->Serialize(write, length) || length > Limit) {
                return false;
            }
            if (!write) {
                value.assign(length, '\0');
            }
            return length == 0 || bs->Serialize(write, value.data(), length);
        }

        inline bool IsBoundedText(const std::string &value, std::size_t limit = kMaxWorldQueryTextLength) {
            return value.size() <= limit && std::all_of(value.begin(), value.end(), [](unsigned char character) {
                return character >= 0x20 && character != 0x7F;
            });
        }

        template <typename Value>
        bool SerializeByteEnum(MafiaNet::BitStream *bs, bool write, Value &value) {
            std::uint8_t wire = static_cast<std::uint8_t>(value);
            if (!bs->Serialize(write, wire)) {
                return false;
            }
            if (!write) {
                value = static_cast<Value>(wire);
            }
            return true;
        }

        inline bool IsKind(WorldQueryKind kind) {
            return kind >= WorldQueryKind::Raycast && kind <= WorldQueryKind::Proximity;
        }

        inline bool IsRayMode(WorldRayMode mode) {
            return mode >= WorldRayMode::Cover && mode <= WorldRayMode::Ground;
        }

        inline bool IsHitKind(WorldHitKind kind) {
            return kind >= WorldHitKind::Terrain && kind <= WorldHitKind::Static;
        }

        inline bool IsFinitePoint(float x, float y, float z) {
            return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
        }

        template <typename Element, std::size_t Limit>
        bool SerializeBoundedVector(MafiaNet::BitStream *bs, bool write, std::vector<Element> &values) {
            if (write && values.size() > Limit) {
                return false;
            }

            std::uint16_t count = write ? static_cast<std::uint16_t>(values.size()) : 0;
            if (!bs->Serialize(write, count) || count > Limit) {
                return false;
            }
            if (!write) {
                values.assign(count, Element {});
            }
            for (std::uint16_t index = 0; index < count; ++index) {
                if (!values[index].Serialize(bs, write)) {
                    return false;
                }
            }
            return true;
        }

        template <typename Element>
        bool AreSane(const std::vector<Element> &values) {
            return std::all_of(values.begin(), values.end(), [](const Element &value) {
                return value.IsSane();
            });
        }
    } // namespace WorldQueryDetail

    /**
     * What one trace struck.
     *
     * The engine's entity id is deliberately absent -- it is one machine's
     * handle. What travels is the level's own `EntityGuid`, zero for terrain,
     * static geometry and anything the session spawned.
     */
    struct WorldRayHit {
        std::string entityName;
        std::string entityClass;
        /** `mat_wood`, `mat_rock`, as the game's tables spell it. */
        std::string surface;
        /** The `.cgf` of a vegetation instance or a brush; empty for terrain and entities. */
        std::string model;
        /** The material drawn where the ray met it, the sub-material when it has several. */
        std::string material;
        std::uint64_t entityGuid = 0;
        float pointX             = 0.0F;
        float pointY             = 0.0F;
        float pointZ             = 0.0F;
        float normalX            = 0.0F;
        float normalY            = 0.0F;
        float normalZ            = 0.0F;
        float distance           = 0.0F;
        WorldHitKind kind        = WorldHitKind::Static;
        bool terrain             = false;

        /**
         * Whether the level placed what was hit, which a level edit can find
         * again: a brush or vegetation instance by `model` and `objectAt`, an
         * entity by `entityGuid`. The three below say where the level put it,
         * even while an edit has moved it, and only travel when this is set.
         */
        bool levelObject = false;
        std::array<float, 3> objectAt {};
        std::array<float, 4> objectRotation {0.0F, 0.0F, 0.0F, 1.0F};
        std::array<float, 3> objectScale {1.0F, 1.0F, 1.0F};

        bool Serialize(MafiaNet::BitStream *bs, bool write) {
            const bool common = WorldQueryDetail::SerializeBoundedString<kMaxWorldQueryTextLength>(bs, write, entityName) && WorldQueryDetail::SerializeBoundedString<kMaxWorldQueryTextLength>(bs, write, entityClass)
                                && WorldQueryDetail::SerializeBoundedString<kMaxWorldQueryTextLength>(bs, write, surface) && WorldQueryDetail::SerializeBoundedString<kMaxWorldQueryPathLength>(bs, write, model)
                                && WorldQueryDetail::SerializeBoundedString<kMaxWorldQueryPathLength>(bs, write, material) && WorldQueryDetail::SerializeByteEnum(bs, write, kind) && bs->Serialize(write, entityGuid) && bs->Serialize(write, pointX) && bs->Serialize(write, pointY)
                                && bs->Serialize(write, pointZ) && bs->Serialize(write, normalX) && bs->Serialize(write, normalY) && bs->Serialize(write, normalZ) && bs->Serialize(write, distance) && bs->Serialize(write, terrain) && bs->Serialize(write, levelObject);
            if (!common || !levelObject) {
                return common;
            }
            for (float &value : objectAt) {
                if (!bs->Serialize(write, value)) {
                    return false;
                }
            }
            for (float &value : objectRotation) {
                if (!bs->Serialize(write, value)) {
                    return false;
                }
            }
            for (float &value : objectScale) {
                if (!bs->Serialize(write, value)) {
                    return false;
                }
            }
            return true;
        }

        bool IsSane() const {
            if (!WorldQueryDetail::IsBoundedText(entityName) || !WorldQueryDetail::IsBoundedText(entityClass) || !WorldQueryDetail::IsBoundedText(surface)) {
                return false;
            }
            if (!WorldQueryDetail::IsBoundedText(model, kMaxWorldQueryPathLength) || !WorldQueryDetail::IsBoundedText(material, kMaxWorldQueryPathLength) || !WorldQueryDetail::IsHitKind(kind)) {
                return false;
            }
            if (levelObject) {
                const float rotationLength = objectRotation[0] * objectRotation[0] + objectRotation[1] * objectRotation[1] + objectRotation[2] * objectRotation[2] + objectRotation[3] * objectRotation[3];
                if (!WorldQueryDetail::IsFinitePoint(objectAt[0], objectAt[1], objectAt[2]) || !WorldQueryDetail::IsFinitePoint(objectScale[0], objectScale[1], objectScale[2]) || !std::isfinite(rotationLength) || rotationLength < 1e-6F) {
                    return false;
                }
            }
            return WorldQueryDetail::IsFinitePoint(pointX, pointY, pointZ) && WorldQueryDetail::IsFinitePoint(normalX, normalY, normalZ) && std::isfinite(distance) && distance >= 0.0F;
        }
    };

    /** One entity the proximity query reported, and where it stood when it did. */
    struct WorldNearbyEntity {
        std::string name;
        std::string entityClass;
        std::uint64_t entityGuid = 0;
        float positionX          = 0.0F;
        float positionY          = 0.0F;
        float positionZ          = 0.0F;
        float distance           = 0.0F;
        bool physicalized        = false;

        bool Serialize(MafiaNet::BitStream *bs, bool write) {
            return WorldQueryDetail::SerializeBoundedString<kMaxWorldQueryTextLength>(bs, write, name) && WorldQueryDetail::SerializeBoundedString<kMaxWorldQueryTextLength>(bs, write, entityClass) && bs->Serialize(write, entityGuid) && bs->Serialize(write, positionX)
                   && bs->Serialize(write, positionY) && bs->Serialize(write, positionZ) && bs->Serialize(write, distance) && bs->Serialize(write, physicalized);
        }

        bool IsSane() const {
            if (!WorldQueryDetail::IsBoundedText(name) || !WorldQueryDetail::IsBoundedText(entityClass)) {
                return false;
            }
            return WorldQueryDetail::IsFinitePoint(positionX, positionY, positionZ) && std::isfinite(distance) && distance >= 0.0F;
        }
    };

    /** Server -> client: ask that client's engine about the world. Same round trip as `DoorNearestQuery`, and it sees only what that machine has streamed in. */
    struct WorldQuery {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::WorldQuery");

        /** Proximity only; empty is every class. */
        std::string entityClass;
        std::uint64_t requestId = 0;
        /** The ray's start, the point the ground is wanted under, or the sphere's centre. */
        float originX = 0.0F;
        float originY = 0.0F;
        float originZ = 0.0F;
        /** Raycast only. */
        float targetX = 0.0F;
        float targetY = 0.0F;
        float targetZ = 0.0F;
        /** Proximity only, in metres. */
        float radius = 0.0F;
        /** Ground only. */
        float probeUp   = 0.0F;
        float probeDown = 0.0F;
        /** Hits along the ray, or entities in the sphere. */
        std::uint16_t maxResults = 1;
        WorldQueryKind kind      = WorldQueryKind::Raycast;
        WorldRayMode rayMode     = WorldRayMode::Cover;
        /** Proximity only. */
        bool physicalOnly = false;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            _complete = WorldQueryDetail::SerializeBoundedString<kMaxWorldQueryTextLength>(bs, write, entityClass) && bs->Serialize(write, requestId) && bs->Serialize(write, originX) && bs->Serialize(write, originY) && bs->Serialize(write, originZ)
                        && bs->Serialize(write, targetX) && bs->Serialize(write, targetY) && bs->Serialize(write, targetZ) && bs->Serialize(write, radius) && bs->Serialize(write, probeUp) && bs->Serialize(write, probeDown) && bs->Serialize(write, maxResults)
                        && WorldQueryDetail::SerializeByteEnum(bs, write, kind) && WorldQueryDetail::SerializeByteEnum(bs, write, rayMode) && bs->Serialize(write, physicalOnly);
        }

        bool IsSane() const {
            if (!_complete || requestId == 0 || !WorldQueryDetail::IsKind(kind) || !WorldQueryDetail::IsRayMode(rayMode) || !WorldQueryDetail::IsBoundedText(entityClass)) {
                return false;
            }
            if (!WorldQueryDetail::IsFinitePoint(originX, originY, originZ) || maxResults == 0) {
                return false;
            }

            switch (kind) {
            case WorldQueryKind::Raycast: {
                if (!WorldQueryDetail::IsFinitePoint(targetX, targetY, targetZ) || maxResults > kMaxWorldQueryHits) {
                    return false;
                }
                const float spanX         = targetX - originX;
                const float spanY         = targetY - originY;
                const float spanZ         = targetZ - originZ;
                const float lengthSquared = spanX * spanX + spanY * spanY + spanZ * spanZ;
                return lengthSquared > 0.0F && lengthSquared <= kMaxWorldQueryRayLength * kMaxWorldQueryRayLength;
            }
            case WorldQueryKind::Ground: return std::isfinite(probeUp) && std::isfinite(probeDown) && probeUp >= 0.0F && probeDown >= 0.0F && probeUp + probeDown > 0.0F && probeUp <= kMaxWorldQueryProbe && probeDown <= kMaxWorldQueryProbe;
            case WorldQueryKind::Proximity: return std::isfinite(radius) && radius > 0.0F && radius <= kMaxWorldQueryRadius && maxResults <= kMaxWorldQueryEntities;
            }
            return false;
        }

      private:
        bool _complete = true;
    };

    /** Client -> server: the answer. `answered` false means there was no world to look in, which is not the same as a query that found nothing. */
    struct WorldQueryAnswer {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::WorldQueryAnswer");

        std::vector<WorldRayHit> hits;
        std::vector<WorldNearbyEntity> entities;
        std::uint64_t requestId = 0;
        std::uint32_t levelHash = 0;
        WorldQueryKind kind     = WorldQueryKind::Raycast;
        bool answered           = false;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            _complete = WorldQueryDetail::SerializeBoundedVector<WorldRayHit, kMaxWorldQueryHits>(bs, write, hits) && WorldQueryDetail::SerializeBoundedVector<WorldNearbyEntity, kMaxWorldQueryEntities>(bs, write, entities) && bs->Serialize(write, requestId)
                        && bs->Serialize(write, levelHash) && WorldQueryDetail::SerializeByteEnum(bs, write, kind) && bs->Serialize(write, answered);
        }

        bool IsSane() const {
            if (!_complete || requestId == 0 || !WorldQueryDetail::IsKind(kind)) {
                return false;
            }
            if (!answered) {
                return hits.empty() && entities.empty();
            }
            if (levelHash == 0) {
                return false;
            }
            // A kind answers with its own list and never the other one.
            if (kind == WorldQueryKind::Proximity ? !hits.empty() : !entities.empty()) {
                return false;
            }
            return WorldQueryDetail::AreSane(hits) && WorldQueryDetail::AreSane(entities);
        }

      private:
        bool _complete = true;
    };

    /**
     * Client -> server: a swing of this player's met something solid that is
     * not a body -- a tree, a wall, a rock, a prop -- for the first time in
     * that attack. `hit` names it as a trace's hit does; its `distance` is
     * how far the contact lies from the player's feet. The client's word,
     * like every answer here.
     */
    struct WeaponHitReport {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::WeaponHitReport");

        WorldRayHit hit;
        std::uint32_t levelHash = 0;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            _complete = hit.Serialize(bs, write) && bs->Serialize(write, levelHash);
        }

        bool IsSane() const {
            return _complete && levelHash != 0 && hit.IsSane();
        }

      private:
        bool _complete = true;
    };
} // namespace KCDC::Shared::RPC
