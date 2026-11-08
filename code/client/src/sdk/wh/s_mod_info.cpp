#include "s_mod_info.h"

#include "../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh {
    void S_ModInfo::Construct(S_ModInfo &storage) {
        hook::this_call<S_ModInfo *>(gPatterns.S_ModInfo__Construct, &storage);
    }

    void S_ModInfo::Destroy() {
        // The destructor's own order: the last string first.
        for (CryStringT<char> *field : {&m_createdOn, &m_version, &m_author, &m_description, &m_name, &m_folder}) {
            hook::this_call<void>(gPatterns.CryStringT_char__Destruct, field);
        }
    }
} // namespace SDK::wh
