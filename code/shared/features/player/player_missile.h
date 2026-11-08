#pragma once

#include "shared/features/player/player_soul_query.h"
#include "shared/item_stack.h"
#include "shared/pose_math.h"

#include <glm/glm.hpp>
#include <networking/rpc/rpc_identifier.h>

namespace KCDC::Shared::RPC {
    enum class MissilePhase : std::uint8_t {
        Idle,
        Charging,
        Reloading,
        Firing,
    };

    struct MissileItemProperties {
        std::uint32_t quality = 1;
        float health          = 1.0F; // Absolute item health.
        ItemClassId poisonBuff;       // WeaponBuffDefinitionId.
        std::uint32_t poisonCharges = 0;

        bool operator==(const MissileItemProperties &) const = default;

        bool IsSane() const {
            return quality >= 1 && quality <= 255 && std::isfinite(health) && health >= 0.0F && health <= 1.0F && poisonCharges <= 10000 && (poisonBuff.IsValid() || poisonCharges == 0);
        }

        bool Serialize(MafiaNet::BitStream *bs, bool write) {
            return bs->Serialize(write, quality) && bs->Serialize(write, health) && poisonBuff.Serialize(bs, write) && bs->Serialize(write, poisonCharges);
        }
    };

    struct MissileState {
        ItemClassId weapon;
        ItemClassId ammunition;
        ItemClassId loadedAmmunition;
        std::uint32_t ammunitionAmount = 0;
        MissilePhase phase             = MissilePhase::Idle;
        bool cocked                    = false;
        float reloadSpeed              = 1.0F; // Native animation multiplier.

        bool operator==(const MissileState &) const = default;

        bool IsSane() const {
            if (phase > MissilePhase::Firing || ammunitionAmount > kMaxInventoryStackAmount || !std::isfinite(reloadSpeed) || reloadSpeed <= 0.0F || reloadSpeed > 10.0F) {
                return false;
            }

            if (!weapon.IsValid()) {
                return !ammunition.IsValid() && !loadedAmmunition.IsValid() && ammunitionAmount == 0 && phase == MissilePhase::Idle && !cocked;
            }

            return ammunition.IsValid() || ammunitionAmount == 0;
        }

        template <typename Fields>
        void SerializeFields(Fields &fields) {
            fields.Field(weapon.low);
            fields.Field(weapon.high);
            fields.Field(ammunition.low);
            fields.Field(ammunition.high);
            fields.Field(loadedAmmunition.low);
            fields.Field(loadedAmmunition.high);
            fields.Field(ammunitionAmount);

            auto wirePhase = static_cast<std::uint8_t>(phase);

            fields.Field(wirePhase);

            if (!fields.Writing()) {
                phase = static_cast<MissilePhase>(wirePhase);
            }

            fields.Field(cocked);
            fields.Field(reloadSpeed);
        }
    };

    namespace MissileDetail {
        inline bool SerializeVector(MafiaNet::BitStream *bs, bool write, glm::vec3 &v) {
            return bs->Serialize(write, v.x) && bs->Serialize(write, v.y) && bs->Serialize(write, v.z);
        }

    } // namespace MissileDetail

    struct MissileLaunched {
        static constexpr auto kIdentifier         = FW_RPC_IDENTIFIER("KCDC::MissileLaunched");
        static constexpr std::uint64_t kLifetimeMs       = 30000;
        static constexpr std::size_t kMaxActivePerPlayer = 64;
        static constexpr float kMaxSpeed                 = 1000.0F;

