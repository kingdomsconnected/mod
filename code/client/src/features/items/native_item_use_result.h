#pragma once

#include <cstddef>
#include <cstdint>

namespace KCDC::Features::Items {
    /**
     * Observed four-byte result word, not a claim about the native C++ type.
     * C_Human slot 195 takes its address in RDX and returns it in RAX. Native
     * success writes 1; refusal paths write 32/64/128. Lua tests bits 0x1FE.
     * It is distinct from S_ActionRequestResult, whose accepted value is 0.
     */
    struct NativeItemUseResultWord {
        std::uint32_t flags;
        void RefuseUnsupported() {
            flags = 128;
        }
    };
    static_assert(sizeof(NativeItemUseResultWord) == 4);
    static_assert(offsetof(NativeItemUseResultWord, flags) == 0);
} // namespace KCDC::Features::Items
