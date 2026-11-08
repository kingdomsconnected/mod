#pragma once

#include <cstdint>

namespace KCDC::Core::States {
    enum StateIds : int32_t {
        Initialize,
        GameSetup,
        MainMenu,
        SessionConnection,
        SessionConnected,
        SessionDisconnection,
        Shutdown,
        SessionResources,
        SessionReplication,
        SessionSpawn,
        SessionFinalize,
        WorldBuilder
    };
} // namespace KCDC::Core::States
