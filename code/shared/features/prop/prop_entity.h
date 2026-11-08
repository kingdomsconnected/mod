#pragma once

#include "shared/features/player/player_entity.h"

#include <cstdint>
#include <networking/replication/delegation.h>
#include <networking/replication/network_entity.h>
#include <string_view>

namespace KCDC::Shared::Entities {
    /** Mirrors the client's `KCDC::Common::Spawn::PropPhysics` value for value. */
    enum class PropPhysics : std::uint8_t {
        Rigid,  // falls, collides, can be pushed around
        Static, // collides, never moves
        None,   // no physics proxy at all -- geometry only
    };

    inline bool IsPropPhysics(std::uint8_t value) {
        return value <= static_cast<std::uint8_t>(PropPhysics::None);
    }

    inline const char *PropPhysicsName(PropPhysics physics) {
        switch (physics) {
        case PropPhysics::Rigid: return "rigid";
        case PropPhysics::Static: return "static";
        case PropPhysics::None: return "none";
        }
        return "static";
    }

    inline bool ParsePropPhysics(std::string_view name, PropPhysics &physics) {
        if (name == "rigid") {
            physics = PropPhysics::Rigid;
            return true;
        }
        if (name == "static") {
            physics = PropPhysics::Static;
            return true;
        }
        if (name == "none") {
            physics = PropPhysics::None;
            return true;
        }
        return false;
    }

    /**
     * Server-created static mesh, spatially streamed and built by every
     * receiver from the shared prop catalog.
     *
     * `physics` and `scale` are on the state channel because a change to
     * either rebuilds the entity, and the model is construction-only because
     * nothing can change it at all.
     *
     * The physics kind has to: `bPhysicalize` and `bRigidBody` are read out of
     * the script table while `OnSpawn` runs and never again. The scale is less
     * certain -- stock CryEngine's `CEntityPhysics::OnEntityXForm` does push a
     * uniform scale onto the proxy through `pe_params_pos`, so this may be one
     * respawn that could become a `SetPosRotScale` -- but the map editor found
     * a rescaled object's collision staying behind, which is why
     * `EditorScene::Rebuild` exists, so a prop is rebuilt until someone
     * measures the uniform case in KCD2's own fork.
     *
     * A rigid prop is simulated by one client, the nearest, which
     * `DelegationManager` elects and makes its owner: that client runs the
     * body and streams its pose, and every other client follows it with a
     * copy that cannot move on its own. The owner writes the pose and the
     * rest state upstream; the server decides everything else, so the build
     * fields are `ServerField`s and reach the owner through the forced state.
     */
    class PropEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::Prop";

        /**
         * A floor, not the distance a prop is seen from.
         *
         * `InterestGrid::Rebuild` keeps an entity at `max(viewerRange,
         * entity range)`, and a viewer carries `HumanEntity::kStreamRange`,
         * so anything at or under 250m streams at 250m regardless.
         *
         * What the number does decide is `_maxEntityRange` -- the maximum over
         * every live entity, which is the radius the grid is *queried* at for
         * every viewer. Props are the one type a script can create without
         * bound, so a generous range here would widen that query for the whole
         * session and buy no extra visibility at all.
         */
        static constexpr float kStreamRange = 150.0F;

        /** The clamp the map editor already places its own objects under. */
        static constexpr float kMinScale = 0.01F;
        static constexpr float kMaxScale = 100.0F;

        static constexpr std::uint32_t kNoModel = 0xFFFFFFFFU;

        /** Row of `Shared::Game::g_PropModels`, so no catalog path crosses the wire. */
        std::uint32_t modelIndex = kNoModel;

        /**
         * A mesh the server streams -- `objects/kcdc/<resource>/...cgf` -- for a prop `modelIndex`
         * does not name; empty for a catalog one. Construction-only, like the index: the receiver
         * builds from it only once its own streamed assets carry the file.
         */
        std::string streamedModel;

        PropPhysics physics = PropPhysics::Static;
        float scale         = 1.0F;
        glm::vec3 axisScale {1.0F}; // Per-axis size for World Builder exports; multiplied by scale.

