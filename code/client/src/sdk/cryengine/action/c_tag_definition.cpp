#include "c_tag_definition.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK {
    std::uint32_t CTagDefinition::GetGroupTag(const std::uint8_t *state, std::uint32_t groupId) const {
        return hook::this_call<std::uint32_t>(gPatterns.CTagDefinition__GetGroup, this, state, groupId);
    }

    void CTagState::SetGroupTag(std::uint32_t groupId, std::uint32_t tagId) {
        hook::this_call<void>(gPatterns.CTagState__SetGroup, this, groupId, tagId);
    }
} // namespace SDK
