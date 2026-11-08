#pragma once

namespace SDK {
    /** A borrowed input key name. */
    struct TKeyName {
        const char *key = "";
        const char *c_str() const {
            return key;
        }
    };
    static_assert(sizeof(TKeyName) == 0x8, "TKeyName size mismatch");
} // namespace SDK