        /**
         * A rigid prop's mass in kilograms, given to the body as it is
         * physicalized; 0 leaves it to the mesh's collision. A collision that
         * carries none -- a tree's, which the level only ever stands still --
         * makes a body that takes no push and feels no gravity, so every
         * client builds a rigid prop that would be one as a static prop.
         */
        float mass = 0.0F;

        static constexpr float kMaxMass = 100000.0F;

        /**
         * Whether a rigid prop's body has stopped, with where, as its
         * simulator saw it. On the reliable state channel rather than the
         * transform one, which is unreliable and deduped, so the pose a body
         * stops in reaches every client even when its last transform does not.
         * A prop that is not rigid is always resting where it stands.
         */
        bool resting           = true;
        glm::vec3 restPosition = glm::vec3(0.0F);
        glm::quat restRotation = glm::identity<glm::quat>();

        /**
         * The server's own record of a rigid prop, never serialized: where a
         * reset puts it back -- where it was spawned -- and where it last
         * came to rest, which bounds how far the next rest may be from it, and
         * whether it was resting last tick, which is how `propRest` is raised
         * once per stop.
         */
        struct Motion {
            glm::vec3 homePosition = glm::vec3(0.0F);
            glm::quat homeRotation = glm::identity<glm::quat>();
            glm::vec3 anchor       = glm::vec3(0.0F);
            bool wasResting        = true;
        };

        Motion motion;

        /**
         * The nearest client within 60 m runs the body; it keeps it until it
         * is 90 m off. Every rigid prop is delegated, resting or not, so a
         * player who walks into one pushes the body the simulator runs.
         */
        static constexpr Framework::Networking::Replication::DelegationPolicy kDelegationPolicy {
            /* acquireRange */ 60.0F,
            /* releaseRange */ 90.0F,
            /* weight */ 1,
            /* graceMs */ 2000,
            /* requireSameVirtualWorld */ true,
        };

        const Framework::Networking::Replication::DelegationPolicy *GetDelegationPolicy() const override {
            return physics == PropPhysics::Rigid ? &kDelegationPolicy : nullptr;
        }

        // A simulator moves a prop; only the server removes one.
        bool CanOwnerDestroy() const override {
            return false;
        }

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(modelIndex);
            fields.Field(streamedModel);
            SerializeBuild(fields);
            SerializeRest(fields);
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            SerializeBuild(fields);
            SerializeRest(fields);
        }

        // What a script changes on a prop a client simulates: its build, and -- for a reset or a move -- its pose and rest.
        void SerializeForcedState(Framework::Networking::Replication::FieldSerializer &fields) override {
            Framework::Networking::Replication::NetworkEntity::SerializeForcedState(fields);
            SerializeBuild(fields);
            SerializeRest(fields);
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
        /** Everything a receiver has to build the entity again for. Only the server decides it, so a simulator never writes it back. */
        void SerializeBuild(Framework::Networking::Replication::FieldSerializer &fields) {
            // Seeded from the current value: on the delta path an unchanged field is not written back, so a bare local would read as Rigid.
            std::uint8_t wirePhysics = static_cast<std::uint8_t>(physics);
            fields.ServerField(wirePhysics);
            physics = IsPropPhysics(wirePhysics) ? static_cast<PropPhysics>(wirePhysics) : PropPhysics::Static;
            fields.ServerField(scale);
            fields.ServerField(axisScale.x);
            fields.ServerField(axisScale.y);
            fields.ServerField(axisScale.z);
            fields.ServerField(mass);
        }

        void SerializeRest(Framework::Networking::Replication::FieldSerializer &fields) {
            fields.Field(resting);
            fields.Field(restPosition.x);
            fields.Field(restPosition.y);
            fields.Field(restPosition.z);
            fields.Field(restRotation.x);
            fields.Field(restRotation.y);
            fields.Field(restRotation.z);
            fields.Field(restRotation.w);
        }
    };
} // namespace KCDC::Shared::Entities
