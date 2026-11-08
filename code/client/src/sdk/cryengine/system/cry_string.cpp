#include "../../types.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK {
    template <> CryStringT<char> &CryStringT<char>::Assign(const char *value) {
        hook::this_call<void *>(gPatterns.CryStringT_char__AssignCStr, this, value);
        return *this;
    }

    ScopedCryString::ScopedCryString(const char *value) {
        hook::this_call<void *>(gPatterns.CryStringT_char__Construct, &m_value, value);
    }

    ScopedCryString::~ScopedCryString() {
        hook::this_call<void>(gPatterns.CryStringT_char__Destruct, &m_value);
    }

    ReturnedCryString::~ReturnedCryString() {
        if (m_value.m_pData != nullptr) {
            hook::this_call<void>(gPatterns.CryStringT_char__Destruct, &m_value);
        }
    }

    CryStringT<char> ConstructCryString(const char *value) {
        CryStringT<char> constructed {nullptr};
        hook::this_call<void *>(gPatterns.CryStringT_char__Construct, &constructed, value);
        return constructed;
    }
} // namespace SDK
