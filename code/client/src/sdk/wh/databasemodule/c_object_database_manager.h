#pragma once

#include "c_object_database_base.h"

#include <cstddef>

namespace SDK::wh::databasemodule {
    /**
     * A `std::vector<C_ObjectDatabaseBase *>` as the game lays it out, in its
     * release STL: begin, end, capacity. What ReloadDatabases takes, and what
     * the manager keeps its databases in.
     */
    struct ObjectDatabaseList {
        C_ObjectDatabaseBase **m_pBegin    = nullptr;
        C_ObjectDatabaseBase **m_pEnd      = nullptr;
        C_ObjectDatabaseBase **m_pCapacity = nullptr;

        C_ObjectDatabaseBase *const *begin() const {
            return m_pBegin;
        }
        C_ObjectDatabaseBase *const *end() const {
            return m_pEnd;
        }
    };

    static_assert(sizeof(ObjectDatabaseList) == 0x18, "ObjectDatabaseList size mismatch");

    /**
     * The owner of every game database (`wh::databasemodule::C_ObjectDatabaseManager`,
     * vtables 0x183E3D860 and 0x183E3D840), a 0xB0-byte magic static at
     * 0x1854A49A0 built by 0x1814BCFF8 and reached through one accessor. Its
     * databases read their tables once, at boot, together with every
     * `<table>__<id>.xml` part and patch CryPak can see; nothing reads them
     * again but ReloadDatabases.
     */
    class C_ObjectDatabaseManager final {
      public:
        static C_ObjectDatabaseManager *GetInstance();

        /** Every database, in registration order. */
        const ObjectDatabaseList &GetDatabases() const {
            return m_databases;
        }

        /**
         * `ReloadDatabases` (0x180EF4048): sorts `databases` by dependency and,
         * with `reload`, loads each one again (Load, then LinkReferences),
         * linking again every database that depends on one it loaded. A
         * database that holds no rows is loaded whatever the flags. False when
         * one failed. `wh_db_ReloadObjectDatabase` passes every database,
         * `reload` set and `force` clear.
         */
        bool ReloadDatabases(const ObjectDatabaseList &databases, bool reload, bool force);

      private:
        std::byte m_vftables0000[0x10]; // 0x00, the primary and secondary vftables
        std::byte m_unknown0010[0x20];  // 0x10, two (pointer, -1, -1) records the constructor seeds
        ObjectDatabaseList m_databases; // 0x30
        std::byte m_unknown0048[0x58];  // 0x48, a vector and an unordered_map
        bool m_reloadPending;           // 0xA0, cleared by the reload's first step (0x180EF44A4)
        bool m_loading;                 // 0xA1, set around each database's Load
        std::byte m_padding00A2[0x6];
        std::byte m_unknown00A8[0x8]; // 0xA8

        static void AssertLayout() {
            static_assert(offsetof(C_ObjectDatabaseManager, m_databases) == 0x30, "C_ObjectDatabaseManager::m_databases offset mismatch");
            static_assert(offsetof(C_ObjectDatabaseManager, m_reloadPending) == 0xA0, "C_ObjectDatabaseManager::m_reloadPending offset mismatch");
        }
    };

    static_assert(sizeof(C_ObjectDatabaseManager) == 0xB0, "C_ObjectDatabaseManager size mismatch");
} // namespace SDK::wh::databasemodule
