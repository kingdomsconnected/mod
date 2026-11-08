#pragma once

#include "../cryengine/entitysystem/i_entity_system_sink.h"
#include "../cryengine/action/i_game_framework_listener.h"
#include "../cryengine/system/i_system_event_listener.h"
#include "c_streamable_entity_class.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    class IEntitySystem;
}

namespace SDK::wh {
    template <typename T>
    struct EntityStreamingVectorLayout {
        T *begin;
        T *end;
        T *capacityEnd;
    };

    /**
     * Warhorse entity-streaming manager owned by CEntitySystem.
     *
     * RTTI and destruction establish IEntitySystemSink at +0x00,
     * ISystemEventListener at +0x08, and IGameFrameworkListener at +0x10.
     * The owner allocation and scalar destructor prove size 0x158. Its
     * primary table adds no entries beyond IEntitySystemSink.
     */
    class C_EntityStreamingManager : public IEntitySystemSink,
                                     public ISystemEventListener,
                                     public IGameFrameworkListener {
      public:
        ~C_EntityStreamingManager() override = default;

        IEntitySystem *GetEntitySystem() const { return m_pEntitySystem; }

        const EntityStreamingVectorLayout<C_StreamableEntityClass *> &
        GetStreamingRecords() const {
            return m_streamingRecords;
        }

      private:
        IEntitySystem *m_pEntitySystem; // 0x018
        std::byte m_streamingClassPool[0x68]; // 0x020, native pool state
        EntityStreamingVectorLayout<C_StreamableEntityClass *>
            m_streamingRecords; // 0x088
        std::uint32_t m_state00A0; // 0x0A0, initialized to 2
        std::byte m_padding00A4[0x4];
        std::byte m_streamingState00A8[0x48]; // 0x0A8
        std::byte m_streamingState00F0[0x30]; // 0x0F0
        std::byte m_streamingState0120[0x8];  // 0x120
        std::byte m_streamingState0128[0x20]; // 0x128
        void *m_pState0148;                   // 0x148
        bool m_state0150;                     // 0x150
        std::byte m_padding0151[0x7];
    };

    static_assert(sizeof(EntityStreamingVectorLayout<C_StreamableEntityClass *>) ==
                      0x18,
                  "EntityStreamingVectorLayout size mismatch");
    static_assert(sizeof(C_EntityStreamingManager) == 0x158,
                  "C_EntityStreamingManager size mismatch");
} // namespace SDK::wh
