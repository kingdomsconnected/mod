#pragma once

#include "i_stat_obj_loaded_callback.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    class IStatObj;

    /**
     * Asynchronous static-object loading callback implementation.
     *
     * RTTI proves the global-namespace hierarchy through
     * IStatObjLoadedCallback. Its table at 0x183A3D9D0 implements exactly the
     * two inherited entries and adds no operations. Constructor state writes
     * and sized deletion prove the complete 0x28-byte layout.
     */
    class CStatObjAsyncLoader : public IStatObjLoadedCallback {
      public:
        ~CStatObjAsyncLoader() override = default;

        IStatObj *GetLoadingObject() const { return m_pLoadingObject; }
        IStatObjLoadedCallback *GetCompletionCallback() const {
            return m_pCompletionCallback;
        }
        const char *GetPath() const { return m_path; }
        std::uint32_t GetLoadFlagsRaw() const { return m_loadFlags; }

      private:
        IStatObj *m_pLoadingObject; // 0x08
        IStatObjLoadedCallback *m_pCompletionCallback; // 0x10
        const char *m_path; // 0x18, CryStringT<char> pointer payload
        std::uint32_t m_loadFlags; // 0x20
        std::uint8_t m_state0024;
        std::uint8_t m_state0025;
        std::byte m_padding0026[0x2];
    };

    static_assert(sizeof(CStatObjAsyncLoader) == 0x28,
                  "CStatObjAsyncLoader size mismatch");
} // namespace SDK
