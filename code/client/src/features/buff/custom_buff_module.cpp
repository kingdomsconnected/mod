#include "custom_buff_module.h"
#include "core/application.h"
#include "core/obfuscation.h"
#include "common/module_support.h"
#include "sdk/patterns.h"
#include "sdk/wh/rpgmodule/c_buff_manager.h"
#include "shared/features/player/player_custom_buff.h"
#include "shared/game/buff_rule_catalog.h"
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <utils/hooking/hook_function.h>

namespace KCDC::Features::Buff {
    namespace {
        // `potion_antiPlaguePotion_3`, a plain `Cpp:BasicTimed` row: every custom
        // definition starts from its native bookkeeping, then overwrites what it
        // describes. Pinned so the template never moves with the rule table.
        constexpr const char *kPrototypeGuid = "005cd31f-a09a-457a-a21e-16632ca0c1f2";

        std::string LabelKey(const std::string &guid, const char *field) {
            std::string key = "kcdc_bf_" + guid + '_' + field;
            std::erase(key, '-');
            return key;
        }
        SDK::CryGUID ParseGuid(std::string_view text) {
            KCDC::Shared::RPC::BuffId id {};
            if (!KCDC::Shared::Game::CryGuidFromText(text, id.low, id.high)) {
                throw std::invalid_argument("Malformed buff GUID");
            }
            return KCDC::Common::ToCryGuid(id);
        }
        bool IsEmpty(const SDK::CryGUID &guid) {
            return guid.hipart == 0 && guid.lopart == 0;
        }
    } // namespace

