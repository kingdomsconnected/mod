#pragma once

#include "shared/features/cart/cart_motion.h"
#include "shared/game/cart_blueprints.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace KCDC::Shared::Game {
    // Where people sit in a cart.
    //
    // Every seat is a `WHCart` of its own, under the body, and is reached by
    // the game's own climb for one of its positions: the co-driver's bench
    // for the front, and for the back the place along the rails the seat is
    // named after -- each has its own approach, climb in, idle and climb
    // down, as the game's passengers use them. A two-wheeler has none of
    // them where its seats are, so its `WHCart` is moved for the position's
    // helper to land on the seat.
    //
    // All of it is in the seat frame: a wagon's body, or a two-wheeler's
    // frame before it is tipped up to its harness -- the frame the `WHCart`
    // stands at the origin of on a wagon.

    struct CartSeat {
        /** What scripts call it. */
        std::string_view name;

        /** Climbed onto with the bench's climb (the front), or the rails' (the back). */
        bool front = false;

        /** Which side of the cart it is on, for a player getting in from that side. */
        bool right = false;

        /** Where the `WHCart` goes for the climb it is reached with to land here. */
        glm::vec3 frame = glm::vec3(0.0F);

        /** Where a body sits: what `frame` is laid out to land the climb on. */
        glm::vec3 point = glm::vec3(0.0F);
    };

    /** The driver's idle, and the flicks of the reins as the horses start and stop. */
    inline constexpr std::string_view kCartDriverClip     = "wagon_driver_idle";
    inline constexpr std::string_view kCartDriverGoClip   = "wagon_driver_idle_var_go";
    inline constexpr std::string_view kCartDriverStopClip = "wagon_driver_idle_var_stop";

    /**
     * A wagon: the reins on the left of the bench -- the bench's climb lands
     * there with the `WHCart` moved over by the bench's width -- the bench
     * beside them, and four places on the rails, two each side.
     */
    inline constexpr CartSeat kWagonSeats[] = {
        {"driver", true, false, glm::vec3(-0.568F, 0.0F, 0.0F), glm::vec3(-0.284F, 3.203F, 0.991F)},
        {"bench", true, true, glm::vec3(0.0F, 0.0F, 0.0F), glm::vec3(0.284F, 3.203F, 0.991F)},
        {"rightBack", false, true, glm::vec3(0.0F, 0.0F, 0.0F), glm::vec3(0.212F, 0.410F, 0.930F)},
        {"leftBack", false, false, glm::vec3(0.0F, 0.0F, 0.0F), glm::vec3(-0.212F, 0.410F, 0.930F)},
        {"rightFront", false, true, glm::vec3(0.0F, 0.0F, 0.0F), glm::vec3(0.212F, 1.600F, 0.930F)},
        {"leftFront", false, false, glm::vec3(0.0F, 0.0F, 0.0F), glm::vec3(-0.212F, 1.600F, 0.930F)},
    };

    /** A two-wheeler: the driver on the bed's front edge, and a rail each side behind the wheels. */
    inline constexpr CartSeat kShaftCartSeats[] = {
        {"driver", true, false, glm::vec3(-0.284F, -2.703F, -0.191F), glm::vec3(0.0F, 0.5F, 0.8F)},
        {"rightBack", false, true, glm::vec3(0.488F, -1.330F, -0.480F), glm::vec3(0.7F, -0.92F, 0.45F)},
        {"leftBack", false, false, glm::vec3(-0.488F, -1.330F, -0.480F), glm::vec3(-0.7F, -0.92F, 0.45F)},
    };

    /** The most seats any cart has. */
    inline constexpr std::size_t kCartMaxSeats = sizeof(kWagonSeats) / sizeof(kWagonSeats[0]);

    /** The seat the reins are held from, on every cart. */
    inline constexpr std::uint8_t kCartDriverSeat = 0;

    struct CartSeatLayout {
        const CartSeat *seats = nullptr;
        std::size_t count     = 0;

        const CartSeat *At(std::size_t seat) const {
            return seat < count ? &seats[seat] : nullptr;
        }

        /** The seat a script names, or nothing for a name this cart has no seat by. */
        std::optional<std::uint8_t> Find(std::string_view name) const {
            for (std::size_t seat = 0; seat < count; ++seat) {
                if (seats[seat].name == name) {
                    return static_cast<std::uint8_t>(seat);
                }
            }
            return std::nullopt;
        }
    };

    inline CartSeatLayout CartSeatsFor(const CartRig &rig) {
        if (rig.articulated) {
            return CartSeatLayout {kWagonSeats, sizeof(kWagonSeats) / sizeof(kWagonSeats[0])};
        }
        return CartSeatLayout {kShaftCartSeats, sizeof(kShaftCartSeats) / sizeof(kShaftCartSeats[0])};
    }

    /** The seats of the blueprint called `blueprint`; none for one that cannot be driven. */
    inline CartSeatLayout CartSeatsForBlueprint(std::string_view blueprint) {
        const CartBlueprint *chosen      = FindCartBlueprint(blueprint);
        const std::optional<CartRig> rig = chosen != nullptr ? BuildCartRig(*chosen) : std::nullopt;
        return rig ? CartSeatsFor(*rig) : CartSeatLayout {};
    }

    /**
     * The seat a player who used the cart's prompt is given: from the bench's
     * prompt the reins while nobody holds them, else the bench beside them;
     * from the rail's the rail on the side they stand, front or back. Failing
     * that, any seat that is free, the side they stand first. `taken` says
     * whether a seat is held.
     */
    template <typename Taken>
    std::optional<std::uint8_t> ChooseCartSeat(const CartSeatLayout &layout, bool front, bool rightSide, Taken &&taken) {
        const auto free = [&](std::size_t seat) {
            return seat < layout.count && !taken(static_cast<std::uint8_t>(seat));
        };
        if (front) {
            for (std::size_t seat = 0; seat < layout.count; ++seat) {
                if (layout.seats[seat].front && free(seat)) {
                    return static_cast<std::uint8_t>(seat);
                }
            }
        }
        for (int pass = 0; pass < 2; ++pass) {
            const bool wantRight = pass == 0 ? rightSide : !rightSide;
            for (std::size_t seat = 0; seat < layout.count; ++seat) {
                if (!layout.seats[seat].front && layout.seats[seat].right == wantRight && free(seat)) {
                    return static_cast<std::uint8_t>(seat);
                }
            }
        }
        for (std::size_t seat = 0; seat < layout.count; ++seat) {
            if (free(seat)) {
                return static_cast<std::uint8_t>(seat);
            }
        }
        return std::nullopt;
    }
} // namespace KCDC::Shared::Game
