#pragma once

#include "shared/features/level_edit/level_edit_entity.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::LevelEdit {
    // Which level entities the server's edits take out of the world, by GUID, so a service that keeps its own record of a kind of level entity -- doors, stashes, stations -- can tell one is gone without walking every edit.
    //
    // Fed by LevelEdits as it applies and drops entity edits. An edit can also go without passing through LevelEdits: a resource that stops destroys the edits it made. So every read checks the edit is still live, and a service that hid one of its own when an edit took it out
    // polls `RemovalOf` to learn it is back.
    class LevelEditModule final {
      public:
        static LevelEditModule &Get();

        // An entity edit was applied or restated.
        void Note(Shared::Entities::LevelEditEntity &edit);

        // An entity edit is about to go.
        void Forget(Shared::Entities::LevelEditEntity &edit);

        // Something about an edit changed: what it hides or swaps, or whether it is there at all.
        void Touch() {
            ++_revision;
        }

        // The live edit that takes the level entity `guid` out of `virtualWorld`, or out of the global world, which every world sees; null while nothing does.
        Shared::Entities::LevelEditEntity *RemovalOf(std::uint64_t guid, std::uint32_t virtualWorld) const;

        // Every live entity edit that takes an entity out of the world, for a service catching up after `Revision` moved.
        std::vector<Shared::Entities::LevelEditEntity *> Removals() const;

        // Bumped by every change LevelEdits makes, so a poller knows when to look again.
        std::uint64_t Revision() const {
            return _revision;
        }

      private:
        // Drops the ids of edits that are gone, which a stopped resource destroys without telling anyone.
        void Prune() const;

        // Level GUID -> the NetworkIDs of the entity edits keyed by it, one per virtual world at most. Pruned on read.
        mutable std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> _byGuid;
        std::uint64_t _revision = 1;
    };
} // namespace KCDC::Features::LevelEdit
