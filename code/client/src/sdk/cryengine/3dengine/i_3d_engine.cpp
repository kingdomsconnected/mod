#include "i_3d_engine.h"

#include "../../patterns.h"

namespace SDK {
    I3DEngine *I3DEngine::GetInstance() {
        return *reinterpret_cast<I3DEngine **>(gPatterns.I3DEngine__Instance);
    }
} // namespace SDK
