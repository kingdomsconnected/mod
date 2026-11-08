#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <v8.h>

namespace KCDC::Scripting {
    /**
     * The global `Dice`: matches between two players on the game's own dice
     * table, dealt and scored by the server.
     *
     * Who may challenge whom, and what a match is played for, belong to the
     * gamemode: it starts a match from wherever it decides a challenge comes
     * from, and settles it on `diceMatchEnd`.
     */
    void RegisterDiceBindings(v8::Isolate *isolate, v8::Local<v8::Object> global);

    // What this feature tells loaded resources happened at a dice table.
    namespace Events {
        void DiceProgress(const char *event, const nlohmann::json &data);
        /** A match started; `first` sits on the table's first seat. */
        void DiceMatchStart(std::uint32_t matchId, std::uint64_t firstNetworkId, std::uint64_t secondNetworkId, std::uint32_t targetScore);

        /** A match ended. `winnerSeat` is 0 or 1, or 255 when nobody won. */
        void DiceMatchEnd(std::uint32_t matchId, std::uint64_t firstNetworkId, std::uint64_t secondNetworkId, std::uint8_t winnerSeat, const char *reason, std::uint32_t firstScore, std::uint32_t secondScore);
    } // namespace Events
} // namespace KCDC::Scripting
