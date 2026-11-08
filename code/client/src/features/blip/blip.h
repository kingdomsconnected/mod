#pragma once

#include "features/map/map_blip_service.h"

#include "shared/features/blip/blip_entity.h"

#include <cstdint>
#include <mafianet/replica_manager3.h>

namespace KCDC::Features::Blip {
    /**
     * Client binding between a replicated server blip and the compass and map
     * screen.
     *
     * The whole of it is one registry row in MapBlipService, owned by the
     * server: made when the blip arrives, restated whenever the wire changes
     * it, dropped when it goes. The registry is safe to write from any thread,
     * so all three happen straight from the peer pump, and MapBlipService's own
     * tick puts the row on screen like any other.
     *
     * Nothing is ever reported back.
     */
    class Blip final: public Shared::Entities::BlipEntity {
      public:
        void OnConstructed() override;
        void OnDeserialized(bool transformUpdated) override;
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

      private:
        /** The row this blip should be, from what the wire says now. */
        KCDC::Features::Map::BlipDescription Describe() const;

        /** Makes the row, or restates it when anything the screen draws changed. */
        void Publish();

        std::uint32_t _rowId = 0;

        // What the row was last written from, so a restated transform that
        // moved nothing does not reconcile every blip on the screen.
        KCDC::Features::Map::BlipDescription _published;

        bool _warnedFull = false;
    };
} // namespace KCDC::Features::Blip
