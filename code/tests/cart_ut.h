#pragma once

#include <array>

#include "shared/features/cart/cart_entity.h"
#include "shared/features/cart/cart_seat.h"
#include "shared/features/cart/cart_motion.h"
#include "shared/game/cart_blueprints.h"

#include <mafianet/bit_stream.h>

#include <glm/glm.hpp>

#include <cmath>
#include <set>
#include <string>
#include <vector>

MODULE(cart_blueprints, {
    IT("ships a catalog with a spawnable default in it", {
        EQUALS(KCDC::Shared::Game::g_CartBlueprints.empty(), false);
        NEQUALS(KCDC::Shared::Game::FindCartBlueprint(KCDC::Shared::Game::kDefaultCartBlueprint), nullptr);
        EQUALS(KCDC::Shared::Game::FindCartBlueprint("no such wagon"), nullptr);
    });

    IT("names every blueprint once", {
        std::set<std::string> seen;
        for (const auto &blueprint : KCDC::Shared::Game::g_CartBlueprints) {
            EQUALS(blueprint.name.empty(), false);
            EQUALS(blueprint.prefabGuid.size(), std::size_t {36});
            EQUALS(seen.insert(std::string(blueprint.name)).second, true);
        }
    });

    IT("gives every blueprint one cart entity and a way onto it", {
        for (const auto &blueprint : KCDC::Shared::Game::g_CartBlueprints) {
            int cartEntities = 0;
            int mountPoints  = 0;
            for (std::size_t index = 0; index < blueprint.partCount; ++index) {
                const auto &part = blueprint.parts[index];
                cartEntities += part.entityClass == "WHCart" ? 1 : 0;
                mountPoints += part.entityClass == "WHCartMountPoint" ? 1 : 0;
            }
            EQUALS(cartEntities, 1);
            NEQUALS(mountPoints, 0);
        }
    });

    IT("orders every blueprint so a parent is spawned before its children", {
        for (const auto &blueprint : KCDC::Shared::Game::g_CartBlueprints) {
            for (std::size_t index = 0; index < blueprint.partCount; ++index) {
                EQUALS(blueprint.parts[index].parent < static_cast<int>(index), true);
            }
        }
    });

    // The setup pass builds the follower model only when the WHCart's parent,
    // or that parent's parent, is a GeomEntity; a blueprint where it is not
    // would be a cart the engine cannot move.
    IT("hangs every cart entity under a GeomEntity chassis the setup pass can find", {
        for (const auto &blueprint : KCDC::Shared::Game::g_CartBlueprints) {
            const int chassis = KCDC::Shared::Game::FindCartChassis(blueprint);
            EQUALS(chassis >= 0, true);
            EQUALS(blueprint.parts[chassis].entityClass == "GeomEntity", true);
            EQUALS(blueprint.parts[chassis].geometry.empty(), false);
        }
    });

    IT("harnesses a wagon for two, the two-wheeler for one, and the helperless chassis for none", {
        EQUALS(KCDC::Shared::Game::CartHarnessSlots(*KCDC::Shared::Game::FindCartBlueprint(KCDC::Shared::Game::kDefaultCartBlueprint)), 2);
        EQUALS(KCDC::Shared::Game::CartHarnessSlots(*KCDC::Shared::Game::FindCartBlueprint("cart_b")), 1);
        EQUALS(KCDC::Shared::Game::CartHarnessSlots(*KCDC::Shared::Game::FindCartBlueprint("wagon_b_covered_empty")), 0);
    });

    IT("keeps every blueprint transform usable", {
        for (const auto &blueprint : KCDC::Shared::Game::g_CartBlueprints) {
            for (std::size_t index = 0; index < blueprint.partCount; ++index) {
                const auto &part      = blueprint.parts[index];
                const float rotLength = (part.rotX * part.rotX) + (part.rotY * part.rotY) + (part.rotZ * part.rotZ) + (part.rotW * part.rotW);
                EQUALS(rotLength > 0.9F && rotLength < 1.1F, true);
                EQUALS(part.scaleX > 0.0F && part.scaleY > 0.0F && part.scaleZ > 0.0F, true);
            }
        }
    });
});

