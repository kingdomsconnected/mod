#pragma once

#include "../types.h"
#include "s_mod_info.h"

#include <cstddef>

namespace SDK::wh {
    /**
     * A mod as the game's mod manager hands it out (`wh::I_Mod`). C_Mod's
     * vftable (0x1846AC178) has exactly these three getters and no
     * destructor, so none is declared here either.
     */
    class I_Mod {
      public:
        virtual const CryStringT<char> &GetPath() const  = 0; // +0x00, the mod's folder with a trailing '/'
        virtual const S_ModInfo &GetInfo() const         = 0; // +0x08
        virtual const CryStringT<char> &GetModId() const = 0; // +0x10, <info><modid>, else the folder's name sanitised

      protected:
        ~I_Mod() = default;
    };

    /**
     * An installed mod: one 0x70-byte element of C_ModManager's discovered
     * list, built by 0x182434AB8 from the folder the scan found.
     */
    class C_Mod final: public I_Mod {
      public:
        const CryStringT<char> &GetPath() const override {
            return m_path;
        }
        const S_ModInfo &GetInfo() const override {
            return m_info;
        }
        const CryStringT<char> &GetModId() const override {
            return m_modId;
        }

      private:
        CryStringT<char> m_path;    // 0x08
        CryStringT<char> m_modId;   // 0x10
        S_ModInfo m_info;           // 0x18
        std::byte m_supports[0x18]; // 0x58, std::vector<CryStringT<char>> of the <supports> game versions

        static void AssertLayout() {
            static_assert(offsetof(C_Mod, m_path) == 0x08, "C_Mod::m_path offset mismatch");
            static_assert(offsetof(C_Mod, m_info) == 0x18, "C_Mod::m_info offset mismatch");
            static_assert(offsetof(C_Mod, m_supports) == 0x58, "C_Mod::m_supports offset mismatch");
        }
    };

    static_assert(sizeof(C_Mod) == 0x70, "C_Mod size mismatch");
} // namespace SDK::wh
