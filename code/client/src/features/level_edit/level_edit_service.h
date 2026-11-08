#pragma once

#include "core/feature_registry.h"

#include "features/prop/prop_editor_objects.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace KCDC::Features::LevelEdit {
    /**
     * The client half of server level edits: the objects the level placed
     * that the server took out of the world or moved, for every player.
     *
     * It keeps its own `WorldObjects`, the World Builder's register of the
     * level's objects, so a server edit is found again and re-applied exactly
     * as one of the editor's: by level EntityGuid or mesh and position,
     * through HLOD streaming and layer switches, with the layer physics
     * collider taken out alongside the brush. It is separate from the
     * editor's own so a map the player edits locally never saves what the
     * server did, and the server's edits never land in the player's undo.
     *
     * Driven by what the wire changes, never by looking at every replica:
     * the replicas queue themselves. Nothing is reported back.
     */
    class LevelEditService final: public KCDC::Core::IFeatureService {
      public:
        /** Applies what the wire changed and keeps every edit on its object. Game thread. */
        void Update() override;

        /** The session ended: every object goes back as the level had it. */
        void Reset() override;

        /** Loading-thread edge: the native world the edits were on is gone. */
        void NativeContextChanged() override;

        /** A replica arrived or changed; the tick applies it. Callable from the peer pump. */
        void QueueApply(std::uint64_t networkId);

        /** A replica went; the tick puts its object back. Callable from the peer pump. */
        void QueueRestore(std::uint64_t networkId);

        /** The level objects the server's edits hold, which know where the level put one an edit has moved. Game thread. */
        const KCDC::Features::Prop::WorldObjects &Objects() const {
            return _objects;
        }

      private:
        void ObserveNativeContext();

        /** Records one replica's edit against its object, afresh. */
        void Apply(std::uint64_t networkId);

        /** Puts one replica's object back and forgets it. */
        void Restore(std::uint64_t networkId);

        KCDC::Features::Prop::WorldObjects _objects;

        /** Replica NetworkID -> the edit in `_objects` it made. */
        std::unordered_map<std::uint64_t, std::uint64_t> _applied;

        std::mutex _pendingMutex;
        std::unordered_set<std::uint64_t> _pendingApplies;
        std::vector<std::uint64_t> _pendingRestores;

        // Incremented from the loading thread and consumed on the game thread.
        std::atomic<std::uint64_t> _nativeContext {1};
        std::uint64_t _observedContext = 0;
    };
} // namespace KCDC::Features::LevelEdit