MODULE(cart_rig, {
    IT("reads a wagon as a front axle with a body turning on it, and the wheelbase between its axles", {
        const auto *blueprint = KCDC::Shared::Game::FindCartBlueprint("wagon_b_covered");
        EQUALS(blueprint != nullptr, true);
        const auto rig = KCDC::Shared::Game::BuildCartRig(*blueprint);
        EQUALS(rig.has_value(), true);
        EQUALS(rig->articulated, true);
        EQUALS(rig->bodyIndex >= 0, true);
        // Front wheels at y 1.06 under the axle, rear ones 0.45 into a body
        // hung 1.84 behind it: 2.46 m between the axles.
        EQUALS(std::fabs(rig->wheelbase - 2.46F) < 0.05F, true);
        EQUALS(rig->lead.y > rig->axle.y, true);
        EQUALS(rig->horseCount, std::size_t {2});
        EQUALS(rig->lift, 0.0F);
    });

    IT("reads the two-wheeler as one frame pulled from ahead of its only axle", {
        const auto *blueprint = KCDC::Shared::Game::FindCartBlueprint("cart_b");
        EQUALS(blueprint != nullptr, true);
        const auto rig = KCDC::Shared::Game::BuildCartRig(*blueprint);
        EQUALS(rig.has_value(), true);
        EQUALS(rig->articulated, false);
        EQUALS(rig->bodyIndex, -1);
        EQUALS(std::fabs(rig->wheelbase - KCDC::Shared::Game::kCartShaftReach) < 0.01F, true);
        EQUALS(rig->horseCount, std::size_t {1});
        EQUALS(rig->lift > 0.0F, true);
    });

    IT("has a rig for every blueprint the catalog ships", {
        for (const auto &blueprint : KCDC::Shared::Game::g_CartBlueprints) {
            EQUALS(KCDC::Shared::Game::BuildCartRig(blueprint).has_value(), true);
        }
    });
});

