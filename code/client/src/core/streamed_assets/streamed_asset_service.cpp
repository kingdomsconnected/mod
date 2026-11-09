#include <utils/safe_win32.h>

#include "streamed_asset_service.h"

#include "core/application.h"
#include "core/boot/local_mods.h"

#include "features/crafting/custom_recipe_module.h"
#include "features/items/custom_item_module.h"

#include "sdk/cryengine/3dengine/i_3d_engine.h"
#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/3dengine/i_material_manager.h"
#include "sdk/cryengine/animation/c_animation_set.h"
#include "sdk/cryengine/animation/character_manager.h"
#include "sdk/cryengine/animation/city_hash.h"
#include "sdk/cryengine/animation/i_animation_set.h"
#include "sdk/cryengine/audio/i_audio_system.h"
#include "sdk/cryengine/audio/s_audio_request.h"
#include "sdk/cryengine/renderer/c_texture.h"
#include "sdk/cryengine/renderer/i_renderer.h"
#include "sdk/cryengine/system/i_cry_pak.h"
#include "sdk/cryengine/system/i_localization_manager.h"
#include "sdk/cryengine/system/i_system.h"
#include "sdk/cryengine/system/i_system_event_dispatcher.h"
#include "sdk/cryengine/system/i_xml_node.h"
#include "sdk/cryengine/system/s_system_global_environment.h"
#include "sdk/fmod/fmod_api.h"
#include "sdk/wh/c_wh_fmod_studio_wrapper.h"
#include "sdk/wh/databasemodule/c_object_database_manager.h"
#include "sdk/wh/playermodule/c_alchemy_recipe_database.h"
#include "sdk/wh/playermodule/c_blacksmith_recipe_database.h"
#include "sdk/wh/xgenaimodule/scheduler/c_scheduler_database.h"
#include "sdk/wh/shared/c_recursive_srw_lock.h"

#include <logging/logger.h>

#include <miniz.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <string>

namespace KCDC::Core::StreamedAssets {
    namespace {
        // CryPak flags: FLAGS_PATH_REAL, and FLAGS_OVERRIDE_PAK for the lanes that replace the game's
        // files, as the game mounts an installed mod's paks.
        constexpr unsigned int kMountFlags         = 0x10000u;
        constexpr unsigned int kOverrideMountFlags = 0x10400u;

        // The human Mannequin files the clip fragment is declared in. Every human body runs on the
        // male set (Human::ExtendMannequinFile folds the female one into it).
        constexpr std::string_view kFragmentIdsFile = "kcd_male_fragmentids.xml";
        constexpr std::string_view kControllerFile  = "kcd_male_controllerdefs.xml";
        constexpr std::string_view kDatabaseFile    = "kcd_male_database.adb";
        constexpr const char *kDatabasePath         = "Animations/Mannequin/ADB/kcd_male_database.adb";

        // The game's localization folder, beside Data: the root its own `<language>_xml.pak`s and an
        // installed mod's are bound to, and the prefix every string file is read through.
        constexpr const char *kLocalizationRoot = "Localization";
        // The language a patch falls back to, as the game falls back to English's pak.
        constexpr std::string_view kFallbackLanguage = "english";

