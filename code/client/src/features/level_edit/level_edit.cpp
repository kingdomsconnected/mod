#include "level_edit.h"

#include "core/application.h"

namespace KCDC::Features::LevelEdit {
    void LevelEdit::OnConstructed() {
        if (KCDC::Core::gApplication != nullptr) {
            KCDC::Core::gApplication->GetLevelEditService().QueueApply(GetNetworkID());
        }
    }

    void LevelEdit::OnDeserialized(bool) {
        if (KCDC::Core::gApplication != nullptr) {
            KCDC::Core::gApplication->GetLevelEditService().QueueApply(GetNetworkID());
        }
    }

    void LevelEdit::DeallocReplica(MafiaNet::Connection_RM3 *) {
        if (KCDC::Core::gApplication != nullptr) {
            KCDC::Core::gApplication->GetLevelEditService().QueueRestore(GetNetworkID());
        }
        delete this;
    }
} // namespace KCDC::Features::LevelEdit
