#pragma once

#include "shared/features/cook_pot/cook_pot_entity.h"

#include <mafianet/replica_manager3.h>

namespace KCDC::Features::CookPot {
    /**
     * Client side of one replicated stew pot. Its arrival, every restated
     * stock and its departure tell `CookPotService` to bring the native pots
     * in line on its next tick; those notices come from the peer pump, while
     * the engine work they lead to has to run on the game thread.
     */
    class CookPot final: public KCDC::Shared::Entities::CookPotEntity {
      public:
        void OnConstructed() override;
        void OnDeserialized(bool transformUpdated) override;
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;
    };
} // namespace KCDC::Features::CookPot
