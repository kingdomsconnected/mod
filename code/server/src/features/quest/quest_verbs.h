#pragma once

#include "shared/features/quest/quest_entity.h"
#include "shared/features/quest/quest_progress.h"
#include "shared/features/quest/quest_rpc.h"

#include <cstddef>
#include <cstdint>
#include <mafianet/types.h>
#include <optional>
#include <string>
#include <vector>

namespace KCDC::Features::Quest {
    /**
     * What the server can do with quests, in one place, so a script and a
     * command write the same journal entry.
     *
     * A quest is state: it exists until it is removed, and a client that
     * arrives late finds it in their journal. The toast that says something
     * changed is the opposite -- an event, raised by `Announce` on whoever is
     * there at the time. The client is a renderer; every transition is decided
     * here.
     */
    class Quests {
      public:
        /** Registers the one message this feature listens for. Called once at startup. */
        static void Install();

        /** Every replicated quest, optionally narrowed to one virtual world. */
        static std::vector<Shared::Entities::QuestEntity *> Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld);

        /** The quest with that key, or null. A key is unique per owner, so `owner` names one player's copy and without it the first match answers. */
        static Shared::Entities::QuestEntity *Find(const std::string &questKey, std::optional<MafiaNet::VirtualWorldId> virtualWorld, std::optional<MafiaNet::PeerGuid> owner = std::nullopt);

        /** The connection a quest was written for, or none when it went to the whole world. */
        static std::optional<MafiaNet::PeerGuid> OwnerOf(const Shared::Entities::QuestEntity *quest);

        /** Raises the game's own quest toast on whoever can see this quest. An event, so a client that was not there never hears it. */
        static void Announce(const Shared::Entities::QuestEntity *quest, Shared::RPC::QuestAnnounceKind kind, const std::string &detail = std::string());

        /**
         * Writes a quest into everyone's journal in `virtualWorld`, or one
         * player's when `owner` names one. Null when the key is malformed or
         * taken: two rows with one identity leave the objective tracker
         * resolving a name to either of them.
         */
        static Shared::Entities::QuestEntity *Give(const std::string &questKey, const std::string &title, const std::string &description, Shared::Entities::QuestType questType, const std::vector<Shared::Entities::QuestObjective> &objectives,
            std::optional<MafiaNet::PeerGuid> owner, std::uint32_t virtualWorld);

        /** Retitles a live quest. Silent: nothing about a correction is worth a toast. */
        static void SetText(Shared::Entities::QuestEntity *quest, const std::string &title, const std::string &description);

        /** Moves a quest's state on. `announce` sends the matching toast alongside the state. */
        static void SetProgress(Shared::Entities::QuestEntity *quest, Shared::Entities::QuestProgress progress, bool announce);

        /** Rewrites one objective in place. Out-of-range indices are refused rather than grown into. */
        static void SetObjective(Shared::Entities::QuestEntity *quest, std::size_t index, const Shared::Entities::QuestObjective &objective, bool announce);

        /** Replaces the whole objective list. Anything past `QuestEntity::kMaxObjectives` is dropped. */
        static void SetObjectives(Shared::Entities::QuestEntity *quest, const std::vector<Shared::Entities::QuestObjective> &objectives, bool announce);

        /**
         * Starts or stops `player`'s client following a quest, as the journal's
         * track button would. Without a player it goes to everyone who can see
         * the quest: its owner, or every player in its world. Answers how many
         * clients were told, and none for a player the quest is not theirs to see.
         *
         * Following is the player's afterwards: this is an instruction, and
         * the outcome comes back through `questTrackingChanged` like any other.
         */
        static std::size_t Track(const Shared::Entities::QuestEntity *quest, bool tracked, std::optional<std::uint64_t> playerNetworkId = std::nullopt);

        static void Remove(Shared::Entities::QuestEntity *quest);
        static std::size_t RemoveAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld);
    };
} // namespace KCDC::Features::Quest
