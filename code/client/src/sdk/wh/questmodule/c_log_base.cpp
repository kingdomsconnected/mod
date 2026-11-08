#include "c_log_base.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstdint>
#include <cstring>

namespace SDK::wh::questmodule {
    C_LogBase *C_LogBase::Create() {
        void *storage = hook::call<void *>(gPatterns.CryActionAllocate, sizeof(C_LogBase));
        std::memset(storage, 0, sizeof(C_LogBase));
        hook::this_call<void>(gPatterns.C_LogBase__Construct, storage);

        // What makes it a `C_Log` rather than the abstract base, and the whole
        // of the derived construction: the reflected factory allocates the same
        // 0x30, runs the base constructor and then overwrites this pointer.
        // Without it slots 2 and 3 are still `_purecall`, and the journal calls
        // both while building an objective row.
        *reinterpret_cast<std::uint64_t *>(storage) = gPatterns.C_Log__VTable;
        return static_cast<C_LogBase *>(storage);
    }

    void C_LogBase::Destroy(C_LogBase *log) {
        if (log == nullptr) {
            return;
        }

        // Freeing the block is the whole teardown. Both strings are the
        // engine's shared empty header, which carries the -1 refcount sentinel
        // and is never released, so there is nothing else holding on.
        hook::call<void>(gPatterns.FreeActionStorage, log);
    }
} // namespace SDK::wh::questmodule
