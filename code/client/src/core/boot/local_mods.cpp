#include <utils/safe_win32.h>

#include <MinHook.h>

#include <logging/logger.h>
#include <utils/hooking/hook_function.h>

#include "local_mods.h"

#include "sdk/patterns.h"

namespace KCDC::Core::Boot {
    namespace {
        using ScanModsFn = void(__fastcall *)(SDK::wh::C_ModManager *);
        ScanModsFn g_scanModsOriginal = nullptr;

        using ForEachEnabledModFn = void(__fastcall *)(SDK::wh::C_ModManager *, SDK::wh::ModFunction *);
        ForEachEnabledModFn g_forEachEnabledModOriginal = nullptr;

        /**
         * The player's own mods never load: a server's data is the only data.
         *
         * The scan runs once, from CSystem::Init. Every consumer -- the override
         * paks, mod.cfg, scripts/mods/<id>.lua, localization patches and the
         * save tooltip -- walks the enabled list alone, so emptying it after
         * the scan leaves the game as if mod_order.txt named nothing. Nothing
         * under mods/ is touched, and the discovered list stays for reporting.
         */
        void __fastcall C_ModManager_ScanMods_Hook(SDK::wh::C_ModManager *manager) {
            g_scanModsOriginal(manager);
            LocalMods::Get().IgnorePlayerMods(*manager);
        }

        /**
         * `C_ModManager::ForEachEnabledMod`, vtable slot 5. The server's mods
         * are visited first: the original releases the callable once it has
         * walked its own list.
         */
        void __fastcall C_ModManager_ForEachEnabledMod_Hook(SDK::wh::C_ModManager *manager, SDK::wh::ModFunction *function) {
            LocalMods::Get().VisitServerMods(*function);
            g_forEachEnabledModOriginal(manager, function);
        }
    } // namespace

    LocalMods::ServerMod::ServerMod(const std::string &id): _path(("kcdc_server_mods/" + id + "/").c_str()), _id(id.c_str()) {
        SDK::wh::S_ModInfo::Construct(_info);
        _info.SetName(id.c_str());
    }

    LocalMods::ServerMod::~ServerMod() {
        _info.Destroy();
    }

    LocalMods &LocalMods::Get() {
        static LocalMods instance;
        return instance;
    }

    void LocalMods::SetServerModIds(const std::set<std::string> &ids) {
        _serverMods.clear();
        for (const std::string &id : ids) {
            _serverMods.push_back(std::make_unique<ServerMod>(id));
        }
    }

    void LocalMods::IgnorePlayerMods(SDK::wh::C_ModManager &manager) const {
        const std::size_t enabled = manager.GetEnabledCount();
        manager.ClearEnabled();
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Ignoring local mods: {} installed, {} enabled", manager.GetDiscoveredCount(), enabled);
    }

    void LocalMods::VisitServerMods(SDK::wh::ModFunction &function) const {
        if (function.m_pImpl == nullptr) {
            return;
        }
        for (const std::unique_ptr<ServerMod> &mod : _serverMods) {
            function.m_pImpl->Call(*mod);
        }
    }
} // namespace KCDC::Core::Boot

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ModManager__ScanMods), reinterpret_cast<LPVOID>(&KCDC::Core::Boot::C_ModManager_ScanMods_Hook), reinterpret_cast<LPVOID *>(&KCDC::Core::Boot::g_scanModsOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ModManager__ForEachEnabledMod), reinterpret_cast<LPVOID>(&KCDC::Core::Boot::C_ModManager_ForEachEnabledMod_Hook), reinterpret_cast<LPVOID *>(&KCDC::Core::Boot::g_forEachEnabledModOriginal));
    },
    "LocalMods");
