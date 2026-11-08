#pragma once

#include "shared/features/player/player_entity.h"
#include "shared/features/siege/siege_wire.h"
#include "shared/game/siege_defence_catalog.h"

#include <networking/replication/network_entity.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace KCDC::Shared::Entities {
    /**
     * A pile of hurling stones a script put on a battlement.
     *
     * Each client builds the game's own pile from `g_StonePileLayout` -- the
     * `StoneThrowingPile`, its path node, its pick point and its nine stones
     * -- so the prompt, the grab, the throw over the wall and the stone's
     * flight are all the game's own stone throwing. `position` is where the
     * thrower stands and `rotation`'s forward the way they throw.
     *
     * The server keeps the count: `stones` is what is left to throw, and a
     * client shows at most nine of them, hiding the native prompt when there
     * are none or the pile is not `usable`. A throw itself is a one-off
     * message, replayed on the thrower's body everywhere else.
     */
    class StonePileEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::StonePile";

        static constexpr float kStreamRange = 200.0F;

        /** `stones` when the pile never runs out. */
        static constexpr std::int32_t kUnlimitedStones = -1;

        /** The most stones a pile shows: the heap the game lays out. */
        static constexpr std::int32_t kShownStones = static_cast<std::int32_t>(std::tuple_size_v<decltype(Shared::Game::StonePileLayout::stones)>);

        /**
         * How near the thrower must stand, in metres, for the server to take
         * a grab as theirs. The game's own check is 0.3-1.8 m from the pile
         * entity; this allows for the copy being a little behind.
         */
        static constexpr float kGrabReach = 3.0F;

        static constexpr float kMaxDamage       = 1000.0F;
        static constexpr float kMaxDamageRadius = 10.0F;

        /** What is left to throw, or `kUnlimitedStones`. */
        std::int32_t stones = kUnlimitedStones;

        /** Whether players are offered the pile at all. On by default: a pile is placed to be thrown from. */
        bool usable = true;

        /** Health taken at the point a stone comes down, and how far from it anything is hurt. */
        float damage       = 60.0F;
        float damageRadius = 1.5F;

        /** How many stones a client shows in its heap now. */
        int ShownStones() const {
            return usable ? (stones < 0 ? kShownStones : std::min(stones, kShownStones)) : 0;
        }

        bool HasStones() const {
            return stones != 0;
        }

        /** A point of the layout in the world. */
        glm::vec3 LayoutPoint(const Shared::Game::SiegePoint &at) const {
            return position + rotation * glm::vec3(at.x, at.y, at.z);
        }

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            SerializeState(fields);
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            SerializeState(fields);
        }

        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            SiegeFields::Transform(fields, position, rotation);
        }

      private:
        void SerializeState(Framework::Networking::Replication::FieldSerializer &fields) {
            fields.Field(stones);
            fields.Field(usable);
            fields.Field(damage);
            SiegeFields::Finite(damage, 0.0F);
            damage = std::clamp(damage, 0.0F, kMaxDamage);
            fields.Field(damageRadius);
            SiegeFields::Finite(damageRadius, 0.0F);
            damageRadius = std::clamp(damageRadius, 0.0F, kMaxDamageRadius);
        }
    };
} // namespace KCDC::Shared::Entities
