#pragma once

#include "i_object_database.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::databasemodule {
    /**
     * Shared abstract base for Warhorse object databases.
     *
     * RTTI proves C_ObjectDatabaseBase -> I_ObjectDatabase. Its table at
     * 0x183E3D888 contains the three inherited entries followed by eighteen
     * database operations. Stripped source contracts remain opaque. Scalar
     * deletion proves the complete 0x30-byte layout.
     */
    class C_ObjectDatabaseBase : public I_ObjectDatabase {
      public:
        ~C_ObjectDatabaseBase() override = default;

        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        /**
         * +0x30. Whether the database holds its rows: the first thing
         * C_ObjectDatabaseManager::ReloadDatabases asks, loading any that do
         * not. A table database answers its +0x94 flag (0x181A7D990); the
         * level's own databases (weather profiles, cat waypoints, dog points)
         * answer false between levels.
         */
        virtual bool IsLoaded() const = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        /**
         * +0x48. Reads the rows from the database's tables, parts and
         * patches as CryPak now resolves them. Zero on success
         * (E_ObjectDBResult); ReloadDatabases runs it under the manager's
         * loading flag.
         */
        virtual std::int32_t Load() = 0;
        /** +0x50. Resolves the rows' references to other databases; ReloadDatabases runs it after every load and for each dependant. */
        virtual bool LinkReferences() = 0;
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        /**
         * Rebuilds the database's lookup structures over its rows (+0x80).
         *
         * Proven on the key-indexed specializations: the clothing-preset one
         * (`FinalizeLoad_180F65004`) sorts the row vector by key and rebuilds
         * its name index of pointers into it; the weapon-preset one
         * (`FinalizeLoad_180ED0A68`) does the same. Required after
         * `AppendFromXml`, which may reallocate the rows.
         */
        virtual bool RebuildIndex() = 0;
        /** +0x88. Called by a load once its rows are read; empty in the base (0x1803B6E80). */
        virtual void OnLoaded() = 0;
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() = 0;
        virtual void unk_fn0020() = 0; // +0xA0

      private:
        std::int32_t m_databaseId;    // 0x08, the ctor (0x180EF4F30) stores its int argument
        std::byte m_padding000C[0x4]; // 0x0C
        std::byte m_unset0010[0x8];   // 0x10, not written by the ctor
        void **m_pRowsBegin;          // 0x18, std::vector zeroed by the ctor
        void **m_pRowsEnd;            // 0x20
        void **m_pRowsCapacity;       // 0x28

        static void AssertLayout() {
            static_assert(offsetof(C_ObjectDatabaseBase, m_databaseId) == 0x08, "C_ObjectDatabaseBase::m_databaseId offset mismatch");
            static_assert(offsetof(C_ObjectDatabaseBase, m_pRowsBegin) == 0x18, "C_ObjectDatabaseBase::m_pRowsBegin offset mismatch");
        }
    };

    static_assert(sizeof(C_ObjectDatabaseBase) == 0x30,
                  "C_ObjectDatabaseBase size mismatch");
} // namespace SDK::wh::databasemodule

