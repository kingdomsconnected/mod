#include "c_obj_manager.h"

#include "../../patterns.h"

namespace SDK {
    CObjManager *CObjManager::GetInstance() {
        return *reinterpret_cast<CObjManager **>(gPatterns.CObjManager__Instance);
    }
} // namespace SDK
