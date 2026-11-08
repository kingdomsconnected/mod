#pragma once

#include "shared/features/dialogue/dialogue_rpc.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Dialogue {
    /** One row of a page, as a gamemode authored it. */
    struct Option {
        std::string id;
        std::string text;
        bool enabled = true;
    };

    /** One page of a conversation. */
    struct Page {
        std::string line;
        bool onRight = false;
        std::vector<Option> options;
    };

    /**
     * Conversations, owned by the server and attached to nothing.
     *
     * A conversation runs **on a player**. It is not bound to an NPC, an entity
     * or a location: what it is about lives in the gamemode's own code, which
     * is what lets the same machinery serve a smith, a notice board or a timer.
     * That is a deliberate trade, and it has a cost worth stating -- this owns
     * no distance checks, no line of sight and no "did the NPC die", because
     * none of those are knowable without the relationship it refuses to hold.
     * A gamemode that wants them closes the conversation itself.
     *
     * Two invariants carry the protocol:
     *
     * - **One conversation per player.** Opening a second closes the first with
     *   `Replaced` rather than silently stacking them.
     * - **Pages are generational.** Every page carries an id the client echoes
     *   with its answer, and an answer against any other page is dropped. That
     *   is not belt-and-braces: a player who presses confirm while the next
     *   page is in flight would otherwise have their answer applied to a page
     *   that no longer exists.
     *
     * The option index a client sends is validated against the page the server
     * actually sent. `enabled` is a rendering hint and never a gate -- anything
     * an option costs is checked when it is taken, not when the page was built.
     */
    class DialogueService final {
      public:
        static DialogueService &Get();

        /** Registers the RPC handlers. Idempotent. */
        void Install();

        /**
         * Opens a conversation on `playerId` and shows `page`.
         * Returns the session id, or 0 if the page was unusable.
         */
        std::uint32_t Open(std::uint64_t playerId, const Page &page);

        /** Replaces the page of a live session. False if the session is gone. */
        bool Update(std::uint32_t sessionId, const Page &page);

        /** Ends a session and tells the client. */
        bool Close(std::uint32_t sessionId, Shared::RPC::DialogueCloseReason reason);

        /** The session a player is in, or 0. */
        std::uint32_t SessionOf(std::uint64_t playerId) const;

        /** Drops a player's session without messaging them; for disconnects. */
        void Forget(std::uint64_t playerId);

      private:
        struct Session {
            std::uint64_t playerId = 0;
            std::uint32_t pageId   = 0;
            Page page;
        };

        bool Send(const Session &session, std::uint32_t sessionId);
        Session *Find(std::uint32_t sessionId);

        std::unordered_map<std::uint32_t, Session> _sessions;
        std::unordered_map<std::uint64_t, std::uint32_t> _byPlayer;
        std::uint32_t _nextSession = 1;
        bool _installed            = false;
    };
} // namespace KCDC::Features::Dialogue