        // Zero selects the sending player; NPC reports name their delegated body.
        std::uint32_t npcRevision = 0;
        // Sequence is unique per simulating client session.
        std::uint64_t shooterNetworkId = 0;
        std::uint64_t sequence         = 0;
        std::uint32_t incarnation      = 0;
        std::uint8_t epoch             = 0;
        ItemClassId weapon;
        ItemClassId ammunition;
        glm::vec3 position {0.0F};
        glm::vec3 direction {0.0F, 1.0F, 0.0F};
        glm::vec3 velocity {0.0F};
        MissileItemProperties weaponProperties;
        MissileItemProperties ammunitionProperties;
        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, npcRevision) && bs->Serialize(write, shooterNetworkId) && bs->Serialize(write, sequence) && bs->Serialize(write, incarnation) && bs->Serialize(write, epoch) && weapon.Serialize(bs, write) && ammunition.Serialize(bs, write)
                    && MissileDetail::SerializeVector(bs, write, position) && MissileDetail::SerializeVector(bs, write, direction) && MissileDetail::SerializeVector(bs, write, velocity) && weaponProperties.Serialize(bs, write) && ammunitionProperties.Serialize(bs, write);
        }

        bool IsSane() const {
            if (!valid || sequence == 0 || incarnation == 0 || !weapon.IsValid() || !ammunition.IsValid() || !weaponProperties.IsSane() || !ammunitionProperties.IsSane() || !Shared::Game::IsWorldPosition(position) || !Shared::Game::IsFinite(direction)
                || !Shared::Game::IsFinite(velocity)) {
                return false;
            }

            const float directionLength = glm::dot(direction, direction);
            const float speedSquared    = glm::dot(velocity, velocity);

            // Native replay requires velocity aligned with the launch direction.
            return directionLength > 0.95F && directionLength < 1.05F && speedSquared > 0.0F && speedSquared <= kMaxSpeed * kMaxSpeed && glm::dot(direction, velocity) > 0.0F && glm::dot(glm::cross(direction, velocity), glm::cross(direction, velocity)) <= 0.001F * speedSquared;
        }
    };

    struct MissileImpact {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::MissileImpact");

        MissileLaunched launch;
        Game::HumanSoulAction soulAction;
        std::uint64_t targetNetworkId = 0;
        glm::vec3 position {0.0F};
        glm::vec3 velocity {0.0F};
        glm::vec3 normal {0.0F, 0.0F, 1.0F};

        // Zero-based body material; RPG resolves material+1 through the race table.
        std::int32_t bodyMaterial       = -1;
        std::int32_t surface            = -1;
        std::int32_t ammunitionCategory = 0;
        float scale                     = 1.0F;
        bool valid                      = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            launch.Serialize(bs, write);
            valid = launch.valid && soulAction.Serialize(bs, write) && bs->Serialize(write, targetNetworkId) && MissileDetail::SerializeVector(bs, write, position) && MissileDetail::SerializeVector(bs, write, velocity) && MissileDetail::SerializeVector(bs, write, normal)
                    && bs->Serialize(write, bodyMaterial) && bs->Serialize(write, surface) && bs->Serialize(write, ammunitionCategory) && bs->Serialize(write, scale);
        }

        bool IsSane() const {
            const float normalLength = glm::dot(normal, normal);

            return valid && soulAction.IsSane() && launch.IsSane() && soulAction.incarnation == launch.incarnation && soulAction.epoch == launch.epoch && targetNetworkId != 0 && Shared::Game::IsWorldPosition(position) && Shared::Game::IsFinite(velocity)
                   && glm::dot(velocity, velocity) <= MissileLaunched::kMaxSpeed * MissileLaunched::kMaxSpeed && Shared::Game::IsFinite(normal) && normalLength > 0.95F && normalLength < 1.05F && bodyMaterial >= -1 && bodyMaterial <= 255 && surface >= -1 && surface <= 255
                   && ammunitionCategory >= 0 && ammunitionCategory < 5 && std::isfinite(scale) && scale >= 0.0F && scale <= 4.0F;
        }
    };

    struct MissileRetained {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::MissileRetained");

        std::uint64_t shooterNetworkId = 0;
        std::uint64_t sequence         = 0;
        std::uint64_t targetNetworkId  = 0;
        MissileItemProperties properties;
        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, shooterNetworkId) && bs->Serialize(write, sequence) && bs->Serialize(write, targetNetworkId) && properties.Serialize(bs, write);
        }

        bool IsSane() const {
            return valid && sequence != 0 && targetNetworkId != 0 && properties.IsSane();
        }
    };

    struct MissileEnded {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::MissileEnded");

        std::uint64_t shooterNetworkId = 0;
        std::uint64_t sequence         = 0;
        bool valid                     = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, shooterNetworkId) && bs->Serialize(write, sequence);
        }

        bool IsSane() const {
            return valid && sequence != 0;
        }
    };
} // namespace KCDC::Shared::RPC
