#include "i_game_framework.h"

#include "../../patterns.h"

namespace SDK {
    IGameFramework *IGameFramework::GetInstance() {
        return *reinterpret_cast<IGameFramework **>(gPatterns.IGameFramework__Instance);
    }
} // namespace SDK
