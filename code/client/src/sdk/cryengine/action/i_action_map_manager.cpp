#include "i_action_map_manager.h"

#include "../system/i_system.h"
#include "i_game_framework.h"

namespace SDK {
    IActionMapManager *IActionMapManager::GetInstance() {
        auto *gameFramework = IGameFramework::GetInstance();
        return gameFramework != nullptr ? gameFramework->GetIActionMapManager() : nullptr;
    }

    IActionFilter *IActionMapManager::FindOrCreateActionFilter(const char *name, const char *xml, std::size_t size) {
        if (IActionFilter *existing = GetActionFilter(name)) {
            return existing;
        }
        ISystem *system = ISystem::GetInstance();
        if (system == nullptr) {
            return nullptr;
        }
        XmlNodeRef node = system->LoadXmlFromBuffer(xml, size, false, false);
        if (!node.IsValid()) {
            return nullptr;
        }

        // The type is ignored by this build; passed for what the XML says.
        IActionFilter *filter = CreateActionFilter(name, 1);
        if (filter == nullptr || !filter->SerializeXML(node, true)) {
            return nullptr;
        }
        return filter;
    }
} // namespace SDK
