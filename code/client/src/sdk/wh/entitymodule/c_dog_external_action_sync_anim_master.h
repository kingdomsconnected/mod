#pragma once

#include "../../types.h"
#include "../framework/i_action.h"
#include "../shared/c_signal.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_ActionActor;
    class C_Dog;

    /**
     * The action that plays the praise a person gives their dog.
     *
     * `E_ActorActionType::DogSyncAnimMaster`, queued on the *dog's* own
     * `C_ActionActor` -- the "master" in the name is the clip's master half,
     * not the person. `Enter` (`0x1828C417C`) chains into
     * `C_ActorActionSyncAnimMaster::Enter`, which is what puts the matching
     * `syncAnimMaster` action on the partner it was configured with, so one
     * queue call animates both bodies.
     *
     * Nothing about it goes through the dog's objective planner.
     * `C_Dog::Bond` (`0x1829DF430`) builds it, fills the block below and
     * queues it, and whatever objective the animal was running simply stops
     * while it is busy. `C_PetDogObjective` is a different path, and the game
     * never takes it.
     *
     * The factory allocates 0xF0 bytes.
     */
    class C_DogExternalActionSyncAnimMaster: public wh::framework::I_Action {
      public:
        /**
         * Builds one against a dog's action queue, unconfigured.
         *
         * Hands back the one reference the caller owns. Queueing takes its
         * own, so a caller that queues must still release this one.
         */
        static C_DogExternalActionSyncAnimMaster *Create(C_ActionActor *dogQueue);

        /**
         * States who the dog is bonding with, and which clip.
         *
         * Every field `C_Dog::Bond` writes is written here, including the ones
         * it clears, because the words between them are the action's own and
         * not something to leave to whatever the allocator returned.
         *
         * `partnerEntityId` is the person. `Bond` reads it off the *local*
         * player, which is the single reason it cannot simply be replayed on a
         * client that does not own the dog, and the only thing such a client
         * has to be told rather than resolve. `variant` is the roll that picks
         * between the praise animations -- the game takes it from its own LCG
         * at `0x18492D7E0`, so two clients left to themselves play different
         * ones.
         *
         * The clip ids are derived rather than carried: one comes from the
         * dog's own animation database and the other is a global, so every
         * client answers them the same way.
         */
        void Configure(EntityId partnerEntityId, std::uint32_t dogClipId, std::uint32_t sharedClipId, std::uint32_t variant);

        /** The person this action was configured to animate with. */
        EntityId GetPartnerEntityId() const {
            return m_partnerEntityId;
        }

        /** Which of the praise animations this one plays. */
        std::uint32_t GetVariant() const {
            return m_variant;
        }

      private:
        // C_Action<I_ActorAction> base ctor 0x1809CD638.
        std::byte m_actionVftable0010[0x8];     // 0x010, C_Action secondary vftable
        SDK::wh::shared::C_Signal m_signal0018; // 0x018, dispatch unk_185666858
        std::uint16_t m_value0028;              // 0x028, zeroed
        std::uint8_t m_value002A;               // 0x02A, zeroed
        std::byte m_padding002B[0x5];
        std::uint64_t m_value0030;              // 0x030, zeroed
        std::int32_t m_value0038;               // 0x038, -1
        std::int32_t m_value003C;               // 0x03C, -1, then zeroed by 0x1828936EC
        void *m_value0040;                      // 0x040, the base ctor's second argument (the queue's +0x48)
        std::uint32_t m_value0048;              // 0x048, zeroed
        std::byte m_opaque004C[0x4];            // 0x04C, not written
        EntityId m_partnerEntityId;   // 0x050
        std::uint32_t m_dogClipId;    // 0x054
        std::uint32_t m_reserved0058; // 0x058
        std::uint32_t m_reserved005C; // 0x05C
        std::uint32_t m_reserved0060; // 0x060
        std::uint32_t m_reserved0064; // 0x064
        std::uint32_t m_reserved0068; // 0x068
        std::uint32_t m_sharedClipId; // 0x06C
        std::uint32_t m_reserved0070; // 0x070
        std::uint32_t m_reserved0074; // 0x074
        std::uint32_t m_reserved0078; // 0x078
        std::uint32_t m_reserved007C; // 0x07C
        std::uint32_t m_reserved0080; // 0x080
        std::uint32_t m_variant;      // 0x084
        std::uint8_t m_flag0088;      // 0x088
        std::uint8_t m_flag0089;      // 0x089
        std::uint8_t m_flag008A;      // 0x08A
        std::uint8_t m_reserved008B;  // 0x08B, the one byte the native leaves as it found it
        std::uint32_t m_reserved008C; // 0x08C
        std::uint32_t m_mode0090;     // 0x090
        std::byte m_opaque0094[0x4];
        // C_ActorActionSyncAnimMaster ctor 0x182893EEC / factory 0x1829D54F4.
        std::byte m_actorObject0098[0x8]; // 0x098, C_ActorObject vtable
        C_ActionActor *m_pActionActor;    // 0x0A0
        std::uint16_t m_value00A8;        // 0x0A8, 0x1828936EC zeroes it
        std::byte m_padding00AA[0x2];
        std::uint32_t m_value00AC;        // 0x0AC, 0x1828936EC zeroes it
        void *m_value00B0;                // 0x0B0, ctor zeroes
        std::uint16_t m_state00B8;        // 0x0B8, ctor zeroes
        std::byte m_padding00BA[0x6];
        void *m_value00C0;                // 0x0C0, ctor zeroes
        std::byte m_syncState00C8[0x20];  // 0x0C8, built by 0x1811FFA0C with the actor's entity and "ActorActionSyncAnim"
        std::uint8_t m_flag00E8;          // 0x0E8, factory zeroes
        std::byte m_padding00E9[0x7];

        static void AssertLayout() {
            static_assert(offsetof(C_DogExternalActionSyncAnimMaster, m_signal0018) == 0x18, "C_DogExternalActionSyncAnimMaster::m_signal0018 offset mismatch");
            static_assert(offsetof(C_DogExternalActionSyncAnimMaster, m_value0040) == 0x40, "C_DogExternalActionSyncAnimMaster::m_value0040 offset mismatch");
            static_assert(offsetof(C_DogExternalActionSyncAnimMaster, m_value00AC) == 0xAC, "C_DogExternalActionSyncAnimMaster::m_value00AC offset mismatch");
            static_assert(offsetof(C_DogExternalActionSyncAnimMaster, m_partnerEntityId) == 0x50, "C_DogExternalActionSyncAnimMaster::m_partnerEntityId offset mismatch");
            static_assert(offsetof(C_DogExternalActionSyncAnimMaster, m_dogClipId) == 0x54, "C_DogExternalActionSyncAnimMaster::m_dogClipId offset mismatch");
            static_assert(offsetof(C_DogExternalActionSyncAnimMaster, m_sharedClipId) == 0x6C, "C_DogExternalActionSyncAnimMaster::m_sharedClipId offset mismatch");
            static_assert(offsetof(C_DogExternalActionSyncAnimMaster, m_variant) == 0x84, "C_DogExternalActionSyncAnimMaster::m_variant offset mismatch");
            static_assert(offsetof(C_DogExternalActionSyncAnimMaster, m_flag0088) == 0x88, "C_DogExternalActionSyncAnimMaster::m_flag0088 offset mismatch");
            static_assert(offsetof(C_DogExternalActionSyncAnimMaster, m_mode0090) == 0x90, "C_DogExternalActionSyncAnimMaster::m_mode0090 offset mismatch");
            static_assert(offsetof(C_DogExternalActionSyncAnimMaster, m_pActionActor) == 0xA0, "C_DogExternalActionSyncAnimMaster::m_pActionActor offset mismatch");
            static_assert(offsetof(C_DogExternalActionSyncAnimMaster, m_syncState00C8) == 0xC8, "C_DogExternalActionSyncAnimMaster::m_syncState00C8 offset mismatch");
            static_assert(offsetof(C_DogExternalActionSyncAnimMaster, m_flag00E8) == 0xE8, "C_DogExternalActionSyncAnimMaster::m_flag00E8 offset mismatch");
        }
    };

    static_assert(sizeof(C_DogExternalActionSyncAnimMaster) == 0xF0, "C_DogExternalActionSyncAnimMaster size mismatch");

    /**
     * The praise clip this dog's own animation database names, or 0xFFFFFFFF.
     *
     * `0x1829E12B0`. Per-animal, so every client resolves it from its own copy
     * of the body rather than being told.
     */
    std::uint32_t GetDogBondClipId(C_Dog *dog);

    /** The partner half of the same pair, which is global. `0x1829E12FC`. */
    std::uint32_t GetDogBondPartnerClipId();
} // namespace SDK::wh::entitymodule
