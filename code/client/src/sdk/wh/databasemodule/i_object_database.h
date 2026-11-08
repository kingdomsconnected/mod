#pragma once

namespace SDK::wh::databasemodule {
    /**
     * Root interface for Warhorse object databases.
     *
     * RTTI identifies this as a class in wh::databasemodule. Its abstract
     * table at 0x183E3D938 contains exactly three entries: the destructor,
     * one shared default operation, and one pure operation. Their stripped
     * source contracts remain opaque.
     */
    class I_ObjectDatabase {
      public:
        virtual ~I_ObjectDatabase() = default;
        virtual void unk_fn0001() = 0;
        /** +0x10. The database's name; a table database answers its table's (0x180EF35F8). */
        virtual const char *GetName() const = 0;
    };

    static_assert(sizeof(I_ObjectDatabase) == 0x8,
                  "I_ObjectDatabase size mismatch");
} // namespace SDK::wh::databasemodule
