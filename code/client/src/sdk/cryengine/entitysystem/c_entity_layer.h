#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Entity streaming/visibility layer interface owned by CEntitySystem.
     *
     * CEntityLayer's standalone table at 0x1840768C0 contains exactly ten
     * entries. Its factory allocation and scalar deleting destructor both
     * prove the complete 0xC8-byte size.
     */
    class IEntityLayer {
      public:
        virtual ~IEntityLayer()          = default; // +0x00
        virtual void unk_fn0001()        = 0;
        virtual void unk_fn0002()        = 0;
        virtual void unk_fn0003()        = 0;
        virtual void unk_fn0004()        = 0;
        virtual void unk_fn0005()        = 0;
        virtual bool IsLoaded() const    = 0; // +0x30
        virtual bool IsUnloading() const = 0; // +0x38
        virtual bool IsUnloaded() const  = 0; // +0x40
        virtual void unk_fn0009()        = 0; // +0x48
    };

    static_assert(sizeof(IEntityLayer) == 0x8, "IEntityLayer size mismatch");

    /** Concrete KCD2 layer object behind IEntitySystem's typed interface. */
    class CEntityLayer: public IEntityLayer {
      public:
        /**
         * Whether the layer's entities and static lights are in the world.
         * Written only by CEntityLayer::EnableEntities (0x1809B67AC).
         *
         * Not a general "is the layer on" query: the constructor hard-codes it
         * to true and level load builds every layer through AddLayer without
         * calling Enable, so a layer the game has never switched reads true.
         */
        bool IsEnabled() const {
            return m_isEnabled;
        }

        /**
         * Whether the layer's brushes -- and with them their physics -- are in
         * the world. Written only by the EnableBrushes half of
         * CEntityLayer::Enable (0x1809B6270).
         *
         * This is the byte that answers for a collision-only layer, one
         * carrying `physics/layer<id>.dat` and no entities at all.
         * IEntitySystem::EnableLayer drives both halves, so a layer switched
         * through the interface moves both bytes together.
         */
        bool IsEnabledBrush() const {
            return m_isEnabledBrush;
        }

      private:
        // Members initialized by constructor 0x1809B5C64.
        std::int32_t m_specs;                 // 0x08
        std::uint32_t m_padding000C;
        SDK::CryStringT<char> m_name;         // 0x10
        SDK::CryStringT<char> m_parentName;   // 0x18
        bool m_isEnabled;      // 0x20, hard-coded true by the constructor
        bool m_isEnabledBrush; // 0x21, seeded from AddLayer's last argument
        bool m_isSerialized;   // 0x22, hard-coded true by the constructor
        bool m_havePhysics;    // 0x23
        bool m_defaultLoaded;  // 0x24
        bool m_state0025;     // 0x25, the constructor's seventh argument
        bool m_wasReEnabled;  // 0x26
        std::byte m_padding0027[0x1];
        std::uint16_t m_id; // 0x28
        std::byte m_padding002A[0x6];
        CEntityLayer **m_childs;              // 0x30, std::vector<CEntityLayer *>
        CEntityLayer **m_childsEnd;           // 0x38
        CEntityLayer **m_childsCapacity;      // 0x40
        std::byte m_entities[0x40];           // 0x48, TEntityProps, an std::unordered_map keyed by EntityId
        void *m_pGarbageHeaps;                // 0x88, TGarbageHeaps
        void *m_pHeap;                        // 0x90, IGeneralMemoryHeap
        void *m_lock;                         // 0x98, SRWLOCK
        std::uint32_t m_state00A0;            // 0xA0
        std::uint32_t m_padding00A4;
        std::uint64_t m_state00A8[4];         // 0xA8, zeroed

        static void AssertLayout() {
            static_assert(offsetof(CEntityLayer, m_name) == 0x10, "CEntityLayer::m_name offset mismatch");
            static_assert(offsetof(CEntityLayer, m_entities) == 0x48, "CEntityLayer::m_entities offset mismatch");
            static_assert(offsetof(CEntityLayer, m_lock) == 0x98, "CEntityLayer::m_lock offset mismatch");
            static_assert(offsetof(CEntityLayer, m_isEnabled) == 0x20, "CEntityLayer::m_isEnabled offset mismatch");
            static_assert(offsetof(CEntityLayer, m_isEnabledBrush) == 0x21, "CEntityLayer::m_isEnabledBrush offset mismatch");
            static_assert(offsetof(CEntityLayer, m_isSerialized) == 0x22, "CEntityLayer::m_isSerialized offset mismatch");
            static_assert(offsetof(CEntityLayer, m_havePhysics) == 0x23, "CEntityLayer::m_havePhysics offset mismatch");
            static_assert(offsetof(CEntityLayer, m_defaultLoaded) == 0x24, "CEntityLayer::m_defaultLoaded offset mismatch");
            static_assert(offsetof(CEntityLayer, m_id) == 0x28, "CEntityLayer::m_id offset mismatch");
        }
    };

    static_assert(sizeof(CEntityLayer) == 0xC8, "CEntityLayer size mismatch");
} // namespace SDK
