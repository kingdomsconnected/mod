#pragma once

#include "c_combat_actor_object.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /** Constructor-initialized inline procedural weapon pose state. */
    struct WeaponProcPoseStateLayout {
        std::byte m_state[0xD4];
    };

    /** ABI header of one native range_t-to-range_t map. */
    struct WeaponProcRangeMapLayout {
        void *m_pHead;
        std::size_t m_size;
    };

    /** Constructor-initialized physics/collision query state. */
    struct WeaponProcCollisionStateLayout {
        std::byte m_state[0xDC];
    };

    static_assert(sizeof(WeaponProcPoseStateLayout) == 0xD4,
                  "WeaponProcPoseStateLayout size mismatch");
    static_assert(sizeof(WeaponProcRangeMapLayout) == 0x10,
                  "WeaponProcRangeMapLayout size mismatch");
    static_assert(sizeof(WeaponProcCollisionStateLayout) == 0xDC,
                  "WeaponProcCollisionStateLayout size mismatch");

    /**
     * Procedural combat weapon solver associated with an actor.
     *
     * Destruction proves C_CombatActorObject as the direct base. The table at
     * 0x183B91370 contains exactly the six inherited entries and adds no
     * operations; the following data proves its boundary. Construction splits
     * the object into an inline pose block, solver indices/control state, two
     * range maps, and a physics/collision query block. Destruction independently
     * erases both maps. Its owner allocates exactly 0x3C8 bytes.
     */
    class C_WeaponProcSolver : public C_CombatActorObject {
      public:
        ~C_WeaponProcSolver() override = default;

        WeaponProcPoseStateLayout *GetPoseState() { return &m_poseState; }
        const WeaponProcPoseStateLayout *GetPoseState() const {
            return &m_poseState;
        }
        static constexpr std::size_t GetRangeMapCount() { return 2; }
        const WeaponProcRangeMapLayout *GetRangeMaps() const {
            return m_rangeMaps;
        }
        const WeaponProcCollisionStateLayout *GetCollisionState() const {
            return &m_collisionState;
        }

      private:
        std::uint16_t m_state0010;
        bool m_state0012;
        std::byte m_padding0013;
        WeaponProcPoseStateLayout m_poseState;   // 0x14
        WeaponProcPoseStateLayout *m_pPoseState; // 0xE8
        std::uint32_t m_state00F0;
        std::byte m_state00F4[0x24];
        std::uint16_t m_indices0118[7];
        std::byte m_state0126[0x4];
        std::uint16_t m_state012A;
        std::uint16_t m_state012C;
        std::byte m_padding012E[0x2];
        std::uint32_t m_state0130;
        std::byte m_state0134[0x4];
        std::uint64_t m_state0138;
        std::uint64_t m_state0140;
        std::byte m_state0148[0x4];
        std::uint32_t m_state014C;
        void *m_pState0150;
        void *m_pState0158;
        std::uint32_t m_state0160;
        std::byte m_state0164[0x8];
        std::uint32_t m_state016C;
        WeaponProcRangeMapLayout m_rangeMaps[2]; // 0x170
        std::byte m_rangeMapState0190[0x10];
        std::uint32_t m_state01A0;
        std::byte m_state01A4[0x10];
        std::byte m_state01B4[0x8];
        std::byte m_state01BC[0xF8];
        bool m_state02B4;
        std::byte m_padding02B5[0x13];
        WeaponProcCollisionStateLayout m_collisionState; // 0x2C8
        std::byte m_state03A4[0x8];
        std::byte m_padding03AC[0x4];
        void *m_pState03B0;
        void *m_pState03B8;
        void *m_pState03C0;
    };

    static_assert(sizeof(C_WeaponProcSolver) == 0x3C8,
                  "C_WeaponProcSolver size mismatch");
} // namespace SDK::wh::combatmodule
