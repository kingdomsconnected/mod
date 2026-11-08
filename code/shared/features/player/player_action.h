#pragma once

#include <networking/replication/network_entity.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace KCDC::Shared::Game {
    /**
     * The stances a player's body is published in, numbered as the engine's
     * own `E_StanceCategory` so a receiver hands the value straight back.
     *
     * Only the three a body takes on a bench, a bed or the ground travel.
     * Horse, cart, crouch and kneel belong to their own replication, and a
     * body in any of them is published as standing so this never fights it.
     */
    enum class BodyStance : std::uint8_t {
        Standing = 1,
        Lying    = 2,
        Sitting  = 3,
    };

    /**
     * Whether the owner's body is sitting or lying, and on which side of
     * what it sits on.
     *
     * Held state, read off the owner's own actor model every tick: a receiver
     * puts its puppet's model in the same stance, on the same seat, and the
     * puppet's own idle controller then plays the seat's idle and sets the
     * Mannequin tags that keep it there, exactly as it does for a body the
     * game seats itself. A player who joins while somebody sits sees them
     * sitting.
     */
    struct BodyStanceState {
        BodyStance stance = BodyStance::Standing;

        /** The engine's stance side, which picks among side-specific idles. */
        std::uint8_t side = 0;

        /**
         * What the body sits or lies on, as its level EntityGUID, or 0.
         *
         * The engine names the seat by WUID, which is this machine's own; the
         * level EntityGUID is the same number everywhere and each receiver
         * turns it back into its WUID. It is not decoration: the seat's own
         * animation list is the only source of the stance's global tags, and
         * without them the engine stands the body back up.
         */
        std::uint64_t seatGuid = 0;

        /**
         * The reading the body is in, as the engine's `E_ActorState`: 41
         * `reading` (a book on a stand), 42 `readingFromInventory` (a book in
         * hand), or 0. A receiver sets the same state on its body's model,
         * and the actor's own tag update turns it into the Mannequin tag the
         * reading idles are keyed on -- standing or seated alike.
         */
        std::uint8_t reading = 0;

        // The model's HoldingDecoy state selects the crouched pebble idle
        // and the draw/throw/place fragments' holdingDecoy global tag.
        bool holdingDecoy = false;

        bool operator==(const BodyStanceState &) const = default;

        bool IsSeatedOrLying() const {
            return stance == BodyStance::Sitting || stance == BodyStance::Lying;
        }

        static bool IsKnown(std::uint8_t value) {
            return value >= static_cast<std::uint8_t>(BodyStance::Standing) && value <= static_cast<std::uint8_t>(BodyStance::Sitting);
        }

        static constexpr std::uint8_t kReading              = 41;
        static constexpr std::uint8_t kReadingFromInventory = 42;

        static bool IsReading(std::uint8_t value) {
            return value == kReading || value == kReadingFromInventory;
        }

        void Serialize(Framework::Networking::Replication::FieldSerializer &fields) {
            auto wire = static_cast<std::uint8_t>(stance);
            fields.Field(wire);
            fields.Field(side);
            fields.Field(seatGuid);
            fields.Field(reading);
            fields.Field(holdingDecoy);
            if (!fields.Writing()) {
                stance = IsKnown(wire) ? static_cast<BodyStance>(wire) : BodyStance::Standing;
                if (!IsReading(reading)) {
                    reading = 0;
                }
            }
        }
    };

    /**
     * The last thing the owner's body did that other players have to see: an
     * animation from the body action catalog (`SitDown`, `Picking`,
     * `WallLook`), the end of a looping one, or a praise of its dog.
     *
     * Transitions and interactions are captured where the owner's controller
     * queues them and replayed with the same fragment, fragment tags, option
     * and priority. The tags are what fit the animation to its object
     * (`sitPlaceBench`, `sitSideBackLeft`, `noTable`), and no receiver could
     * work them out, so they travel. A praise is a paired animation the dog
     * starts (`C_Dog::Bond`), so it travels as the dog and the variant and is
     * replayed through the receiver's copy of that dog.
     *
     * Held state with a revision, like `AnimationPlayback`: a receiver plays a
     * revision it watched change, and one that was already current when it
     * first saw the body -- something done before it joined -- is not played.
     * Names travel as the CRC-32 of their lowercase spelling, Mannequin's own
     * key, and each receiver resolves them against its own dictionaries.
     */
    struct BodyAction {
        static constexpr std::size_t kMaxTags       = 8;
        static constexpr std::uint8_t kOptionRandom = 0xFF;
        static constexpr std::uint32_t kPickingFragment = 0x03E46E25U;
        static constexpr std::uint32_t kButcheringFragment = 0x36F0D7F9U;

        enum class Kind : std::uint8_t {
            None = 0,

            /** Play `fragment` with `tags`, `option` and `priority`. */
            Fragment = 1,

            /** The owner's tracked action ended; stop the one playing. */
            Stop = 2,

            /** Praise `partner`, a dog, with `variant`. */
            Praise = 3,
        };

        /** Bumped by every action; 0 means none has happened. */
        std::uint32_t revision = 0;

        Kind kind = Kind::None;

        /** The fragment's name CRC. */
        std::uint32_t fragment = 0;

        /** The option the owner's game asked for, or `kOptionRandom`. */
        std::uint8_t option = kOptionRandom;

        /** Mannequin's action priority, as the owner's game queued it. */
        std::uint8_t priority = 0;

        /** The fragment's own tags, as name CRCs, `tagCount` of them. */
        std::uint8_t tagCount = 0;
        std::uint32_t tags[kMaxTags] {};

        // Picking's native blend space: height, angle and pose variation.
        bool hasPickingMotion = false;
        std::array<float, 3> pickingMotion {};

        /**
         * The world transform the owner's fragment was aligned to, the
         * Mannequin param `TargetPos`: a seat's sit point, read by the
         * fragment's PositionAdjust clips that slide the body onto it.
         * Rotation (x, y, z, w), then position.
         */
        bool hasTarget = false;
        std::array<float, 4> targetRotation {0.0F, 0.0F, 0.0F, 1.0F};
        std::array<float, 3> targetPosition {};

        /** The dog a praise is given to, as its network id. */
        std::uint64_t partner = 0;

        /** Which of the praise's animations the owner's game rolled. */
        std::uint32_t variant = 0;

        bool operator==(const BodyAction &) const = default;

        // The three upper-body pebble fragments in the generated catalog.
        static constexpr std::uint32_t kDecoyDraw  = 0x99056183U;
        static constexpr std::uint32_t kDecoyThrow = 0xAB7479E0U;
        static constexpr std::uint32_t kDecoyPlace = 0x90FA6B1AU;

        bool IsDecoy() const {
            return kind == Kind::Fragment && (fragment == kDecoyDraw || fragment == kDecoyThrow || fragment == kDecoyPlace);
        }

        bool IsSane() const {
            if (hasPickingMotion) {
                if (kind != Kind::Fragment || fragment != kPickingFragment) {
                    return false;
                }
                for (const float value : pickingMotion) {
                    if (!std::isfinite(value) || value < 0.0F || value > 1.0F) {
                        return false;
                    }
                }
            }
            if (hasTarget) {
                if (kind != Kind::Fragment) {
                    return false;
                }
                for (const float value : targetRotation) {
                    if (!std::isfinite(value)) {
                        return false;
                    }
                }
                for (const float value : targetPosition) {
                    if (!std::isfinite(value)) {
                        return false;
                    }
                }
            }
            switch (kind) {
            case Kind::None: return revision == 0;
            case Kind::Fragment: return revision != 0 && fragment != 0 && tagCount <= kMaxTags;
            case Kind::Stop: return revision != 0;
            case Kind::Praise: return revision != 0 && partner != 0;
            }
            return false;
        }

        static std::uint32_t Advance(std::uint32_t revision) {
            return revision + 1 != 0 ? revision + 1 : 1;
        }

        /** Every field is written, used or not: the delta channel matches fields by call order. */
        void Serialize(Framework::Networking::Replication::FieldSerializer &fields) {
            auto wireKind = static_cast<std::uint8_t>(kind);
            fields.Field(revision);
            fields.Field(wireKind);
            fields.Field(fragment);
            fields.Field(option);
            fields.Field(priority);
            fields.Field(tagCount);
            for (std::uint32_t &tag : tags) {
                fields.Field(tag);
            }
            fields.Field(hasPickingMotion);
            for (float &value : pickingMotion) {
                fields.Field(value);
            }
            fields.Field(hasTarget);
            for (float &value : targetRotation) {
                fields.Field(value);
            }
            for (float &value : targetPosition) {
                fields.Field(value);
            }
            fields.Field(partner);
            fields.Field(variant);
            if (!fields.Writing()) {
                kind = wireKind <= static_cast<std::uint8_t>(Kind::Praise) ? static_cast<Kind>(wireKind) : Kind::None;
            }
        }
    };

    /**
     * The minigame actions the owner's body has queued or is playing, oldest
     * first, and how far a lockpicking turn has got.
     *
     * The lockpicking, alchemy and smithing minigames animate their player
     * through plain Mannequin actions on the player's own controller.
     * Lockpicking (`C_LockPicking::CreateAndQueueAction`) queues an idle and a
     * loop ahead of it, the turn, the turn's success or failure, a failed idle
     * and a way out; alchemy (`C_AlchemyActionDirector`) queues taking, using,
     * putting and returning each thing on the table (`AlchemyTake`,
     * `AlchemyUse`, `AlchemyPut*`, `AlchemyReturn`), its idles and the book's;
     * smithing (`C_Blacksmithing::QueueAnimationAction`) one `Blacksmithing*`
     * step per state the forge, anvil and quench pass through. Each often
     * queues two in one frame and ends others in that same frame, and which
     * of them plays is the controller's call, made from their order and
     * priorities. No single last action describes that, so this is the list
     * itself: each action with the fragment, its tags and the priority the
     * owner's game gave it. A receiver queues each one it has not queued yet,
     * in this order, and ends each one that has left the list, which hands
     * its controller the queue the owner's had.
     *
     * Names travel as the CRC-32 of their lowercase spelling, as in
     * `BodyAction`.
     */
    struct MinigameActions {
        static constexpr std::size_t kMaxActions = 6;

        /** The most tags one of them carries: `AlchemyUse` with `alchemyOil+alchemyPotUpPosition+slaveBellows`. */
        static constexpr std::size_t kMaxTags = 3;

        /** LockpickingTurn, whose animation is posed by `turnFraction` rather than played. */
        static constexpr std::uint32_t kTurnFragment = 0xDDB1D076U;

        /**
         * The `IAction` flags an action carries from its constructor and a
         * receiver builds its own with. Alchemy's idles are made with 0x6
         * (0x1806C6A7C); 0x4 is what IAction::Interrupt (0x181E3E120) returns
         * for the controller to queue an interrupted action again. Lockpicking
         * makes its actions with none.
         */
        static constexpr std::uint8_t kReplayedFlags = 0x6;

        struct Action {
            /** Counted by the owner, so a receiver knows an action it has already queued. */
            std::uint8_t id        = 0;
            std::uint32_t fragment = 0;

            /**
             * The fragment's tags: the lock's (`chest`, `cartChest`, `pillory`;
             * none for a door), the table object and its position
             * (`alchemyMortar`, `alchemyPotUpPosition`), or the workpiece's
             * and the forge's (`sword`, `forgeBag`, `slaveTongs`).
             */
            std::uint8_t tagCount = 0;
            std::uint32_t tags[kMaxTags] {};

            std::uint8_t priority = 0;

            /** Its constructor's flags, within kReplayedFlags. */
            std::uint8_t flags = 0;

            bool operator==(const Action &) const = default;
        };

        std::uint8_t count = 0;
        Action actions[kMaxActions] {};

        /**
         * The turn's `TurnFraction`, 0..255 for 0..1: the lock's angle, which
         * the owner's game writes on its turn action and the fragment's
         * LayerManualUpdate clip turns into the hands' pose.
         */
        std::uint8_t turnFraction = 0;

        bool operator==(const MinigameActions &) const = default;

        bool IsSane() const {
            if (count > kMaxActions) {
                return false;
            }
            for (std::uint8_t i = 0; i < count; ++i) {
                if (actions[i].fragment == 0 || actions[i].tagCount > kMaxTags || (actions[i].flags & ~kReplayedFlags) != 0) {
                    return false;
                }
            }
            return true;
        }

        /** Every field is written, used or not: the delta channel matches fields by call order. */
        void Serialize(Framework::Networking::Replication::FieldSerializer &fields) {
            fields.Field(count);
            for (Action &action : actions) {
                fields.Field(action.id);
                fields.Field(action.fragment);
                fields.Field(action.tagCount);
                for (std::uint32_t &tag : action.tags) {
                    fields.Field(tag);
                }
                fields.Field(action.priority);
                fields.Field(action.flags);
            }
            fields.Field(turnFraction);
        }
    };
} // namespace KCDC::Shared::Game
