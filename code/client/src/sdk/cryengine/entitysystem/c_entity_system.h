#pragma once

#include "c_entity_archetype_manager.h"
#include "i_entity_system.h"

#include <cstddef>

namespace SDK {
    class CAreaManager;
    class CBreakableManager;
    class CComponentEventDistributer;
    class CEntityLoadManager;
    class CEntityPoolManager;
    namespace wh {
        class C_EntityStreamingManager;
    }

    /**
     * Concrete entity-system implementation used for entity and prop spawn.
     *
     * CEntitySystem's primary table at 0x184077048 contains exactly the 98
     * IEntitySystem entries and adds no primary virtual methods. Its factory
     * allocates exactly 0x601BB0 bytes. The additional UnsafeOp RTTI base at
     * +0x08 and all entity-pool state are retained as opaque storage.
     */
    class CEntitySystem: public IEntitySystem {
      public:
        ~CEntitySystem() override = default;

        static CEntitySystem *GetInstance() {
            return static_cast<CEntitySystem *>(IEntitySystem::GetInstance());
        }

        ISystem *GetSystem() const override {
            return reinterpret_cast<ISystem *>(m_pSystem);
        }

        // SDK convenience accessor for the owner field proven at +0x600278;
        // this is not presented as an original CEntitySystem virtual method.
        CEntityPoolManager *GetPoolManager() {
            return m_pEntityPoolManager;
        }

        const CEntityPoolManager *GetPoolManager() const {
            return m_pEntityPoolManager;
        }

        IEntityPoolManager *GetIEntityPoolManager() const override {
            return reinterpret_cast<IEntityPoolManager *>(m_pEntityPoolManager);
        }

        // SDK convenience accessor for the owner field proven at +0x600260;
        // this is not presented as an original CEntitySystem virtual method.
        wh::C_EntityStreamingManager *GetStreamingManager() {
            return m_pEntityStreamingManager;
        }

        const wh::C_EntityStreamingManager *GetStreamingManager() const {
            return m_pEntityStreamingManager;
        }

        // SDK convenience accessor for the owner field proven at +0x601AF0;
        // this is not presented as an original CEntitySystem virtual method.
        CBreakableManager *GetBreakableManager() {
            return m_pBreakableManager;
        }

        IBreakableManager *GetBreakableManager() const override {
            return reinterpret_cast<IBreakableManager *>(m_pBreakableManager);
        }

        // SDK convenience accessor for the owner field proven at +0x600200;
        // this is not presented as an original CEntitySystem virtual method.
        CComponentEventDistributer *GetComponentEventDistributer() {
            return m_pComponentEventDistributer;
        }

        const CComponentEventDistributer *GetComponentEventDistributer() const {
            return m_pComponentEventDistributer;
        }

        // SDK convenience accessor for the native XML/batch spawn manager
        // proven at +0x600268. This is not an original virtual method.
        CEntityLoadManager *GetEntityLoadManager() {
            return m_pEntityLoadManager;
        }

        const CEntityLoadManager *GetEntityLoadManager() const {
            return m_pEntityLoadManager;
        }

        // SDK convenience accessor for the entity-archetype registry proven
        // at +0x601AF8 by CEntitySystem::LoadEntityArchetype, which forwards
        // the requested name straight to it. Not an original virtual method.
        EntityArchetypeManagerLayout *GetArchetypeManager() {
            return m_pArchetypeManager;
        }

        const EntityArchetypeManagerLayout *GetArchetypeManager() const {
            return m_pArchetypeManager;
        }

        // SDK convenience accessor for the owner field proven at +0x600258;
        // this is not presented as an original CEntitySystem virtual method.
        CAreaManager *GetAreaManager() {
            return m_pAreaManager;
        }

        IAreaManager *GetAreaManager() const override {
            return reinterpret_cast<IAreaManager *>(m_pAreaManager);
        }

