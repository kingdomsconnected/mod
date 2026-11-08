#pragma once

#include "core/feature_registry.h"

#include "shared/game/animation_tags.h"

#include "sdk/types.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace KCDC::Features::Npc {
    /**
     * What the locomotion hooks should impose on a replicated body.
     *
     * Published by whatever module owns that body -- an NPC, a dog -- and
     * read from the engine's animation update, which need not be the game
     * thread, so it travels through plain atomics rather than behind a lock.
     * One table for every kind of body, because the hooks are keyed on the
     * entity id and have no idea what kind of actor it belongs to.
     */
    struct LocomotionOverride {
        std::uint32_t state = 0; // 0 when the entity is not a replicated body

        std::uint8_t moveSpeedTag = Shared::Game::kNoAnimationTag;
        std::uint8_t moveDirTag   = Shared::Game::kNoAnimationTag;
        std::uint8_t stanceTag    = Shared::Game::kNoAnimationTag;

        // The body's movement this frame, handed to the animated character so
        // the engine derives the blend coordinate itself.
        float velocityX = 0.0F;
        float velocityY = 0.0F;
        float velocityZ = 0.0F;

        // Where the body should be facing. Handed to the animated character
        // for the same reason as the movement: it turns the entity itself
        // every frame, so a facing written from outside lasts until it does.
        float rotationX = 0.0F;
        float rotationY = 0.0F;
        float rotationZ = 0.0F;
        float rotationW = 1.0F;

        /** Bytes of a Mannequin tag state; `CTagState::kStateBytes`. */
        static constexpr std::size_t kTagStateBytes = 20;

        /**
         * The owner's whole tag state, for bodies that publish one.
         *
         * The three groups above are a pace, which is all an NPC needs. An animal carries more in its tags, so a dog sends its state
         * whole. Imposed before the three groups, so the pace the body is
         * drawn at still wins over the pace the raw state was captured at.
         */
        bool hasTagState                      = false;
        std::uint8_t tagState[kTagStateBytes] = {};
    };

    /**
     * The table the locomotion hooks read, and the bodies that publish into it.
     *
     * NPCs and dogs share the capacity: the hooks are keyed on the entity id
     * and have no idea what kind of actor it belongs to, so one service owns
     * the whole table rather than one per kind of body. Players and horses
     * are not in it: they move through their own native controllers.
     */
    class NpcLocomotionService final: public KCDC::Core::IFeatureService {
      public:
        /** What the locomotion hooks should impose on `entityId`, if anything. */
        LocomotionOverride GetLocomotionOverride(SDK::EntityId entityId);

        /**
         * Session ended: withdraws every entry.
         *
         * Only replicated bodies are ever in the table and they all go with
         * the session, so an entry that outlived one would impose a departed
         * body's animation on whoever the engine gives that entity id next.
         * Published through the same generation bracket as an ordinary
         * withdrawal, because the animation thread reads this table while it
         * is being cleared. Game thread only, like every other writer.
         */
        void Reset() override;

        /**
         * Publishes what a replicated body should be doing this frame.
         *
         * A `state` of 0 withdraws the entry, which is what a despawn does.
         */
        void PublishLocomotionOverride(SDK::EntityId entityId, std::uint32_t state, std::uint8_t moveSpeedTag, std::uint8_t moveDirTag, std::uint8_t stanceTag, const glm::vec3 &velocity, const glm::quat &rotation, const std::uint8_t *tagState = nullptr);

      private:
        /**
         * Bodies whose locomotion state the mod decides, and what it should be.
         *
         * A fixed table rather than a container because it is read from the
         * animation update, which need not be the game thread: entries are
         * written only from the game thread. A sequence guard lets the
         * animation thread reject a pose that overlapped a write, so it never
         * combines words from two frames or from two owners of a reused slot.
         */
        static constexpr std::size_t kMaxOverriddenBodies = 256;

        /**
         * One body's override, published as independently atomic words.
         *
         * Even generations are stable and odd generations are being written.
         * The writer brackets every mutation; a reader accepts the words only
         * when the same even generation surrounds the complete read.
         */
        struct LocomotionSlot {
            std::atomic<std::uint32_t> generation {0};
            std::atomic<std::uint64_t> identity {0};   // entity id | tags | state
            std::atomic<std::uint64_t> velocityXY {0}; // as float bits, two to a word
            std::atomic<std::uint32_t> velocityZ {0};  // as float bits
            std::atomic<std::uint64_t> rotationXY {0};
            std::atomic<std::uint64_t> rotationZW {0};

            // The twenty raw tag bytes, three words wide. The last word holds
            // four of them; its top byte says whether this body published a
            // state at all, since a body that did not must not have its tags
            // replaced with twenty zeroes.
            std::atomic<std::uint64_t> tagState0 {0};
            std::atomic<std::uint64_t> tagState1 {0};
            std::atomic<std::uint64_t> tagState2 {0};
        };

        static_assert(std::atomic<std::uint32_t>::is_always_lock_free && std::atomic<std::uint64_t>::is_always_lock_free, "Locomotion publication must remain lock-free");

        std::array<LocomotionSlot, kMaxOverriddenBodies> _locomotionOverrides {};
        bool _capacityWarningIssued = false; // game-thread writer only

        /**
         * Which slot an entity's override is in, by the entity's index: the
         * low 18 bits of KCD2's salted EntityId. The hooks ask for every
         * actor in the world every frame and almost all of them have no
         * override, so a miss is one load here instead of a walk of the
         * table; a hit still checks the full id, salt included, against the
         * slot. Slot number plus one; zero is none.
         */
        static constexpr std::uint32_t kEntityIndexMask = 0x3FFFF;
        std::array<std::atomic<std::uint16_t>, kEntityIndexMask + 1> _slotByEntityIndex {};

        // Overrides currently published; zero answers every lookup at once.
        std::atomic<std::uint32_t> _publishedCount {0};
    };
} // namespace KCDC::Features::Npc
