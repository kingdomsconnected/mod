#include "cook_pot.h"
#include "cook_pot_service.h"

namespace KCDC::Features::CookPot {
    void CookPot::OnConstructed() {
        CookPotService::Get().MarkStockChanged();
    }

    void CookPot::OnDeserialized(bool) {
        CookPotService::Get().MarkStockChanged();
    }

    void CookPot::DeallocReplica(MafiaNet::Connection_RM3 *) {
        CookPotService::Get().MarkStockChanged();
        delete this;
    }
} // namespace KCDC::Features::CookPot
