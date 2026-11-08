#pragma once

#include <cstdint>

namespace KCDC::Shared::Entities {
    /**
     * The owner's input for this frame, before the engine applies it.
     *
     * Each receiver replays it through its own copy of the body -- the move
     * axes and pace keys through `C_PlayerInput`, the saddle keys through
     * `C_RiderPlayerInput` -- so acceleration, gait, tags and the blend come
     * out of the native passes there rather than being guessed back from a
     * velocity that lags every edge. Discrete inputs travel as wrapping
     * counts rather than edges, because a count survives a dropped packet
     * and an edge does not.
     */
    struct PlayerInput {
        static constexpr float kMoveInputScale = 127.0F;

        // The counts wrap at eight on the wire, as CryEngine's jump counter
        // does (ui3). Receivers only look for a change.
        static constexpr std::uint8_t kCountMask = 7;

        /**
         * The raw move axes every move handler writes (`C_PlayerInput+0x294`):
         * x strafes, y goes forward, in the body's own frame, scaled by
         * `kMoveInputScale`. On foot they are the move vector; in the saddle
         * the stick before the riding curve, which the receiver's own stick
         * handler applies again.
         */
        std::int8_t moveX = 0;
        std::int8_t moveY = 0;

        /**
         * On foot, the walk/jog toggle (`C_PlayerInput+0x7C`), forced on while
         * an analog stick drives the body because the walk floor is skipped
         * for analog movement. In the saddle, the rider input's walk toggle
         * inverted (`C_RiderPlayerInput+0x9C6`).
         */
        bool run = true;

        /**
         * On foot, whether `PreUpdate` holds a sprint (request flag bit 2). In
         * the saddle, whether the rider input holds the gallop
         * (`C_RiderPlayerInput+0x9C7`).
         */
        bool sprint = false;

        /** Whether the body is crouched, as its actor model reports it. */
        bool crouch = false;

        /** How many jumps the owner's engine has accepted, wrapping. */
        std::uint8_t jumps = 0;

        /**
         * Whether the owner's rider input has a horse bound. The two saddle
         * counts below only travel while it does, so a body on foot pays one
         * bit for them; a receiver keeps the last counts it was sent and
         * takes the first ones of a ride as its base.
         */
        bool inSaddle = false;

        /**
         * `sprint` presses in the saddle: the gallop handler turns a second
         * one inside its double-press window into a spur, so each press has
         * to reach the receiver's handler rather than the held state alone.
         */
        std::uint8_t sprintPresses = 0;

        /** `horse_slowdown` presses in the saddle; the action is press-only. */
        std::uint8_t slowdownPresses = 0;

        bool IsMoving() const {
            return moveX != 0 || moveY != 0;
        }

        float AxisX() const {
            return static_cast<float>(moveX) / kMoveInputScale;
        }
        float AxisY() const {
            return static_cast<float>(moveY) / kMoveInputScale;
        }

        /** The input with its held movement let go, for a pose held past the newest packet. */
        void Release() {
            moveX  = 0;
            moveY  = 0;
            sprint = false;
        }

        template <typename Fields>
        void Serialize(Fields &fields) {
            // The stick at rest is most poses, so it costs a bit there.
            bool steering = IsMoving();
            fields.Field(steering);
            if (steering) {
                fields.Field(moveX);
                fields.Field(moveY);
            }
            else {
                moveX = 0;
                moveY = 0;
            }
            fields.Field(run);
            fields.Field(sprint);
            fields.Field(crouch);
            std::uint8_t wireJumps = jumps & kCountMask;
            fields.BitsRange(wireJumps, std::uint8_t {0}, kCountMask);
            if (!fields.Writing()) {
                jumps = wireJumps;
            }
            fields.Field(inSaddle);
            if (inSaddle) {
                std::uint8_t wireSprintPresses   = sprintPresses & kCountMask;
                std::uint8_t wireSlowdownPresses = slowdownPresses & kCountMask;
                fields.BitsRange(wireSprintPresses, std::uint8_t {0}, kCountMask);
                fields.BitsRange(wireSlowdownPresses, std::uint8_t {0}, kCountMask);
                if (!fields.Writing()) {
                    sprintPresses   = wireSprintPresses;
                    slowdownPresses = wireSlowdownPresses;
                }
            }
        }
    };
} // namespace KCDC::Shared::Entities
