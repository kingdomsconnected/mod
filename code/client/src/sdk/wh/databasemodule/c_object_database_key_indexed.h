#pragma once

namespace SDK::wh::databasemodule {
    /**
     * The key policy RTTI names as the second argument of
     * `C_ObjectDatabaseKeyIndexed`. It carries no state and no virtuals; it
     * exists here only so the database types can be spelled as the game does.
     */
    template <typename TObject>
    class C_ObjectDatabaseDefaultKeyExtractor {};

    /**
     * The layer that makes a database answer lookups by key.
     *
     * RTTI puts it between a concrete database and its tree database -- for
     * clothing presets `C_ObjectDatabaseKeyIndexed<C_ObjectTreeDatabase<
     * S_ClothingPreset, std::vector>, C_ObjectDatabaseDefaultKeyExtractor<
     * S_ClothingPreset>>`. Its constructor (0x18154141C for clothing presets)
     * only installs the vtable, so it adds no data; its table has the tree
     * database's 22 slots, `RebuildIndex` among them re-sorting the rows by
     * key.
     */
    template <typename TDatabase, typename TKeyExtractor>
    class C_ObjectDatabaseKeyIndexed: public TDatabase {
      public:
        ~C_ObjectDatabaseKeyIndexed() override = default;
    };
} // namespace SDK::wh::databasemodule
