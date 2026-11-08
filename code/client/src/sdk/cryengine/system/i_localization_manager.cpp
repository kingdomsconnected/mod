#include "i_localization_manager.h"

#include "i_system.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK {
    ILocalizationManager *ILocalizationManager::GetInstance() {
        ISystem *system = ISystem::GetInstance();
        return system != nullptr ? system->GetLocalizationManager() : nullptr;
    }

    namespace {
        // (manager, const CryString &file, keepTag, forceReload, isModPatch, replayAsPatch). Without
        // forceReload, a file already in the loaded set answers true without being read. The last
        // flag is kept on the file's record and picks the pass the game's own full reload replays
        // it in; the game's mod-patch loader clears it, so this does too.
        bool LoadSpreadsheet(ILocalizationManager *manager, const char *path, bool isModPatch) {
            ScopedCryString file(path);
            return hook::call<bool>(gPatterns.CLocalizedStringsManager__LoadExcelXmlSpreadsheet, manager, &file.Get(), false, true, isModPatch, false);
        }
    } // namespace

    bool ILocalizationManager::LoadPatch(const char *path) { return LoadSpreadsheet(this, path, true); }

    bool ILocalizationManager::ReloadFile(const char *path) { return LoadSpreadsheet(this, path, true); }
} // namespace SDK
