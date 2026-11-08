#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    /**
     * A named list of souls the game draws from to populate a guard post, a
     * bandit camp or a random-event group.
     *
     * A `SoulPoolDatabase` row rather than a live object, and only the list:
     * how many souls appear, where they stand and which have been used
     * already all belong to the spawn context its caller passes alongside it.
     * `C_StaticSoulPool` and `C_SingleSoulPool` share this base's
     * `SpawnSouls`, which is vtable slot 2.
     */
    class C_SoulPoolBase {
      public:
        /**
         * The request id that means "this produced no entity", minted by
         * `C_EntitySpawnerQueue::SubmitSpawnRequest` at 0x180628A5A and
         * dropped by `SpawnSouls` at 0x180627023.
         */
        static constexpr std::int32_t kSpawnRequestRefused = -2;

        const CryStringT<char> &GetName() const {
            return m_name;
        }

        const StdVector<CryStringT<char>> &GetSouls() const {
            return m_souls;
        }

      private:
        void *m_vftable;                     // 0x00
        CryStringT<char> m_name;             // 0x08, stamped onto a spawn request at +0x68
        std::uint32_t m_value0010;           // 0x10; ctor sub_1813D0118 writes 0
        float m_value0014;                   // 0x14; C_StaticSoulPool writes 1.0f
        StdVector<CryStringT<char>> m_souls; // 0x18

        static void AssertLayout() {
            static_assert(offsetof(C_SoulPoolBase, m_name) == 0x08, "C_SoulPoolBase::m_name offset mismatch");
            static_assert(offsetof(C_SoulPoolBase, m_value0010) == 0x10, "C_SoulPoolBase::m_value0010 offset mismatch");
            static_assert(offsetof(C_SoulPoolBase, m_souls) == 0x18, "C_SoulPoolBase::m_souls offset mismatch");
        }
    };

    // `make_shared<C_SingleSoulPool>` asks for 0x40 bytes behind a 0x10 control block, and that kind adds nothing of its own.
    static_assert(sizeof(C_SoulPoolBase) == 0x30, "C_SoulPoolBase size mismatch");
} // namespace SDK::wh::rpgmodule
