#include "s_ui_arguments.h"

#include "../../patterns.h"

#include <utils/safe_win32.h>

#include <utils/hooking/hooking.h>

namespace SDK {
    ScopedUIArguments::ScopedUIArguments() {
        hook::this_call<void *>(gPatterns.SUIArguments__Construct, m_storage);
    }

    ScopedUIArguments::~ScopedUIArguments() {
        hook::this_call<void>(gPatterns.SUIArguments__Destruct, m_storage);
    }

    void ScopedUIArguments::AppendString(const char *value) {
        const ScopedCryString text(value);
        hook::this_call<void>(gPatterns.SUIArguments__AppendStringWithType, m_storage, &text.Get(), kStringType);
    }

    void ScopedUIArguments::AppendUInt(unsigned int value) {
        hook::this_call<void>(gPatterns.SUIArguments__AppendUInt, m_storage, &value);
    }
} // namespace SDK
