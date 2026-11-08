#pragma once

#include "../cryengine/system/i_system.h"
#include "i_mod.h"

#include <cstddef>

namespace SDK::wh {
    /**
     * The callable behind a `std::function<void(const I_Mod &)>`, in the
     * vtable order of the game's own STL (`_Func_impl_no_alloc<lambda, void,
     * const wh::I_Mod &>`, as at 0x183DBE790): `_Do_call` third. Spelled out
     * for the reason IScriptArgumentsCallable is: the client's STL drops a
     * slot the game's keeps.
     */
    class I_ModCallable {
      public:
        virtual I_ModCallable *Copy(void *storage) const = 0; // +0x00, _Copy
        virtual I_ModCallable *Move(void *storage)       = 0; // +0x08, _Move
        virtual void Call(const I_Mod &mod)              = 0; // +0x10, _Do_call
        virtual const void *TargetType() const           = 0; // +0x18, _Target_type
        virtual void DeleteThis(bool deallocate)         = 0; // +0x20, _Delete_this
        virtual const void *Target() const               = 0; // +0x28, _Get

      protected:
        ~I_ModCallable() = default;
    };

    /** A `std::function<void(const I_Mod &)>` as the game lays it out: small-object storage, then `_Impl`. */
    struct ModFunction {
        std::byte m_storage[0x38];        // 0x00
        I_ModCallable *m_pImpl = nullptr; // 0x38, null: the empty function
    };

    static_assert(sizeof(ModFunction) == 0x40, "ModFunction size mismatch");
    static_assert(offsetof(ModFunction, m_pImpl) == 0x38, "ModFunction::m_pImpl offset mismatch");

    /**
     * The game's mod manager (`wh::C_ModManager`, vtable 0x183AA41E0), built
     * once by CSystem::Init and held at CSystem+0x2B30. Its scan (0x180D9DC4C)
     * fills the discovered list from `mods/` and the platform's folders, then
     * the enabled list from `mod_order.txt`. Every consumer -- the mod paks,
     * `mod.cfg`, `scripts/mods/<id>.lua`, string and table patches -- walks
     * the enabled list through ForEachEnabledMod (vtable slot 5, 0x1804D98BC).
     */
    class C_ModManager final {
      public:
        std::size_t GetDiscoveredCount() const {
            return static_cast<std::size_t>(m_pDiscoveredEnd - m_pDiscoveredBegin);
        }
        std::size_t GetEnabledCount() const {
            return static_cast<std::size_t>(m_pEnabledEnd - m_pEnabledBegin);
        }

        /** Empties the enabled list in place, keeping its storage, as the scan does before it fills it. */
        void ClearEnabled() {
            m_pEnabledEnd = m_pEnabledBegin;
        }

      private:
        const void *m_pVftable;        // 0x00
        ISystem *m_pSystem;            // 0x08, the CSystem that built it
        CryStringT<char> m_folder;     // 0x10, "mods"
        C_Mod *m_pDiscoveredBegin;     // 0x18, std::vector<C_Mod>
        C_Mod *m_pDiscoveredEnd;       // 0x20
        C_Mod *m_pDiscoveredCapacity;  // 0x28
        I_Mod **m_pEnabledBegin;       // 0x30, std::vector<I_Mod *> in load order
        I_Mod **m_pEnabledEnd;         // 0x38
        I_Mod **m_pEnabledCapacity;    // 0x40
        std::byte m_unknown0048[0x18]; // 0x48, a vector the constructor zeroes
        bool m_unknown0060;            // 0x60, the constructor sets it
        std::byte m_padding0061[0x7];

        static void AssertLayout() {
            static_assert(offsetof(C_ModManager, m_pDiscoveredBegin) == 0x18, "C_ModManager::m_pDiscoveredBegin offset mismatch");
            static_assert(offsetof(C_ModManager, m_pEnabledBegin) == 0x30, "C_ModManager::m_pEnabledBegin offset mismatch");
            static_assert(offsetof(C_ModManager, m_unknown0060) == 0x60, "C_ModManager::m_unknown0060 offset mismatch");
        }
    };

    // The constructor (0x180D9DAB0) allocates 104 bytes.
    static_assert(sizeof(C_ModManager) == 0x68, "C_ModManager size mismatch");
} // namespace SDK::wh
