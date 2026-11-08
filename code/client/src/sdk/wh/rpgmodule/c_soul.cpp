#include "c_soul.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::rpgmodule {
    const CryStringT<char> &I_Soul::ResolveBodyDescriptionPart(std::uint8_t part) const {
        return *hook::this_call<const CryStringT<char> *>(gPatterns.C_Soul__ResolveBodyDescriptionPart, this, part);
    }
} // namespace SDK::wh::rpgmodule