    CustomBuffModule &CustomBuffModule::Get() {
        static CustomBuffModule module;
        return module;
    }
    CustomBuffModule::NativeDefinition::NativeDefinition(const KCDC::Shared::Game::CustomBuffDefinition &definition, const SDK::wh::rpgmodule::S_BuffDefinition &prototype, const SDK::wh::rpgmodule::S_BuffDefinition &visual)
        : name(definition.id.c_str())
        , description(LabelKey(definition.nativeGuid, "d").c_str())
        , uiName(LabelKey(definition.nativeGuid, "n").c_str())
        , implementation("Cpp:BasicTimed")
        , factoryClassName("BasicTimed")
        , parameters(definition.parameters.c_str())
        , row(prototype) {
        row.buffId                   = ParseGuid(definition.nativeGuid);
        row.buffName                 = name.c_str();
        row.buffDesc                 = description.c_str();
        row.buffUiName               = uiName.c_str();
        row.slotBuffUiName           = uiName.c_str();
        row.implementation           = implementation.c_str();
        row.factoryClassName         = factoryClassName.c_str();
        row.iconId                   = visual.iconId;
        row.slotIconId               = visual.slotIconId;
        row.visualEffect             = visual.visualEffect;
        row.buffParams               = parameters.c_str();
        row.duration                 = definition.entry.duration;
        row.buffClassId              = definition.entry.classId;
        row.buffFamilyId             = definition.entry.family;
        row.buffAiTagId              = definition.entry.aiTag;
        row.buffExclusivityId        = definition.entry.exclusivity;
        row.buffUiTypeId             = definition.entry.uiType;
        row.buffLifetimeId           = definition.entry.lifetime;
        row.buffUiVisibilityId       = 3;
        row.buffHcModeUiVisibilityId = 3;
        row.isPersistent             = definition.entry.persistent;
    }
    void CustomBuffModule::Install() {
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<KCDC::Shared::RPC::CustomBuffDefinition>([](const KCDC::Shared::RPC::CustomBuffDefinition &message, MafiaNet::Packet *) {
            if (!message.valid) {
                return;
            }
            auto &module = Get();
            std::scoped_lock lock(module._mutex);
            // The server sends a reset and at most 512 definitions per connection.
            if (module._incoming.size() >= 1024) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Effect registration queue is full; dropping a definition");
                return;
            }
            module._incoming.push_back(message.body);
        });
    }
    void CustomBuffModule::Update() {
        std::vector<std::string> incoming;
        {
            std::scoped_lock lock(_mutex);
            incoming.swap(_incoming);
        }
        // One rejected definition costs only itself: its orders resolve to no
        // native row and are skipped, every other definition still installs.
        for (const auto &body : incoming) {
            try {
                Accept(nlohmann::json::parse(body));
            }
            catch (const std::exception &error) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Effect registration failed: {}", error.what());
            }
        }
    }
    void CustomBuffModule::Accept(const nlohmann::json &message) {
        if (message == nlohmann::json {{"reset", true}}) {
            KCDC::Shared::Game::CustomBuffRegistry::Get().Reset();
            std::scoped_lock lock(_mutex);
            _logical.clear();
            return;
        }
        if (!message.is_object() || message.size() != 2 || !message.contains("id") || !message.at("id").is_string() || !message.contains("definition")) {
            throw std::invalid_argument("Invalid buff registry message");
        }
        const auto &definition = KCDC::Shared::Game::CustomBuffRegistry::Get().Register(message.at("id"), message.at("definition"));
        auto *manager          = SDK::wh::rpgmodule::C_BuffManager::GetInstance();
        const auto native      = ParseGuid(definition.nativeGuid);
        if (manager == nullptr) {
            throw std::invalid_argument("Native buff manager is missing");
        }
        if (FindNative(native) == nullptr) {
            const auto &visual = manager->GetDefinitionOrDefault(ParseGuid(definition.visual));
            if (IsEmpty(visual.buffId)) {
                throw std::invalid_argument("Native buff visual is missing");
            }
            if (!IsEmpty(manager->GetDefinitionOrDefault(native).buffId)) {
                throw std::invalid_argument("Native buff GUID collision");
            }
            const auto &prototype = manager->GetDefinitionOrDefault(ParseGuid(kPrototypeGuid));
            if (IsEmpty(prototype.buffId)) {
                throw std::invalid_argument("Native timed effect prototype is missing");
            }
            auto row = std::make_unique<NativeDefinition>(definition, prototype, visual);
            std::scoped_lock lock(_mutex);
            if (_installed.size() >= 2048) {
                throw std::invalid_argument("Native buff registry capacity exceeded; reconnect with a fresh client");
            }
            const auto *nativeRow = &row->row;
            _installed.emplace(definition.nativeGuid, std::move(row));
            _nativeRows[{native.hipart, native.lopart}] = nativeRow;
            if (definition.maximumHealth) {
                _healthModifiers[nativeRow] = *definition.maximumHealth;
                _hasHealthModifiers.store(true, std::memory_order_release);
            }
            _labels[LabelKey(definition.nativeGuid, "n")] = definition.name;
            _labels[LabelKey(definition.nativeGuid, "d")] = definition.description;
        }
        std::scoped_lock lock(_mutex);
        _logical[{native.hipart, native.lopart}] = definition.entry.id;
    }
    void CustomBuffModule::Reset() {
        std::vector<std::uint64_t> handles;
        if (const auto *soul = KCDC::Common::LocalSoul()) {
            std::scoped_lock lock(_mutex);
            for (auto it = soul->ActiveBuffsBegin(); it != soul->ActiveBuffsEnd(); ++it) {
                if (*it != nullptr && std::any_of(_installed.begin(), _installed.end(), [it](const auto &entry) {
                        return &entry.second->row == (*it)->GetDefinition();
                    })) {
                    handles.push_back((*it)->GetHandle());
                }
            }
        }
        if (auto *manager = SDK::wh::rpgmodule::C_BuffManager::GetInstance()) {
            for (const auto handle : handles) {
                manager->RemoveBuff(handle);
            }
        }
        std::scoped_lock lock(_mutex);
        _incoming.clear();
        _logical.clear();
        KCDC::Shared::Game::CustomBuffRegistry::Get().Reset();
        // Rows and native CryString storage survive every instance that
        // borrows them, including teardown after the feature reset sweep.
    }
    SDK::CryGUID CustomBuffModule::ToNative(const KCDC::Shared::RPC::BuffId &id) const {
        const auto *definition = KCDC::Shared::Game::CustomBuffRegistry::Get().Find(KCDC::Shared::Game::CryGuidToString(id.low, id.high));
        if (definition == nullptr) {
            return KCDC::Common::ToCryGuid(id);
        }
        // Registered here but never installed natively: nothing to apply.
        const auto native = ParseGuid(definition->nativeGuid);
        std::scoped_lock lock(_mutex);
        return _logical.contains({native.hipart, native.lopart}) ? native : SDK::CryGUID {};
    }
    KCDC::Shared::RPC::BuffId CustomBuffModule::ToLogical(const SDK::CryGUID &id) const {
        std::scoped_lock lock(_mutex);
        const auto found = _logical.find({id.hipart, id.lopart});
        return found != _logical.end() ? found->second : KCDC::Common::FromCryGuid<KCDC::Shared::RPC::BuffId>(id);
    }
    const SDK::wh::rpgmodule::S_BuffDefinition *CustomBuffModule::FindNative(const SDK::CryGUID &id) const {
        std::scoped_lock lock(_mutex);
        const auto found = _nativeRows.find({id.hipart, id.lopart});
        return found != _nativeRows.end() ? found->second : nullptr;
    }
    const SDK::wh::rpgmodule::S_BuffDefinition *CustomBuffModule::Lookup(void *database, const SDK::CryGUID *id) {
        auto &module = Get();
        if (id != nullptr) {
            if (const auto *definition = module.FindNative(*id)) {
                return definition;
            }
        }
        return module._lookupOriginal(database, id);
    }
    bool CustomBuffModule::ResolveLabel(const char *label, SDK::CryStringT<char> &out) const {
        std::string_view key(label);
        if (key.starts_with('@')) {
            key.remove_prefix(1);
        }
        if (!key.starts_with("kcdc_bf_")) {
            return false;
        }
        std::scoped_lock lock(_mutex);
        const auto found = _labels.find(key);
        if (found == _labels.end()) {
            return false;
        }
        out.Assign(found->second.c_str());
        return true;
    }
    void CustomBuffModule::InstallHooks() {
        // The only lookup over the native definition table: every creation
        // path (`C_BuffManager::CreateAndAttachBuff`) resolves its rows here.
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.rpgmodule__FindBuffDefinition), reinterpret_cast<LPVOID>(&Lookup), reinterpret_cast<LPVOID *>(&Get()._lookupOriginal));
    }
    float CustomBuffModule::HealthMaximum(const SDK::wh::rpgmodule::I_Soul *soul, float base) const {
        // Called for every health maximum query: stay lock-free until a
        // definition actually carries a maximum-health modifier.
        if (!_hasHealthModifiers.load(std::memory_order_acquire) || soul == nullptr || KCDC::Common::LocalSoul() != soul) {
            return base;
        }
        const auto *native = static_cast<const SDK::wh::rpgmodule::C_Soul *>(soul);
        std::array<KCDC::Shared::Game::CustomBuffMaximum, KCDC::Shared::RPC::kMaxReportedBuffs> modifiers;
        std::size_t count = 0;
        std::scoped_lock lock(_mutex);
        for (auto it = native->ActiveBuffsBegin(); it != native->ActiveBuffsEnd() && count < modifiers.size(); ++it) {
            if (*it == nullptr) {
                continue;
            }
            const auto found = _healthModifiers.find((*it)->GetDefinition());
            if (found != _healthModifiers.end()) {
                modifiers[count++] = found->second;
            }
        }
        return KCDC::Shared::Game::ApplyCustomHealthMaximum(base, std::span(modifiers.data(), count));
    }
} // namespace KCDC::Features::Buff

static InitFunction init(
    [] {
        KCDC::Features::Buff::CustomBuffModule::InstallHooks();
    },
    "Effect presentation");
