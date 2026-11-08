#pragma once

#include "../types.h"

#include <cstddef>

namespace SDK::wh {
    /**
     * What a mod's `mod.manifest` says about it (`wh::S_ModInfo`, vtable
     * 0x1846AC198), held inline in every C_Mod at +0x18. The constructor
     * (0x182434BD8) stores the vftable, gives every string the engine's empty
     * one and clears `modifies_level`. The manifest parser fills the rest.
     * The vftable (destructor and two helpers) is the game's: an object
     * of this type is only ever built by that constructor.
     */
    class S_ModInfo {
      public:
        /** Runs the game's constructor on `storage`. */
        static void Construct(S_ModInfo &storage);

        /** Releases every string the way the game's destructor (0x1824351F0) does. */
        void Destroy();

        const CryStringT<char> &GetName() const {
            return m_name;
        }
        void SetName(const char *name) {
            m_name.Assign(name);
        }

      private:
        const void *m_pVftable;         // 0x00
        CryStringT<char> m_folder;      // 0x08, the mod's folder as the scan found it
        CryStringT<char> m_name;        // 0x10, <info><name>
        CryStringT<char> m_description; // 0x18, <info><description>
        CryStringT<char> m_author;      // 0x20, <info><author>
        CryStringT<char> m_version;     // 0x28, <info><version>
        CryStringT<char> m_createdOn;   // 0x30, <info><created_on>
        bool m_modifiesLevel;           // 0x38, <info><modifies_level> is "true"
        std::byte m_padding0039[0x7];

        static void AssertLayout() {
            static_assert(offsetof(S_ModInfo, m_folder) == 0x08, "S_ModInfo::m_folder offset mismatch");
            static_assert(offsetof(S_ModInfo, m_name) == 0x10, "S_ModInfo::m_name offset mismatch");
            static_assert(offsetof(S_ModInfo, m_createdOn) == 0x30, "S_ModInfo::m_createdOn offset mismatch");
            static_assert(offsetof(S_ModInfo, m_modifiesLevel) == 0x38, "S_ModInfo::m_modifiesLevel offset mismatch");
        }
    };

    static_assert(sizeof(S_ModInfo) == 0x40, "S_ModInfo size mismatch");
} // namespace SDK::wh
