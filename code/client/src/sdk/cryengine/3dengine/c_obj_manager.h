#pragma once

#include "i_stat_inst_group.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * The 3D engine's object manager, as far as its vegetation groups go:
     * the table a group id indexes, wherever the engine carries one -- a
     * `CVegetation`, a merged-mesh group header.
     *
     * It has no table of its own and no RTTI, so the identity is the role
     * stock CryEngine gives `CObjManager`, and only the one member two
     * natives agree on is laid out. `C3DEngine::GetStatInstGroup`
     * (0x183493338) bounds an id against the 0x1A0-byte rows between +0x10
     * and +0x18; the merged-mesh walkers (0x18043C258, 0x18043C598) index
     * +0x10 the same way without the check.
     *
     * Read the rows in place. `GetStatInstGroup` copies one out instead and
     * takes a reference on its mesh and material for the copy, which nothing
     * on this side would ever give back.
     */
    class CObjManager {
      public:
        /** The row for `id`, or null for an id past the table. Valid until the level's groups are reloaded. */
        const IStatInstGroup *FindStatInstGroup(std::int32_t id) const {
            if (id < 0 || m_pStatInstGroupsBegin == nullptr || static_cast<std::ptrdiff_t>(id) >= m_pStatInstGroupsEnd - m_pStatInstGroupsBegin) {
                return nullptr;
            }
            return m_pStatInstGroupsBegin + id;
        }

        /** Null until the 3D engine has made it. */
        static CObjManager *GetInstance();

      private:
        std::byte m_opaque0000[0x10];
        IStatInstGroup *m_pStatInstGroupsBegin; // 0x10
        IStatInstGroup *m_pStatInstGroupsEnd;   // 0x18

        static void AssertLayout() {
            static_assert(offsetof(CObjManager, m_pStatInstGroupsBegin) == 0x10, "CObjManager::m_pStatInstGroupsBegin offset mismatch");
            static_assert(offsetof(CObjManager, m_pStatInstGroupsEnd) == 0x18, "CObjManager::m_pStatInstGroupsEnd offset mismatch");
        }
    };
} // namespace SDK
