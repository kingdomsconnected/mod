#include "spawn_params.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK {
    SpawnParams MakeSpawnParams() {
        SpawnParams params;
        hook::this_call<void>(gPatterns.SpawnParams__Construct, &params);
        return params;
    }
} // namespace SDK
