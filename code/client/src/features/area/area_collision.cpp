#include "area_collision.h"
#include "core/application.h"

namespace KCDC::Features::Area {
    void AreaCollision::OnConstructed() {
        if (Core::gApplication) {
            Core::gApplication->GetAreaCollisionService().Queue(GetNetworkID());
        }
    }

    void AreaCollision::OnDeserialized(bool) {
        OnConstructed();
    }

    void AreaCollision::DeallocReplica(MafiaNet::Connection_RM3 *) {
        OnConstructed();
        delete this;
    }
} // namespace KCDC::Features::Area
