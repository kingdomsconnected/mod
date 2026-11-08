#include "dlc.h"

#include "sdk/wh/c_wh_global_environment.h"
#include "sdk/wh/game/dlc/i_dlc_manager.h"
#include "sdk/wh/shared/e_dlc.h"

#include "shared/game/dlc_catalog.h"

#include <logging/logger.h>

namespace KCDC::Core {
    namespace {
        // The catalog's ids are the engine's own enum values, so the mask and the manager's argument are one number.
        static_assert(static_cast<std::uint8_t>(SDK::wh::shared::E_DLC::ForgeTycoon) == 5, "E_DLC::ForgeTycoon must stay the dlc table's dlc_id");
        static_assert(static_cast<std::uint8_t>(SDK::wh::shared::E_DLC::Last) == KCDC::Shared::Game::kMaxDLCId + 1, "The catalog and the engine enum disagree about how many DLCs exist");

        std::uint16_t InstalledDLC() {
            SDK::wh::C_GlobalEnvironment *environment = SDK::wh::C_GlobalEnvironment::GetInstance();
            SDK::wh::game::dlc::I_DLCManager *manager = environment != nullptr ? environment->GetDLCManager() : nullptr;
            if (manager == nullptr) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("The DLC manager is not up yet; reporting no DLC, so any requirement will look unmet");
                return 0;
            }

            std::uint16_t installed = 0;
            for (const KCDC::Shared::Game::DLCEntry &dlc : KCDC::Shared::Game::g_DLCs) {
                if (manager->IsInstalled(static_cast<SDK::wh::shared::E_DLC::Type>(dlc.id))) {
                    installed |= KCDC::Shared::Game::DLCBit(dlc.id);
                }
            }
            return installed;
        }
    } // namespace

    std::uint16_t MissingDLC(std::uint16_t required) {
        const std::uint16_t installed = InstalledDLC();
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("DLC installed: {}", installed != 0 ? KCDC::Shared::Game::DLCMaskToLabels(installed) : std::string("none"));
        return static_cast<std::uint16_t>(required & ~installed);
    }
} // namespace KCDC::Core
