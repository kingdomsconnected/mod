#pragma once

#include "../../patterns.h"

#include "i_animation_database.h"

#include <utils/hooking/hooking.h>

#include <cstdint>

namespace SDK {
    /**
     * The Mannequin asset manager, `IMannequin::GetAnimationDatabaseManager`. Slots verified against
     * `CAnimationDatabaseManager`'s primary vtable (0x183A9D608).
     * Same-name overloads sit in the vtable in the reverse of their declaration order.
     */
    class IAnimationDatabaseManager {
      public:
        virtual ~IAnimationDatabaseManager()                                     = default; // 0
        virtual int GetTotalDatabases() const                                    = 0;       // 1
        virtual const IAnimationDatabase *GetDatabase(int index) const           = 0;       // 2
        virtual const IAnimationDatabase *FindDatabase(std::uint32_t crc) const  = 0;       // 3

        /** `Load` (0x1809402A8): the database at `filename`, the one already loaded when it is. */
        virtual const IAnimationDatabase *Load(const char *filename)             = 0;       // 4
        virtual const void *LoadControllerDef(const char *filename)              = 0;       // 5
        virtual const void *LoadTagDefs(const char *filename, bool isTags)       = 0;       // 6
        virtual const void *FindControllerDef(std::uint32_t crc) const           = 0;       // 8
        virtual const void *FindControllerDef(const char *filename) const        = 0;       // 7
        virtual const void *FindTagDef(std::uint32_t crc) const                  = 0;       // 10
        virtual const void *FindTagDef(const char *filename) const               = 0;       // 9
        virtual IAnimationDatabase *Create(const char *filename, const char *defFilename) = 0; // 11
        virtual void *CreateTagDefinition(const char *filename)                  = 0;       // 12
        virtual void ReloadAll()                                                 = 0;       // 13
        virtual void UnloadAll()                                                 = 0;       // 14
    };

    /** The only implementation of IAnimationDatabaseManager. */
    class CAnimationDatabaseManager final: public IAnimationDatabaseManager {
      public:
        /**
         * `ReloadDatabase` (0x1836C2150): clears the database's fragments and blends and reads its
         * file again, sub-databases included, through `ISystem::LoadXmlFromFile` -- which reaches
         * `CXmlUtils::LoadXmlFromFile`, so the mod's own edits to the file are applied again. The
         * engine's `mn_reload` runs it on every database while bodies play: scopes keep fragment
         * ids and their own copies of what they play, never the database's fragments.
         */
        void ReloadDatabase(const IAnimationDatabase *database) {
            hook::this_call<void>(gPatterns.CAnimationDatabaseManager__ReloadDatabase, this, database);
        }
    };
} // namespace SDK
