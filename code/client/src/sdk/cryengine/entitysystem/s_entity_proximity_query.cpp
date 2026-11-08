#include "s_entity_proximity_query.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstddef>

namespace SDK {
    SEntityProximityQuery::SEntityProximityQuery() {
        hook::this_call<void>(gPatterns.SEntityProximityQuery__Construct, this);
    }

    SEntityProximityQuery::~SEntityProximityQuery() {
        if (_begin == nullptr) {
            return;
        }

        // The capacity, not the count: the release frees the block the vector
        // grew to, and recognises the inline arena by the pointer it is handed.
        hook::call<void>(gPatterns.SEntityProximityQuery__FreeResultBuffer, &_allocatorArena, _begin, static_cast<std::size_t>(_capacityEnd - _begin));
    }
} // namespace SDK
