#include "c_terrain.h"

#include "i_3d_engine.h"

namespace SDK {
    CTerrain *CTerrain::GetInstance() {
        const auto engine = I3DEngine::GetInstance();
        return engine != nullptr ? static_cast<CTerrain *>(engine->GetITerrain())
                                 : nullptr;
    }
} // namespace SDK