      private:
        // Laid out by the constructor 0x180D25458.
        std::byte m_eventQueues[5][0x30];       // 0x08, five 0x30-byte objects built by 0x1820FA040
        void *m_vector00F8[3];                  // 0xF8
        std::byte m_state0110[0x10];            // 0x110, built by 0x180D26214
        std::uint32_t m_state0120;              // 0x120
        std::uint32_t m_padding0124;
        void *m_pSystem;                        // 0x128, ISystem, the constructor's argument
        std::uint64_t m_state0130[6];           // 0x130, zeroed
        std::byte m_set0160[0x10];              // 0x160, an std::set
        std::byte m_lock0170[0x10];             // 0x170, wh::shared::C_RecursiveSRWLock (0x18091865C)
        std::byte m_state0180[0x28];            // 0x180
        std::byte m_map01A8[0x10];              // 0x1A8, an std::map
        std::byte m_lock01B8[0x10];             // 0x1B8, wh::shared::C_RecursiveSRWLock
        std::byte m_state01C8[0x20];            // 0x1C8
        std::byte m_map01E8[0x10];              // 0x1E8, an std::map
        std::uint64_t m_state01F8;              // 0x1F8, built by 0x180BBD62C
        // The entity table 0x1823D5148 builds: 65536 slots and their salts.
        std::byte m_entityArray[0x5FFFE8];      // 0x200
        void *m_vector6001E8[3];                // 0x6001E8
        CComponentEventDistributer *m_pComponentEventDistributer; // 0x600200, built by 0x180C20DB0
        std::byte m_state600208[0x10];          // 0x600208, built by 0x180A5448C
        void *m_vector600218[3];                // 0x600218
        bool m_state600230;                     // 0x600230
        std::byte m_padding600231[0x7];
        std::int64_t m_state600238;             // 0x600238, a CTimeValue, -100000 by default
        std::uint64_t m_state600240[3];         // 0x600240
        CAreaManager *m_pAreaManager;           // 0x600258
        wh::C_EntityStreamingManager *m_pEntityStreamingManager; // 0x600260, built by 0x181684A64
        CEntityLoadManager *m_pEntityLoadManager; // 0x600268
        void *m_pOwnedState600270;              // 0x600270, a 0x70-byte object built by 0x180D24D84
        CEntityPoolManager *m_pEntityPoolManager; // 0x600278
        std::byte m_sets600280[64][0x10];       // 0x600280, sixty-four std::sets
        std::byte m_state600680[0x40];          // 0x600680, built by 0x1806030C0
        std::byte m_state6006C0[0x40];          // 0x6006C0, built by 0x1806030C0
        std::byte m_state600700[0x58];          // 0x600700, built by 0x180D25E9C
        bool m_state600758;                     // 0x600758
        std::byte m_padding600759[0x7];
        std::byte m_state600760[0x1390];        // 0x600760, built by 0x180D24A14
        CBreakableManager *m_pBreakableManager; // 0x601AF0, built by 0x180D25E6C
        EntityArchetypeManagerLayout *m_pArchetypeManager; // 0x601AF8, read by CEntitySystem::LoadEntityArchetype
        void *m_pOwnedVector601B00;             // 0x601B00, an owned 0x18-byte vector
        void *m_pOwnedVector601B08;             // 0x601B08, an owned 0x18-byte vector
        void *m_pOwnedState601B10;              // 0x601B10, a 0x58-byte object built by 0x180D24154
        void *m_pOwnedState601B18;              // 0x601B18, a 0x138-byte object built by 0x18193C984
        std::uint32_t m_state601B20;            // 0x601B20
        std::uint32_t m_padding601B24;
        std::byte m_state601B28[0x10];          // 0x601B28, built by 0x180D24C7C with the timer
        std::byte m_set601B38[0x10];            // 0x601B38, an std::set
        std::byte m_set601B48[0x10];            // 0x601B48, an std::set
        std::uint64_t m_state601B58[9];         // 0x601B58, zeroed
        bool m_state601BA0;                     // 0x601BA0
        std::byte m_padding601BA1[0x7];
        bool m_state601BA8;                     // 0x601BA8
        std::byte m_padding601BA9[0x7];

        static void AssertLayout() {
            static_assert(offsetof(CEntitySystem, m_pSystem) == 0x128, "CEntitySystem::m_pSystem offset mismatch");
            static_assert(offsetof(CEntitySystem, m_entityArray) == 0x200, "CEntitySystem::m_entityArray offset mismatch");
            static_assert(offsetof(CEntitySystem, m_pAreaManager) == 0x600258, "CEntitySystem::m_pAreaManager offset mismatch");
            static_assert(offsetof(CEntitySystem, m_pEntityPoolManager) == 0x600278, "CEntitySystem::m_pEntityPoolManager offset mismatch");
            static_assert(offsetof(CEntitySystem, m_state600760) == 0x600760, "CEntitySystem::m_state600760 offset mismatch");
            static_assert(offsetof(CEntitySystem, m_pArchetypeManager) == 0x601AF8, "CEntitySystem::m_pArchetypeManager offset mismatch");
            static_assert(offsetof(CEntitySystem, m_state601BA8) == 0x601BA8, "CEntitySystem::m_state601BA8 offset mismatch");
        }
    };

    static_assert(sizeof(CEntitySystem) == 0x601BB0, "CEntitySystem size mismatch");
} // namespace SDK
