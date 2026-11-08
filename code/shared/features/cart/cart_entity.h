#pragma once

#include "shared/features/cart/cart_motion.h"
#include "shared/features/cart/cart_seats.h"
#include "shared/features/player/player_entity.h"

#include <networking/replication/delegation.h>
#include <networking/replication/network_entity.h>

#include <mafianet/string.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace KCDC::Shared::Entities {
    /** How the driver has the cart going; a standstill, the game's own two gaits, and backing up. */
    enum class CartPace : std::uint8_t {
        Stand   = 0,
        Walk    = 1,
        Trot    = 2,
        Reverse = 3,
    };

    inline bool IsCartPace(std::uint8_t value) {
        return value <= static_cast<std::uint8_t>(CartPace::Reverse);
    }

    /**
     * A replicated cart or wagon.
     *
     * Server-spawned from one of the game's own cart prefabs, and built on
     * every client the way the level builds its own: the chassis, its wheels
     * and body, the `WHCart` that owns the seats, the mount points, and the
     * horses at its pole -- each client's own, put there every frame and
     * stepped in the gait the cart's pace calls for.
     *
     * The game only ever moves a cart along a road, as an NPC caravan; a
     * player drives one as a wagon (`Shared::Game::StepCart`). What travels is
     * that drive -- where the lead point is, which way it and the body face,
     * how fast the cart rolls -- and every client poses its own copy from it.
     *
     * Whoever holds the reins runs it: the driver's client is pinned as the
     * owner for as long as they sit there, and a cart nobody drives is the
     * server's, standing where it was left.
     */
    class CartEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::Cart";

        // The seats, as `Shared::Game::CartSeatsFor` lays them out for the
        // cart's kind: a wagon's six, a two-wheeler's three, the reins first.
        static constexpr std::size_t kSeatCount   = Shared::Game::kCartMaxSeats;
        static constexpr std::uint8_t kSeatDriver = Shared::Game::kCartDriverSeat;

        /** This cart's seats. */
        Shared::Game::CartSeatLayout Seats() const {
            return Shared::Game::CartSeatsForBlueprint(blueprint);
        }

        /** Whether `seat` is one this cart has. */
        bool IsSeat(std::uint8_t seat) const {
            return seat < Seats().count;
        }

        /** The blueprint the cart is built from (`Shared::Game::g_CartBlueprints`). Immutable: it rides the construction snapshot. */
        std::string blueprint;

        /**
         * The HumanEntity in each seat, 0 for an empty one, by `E_CartPosition`.
         * A server field: a seat is the server's verdict, and the driver, who
         * owns the cart, hears it from the forced state.
         */
        std::array<std::uint64_t, kSeatCount> seats {};

        /**
         * How far the player in each seat has got with their own climb, one
         * bit per seat: `boarded` once the climb in has started, `seated` once
         * it has ended, `leaving` once the climb down has started. Every other
         * client's copy of them climbs in and down alongside.
         */
        std::uint8_t boarded = 0;
        std::uint8_t seated  = 0;
        std::uint8_t leaving = 0;

        static bool HasSeatBit(std::uint8_t mask, std::uint8_t seat) {
            return seat < kSeatCount && (mask & (1U << seat)) != 0;
        }

        /** How the driver has it going, as a `CartPace`; what scripts read. */
        std::uint8_t pace = 0;

        /** The fastest the driver may take it, urged on, in metres a second; a script's to set. */
        static constexpr float kDefaultMaxSpeed = 7.0F;
        float maxSpeed                          = kDefaultMaxSpeed;

        /** The gaits the driver's keys ask for under `maxSpeed`. */
        Shared::Game::CartGaits Gaits() const {
            Shared::Game::CartGaits gaits;
            gaits.trot = std::clamp(maxSpeed, 0.0F, Shared::Game::kCartMaxSpeed);
            return gaits;
        }

        /**
         * The drive, beside the pose: `position` is the lead point on the
         * ground and `rotation` turns about Z by the lead's yaw, so the
         * stream and anything that only reads a pose see the cart where it
         * is; the body's yaw and the speed ride along.
         */
        float bodyYaw = 0.0F;
        float speed   = 0.0F;

        /** The drive as the motion steps it, read off the replicated pose. */
        Shared::Game::CartDrive GetDrive() const {
            Shared::Game::CartDrive drive;
            drive.lead    = position;
            drive.leadYaw = glm::eulerAngles(rotation).z;
            drive.bodyYaw = bodyYaw;
            drive.speed   = speed;
            return drive;
        }

        /** Writes a drive into the replicated pose. */
        void SetDrive(const Shared::Game::CartDrive &drive) {
            position = drive.lead;
            rotation = glm::angleAxis(drive.leadYaw, glm::vec3(0.0F, 0.0F, 1.0F));
            bodyYaw  = drive.bodyYaw;
            speed    = drive.speed;
        }

        /** The driver, or 0 when nobody holds the reins. */
        std::uint64_t Driver() const {
            return seats[kSeatDriver];
        }

        /** The seat `humanNetworkId` is in, or kSeatCount when none. */
        std::size_t SeatOf(std::uint64_t humanNetworkId) const {
            if (humanNetworkId == 0) {
                return kSeatCount;
            }
            for (std::size_t seat = 0; seat < kSeatCount; ++seat) {
                if (seats[seat] == humanNetworkId) {
                    return seat;
                }
            }
            return kSeatCount;
        }

        // --- Delegation ---------------------------------------------------------

        /**
         * Nobody is elected to run a cart: it is run by its driver, pinned for
         * as long as they hold the reins, and otherwise by nobody at all -- a
         * parked wagon has nothing to simulate.
         */
        static constexpr Framework::Networking::Replication::DelegationPolicy kDelegationPolicy {
            /* acquireRange */ 0.0F,
            /* releaseRange */ 0.0F,
            /* weight */ 0,
            /* graceMs */ 0,
            /* requireSameVirtualWorld */ true,
        };

        const Framework::Networking::Replication::DelegationPolicy *GetDelegationPolicy() const override {
            return &kDelegationPolicy;
        }

        /** A cart in view brings its driver with it. */
        Framework::Networking::Replication::NetworkEntity *GetInterestDependency() override {
            return Driver() != 0 ? ResolveSibling(Driver()) : nullptr;
        }

        // --- Wire -------------------------------------------------------------

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            MafiaNet::RakString wireBlueprint(blueprint.c_str());
            fields.Field(wireBlueprint);
            if (!fields.Writing()) {
                blueprint = wireBlueprint.C_String();
            }
            fields.Field(bodyYaw);
            fields.Field(maxSpeed);
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            for (std::uint64_t &seat : seats) {
                fields.ServerField(seat);
            }
            fields.ServerField(boarded);
            fields.ServerField(seated);
            fields.ServerField(leaving);
            fields.ServerField(maxSpeed);
            fields.Field(pace);
        }

        /**
         * The server's override for the driver, the one peer the field channel
         * never tells: the seats -- a driver who has just been seated learns
         * from this that the reins are theirs -- the pace a teleport resets,
         * and how fast a script lets them go.
         */
        void SerializeForcedState(Framework::Networking::Replication::FieldSerializer &fields) override {
            Framework::Networking::Replication::NetworkEntity::SerializeForcedState(fields);
            for (std::uint64_t &seat : seats) {
                fields.Field(seat);
            }
            fields.Field(boarded);
            fields.Field(seated);
            fields.Field(leaving);
            fields.Field(maxSpeed);
            fields.Field(pace);
            fields.Field(bodyYaw);
            fields.Field(speed);
        }

        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.FloatBits(position.x, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(position.y, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(position.z, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(rotation.x, -1.0F, 1.0F, HumanEntity::kRotationBits);
            fields.FloatBits(rotation.y, -1.0F, 1.0F, HumanEntity::kRotationBits);
            fields.FloatBits(rotation.z, -1.0F, 1.0F, HumanEntity::kRotationBits);
            fields.FloatBits(rotation.w, -1.0F, 1.0F, HumanEntity::kRotationBits);
            fields.FloatBits(bodyYaw, -4.0F, 4.0F, HumanEntity::kRotationBits);
            fields.FloatBits(speed, -Shared::Game::kCartMaxSpeed, Shared::Game::kCartMaxSpeed, HumanEntity::kRotationBits);
        }
    };
} // namespace KCDC::Shared::Entities