MODULE(cart_motion, {
    constexpr float kWheelbase = 2.5F;
    constexpr float kStep      = 1.0F / 60.0F;

    const auto run = [](KCDC::Shared::Game::CartDrive drive, const KCDC::Shared::Game::CartControls &controls, float seconds, bool blocked = false) {
        const KCDC::Shared::Game::CartGaits gaits;
        for (float t = 0.0F; t < seconds; t += kStep) {
            drive = KCDC::Shared::Game::StepCart(drive, controls, gaits, kWheelbase, true, blocked, kStep);
        }
        return drive;
    };

    IT("gets under way to the walk and rolls straight on along its heading", {
        KCDC::Shared::Game::CartControls controls;
        controls.throttle = 1;
        const auto drive  = run({}, controls, 5.0F);
        EQUALS(std::fabs(drive.speed - KCDC::Shared::Game::CartGaits {}.walk) < 1e-3F, true);
        EQUALS(drive.lead.y > 5.0F, true);
        EQUALS(std::fabs(drive.lead.x) < 1e-3F, true);
        EQUALS(std::fabs(drive.bodyYaw) < 1e-3F, true);
    });

    IT("turns the moment the reins pull, and the body follows the lead round", {
        KCDC::Shared::Game::CartControls controls;
        controls.throttle = 1;
        auto drive        = run({}, controls, 2.0F);
        controls.steer    = 1.0F;
        const auto turned = run(drive, controls, 0.25F);
        EQUALS(turned.leadYaw > 0.1F, true);
        const auto around = run(turned, controls, 4.0F);
        EQUALS(around.bodyYaw > 1.0F, true);
        // Turning left from +Y heads towards -X.
        EQUALS(around.lead.x < -1.0F, true);
    });

    IT("never swings the front axle past its lock under the bed", {
        KCDC::Shared::Game::CartControls controls;
        controls.throttle = 1;
        controls.steer    = 1.0F;
        const auto drive  = run({}, controls, 1.5F);
        EQUALS(KCDC::Shared::Game::CartWrapAngle(drive.leadYaw - drive.bodyYaw) <= KCDC::Shared::Game::kCartMaxLock + 1e-4F, true);
    });

    IT("does not turn a standing cart, and turns one backing up the other way round", {
        KCDC::Shared::Game::CartControls controls;
        controls.steer   = 1.0F;
        const auto still = run({}, controls, 1.0F);
        EQUALS(still.leadYaw, 0.0F);

        controls.throttle = -1;
        const auto back   = run({}, controls, 2.0F);
        EQUALS(back.leadYaw < 0.0F, true);
        EQUALS(back.speed < 0.0F, true);
        EQUALS(back.lead.y < 0.0F, true);
    });

    IT("brakes before it backs up, and comes to a stop when let go", {
        KCDC::Shared::Game::CartControls controls;
        controls.throttle = 1;
        auto drive        = run({}, controls, 3.0F);
        controls.throttle = -1;
        drive             = run(drive, controls, 0.1F);
        EQUALS(drive.speed > 0.0F, true);
        controls.throttle = 0;
        drive             = run(drive, controls, 2.0F);
        EQUALS(drive.speed, 0.0F);
    });

    IT("stops dead against something in the way", {
        KCDC::Shared::Game::CartControls controls;
        controls.throttle = 1;
        auto drive        = run({}, controls, 2.0F);
        drive             = run(drive, controls, kStep, true);
        EQUALS(drive.speed, 0.0F);
    });

    IT("keeps the trailing axle a wheelbase behind the lead", {
        KCDC::Shared::Game::CartControls controls;
        controls.throttle = 1;
        controls.steer    = -0.6F;
        const auto drive  = run({}, controls, 6.0F);
        EQUALS(std::fabs(glm::distance(glm::vec2(drive.lead), glm::vec2(drive.Axle(kWheelbase))) - kWheelbase) < 1e-3F, true);
    });

    IT("puts the team in the gait its pace calls for, and keeps a gait near its edge", {
        const KCDC::Shared::Game::CartHorseGait idle = KCDC::Shared::Game::CartHorseGait::Idle;
        EQUALS(KCDC::Shared::Game::ChooseCartHorseGait(0.0F, idle, 0.2F) == idle, true);
        EQUALS(KCDC::Shared::Game::ChooseCartHorseGait(1.0F, idle, 0.2F) == KCDC::Shared::Game::CartHorseGait::Walk, true);
        EQUALS(KCDC::Shared::Game::ChooseCartHorseGait(3.6F, idle, 0.2F) == KCDC::Shared::Game::CartHorseGait::Trot, true);
        EQUALS(KCDC::Shared::Game::ChooseCartHorseGait(7.0F, idle, 0.2F) == KCDC::Shared::Game::CartHorseGait::Canter, true);
        EQUALS(KCDC::Shared::Game::ChooseCartHorseGait(-1.5F, idle, 0.2F) == KCDC::Shared::Game::CartHorseGait::WalkBack, true);
        EQUALS(KCDC::Shared::Game::ChooseCartHorseGait(2.7F, KCDC::Shared::Game::CartHorseGait::Walk, 0.2F) == KCDC::Shared::Game::CartHorseGait::Walk, true);
        EQUALS(KCDC::Shared::Game::ChooseCartHorseGait(2.9F, KCDC::Shared::Game::CartHorseGait::Walk, 0.2F) == KCDC::Shared::Game::CartHorseGait::Trot, true);
        EQUALS(std::fabs(KCDC::Shared::Game::CartHorseGaitRate(KCDC::Shared::Game::CartHorseGait::Trot, 3.6F) - 1.0F) < 1e-4F, true);
        EQUALS(std::fabs(KCDC::Shared::Game::CartHorseGaitRate(KCDC::Shared::Game::CartHorseGait::WalkBack, -1.2F) - 0.8F) < 1e-4F, true);
        // A stride is never played in slow motion or as a scurry.
        EQUALS(std::fabs(KCDC::Shared::Game::CartHorseGaitRate(KCDC::Shared::Game::CartHorseGait::Walk, 0.4F) - KCDC::Shared::Game::kCartHorseMinRate) < 1e-4F, true);
        EQUALS(std::fabs(KCDC::Shared::Game::CartHorseGaitRate(KCDC::Shared::Game::CartHorseGait::Walk, 2.75F) - KCDC::Shared::Game::kCartHorseMaxRate) < 1e-4F, true);
        // A cart all but stopped stands its team rather than walking it in place.
        EQUALS(KCDC::Shared::Game::ChooseCartHorseGait(0.25F, KCDC::Shared::Game::CartHorseGait::Walk, 0.2F) == KCDC::Shared::Game::CartHorseGait::Idle, true);
    });

    IT("takes no drive that is not finite or goes faster than any team", {
        KCDC::Shared::Game::CartDrive drive;
        EQUALS(KCDC::Shared::Game::IsSaneCartDrive(drive), true);
        drive.speed = 40.0F;
        EQUALS(KCDC::Shared::Game::IsSaneCartDrive(drive), false);
        drive.speed  = 0.0F;
        drive.lead.x = std::nanf("");
        EQUALS(KCDC::Shared::Game::IsSaneCartDrive(drive), false);
    });

    IT("lets a script cap how fast the driver goes urged on", {
        KCDC::Shared::Entities::CartEntity cart;
        EQUALS(cart.Gaits().trot, KCDC::Shared::Entities::CartEntity::kDefaultMaxSpeed);
        cart.maxSpeed = 2.0F;
        KCDC::Shared::Game::CartControls controls;
        controls.throttle = 1;
        EQUALS(KCDC::Shared::Game::CartTargetSpeed(controls, cart.Gaits()), 2.0F);
        controls.fast = true;
        EQUALS(KCDC::Shared::Game::CartTargetSpeed(controls, cart.Gaits()), 2.0F);
    });
});

