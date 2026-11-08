#pragma once

#include <cstdint>
#include <string>
#include <v8.h>

namespace KCDC::Scripting {
    /**
     * The global `Dialogue`: conversations the server drives on a player.
     *
     * A conversation is attached to nothing. It runs on a player, and what it
     * is about -- a smith, a notice board, a timer -- is the gamemode's own
     * business, held in its own closures. That is why nothing here takes an
     * entity.
     */
    void RegisterDialogueBindings(v8::Isolate *isolate, v8::Local<v8::Object> global);

    // What this feature tells loaded resources happened in a conversation.
    namespace Events {
        /** A player picked an option. Carries the option's own id, not its index. */
        void DialogueChoice(std::uint32_t sessionId, std::uint64_t playerNetworkId, const std::string &optionId);

        /** The conversation ended, whoever ended it. */
        void DialogueClosed(std::uint32_t sessionId, std::uint64_t playerNetworkId, std::uint8_t reason);
    } // namespace Events
} // namespace KCDC::Scripting
