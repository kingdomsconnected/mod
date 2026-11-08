#pragma once

#include "shared/features/quest/quest_entity.h"

#include "sdk/wh/questmodule/c_log_base.h"
#include "sdk/wh/questmodule/c_objective.h"
#include "sdk/wh/questmodule/c_quest.h"

#include <atomic>
#include <cstdint>
#include <mafianet/replica_manager3.h>
#include <vector>

namespace KCDC::Features::Quest {
    /**
     * Client binding between a replicated quest and the native quest node the
     * journal reads.
     *
     * The node is a real `wh::questmodule::C_Quest` built with the game's own
     * constructor, so the journal row, the diary page, the objective HUD and
     * the quest marker all read it as they read a shipped quest. The one thing
     * that is not native is how it is found: `QuestService` splices it into
     * what the manager's two readers answer, rather than into the manager.
     *
     * The server is the only writer, and the toast is a separate
     * `QuestAnnounced` event. `QuestService` owns the tick.
     */
    class Quest final: public Shared::Entities::QuestEntity {
      public:
        /** Queues teardown: a replica can be destroyed from the peer pump, where the quest graph is not safe to touch. */
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

        // Both raised from the peer pump, so both only set the flag the tick reads.
        void OnConstructed() override;
        void OnDeserialized(bool transformUpdated) override;

        /** Builds the node, brings it up to date, and announces the change. Game thread only. */
        void Frame();

        /** Forgets the node without touching it, for a game that is going away under us. */
        void Unbind();

        /** The node the journal reads, or null until it is built. Game thread. */
        SDK::wh::questmodule::C_Quest *GetNative() const {
            return _native;
        }

      private:
        /** Builds the node and registers it, or answers false while the quest module is not up yet. */
        bool EnsureNative();

        void ApplyState();
        void ApplyObjectives();

        /** Forgets what the node was written with, so the next apply writes all of it. */
        void ForgetApplied();

        void DestroyNative();

        SDK::wh::questmodule::C_Quest *_native = nullptr;
        std::vector<SDK::wh::questmodule::C_Objective *> _objectives;

        // One per progress value per objective, since an objective whose log
        // list misses its own progress is hidden. Nothing else owns them.
        std::vector<SDK::wh::questmodule::C_LogBase *> _logs;

        // What the node was last written with. The delta serializer says only
        // that something changed, so without this every tick would rebuild a
        // constant port per line. The text is not tracked: the node holds a
        // label, so a retitled quest needs no write at all.
        std::uint8_t _appliedProgress                                                         = 0xFFU;
        std::uint8_t _appliedObjectiveProgress[Shared::Entities::QuestEntity::kMaxObjectives] = {};

        // Set from the peer pump, cleared on the game thread once the node
        // matches the wire.
        std::atomic<bool> _dirty {true};

        /** Logged once, so a quest module that never comes up does not fill the log. */
        bool _buildDeferred = false;
    };
} // namespace KCDC::Features::Quest
