#include "c_vis_area_manager.h"

#include "../../patterns.h"

namespace SDK {
    CVisAreaManager *CVisAreaManager::GetInstance() {
        return *reinterpret_cast<CVisAreaManager **>(
            gPatterns.CVisAreaManager__Instance);
    }
} // namespace SDK
