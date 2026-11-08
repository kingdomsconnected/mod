#pragma once

#include "core/feature_registry.h"

#include "sdk/cryengine/3dengine/i_render_node.h"
#include "sdk/cryengine/physics/i_physical_entity.h"
#include "sdk/types.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_set>
#include <utility>
#include <vector>

namespace KCDC::Features::SmartObject {
    /**
     * What spans every replicated smart object: the tick that builds them,
     * the queue of natives a replica that went left behind, and the item
     * slots placed stations own.
     *
     * Every level `ItemSlot` is left dormant (`world_content_hooks.cpp`), but
     * a station's own fixtures -- an alchemy table's recipe book, a smithy's
     * tongs and hammer -- are what its minigame hands the player. A placed
     * station's are listed here, by the GUID their part spawns under, before
     * it is initialised, which is when that hook asks.
     */
    class SmartObjectModule final: public KCDC::Core::IFeatureService {
      public:
        static SmartObjectModule &Get();

        void Update() override;
        void Reset() override;
        void NativeContextChanged() override;

        /** Natives a replica that went leaves for the tick to remove; callable from the peer pump. */
        void QueueRelease(std::vector<SDK::EntityId> parts, std::vector<std::pair<SDK::IRenderNode *, SDK::IPhysicalEntity *>> brushes);

        void AddFixture(std::uint64_t guid);
        void RemoveFixture(std::uint64_t guid);

        /** Whether an item slot with this GUID is a placed station's fixture, which may spawn its item. */
        bool IsFixture(std::uint64_t guid) const;

      private:
        void DrainReleases();
        void ObserveNativeContext();

        std::mutex _releaseMutex;
        std::vector<SDK::EntityId> _pendingParts;
        std::vector<std::pair<SDK::IRenderNode *, SDK::IPhysicalEntity *>> _pendingBrushes;

        // Asked from the item slot hook, which can run on a loading thread.
        mutable std::mutex _fixtureMutex;
        std::unordered_set<std::uint64_t> _fixtures;

        std::atomic<std::uint64_t> _nativeContext {1};
        std::uint64_t _observedContext = 0;
    };
} // namespace KCDC::Features::SmartObject