MODULE(cart_seats, {
    IT("seats six on a wagon and three on the two-wheeler, the reins first", {
        KCDC::Shared::Entities::CartEntity wagon;
        wagon.blueprint = "wagon_b_covered";
        EQUALS(wagon.Seats().count, std::size_t {6});
        EQUALS(wagon.Seats().seats[KCDC::Shared::Entities::CartEntity::kSeatDriver].name == "driver", true);
        EQUALS(wagon.IsSeat(5), true);
        EQUALS(wagon.IsSeat(6), false);

        KCDC::Shared::Entities::CartEntity cart;
        cart.blueprint = "cart_b";
        EQUALS(cart.Seats().count, std::size_t {3});
        EQUALS(cart.Seats().Find("leftBack").value_or(9), std::uint8_t {2});
        EQUALS(cart.Seats().Find("bench").has_value(), false);
    });

    IT("lands each wagon seat's own climb on it", {
        // The co-driver's bench sits at the `WHCart`'s (0.284, 3.203); the
        // places along the rails at their own helpers, where the seats named
        // after them are. Only the reins move the `WHCart`, by the bench's width.
        const auto helper = [](std::string_view name) {
            if (name == "leftBack") {
                return glm::vec2(-0.212F, 0.410F);
            }
            if (name == "rightFront") {
                return glm::vec2(0.212F, 1.600F);
            }
            if (name == "leftFront") {
                return glm::vec2(-0.212F, 1.600F);
            }
            return glm::vec2(0.212F, 0.410F);
        };
        for (const auto &seat : KCDC::Shared::Game::kWagonSeats) {
            const glm::vec2 lands = glm::vec2(seat.frame) + (seat.front ? glm::vec2(0.284F, 3.203F) : helper(seat.name));
            EQUALS(glm::distance(lands, glm::vec2(seat.point)) < 0.01F, true);
        }
    });

    IT("gives the bench's prompt the reins, then the bench, and the rail's prompt the rail on the player's side", {
        const auto layout = KCDC::Shared::Game::CartSeatsFor(*KCDC::Shared::Game::BuildCartRig(*KCDC::Shared::Game::FindCartBlueprint("wagon_b_covered")));
        std::array<bool, 6> taken {};
        const auto isTaken = [&taken](std::uint8_t seat) {
            return taken[seat];
        };
        EQUALS(KCDC::Shared::Game::ChooseCartSeat(layout, true, true, isTaken).value_or(9), std::uint8_t {0});
        taken[0] = true;
        EQUALS(KCDC::Shared::Game::ChooseCartSeat(layout, true, true, isTaken).value_or(9), std::uint8_t {1});
        EQUALS(KCDC::Shared::Game::ChooseCartSeat(layout, false, true, isTaken).value_or(9), std::uint8_t {2});
        EQUALS(KCDC::Shared::Game::ChooseCartSeat(layout, false, false, isTaken).value_or(9), std::uint8_t {3});
        taken[3] = true;
        taken[5] = true;
        // The left rails full: the right one.
        EQUALS(KCDC::Shared::Game::ChooseCartSeat(layout, false, false, isTaken).value_or(9), std::uint8_t {2});
        taken = {true, true, true, true, true, true};
        EQUALS(KCDC::Shared::Game::ChooseCartSeat(layout, true, true, isTaken).has_value(), false);
    });
});

