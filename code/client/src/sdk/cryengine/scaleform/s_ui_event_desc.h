#pragma once

#include <cstddef>

namespace SDK {
    /** Borrowed XML descriptor prefix; strings belong to the native XML. */
    class SUIParameterDesc {
      public:
        const char *GetName() const { return m_name; }
        const char *GetDisplayName() const { return m_displayName; }
        const char *GetDescription() const { return m_description; }
        const SUIParameterDesc *GetParent() const { return m_parent; }
        int GetParameterType() const { return m_parameterType; }

      private:
        const char *m_name;
        const char *m_displayName;
        const char *m_description;
        const SUIParameterDesc *m_parent;
        int m_parameterType;
        std::byte m_opaque0024[4];

        static void AssertLayout() {
            static_assert(offsetof(SUIParameterDesc, m_name) == 0x0);
            static_assert(offsetof(SUIParameterDesc, m_displayName) == 0x8);
            static_assert(offsetof(SUIParameterDesc, m_description) == 0x10);
            static_assert(offsetof(SUIParameterDesc, m_parent) == 0x18);
            static_assert(offsetof(SUIParameterDesc, m_parameterType) == 0x20);
        }
    };
    static_assert(sizeof(SUIParameterDesc) == 0x28);

    /** Native event/function descriptor, allocated by 0x18088B0CC. */
    class SUIEventDesc: public SUIParameterDesc {
      public:
        /** `SUIEventDesc::SEvtParams`; two of them fill the 0x40 bytes after the base exactly. */
        struct SEvtParams {
            bool IsDynamic;                // 0x00
            std::byte m_opaque0001[0x7];   // 0x01
            const char *sDynamicName;      // 0x08
            const char *sDynamicDesc;      // 0x10
            void *Params;                  // 0x18, TUIParams (DynArray<SUIParameterDesc>) data
        };

      private:
        SEvtParams m_inputParams;  // 0x28
        SEvtParams m_outputParams; // 0x48

        static void AssertLayout() {
            static_assert(sizeof(SEvtParams) == 0x20);
            static_assert(offsetof(SEvtParams, sDynamicName) == 0x08);
            static_assert(offsetof(SUIEventDesc, m_inputParams) == 0x28);
            static_assert(offsetof(SUIEventDesc, m_outputParams) == 0x48);
        }
    };
    static_assert(sizeof(SUIEventDesc) == 0x68);
} // namespace SDK
