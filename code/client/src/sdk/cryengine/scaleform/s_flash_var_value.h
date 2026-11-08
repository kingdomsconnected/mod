#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Type tag of a value crossing the C++/ActionScript boundary.
     *
     * Proven by the SFlashVarValue-to-GFxValue converter, which branches on
     * exactly these nine values.
     */
    enum class EFlashVariableType : std::int32_t {
        Undefined    = 0,
        Null         = 1,
        Bool         = 2,
        Int          = 3,
        UInt         = 4,
        Double       = 5,
        Float        = 6,
        ConstStrPtr  = 7,
        ConstWstrPtr = 8,
    };

    /**
     * One argument to, or result from, ActionScript.
     *
     * The converter reads the tag at +0x0 and the payload at +0x8, and the
     * engine strides an argument array by 16 bytes.
     *
     * A string is borrowed, not copied: the pointer is read during the call
     * and never retained, so the storage only has to outlive the call.
     */
    struct SFlashVarValue {
        EFlashVariableType type;
        std::int32_t m_padding0004;

        union {
            bool b;
            std::int32_t i;
            std::uint32_t u;
            double d;
            float f;
            const char *str;
            const wchar_t *wstr;
        };

        static SFlashVarValue Bool(bool value) {
            SFlashVarValue result {EFlashVariableType::Bool, 0, {}};
            result.b = value;
            return result;
        }

        static SFlashVarValue Int(std::int32_t value) {
            SFlashVarValue result {EFlashVariableType::Int, 0, {}};
            result.i = value;
            return result;
        }

        static SFlashVarValue Double(double value) {
            SFlashVarValue result {EFlashVariableType::Double, 0, {}};
            result.d = value;
            return result;
        }

        static SFlashVarValue String(const char *value) {
            SFlashVarValue result {EFlashVariableType::ConstStrPtr, 0, {}};
            result.str = value;
            return result;
        }

      private:
        static void AssertLayout() {
            static_assert(offsetof(SFlashVarValue, type) == 0x0, "SFlashVarValue::type offset mismatch");
            static_assert(offsetof(SFlashVarValue, b) == 0x8, "SFlashVarValue payload offset mismatch");
        }
    };

    static_assert(sizeof(SFlashVarValue) == 0x10, "SFlashVarValue size mismatch");
} // namespace SDK