        bool EndsWithFile(const char *path, std::string_view file) {
            std::string folded;
            for (const char *c = path; *c != '\0'; ++c) {
                folded.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*c == '\\' ? '/' : *c))));
            }
            return folded.size() >= file.size() && folded.compare(folded.size() - file.size(), file.size(), file) == 0;
        }

        std::string StemOf(const std::string &path, std::string_view extension) {
            return path.substr(0, path.size() - extension.size());
        }

        std::string FileNameOf(const std::string &path) {
            const std::size_t slash = path.rfind('/');
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }

        // Reads one entry of a validated pak; the policy has already said what it may be.
        std::string ReadEntry(const std::string &pakPath, const std::string &entry) {
            mz_zip_archive zip {};
            if (!mz_zip_reader_init_file(&zip, pakPath.c_str(), 0)) {
                return {};
            }
            std::size_t size = 0;
            void *data       = mz_zip_reader_extract_file_to_heap(&zip, entry.c_str(), &size, 0);
            std::string bytes;
            if (data != nullptr) {
                bytes.assign(static_cast<const char *>(data), size);
                mz_free(data);
            }
            mz_zip_reader_end(&zip);
            return bytes;
        }

        // `<Blend ExitTime="0" StartTime="0" Duration="..."/>`, the first child of every layer.
        void AddBlend(const SDK::XmlNodeRef &layer, const char *duration) {
            SDK::XmlNodeRef blend = layer->newChild("Blend");
            blend->setAttr("ExitTime", "0");
            blend->setAttr("StartTime", "0");
            blend->setAttr("Duration", duration);
        }
    } // namespace

    void StreamedAssetService::EnsureListening() {
        if (_listening) {
            return;
        }
        SDK::ISystem *system = SDK::SSystemGlobalEnvironment::Get().pSystem;
        _listening           = system->GetISystemEventDispatcher()->RegisterListener(this, "", true);
    }

    void StreamedAssetService::BeginJoin() {
        EnsureListening();
        _error.clear();
        _joined      = false;
        _joinMounted = false;
        _applying    = false;
        _pending.clear();
        _scripts.Stop();
        _scripts.ClearStaged();
    }

    void StreamedAssetService::EndSession() {
        _scripts.Stop();
        _scripts.ClearStaged();
        // The level stays up behind the menu. Whatever loads next, a session or the offline World
        // Builder, replaces it, and that edge retires everything this session mounted.
        _current.clear();
        _joined      = false;
        _joinMounted = false;
        _applying    = false;
        _pending.clear();
    }

    std::vector<std::string> StreamedAssetService::GetUnapprovedScripts() const {
        return _scripts.DescribeUnapproved();
    }

    void StreamedAssetService::ApproveScripts() {
        _scripts.ApproveStaged();
    }

    std::unordered_set<std::string> StreamedAssetService::CurrentPaths() const {
        std::unordered_set<std::string> paths;
        for (const Framework::Integrations::Client::ResourceAssetPak &pak : KCDC::Core::gApplication->GetResourceAssetPaks()) {
            paths.insert(pak.path);
        }
        return paths;
    }

    StreamedAssetService::JoinStatus StreamedAssetService::PollJoin() {
        KCDC::Core::Application &application = *KCDC::Core::gApplication;
        // The paks are announced with the resource list, which follows the server's build check.
        if (application.GetConnectionPhase() < Framework::Integrations::Client::ConnectionPhase::Downloading) {
            return JoinStatus::Downloading;
        }
        if (_joined) {
            return JoinStatus::Ready;
        }

        const std::vector<Framework::Integrations::Client::ResourceAssetPak> &paks = application.GetResourceAssetPaks();
        if (!paks.empty()) {
            if (application.GetAssetConsentBytes() > 0) {
                return JoinStatus::AwaitingConsent;
            }
            if (!application.AreResourceAssetPaksDownloaded()) {
                return JoinStatus::Downloading;
            }
        }

        // A server that ships nothing still settles the set: whatever the last one shipped retires at
        // the level edge.
        if (!_joinMounted) {
            _clips.clear();
            for (const Framework::Integrations::Client::ResourceAssetPak &pak : paks) {
                if (!Mount(pak, true)) {
                    return JoinStatus::Failed;
                }
            }
            _current     = CurrentPaths();
            _joinMounted = true;
            RebuildClipDatabase();
            SyncModData(false);
            if (!paks.empty()) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Asset paks in use: {}", paks.size());
            }
        }
        if (!_scripts.DescribeUnapproved().empty()) {
            return JoinStatus::AwaitingScriptApproval;
        }
        _scripts.Start();
        _joined = true;
        return JoinStatus::Ready;
    }

    void StreamedAssetService::OnResourceAssetPaksChanged() {
        if (!_joined) {
            return;
        }
        const std::vector<Framework::Integrations::Client::ResourceAssetPak> &paks = KCDC::Core::gApplication->GetResourceAssetPaks();
        _pending.clear();
        for (std::size_t index = 0; index < paks.size(); ++index) {
            _pending.push_back(index);
        }
        _applying = true;
        _clips.clear();
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Applying {} asset pak(s) in place", paks.size());
    }

    void StreamedAssetService::Shutdown() {
        _settings.Shutdown();
    }

    void StreamedAssetService::Update() {
        _settings.Update();
        for (auto &[path, mounted] : _mounted) {
            if (!mounted.banksReported && !mounted.eventBanks.empty()) {
                ReportLinkedBanks(mounted);
            }
        }
        if (!_applying) {
            return;
        }

        // Paks already in use cost nothing; at most one new mount a frame, since opening a pak holds
        // CryPak's list lock while it reads the central directory.
        while (!_pending.empty()) {
            const Framework::Integrations::Client::ResourceAssetPak &pak = KCDC::Core::gApplication->GetResourceAssetPaks()[_pending.front()];
            _pending.pop_front();
            const bool fresh = _mounted.count(pak.path) == 0;
            if (!Mount(pak, false)) {
                _pending.clear();
                _applying = false;
                return;
            }
            if (fresh) {
                break;
            }
        }
        if (_pending.empty()) {
            _applying = false;
            _current  = CurrentPaths();
            RebuildClipDatabase();
            SyncModData(false);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Asset paks in use: {}", _current.size());
        }
    }

    bool StreamedAssetService::Mount(const Framework::Integrations::Client::ResourceAssetPak &pak, bool joining) {
        const std::string &path = pak.path;

        // The hash proves the bytes are the server's, not that the server is honest: the lane's
        // rules are checked here, on the machine they protect.
        std::vector<Framework::Utils::StreamedAssets::PakEntry> entries;
        std::string error;
        if (!Framework::Utils::StreamedAssets::PakArchive::Validate(path, *KCDC::Shared::StreamedAssets::Policy::Get(), pak.info.lane, pak.resource, entries, error)) {
            Fail("A streamed pak from '" + pak.resource + "' was refused: " + error);
            return false;
        }

        // Scripts are never mounted, so the game cannot find them by itself: they run in the
        // environment the host gives them, from the text read here.
        if (pak.info.lane == KCDC::Shared::StreamedAssets::kLaneScript) {
            if (!joining) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("'{}' changed its scripts mid-session; they run from the next join", pak.resource);
                return true;
            }
            std::vector<KCDC::Core::StreamedAssets::ModScriptHost::Source> sources;
            for (const Framework::Utils::StreamedAssets::PakEntry &entry : entries) {
                std::string text = ReadEntry(path, entry.path);
                if (text.empty() && entry.size != 0) {
                    Fail("A streamed pak from '" + pak.resource + "' could not be read");
                    return false;
                }
                sources.emplace_back(entry.path, std::move(text));
            }
            std::string refused;
            if (!_scripts.Stage(pak.resource, path, std::move(sources), refused)) {
                Fail(refused);
                return false;
            }
            return true;
        }

        SDK::ICryPak *cryPak        = SDK::ICryPak::GetInstance();
        const auto existing         = _mounted.find(path);
        const bool replaceLane      = pak.info.lane == KCDC::Shared::StreamedAssets::kLaneReplace;
        const bool localizationLane = pak.info.lane == KCDC::Shared::StreamedAssets::kLaneLocalization;
        const bool levelLane        = pak.info.lane == KCDC::Shared::StreamedAssets::kLaneLevel;
        if (levelLane) {
            // The level already read these files; swapping them under it would mix two worlds.
            if (!joining) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("'{}' changed its level edits mid-session; they apply from the next join", pak.resource);
                return true;
            }
            if (existing == _mounted.end()) {
                for (const Framework::Utils::StreamedAssets::PakEntry &entry : entries) {
                    if (!KCDC::Shared::StreamedAssets::Policy::IsLevelEntityXml(entry.path)) {
                        continue;
                    }
                    const std::string xml = ReadEntry(path, entry.path);
                    if (xml.empty() || KCDC::Shared::StreamedAssets::Policy::LevelXmlRunsScript(xml)) {
                        Fail("A streamed pak from '" + pak.resource + "' was refused: its level data runs a script or cannot be read");
                        return false;
                    }
                }
            }
        }
        if (existing != _mounted.end()) {
            if (!existing->second.accessible && !cryPak->SetPackAccessible(true, path.c_str(), kMountFlags)) {
                Fail("Cannot re-enable a streamed pak from '" + pak.resource + "'");
                return false;
            }
            existing->second.accessible = true;
        }
        else {
            // String patches bind where the game binds an installed mod's localization pak; both
            // override lanes win over whatever the game mounted at the same paths.
            const char *bindRoot = localizationLane ? kLocalizationRoot : cryPak->GetGameFolder();
            if (!cryPak->MountArchive(bindRoot, path.c_str(), replaceLane || localizationLane || levelLane ? kOverrideMountFlags : kMountFlags)) {
                Fail("Cannot mount a streamed pak from '" + pak.resource + "'");
                return false;
            }
            MountedPak mounted;
            mounted.resource   = pak.resource;
            mounted.lane       = pak.info.lane;
            mounted.path       = path;
            mounted.entries    = std::move(entries);
            mounted.accessible = true;
            _mounted.emplace(path, std::move(mounted));
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Mounted '{}' {}: {} entries", pak.resource, pak.info.lane, _mounted[path].entries.size());
        }
        MountedPak &mounted = _mounted[path];
        ReadItemTables(mounted);

        for (const Framework::Utils::StreamedAssets::PakEntry &entry : mounted.entries) {
            if (!replaceLane && KCDC::Shared::StreamedAssets::Policy::IsAnimationClip(entry.path)) {
                _clips.push_back(entry.path);
            }
            // The one cache the level load does not flush: a stock texture already resident keeps
            // drawing, and streaming mips from, the file it was first read from.
            if (replaceLane && KCDC::Shared::StreamedAssets::Policy::IsTextureBase(entry.path)) {
                SDK::CTexture::ReloadFile(entry.path.c_str());
            }
        }
        LoadBanks(mounted);

        if (!joining) {
            if (_levelLoaded) {
                ParseAudioControls(mounted);
            }
            // A resource's new pak supersedes any older one still answering -- in use, or left enabled
            // by a restart's stop until the level edge: what changed is reloaded where it is resident,
            // and the old pak stops answering.
            for (auto &[other, previous] : _mounted) {
                if (other == path || !previous.accessible || previous.resource != pak.resource || previous.lane != pak.info.lane) {
                    continue;
                }
                ReloadChanged(previous, mounted);
                previous.accessible = !cryPak->SetPackAccessible(false, other.c_str(), kMountFlags);
            }
        }
        return true;
    }

    void StreamedAssetService::LoadBanks(MountedPak &mounted) {
        if (!mounted.eventBanks.empty() || !mounted.rawBanks.empty()) {
            return;
        }
        std::vector<std::string> strings;
        std::vector<std::string> masters;
        std::vector<std::string> events;
        for (const Framework::Utils::StreamedAssets::PakEntry &entry : mounted.entries) {
            if (!KCDC::Shared::StreamedAssets::Policy::IsFmodBank(entry.path)) {
                continue;
            }
            const std::string name = FileNameOf(entry.path);
            if (name.size() > 13 && name.compare(name.size() - 13, 13, ".strings.bank") == 0) {
                strings.push_back(entry.path);
            }
            else if (name.size() > 12 && name.compare(name.size() - 12, 12, "_master.bank") == 0) {
                masters.push_back(entry.path);
            }
            else {
                events.push_back(name);
            }
        }
        if (strings.empty() && masters.empty() && events.empty()) {
            return;
        }

        SDK::wh::CWh_FmodStudioWrapper *wrapper = SDK::wh::CWh_FmodStudioWrapper::GetInstance();
        if (wrapper == nullptr || !wrapper->IsReady() || !SDK::Fmod::Api::Get().Resolve()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("'{}' streams FMOD banks but the game's audio is not up; they are not loaded", mounted.resource);
            return;
        }

        // The mod's own master and strings banks, loaded the way the game loads its own at start:
        // synchronously and outside the wrapper, so every event bank after them can read its paths.
        const std::string root = SDK::ICryPak::GetInstance()->GetGameFolder();
        for (const std::vector<std::string> *group : {&strings, &masters}) {
            for (const std::string &bank : *group) {
                SDK::Fmod::FMOD_STUDIO_BANK *loaded = nullptr;
                const std::string file              = root + "/" + bank;
                const SDK::Fmod::FMOD_RESULT result = SDK::Fmod::Api::Get().Studio_System_LoadBankFile(wrapper->GetStudioSystem(), file.c_str(), 0, &loaded);
                if (result != SDK::Fmod::FMOD_OK || loaded == nullptr) {
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("FMOD refused '{}' ({}): a mod's master bank must use the game's master bus (kcd.bank's GUID), or none of its events can play", bank, static_cast<int>(result));
                    continue;
                }
                mounted.rawBanks.push_back(loaded);
            }
        }

        for (const std::string &name : events) {
            SDK::wh::FSW_BANK *bank = nullptr;
            if (const auto registered = _registeredBanks.find(name); registered != _registeredBanks.end()) {
                bank = registered->second;
            }
            else {
                bank = wrapper->RegisterBank(name.c_str());
                if (bank == nullptr) {
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("The FMOD wrapper would not register '{}'", name);
                    continue;
                }
                _registeredBanks[name] = bank;
            }
            bank->LoadMetadataAsync(false, true);
            mounted.eventBanks.push_back(bank);
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("'{}': {} FMOD bank(s) loading, {} loaded directly", mounted.resource, mounted.eventBanks.size(), mounted.rawBanks.size());
    }

    void StreamedAssetService::ReportLinkedBanks(MountedPak &mounted) {
        for (SDK::wh::FSW_BANK *bank : mounted.eventBanks) {
            if (bank->GetState() != SDK::wh::FSW_BANK::EState::Linked) {
                return;
            }
        }
        mounted.banksReported = true;

        // Read back from FMOD itself: a path only resolves when the resource's strings bank loaded,
        // and an event listed here is one an ATL trigger naming it plays.
        SDK::Fmod::Api &fmod = SDK::Fmod::Api::Get();
        for (SDK::wh::FSW_BANK *registered : mounted.eventBanks) {
            SDK::Fmod::FMOD_STUDIO_BANK *bank = registered->GetBank();
            char path[256]                    = {};
            fmod.Studio_Bank_GetPath(bank, path, sizeof(path), nullptr);
            int events = 0;
            fmod.Studio_Bank_GetEventCount(bank, &events);
            std::vector<SDK::Fmod::FMOD_STUDIO_EVENTDESCRIPTION *> descriptions(static_cast<std::size_t>(events));
            fmod.Studio_Bank_GetEventList(bank, descriptions.data(), events, &events);
            std::string names;
            for (int i = 0; i < events; ++i) {
                char eventPath[256] = {};
                fmod.Studio_EventDescription_GetPath(descriptions[static_cast<std::size_t>(i)], eventPath, sizeof(eventPath), nullptr);
                names += (names.empty() ? "" : ", ") + std::string(eventPath);
            }
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("'{}': FMOD bank '{}' is linked with {} event(s): {}", mounted.resource, path, events, names);
        }
    }

    void StreamedAssetService::ReleaseBanks(MountedPak &mounted) {
        for (SDK::wh::FSW_BANK *bank : mounted.eventBanks) {
            bank->RequestMetadataUnload();
        }
        mounted.eventBanks.clear();
        if (!mounted.rawBanks.empty() && SDK::Fmod::Api::Get().Resolve()) {
            for (void *bank : mounted.rawBanks) {
                SDK::Fmod::Api::Get().Studio_Bank_Unload(static_cast<SDK::Fmod::FMOD_STUDIO_BANK *>(bank));
            }
        }
        mounted.rawBanks.clear();
    }

    void StreamedAssetService::ParseAudioControls(const MountedPak &mounted) {
        SDK::IAudioSystem *audio = SDK::IAudioSystem::GetInstance();
        if (audio == nullptr) {
            return;
        }
        for (const Framework::Utils::StreamedAssets::PakEntry &entry : mounted.entries) {
            if (!KCDC::Shared::StreamedAssets::Policy::IsAudioControls(entry.path)) {
                continue;
            }
            const std::string folder = entry.path.substr(0, entry.path.rfind('/'));
            if (!_parsedControls.insert(folder).second) {
                continue;
            }
            // Scope 2 is the level's: UnLoadLevel clears it, like the level folder's own controls.
            // Blocking, so a trigger named in them resolves the moment this returns.
            SDK::SAudioParseControlsRequest data(folder.c_str(), SDK::EATLDataScope::LevelSpecific);
            SDK::SAudioRequest request;
            request.flags = SDK::SAudioRequest::kHighPriorityBlocking;
            request.data  = &data;
            audio->PushRequest(request);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Parsed the audio controls of '{}'", mounted.resource);
        }
    }

    void StreamedAssetService::ReloadChanged(const MountedPak &previous, const MountedPak &current) {
        std::unordered_map<std::string, std::uint32_t> before;
        for (const Framework::Utils::StreamedAssets::PakEntry &entry : previous.entries) {
            before[entry.path] = entry.crc32;
        }
        std::size_t reloaded = 0;
        for (const Framework::Utils::StreamedAssets::PakEntry &entry : current.entries) {
            const auto old = before.find(entry.path);
            if (old != before.end() && old->second == entry.crc32) {
                continue;
            }
            if (KCDC::Shared::StreamedAssets::Policy::IsTextureBase(entry.path)) {
                SDK::CTexture::ReloadFile(entry.path.c_str());
                ++reloaded;
            }
            else if (KCDC::Shared::StreamedAssets::Policy::IsStaticMesh(entry.path)) {
                SDK::IRenderer::GetInstance()->ReloadFile(entry.path.c_str());
                ++reloaded;
            }
            else if (KCDC::Shared::StreamedAssets::Policy::IsMaterial(entry.path)) {
                const std::string text = ReadEntry(current.path, entry.path);
                SDK::XmlNodeRef node   = SDK::SSystemGlobalEnvironment::Get().pSystem->LoadXmlFromBuffer(text.data(), text.size(), false, false);
                if (node.IsValid()) {
                    SDK::I3DEngine::GetInstance()->GetMaterialManager()->LoadMaterialFromXml(StemOf(entry.path, ".mtl").c_str(), node);
                    ++reloaded;
                }
            }
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("'{}' {} changed: {} resident asset(s) reloaded in place", current.resource, current.lane, reloaded);
    }

    void StreamedAssetService::RebuildClipDatabase() {
        if (_clips == _databaseClips) {
            return;
        }
        // The human database is loaded once, by the front end, before any server is known, and no
        // level load reads it again. Reloading it runs the file back through
        // Human::ExtendMannequinFile, which writes the clips of the paks in use in as options.
        SDK::IAnimationDatabaseManager &manager = SDK::IGameFramework::GetInstance()->GetMannequinInterface().GetAnimationDatabaseManager();
        const SDK::IAnimationDatabase *database = manager.Load(kDatabasePath);
        if (database == nullptr) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("The human animation database is not loaded; streamed clips cannot play");
            return;
        }
        static_cast<SDK::CAnimationDatabaseManager &>(manager).ReloadDatabase(database);
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Rebuilt the human animation database with {} streamed clip(s)", _databaseClips.size());
    }

    void StreamedAssetService::SyncModData(bool levelEdge) {
        // What the answering paks patch, keyed by pak and entry: a resource's changed pak is a new
        // file, so a changed patch reads as a new one.
        std::set<std::string> tables;
        // String patches by the file they patch: the language in use, else the fallback. And every
        // pak that carries each one: two resources may ship the same patch, CryPak decides which
        // answers, and a change to any of them has to read as a change.
        std::map<std::string, std::pair<std::string, std::string>> strings;
        std::map<std::string, std::set<std::string>> providers;
        SDK::ILocalizationManager *localization = SDK::ILocalizationManager::GetInstance();
        const std::string language              = localization != nullptr && localization->GetLanguage() != nullptr ? localization->GetLanguage() : "";
        for (const auto &[path, mounted] : _mounted) {
            for (const Framework::Utils::StreamedAssets::PakEntry &entry : mounted.entries) {
                // A table patch counts while its pak is mounted at all: the loader finds patches
                // through CryPak's listing, which lists any mounted pak's files, so a retired pak
                // ClosePack refused keeps its id enabled -- and is kept readable (Retire) -- until it
                // closes. Read as plain rows, or not at all, it would fail the whole table.
                if (mounted.lane == KCDC::Shared::StreamedAssets::kLaneReplace && KCDC::Shared::StreamedAssets::Policy::IsTablePatch(entry.path)) {
                    tables.insert(path + "|" + entry.path);
                    continue;
                }
                if (!mounted.accessible || mounted.lane != KCDC::Shared::StreamedAssets::kLaneLocalization) {
                    continue;
                }
                const std::string patchLanguage(KCDC::Shared::StreamedAssets::Policy::LanguageOf(entry.path));
                if (patchLanguage != language && patchLanguage != kFallbackLanguage) {
                    continue;
                }
                const std::string file      = entry.path.substr(patchLanguage.size() + 1);
                const std::string patchPath = std::string(kLocalizationRoot) + "/" + entry.path;
                providers[patchPath].insert(path);
                const auto chosen = strings.find(file);
                if (chosen == strings.end() || (patchLanguage == language && chosen->second.first != language)) {
                    strings[file] = {patchLanguage, patchPath};
                }
            }
        }

        // Tables are read once at boot, before any server is known, and nothing reads them again:
        // the game's own reload reads every table back through CryPak, which now resolves the
        // patches in use and no longer the retired ones. Only while no level is up -- at the menu,
        // or at the level edge between the old level's unload and the new one's load: every item,
        // soul and actor of a level holds pointers into the databases' rows, and a reload frees the
        // rows they point at.
        levelEdge = levelEdge || !_levelLoaded;
        if (tables != _appliedTablePatches && !levelEdge) {
            if (!_tablesPending) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Table patches changed; they apply at the next level load");
            }
            _tablesPending = true;
        }
        if (tables != _appliedTablePatches && levelEdge) {
            // The loader merges `<table>__<id>.xml` row by row only for an enabled mod's id, and
            // takes any other part as rows of its own; the server's mods are reported as enabled.
            // Every database is read again: the specialised ones (items, souls, shops, AI) build
            // their rows from several tables, so a patch reaches them only through their own load.
            std::set<std::string> ids;
            for (const std::string &table : tables) {
                const std::string file   = table.substr(table.rfind('/') + 1);
                const std::size_t marker = file.rfind("__");
                ids.insert(file.substr(marker + 2, file.size() - marker - 2 - 4));
            }
            KCDC::Core::Boot::LocalMods::Get().SetServerModIds(ids);
            // Only the databases holding rows: the level's own (weather profiles, cat waypoints, dog
            // points) are empty between levels and load with the next one. Never the AI scheduler's,
            // which cannot be read twice.
            SDK::wh::databasemodule::C_ObjectDatabaseManager *manager = SDK::wh::databasemodule::C_ObjectDatabaseManager::GetInstance();
            std::vector<SDK::wh::databasemodule::C_ObjectDatabaseBase *> selected;
            for (SDK::wh::databasemodule::C_ObjectDatabaseBase *database : manager->GetDatabases()) {
                if (database->IsLoaded() && !SDK::wh::xgenaimodule::scheduler::C_SchedulerDatabase::IsInstance(database)) {
                    selected.push_back(database);
                }
            }
            const SDK::wh::databasemodule::ObjectDatabaseList list {selected.data(), selected.data() + selected.size(), selected.data() + selected.size()};
            const bool loaded = manager->ReloadDatabases(list, true, false);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Reloaded {} database(s) for {} table patch(es){}", selected.size(), tables.size(), loaded ? "" : "; a database failed to load");
            // A reload reads each database from its tables alone, so the rows the mod appended at
            // runtime are gone with the old ones.
            KCDC::Features::Items::CustomItemModule::Get().OnItemDatabaseReloaded();
            KCDC::Features::Crafting::CustomRecipeModule::Get().OnRecipeTablesReloaded();
            _appliedTablePatches = std::move(tables);
            _tablesPending       = false;
        }
        else if (levelEdge && KCDC::Features::Crafting::CustomRecipeModule::Get().WantsReload()) {
            // The session before appended recipes of its own, which would hold their ids in this one:
            // the two recipe tables alone are read again, at the same edge and for the same reason.
            SDK::wh::databasemodule::C_ObjectDatabaseBase *recipes[] = {SDK::wh::playermodule::C_AlchemyRecipeDatabase::GetInstance(), SDK::wh::playermodule::C_BlacksmithRecipeDatabase::GetInstance()};
            const SDK::wh::databasemodule::ObjectDatabaseList list {recipes, recipes + 2, recipes + 2};
            const bool loaded = SDK::wh::databasemodule::C_ObjectDatabaseManager::GetInstance()->ReloadDatabases(list, true, false);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Reloaded the recipe tables the last session appended to{}", loaded ? "" : "; a table failed to load");
            KCDC::Features::Crafting::CustomRecipeModule::Get().OnRecipeTablesReloaded();
        }
        SyncModItems();

        // Each is the paks that carry it, one per line, then `|<CryPak path>`.
        std::set<std::string> patches;
        for (const auto &[file, chosen] : strings) {
            std::string key;
            for (const std::string &pak : providers[chosen.second]) {
                key += pak + '\n';
            }
            patches.insert(key + "|" + chosen.second);
        }
        if (localization == nullptr || patches == _appliedStringPatches) {
            return;
        }
        // A merge cannot be taken back, so every stock file a retired patch touched is read again
        // first -- a patch whose paks changed counts as retired -- and the patches in use then merge
        // over the stock strings.
        const auto cryPakPath = [](const std::string &patch) { return patch.substr(patch.rfind('|') + 1); };
        std::set<std::string> restored;
        for (const std::string &applied : _appliedStringPatches) {
            if (patches.count(applied) == 0) {
                restored.insert(std::string(kLocalizationRoot) + "/" + KCDC::Shared::StreamedAssets::Policy::PatchedFileOf(cryPakPath(applied)));
            }
        }
        for (const std::string &file : restored) {
            localization->ReloadFile(file.c_str());
        }
        std::size_t merged = 0;
        for (const std::string &patch : patches) {
            merged += localization->LoadPatch(cryPakPath(patch).c_str()) ? 1 : 0;
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("String patches in use: {} of {} merged ({}), {} stock file(s) restored", merged, patches.size(), language, restored.size());
        _appliedStringPatches = std::move(patches);
    }

    void StreamedAssetService::ReadItemTables(MountedPak &mounted) {
        if (mounted.lane != KCDC::Shared::StreamedAssets::kLaneReplace || !mounted.itemClasses.empty()) {
            return;
        }
        // The mod's own clothing components first: an item's repairability follows its component's armor type.
        KCDC::Shared::Game::ModClothingTypes clothing;
        for (const Framework::Utils::StreamedAssets::PakEntry &entry : mounted.entries) {
            if (!KCDC::Shared::StreamedAssets::Policy::IsTablePatch(entry.path) || entry.path.rfind("libs/tables/character/charactercomponent", 0) != 0) {
                continue;
            }
            if (entry.size > KCDC::Shared::Game::kMaximumModTableBytes) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("'{}' {}: {} is too large to read ({} bytes)", mounted.resource, mounted.lane, entry.path, entry.size);
                continue;
            }
            std::string reason;
            if (!KCDC::Shared::Game::ParseModClothingTypes(ReadEntry(mounted.path, entry.path), clothing, reason)) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("'{}' {}: {} is not a character component table ({})", mounted.resource, mounted.lane, entry.path, reason);
            }
        }
        for (const Framework::Utils::StreamedAssets::PakEntry &entry : mounted.entries) {
            if (!KCDC::Shared::StreamedAssets::Policy::IsTablePatch(entry.path) || entry.path.rfind("libs/tables/item/item", 0) != 0) {
                continue;
            }
            if (entry.size > KCDC::Shared::Game::kMaximumModTableBytes) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("'{}' {}: {} is too large to read ({} bytes)", mounted.resource, mounted.lane, entry.path, entry.size);
                continue;
            }
            const std::string text = ReadEntry(mounted.path, entry.path);
            std::string reason;
            if (!KCDC::Shared::Game::ParseModItemTable(text, clothing, mounted.itemClasses, reason)) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("'{}' {}: {} is not an item table ({})", mounted.resource, mounted.lane, entry.path, reason);
            }
        }
        if (!mounted.itemClasses.empty()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("'{}' {}: {} item class(es) read from its table patches", mounted.resource, mounted.lane, mounted.itemClasses.size());
        }
    }

    void StreamedAssetService::SyncModItems() {
        // The classes stay in the catalog while the pak is mounted at all, enabled or not: that is
        // how long its table patches stay in the game's own databases. Keyed by resource first, as
        // the server keys them, so two resources defining one GUID settle it the same way on both:
        // a space sorts below every character a resource name may hold, so `mod` stays ahead of
        // `mod-extra` here as it is there.
        std::set<std::string> live;
        for (const auto &[path, mounted] : _mounted) {
            if (mounted.itemClasses.empty()) {
                continue;
            }
            const std::string source = mounted.resource + " " + path;
            live.insert(source);
            if (_modItemSources.count(source) == 0) {
                KCDC::Shared::Game::ModItemRegistry::Get().Replace(source, mounted.itemClasses);
            }
        }
        for (const std::string &source : _modItemSources) {
            if (live.count(source) == 0) {
                KCDC::Shared::Game::ModItemRegistry::Get().Remove(source);
            }
        }
        if (live != _modItemSources) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Item catalog: {} class(es) from {} streamed pak(s)", KCDC::Shared::Game::ModItemRegistry::Get().Size(), live.size());
        }
        _modItemSources = std::move(live);
    }

    void StreamedAssetService::Retire() {
        SDK::ICryPak *cryPak = SDK::ICryPak::GetInstance();
        for (auto entry = _mounted.begin(); entry != _mounted.end();) {
            const std::string &path = entry->first;
            MountedPak &mounted     = entry->second;
            if (_current.count(path) != 0) {
                ++entry;
                continue;
            }
            // A pak carrying table patches is closed rather than disabled: CryPak's file listing,
            // which is how the table loader finds a table's patches, still lists a disabled pak's
            // files, and its lookup cannot read them. The loader reads every part and patch it
            // lists and fails the whole table on one it cannot read -- "Database system error",
            // 0x180D18490 -- so a pak ClosePack refuses is kept readable instead, its patches in
            // use and its id enabled until it closes. It is tried again at every level edge.
            const bool tablePatches = std::any_of(mounted.entries.begin(), mounted.entries.end(), [](const Framework::Utils::StreamedAssets::PakEntry &file) {
                return KCDC::Shared::StreamedAssets::Policy::IsTablePatch(file.path);
            });
            if (tablePatches) {
                ReleaseBanks(mounted);
                if (cryPak->ClosePack(path.c_str(), kMountFlags)) {
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Retired '{}' {} (closed)", mounted.resource, mounted.lane);
                    entry = _mounted.erase(entry);
                    continue;
                }
                if (!mounted.accessible) {
                    mounted.accessible = cryPak->SetPackAccessible(true, path.c_str(), kMountFlags);
                }
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Retired '{}' {} is still held open; its table patches stay in use until it closes{}", mounted.resource, mounted.lane, mounted.accessible ? "" : ", and it could not be made readable again");
                ++entry;
                continue;
            }
            else if (!mounted.accessible) {
                ++entry;
                continue;
            }
            // Disabled, not closed: ClosePack refuses while a resident texture still holds the pak
            // and spins for up to two seconds under the pak list's lock before it says so.
            mounted.accessible = !cryPak->SetPackAccessible(false, path.c_str(), kMountFlags);
            for (const Framework::Utils::StreamedAssets::PakEntry &entry : mounted.entries) {
                if (KCDC::Shared::StreamedAssets::Policy::IsTextureBase(entry.path)) {
                    SDK::CTexture::ReloadFile(entry.path.c_str());
                }
            }
            ReleaseBanks(mounted);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Retired '{}' {} ({})", mounted.resource, mounted.lane, mounted.accessible ? "still enabled" : "disabled");
            ++entry;
        }
        SyncModData(true);
        SyncModItems();
    }

    bool StreamedAssetService::HasEntry(std::string_view path) const {
        for (const std::string &current : _current) {
            const auto mounted = _mounted.find(current);
            if (mounted == _mounted.end() || mounted->second.lane != KCDC::Shared::StreamedAssets::kLaneStream) {
                continue;
            }
            const std::vector<Framework::Utils::StreamedAssets::PakEntry> &entries = mounted->second.entries;
            const auto found = std::lower_bound(entries.begin(), entries.end(), path, [](const Framework::Utils::StreamedAssets::PakEntry &entry, std::string_view value) { return entry.path < value; });
            if (found != entries.end() && found->path == path) {
                return true;
            }
        }
        return false;
    }

    bool StreamedAssetService::PrepareClip(SDK::ICharacterInstance *character, const std::string &clip, int &option) {
        std::string path;
        for (const char c : clip) {
            path.push_back(c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (!KCDC::Shared::StreamedAssets::Policy::IsAnimationClip(path)) {
            path += ".caf";
        }
        const auto found = std::find(_databaseClips.begin(), _databaseClips.end(), path);
        if (found == _databaseClips.end()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("'{}' is not a clip this level's animation database was built with", path);
            return false;
        }
        option = static_cast<int>(found - _databaseClips.begin());

        auto *manager = static_cast<SDK::CharacterManager *>(SDK::SSystemGlobalEnvironment::Get().pCharacterManager);
        if (manager == nullptr || character == nullptr) {
            return false;
        }
        SDK::IAnimationSet *clips = character->GetIAnimationSet();
        const std::string name    = StemOf(path, ".caf");
        const std::string key     = std::to_string(reinterpret_cast<std::uintptr_t>(clips)) + "|" + path;
        if (_preparedClips.count(key) == 0 && clips->GetAnimIDByName(name.c_str()) < 0) {
            // Created only while a level is up: the facial pak, whose reload would truncate the table
            // behind our header, is never reloaded while one is (CSystem::SelectAndOpenLanguageFacialPak).
            manager->SyncAllAnimations();
            SDK::wh::shared::C_ScopedRecursiveSRWLock lock(&manager->GetAnimationLock());
            SDK::ScopedCryString cryPath(path.c_str());
            if (manager->GetAnimationManager().CreateGlobalHeaderCAF(cryPath.Get()) < 0) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("The animation system would not take '{}'", path);
                return false;
            }
            // CAnimationSet is the only implementation of IAnimationSet the game has.
            static_cast<SDK::CAnimationSet *>(clips)->LoadFileCAF(SDK::CityHash64Lowercase(path.c_str()), SDK::CityHash64Lowercase(name.c_str()), path.c_str(), name.c_str());
        }
        _preparedClips.insert(key);
        return clips->GetAnimIDByName(name.c_str()) >= 0;
    }

    void StreamedAssetService::ExtendMannequinFile(const char *file, SDK::XmlNodeRef &document) {
        if (!document.IsValid()) {
            return;
        }
        if (EndsWithFile(file, kFragmentIdsFile)) {
            SDK::XmlNodeRef tags = document->findChild("Tags");
            if (tags.IsValid()) {
                tags->newChild("Tag")->setAttr("name", KCDC::Shared::StreamedAssets::kStreamedClipFragment);
            }
            return;
        }
        if (EndsWithFile(file, kControllerFile)) {
            SDK::XmlNodeRef definitions = document->findChild("FragmentDefs");
            if (definitions.IsValid()) {
                // The scope set the game's own animation-driven one-shots use.
                definitions->newChild(KCDC::Shared::StreamedAssets::kStreamedClipFragment)->setAttr("scopes", "FullBody+HoldItem+Looking");
            }
            return;
        }
        if (!EndsWithFile(file, kDatabaseFile)) {
            return;
        }

        // Its options are the clips mounted when it is read: at boot none, then whatever each
        // set of paks carries, as RebuildClipDatabase reads it again.
        _databaseClips = _clips;
        if (_clips.empty()) {
            return;
        }
        SDK::XmlNodeRef list = document->findChild("FragmentList");
        if (!list.IsValid()) {
            return;
        }
        SDK::XmlNodeRef fragment = list->newChild(KCDC::Shared::StreamedAssets::kStreamedClipFragment);
        for (const std::string &clip : _clips) {
            // Shaped like the game's own `AnimationControlled` entries: the clip, and
            // MovementControlMethod letting the animation drive the body's root.
            SDK::XmlNodeRef entry = fragment->newChild("Fragment");
            entry->setAttr("BlendOutDuration", "0.2");
            entry->setAttr("Tags", "");
            SDK::XmlNodeRef animation = entry->newChild("AnimLayer");
            AddBlend(animation, "0.2");
            animation->newChild("Animation")->setAttr("name", StemOf(clip, ".caf").c_str());

            SDK::XmlNodeRef procedural = entry->newChild("ProcLayer");
            AddBlend(procedural, "0.2");
            SDK::XmlNodeRef method = procedural->newChild("Procedural");
            method->setAttr("type", "MovementControlMethod");
            SDK::XmlNodeRef parameters = method->newChild("ProceduralParams");
            parameters->newChild("Horizontal")->setAttr("value", "2");
            for (const char *name : {"Vertical", "XyMove", "ZMove", "Rotate", "Velocity", "Inertia"}) {
                parameters->newChild(name)->setAttr("value", "0");
            }
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("{}: {} streamed clip(s) joined as {} options", file, _clips.size(), KCDC::Shared::StreamedAssets::kStreamedClipFragment);
    }

    void StreamedAssetService::Fail(const std::string &error) {
        _error = error;
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("{}", error);
    }

    void StreamedAssetService::OnSystemEventAnyThread(SDK::ESystemEvent, std::uint64_t, std::uint64_t) {}

    void StreamedAssetService::OnSystemEvent(SDK::ESystemEvent event, std::uint64_t, std::uint64_t) {
        if (event == SDK::ESYSTEM_EVENT_LEVEL_POST_UNLOAD) {
            Retire();
            // The level's sets and its scope-2 controls are gone with it.
            _levelLoaded = false;
            _preparedClips.clear();
            _parsedControls.clear();
        }
        else if (event == SDK::ESYSTEM_EVENT_LEVEL_LOAD_END) {
            _levelLoaded = true;
            for (const std::string &path : _current) {
                if (const auto mounted = _mounted.find(path); mounted != _mounted.end() && mounted->second.accessible) {
                    ParseAudioControls(mounted->second);
                }
            }
        }
    }
} // namespace KCDC::Core::StreamedAssets
