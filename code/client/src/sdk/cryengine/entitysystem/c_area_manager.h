#pragma once

#include "../system/i_system_event_listener.h"
#include "i_area_manager.h"

#include <cstddef>

namespace SDK {
    /** Exact MSVC three-pointer vector layout used by CAreaManager. */
    template <typename T>
    struct AreaManagerVectorLayout {
        T *begin;
        T *end;
        T *capacityEnd;
    };

    /**
     * Concrete spatial-area manager owned by CEntitySystem.
     *
     * RTTI and destruction establish IAreaManager at +0x00 and
     * ISystemEventListener at +0x08. Its 17-slot primary table adds nothing
     * beyond IAreaManager, and owner allocation proves the exact 0x180 size.
     */
    class CAreaManager : public IAreaManager, public ISystemEventListener {
      public:
        ~CAreaManager() override = default;

        const AreaManagerVectorLayout<IArea *> &GetAreas() const {
            return m_areas;
        }

        bool AreAreasDirty() const {
            return m_areasDirty;
        }

        const AreaManagerVectorLayout<IAreaManagerEventListener *> &
        GetEventListeners() const {
            return m_eventListeners;
        }

        const AreaManagerVectorLayout<EntityId> &GetEntitiesToUpdate() const {
            return m_entitiesToUpdate;
        }

      private:
        AreaManagerVectorLayout<IArea *> m_areas; // 0x010
        bool m_areasDirty;                        // 0x028
        std::byte m_padding0029[0x7];
        std::byte m_spatialAreaIndex[0x38];       // 0x030
        AreaManagerVectorLayout<void *> m_opaqueRecords0068; // 0x068
        AreaManagerVectorLayout<IAreaManagerEventListener *>
            m_eventListeners;                    // 0x080
        AreaManagerVectorLayout<std::byte> m_opaqueRecords0098; // 0x098
        // From the constructor 0x1810D4FF0: two empty vectors, then two
        // more reserved to sixteen entries each.
        AreaManagerVectorLayout<void *> m_records00B0; // 0x0B0
        AreaManagerVectorLayout<void *> m_records00C8; // 0x0C8
        AreaManagerVectorLayout<void *> m_reserved00E0[2]; // 0x0E0, reserved to 16 by 0x180440378
        void *m_updateLock;                       // 0x110, RTL_SRWLOCK
        AreaManagerVectorLayout<EntityId> m_entitiesToUpdate; // 0x118
        std::int32_t m_state0130;                 // 0x130, 2 by default
        std::uint32_t m_padding0134;
        std::uint64_t m_state0138[2];             // 0x138
        std::byte m_state0148[0x30];              // 0x148, zeroed
        std::int32_t m_state0178;                 // 0x178, -1 by default
        std::uint32_t m_state017C;                // 0x17C
    };

    static_assert(sizeof(AreaManagerVectorLayout<IArea *>) == 0x18,
                  "AreaManagerVectorLayout size mismatch");
    static_assert(sizeof(CAreaManager) == 0x180, "CAreaManager size mismatch");
} // namespace SDK