MODULE(cart_protocol, {
    IT("carries a seat request and refuses a kind it does not know", {
        KCDC::Shared::RPC::CartSeatRequest sent;
        sent.cartNetworkId = 9;
        sent.kind          = static_cast<std::uint8_t>(KCDC::Shared::RPC::CartSeatRequest::Kind::Back);

        MafiaNet::BitStream stream;
        sent.Serialize(&stream, true);
        KCDC::Shared::RPC::CartSeatRequest received;
        received.Serialize(&stream, false);
        EQUALS(received.cartNetworkId, std::uint64_t {9});
        EQUALS(received.IsSane(), true);

        received.kind = 9;
        EQUALS(received.IsSane(), false);
    });

    IT("hands a late joiner the blueprint, the body's heading and the seats with the construction", {
        KCDC::Shared::Entities::CartEntity sent;
        sent.blueprint = "wagon_b_covered";
        sent.seats[0]  = 5;
        sent.seats[5]  = 8;
        sent.bodyYaw   = 0.75F;
        sent.maxSpeed  = 4.5F;
        sent.pace      = static_cast<std::uint8_t>(KCDC::Shared::Entities::CartPace::Trot);
        sent.boarded   = 1U << 0;
        sent.seated    = 1U << 0;
        sent.leaving   = 1U << 5;

        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        sent.OnSerializeConstruction(writer);
        sent.SerializeFields(writer);

        KCDC::Shared::Entities::CartEntity received;
        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        received.OnSerializeConstruction(reader);
        received.SerializeFields(reader);
        EQUALS(reader.Good(), true);
        EQUALS(received.blueprint, std::string("wagon_b_covered"));
        EQUALS(received.bodyYaw, 0.75F);
        EQUALS(received.seats[0], std::uint64_t {5});
        EQUALS(received.seats[5], std::uint64_t {8});
        EQUALS(received.Driver(), std::uint64_t {5});
        EQUALS(received.SeatOf(8), std::size_t {5});
        EQUALS(received.maxSpeed, 4.5F);
        EQUALS(received.pace, static_cast<std::uint8_t>(KCDC::Shared::Entities::CartPace::Trot));
        EQUALS(KCDC::Shared::Entities::CartEntity::HasSeatBit(received.boarded, 0), true);
        EQUALS(KCDC::Shared::Entities::CartEntity::HasSeatBit(received.seated, 0), true);
        EQUALS(KCDC::Shared::Entities::CartEntity::HasSeatBit(received.leaving, 5), true);
        EQUALS(KCDC::Shared::Entities::CartEntity::HasSeatBit(received.leaving, 0), false);
    });

    IT("carries the drive in the pose stream, close enough to drive the copies by", {
        KCDC::Shared::Game::CartDrive drive;
        drive.lead    = glm::vec3(812.5F, 3320.25F, 141.75F);
        drive.leadYaw = 1.2F;
        drive.bodyYaw = 0.9F;
        drive.speed   = -0.8F;

        KCDC::Shared::Entities::CartEntity sent;
        sent.SetDrive(drive);
        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        sent.SerializeTransform(writer);

        KCDC::Shared::Entities::CartEntity received;
        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        received.SerializeTransform(reader);
        EQUALS(reader.Good(), true);
        const auto back = received.GetDrive();
        EQUALS(glm::distance(back.lead, drive.lead) < 0.01F, true);
        EQUALS(std::fabs(back.leadYaw - drive.leadYaw) < 0.01F, true);
        EQUALS(std::fabs(back.bodyYaw - drive.bodyYaw) < 0.01F, true);
        EQUALS(std::fabs(back.speed - drive.speed) < 0.01F, true);
    });

    IT("tells the driver the seats, their climbs and a server reset in the forced state", {
        KCDC::Shared::Entities::CartEntity sent;
        sent.seats[0] = 5;
        sent.boarded  = 1U << 0;
        sent.pace     = static_cast<std::uint8_t>(KCDC::Shared::Entities::CartPace::Stand);
        sent.speed    = 0.0F;

        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        sent.SerializeForcedState(writer);

        KCDC::Shared::Entities::CartEntity received;
        received.pace  = static_cast<std::uint8_t>(KCDC::Shared::Entities::CartPace::Trot);
        received.speed = 3.0F;
        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        received.SerializeForcedState(reader);
        EQUALS(reader.Good(), true);
        EQUALS(received.Driver(), std::uint64_t {5});
        EQUALS(KCDC::Shared::Entities::CartEntity::HasSeatBit(received.boarded, 0), true);
        EQUALS(received.pace, static_cast<std::uint8_t>(KCDC::Shared::Entities::CartPace::Stand));
        EQUALS(received.speed, 0.0F);
    });
});
