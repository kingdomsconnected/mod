#include "input.h"

#include "core/application.h"

namespace KCDC::Common {
    bool GameInput::IsInputLocked() const {
        const auto *app = Core::gApplication.get();
        return app != nullptr && app->AreControlsLocked();
    }

    void GameInput::SetInputLocked(bool locked) {
        if (auto *app = Core::gApplication.get()) {
            app->LockControls(locked);
        }
    }
} // namespace KCDC::Common
