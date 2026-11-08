#pragma once

#include <cstdint>

namespace SDK::wh::xgenaimodule::activitysystem {
    /** `E_DogCompanionMode`, the name table at `0x182056490`. */
    enum class E_DogCompanionMode : std::uint8_t {
        Wait       = 0,
        Follow     = 1,
        Free       = 2,
        Aggressive = 3,
        Search     = 4,
        Hunt       = 5,
        Guard      = 6,
        Ambush     = 7,
    };

    /**
     * Puts the local player's dog into a companion mode.
     *
     * The game's own reflected entry point, also reached from the console as
     * `wh_ai_Dog_SetMode` and from `C_ScriptBindXGenAIModule::SetPlayerDogMode`.
     * It resolves the *local player's* primary companion of category 1, queues
     * objective 2 (Bark) as an acknowledgement, then compares the mode byte at
     * `+0x28` of the companion control object and writes it -- notifying
     * listeners only on an actual change, so a redundant call costs a bark and
     * nothing else.
     *
     * Local-player scoped by construction, which is exactly the scope the mod
     * wants: the owning client is the only peer that runs its dog's brain, and
     * the only one the mode means anything to.
     */
    void SetPlayerDogMode(E_DogCompanionMode mode);
} // namespace SDK::wh::xgenaimodule::activitysystem
