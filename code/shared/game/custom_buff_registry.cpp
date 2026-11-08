#include "custom_buff_registry.h"
#include "custom_buff_modifiers.h"
#include "progression_catalog.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <mutex>
#include <stdexcept>
#include <utils/crypto.h>

namespace KCDC::Shared::Game {
    namespace {
        std::string GuidFor(const std::string &seed) {
            const auto hash = Framework::Utils::Crypto::Sha256Hex(seed);
            if (hash.size() != 64) {
                throw std::runtime_error("Cannot generate buff identity");
            }
            return hash.substr(0, 8) + "-" + hash.substr(8, 4) + "-" + hash.substr(12, 4) + "-" + hash.substr(16, 4) + "-" + hash.substr(20, 12);
        }
        bool Text(const nlohmann::json &value, std::size_t maximum) {
            if (!value.is_string()) {
                return false;
            }
            const auto &text = value.get_ref<const std::string &>();
            return text.size() <= maximum && std::none_of(text.begin(), text.end(), [](unsigned char c) {
                return c < 0x20 || c == 0x7f;
            });
        }
        double Number(const nlohmann::json &value, double minimum, double maximum) {
            if (!value.is_number()) {
                throw std::invalid_argument("Expected a numeric buff value");
            }
            const double number = value.get<double>();
            if (!std::isfinite(number) || number < minimum || number > maximum) {
                throw std::invalid_argument("Buff value is outside the supported range");
            }
            return number;
        }
        std::string NativeModifier(std::string_view name) {
            for (const auto &[alias, native] : g_CustomBuffModifiers) {
                if (alias == name) {
                    return std::string(native);
                }
            }
            for (const auto &track : g_ProgressionTracks) {
                if (track.name == name) {
                    return std::string(track.name);
                }
            }
            throw std::invalid_argument("Unsupported buff modifier: " + std::string(name));
        }
        std::string Parameters(const nlohmann::json &modifiers) {
            if (!modifiers.is_object() || modifiers.size() > 32) {
                throw std::invalid_argument("Buff modifiers must be an object with at most 32 entries");
            }
            std::string result;
            std::vector<std::string> used;
            for (const auto &[key, input] : modifiers.items()) {
                const std::string native = NativeModifier(key);
                if (std::find(used.begin(), used.end(), native) != used.end()) {
                    throw std::invalid_argument("Duplicate buff modifier alias");
                }
                used.push_back(native);
                std::string operation = "add";
                double value          = 0;
                if (input.is_number()) {
                    value = Number(input, -10000, 10000);
                }
                else {
                    if (!input.is_object() || input.size() != 2 || !input.contains("operation") || !input.at("operation").is_string() || !input.contains("value")) {
                        throw std::invalid_argument("A buff modifier requires operation and value");
                    }
                    operation = input.at("operation").get<std::string>();
                    value     = Number(input.at("value"), operation == "add" ? -10000 : 0, operation == "multiply" ? 100 : 10000);
                }
                if (operation != "add" && operation != "multiply" && operation != "set") {
                    throw std::invalid_argument("Buff modifier operation must be add, multiply or set");
                }
                if (key == "healthRegeneration" && operation != "add") {
                    throw std::invalid_argument("Health regeneration supports additive points per second");
                }
                if (key == "maxHealth") {
                    if (operation == "set" && value < 1) {
                        throw std::invalid_argument("Maximum health must be at least 1");
                    }
                    continue;
                }
                if (!result.empty()) {
                    result += ',';
                }
                result += native;
                result += operation == "multiply" ? '*' : operation == "set" ? '=' : value < 0 ? '-' : '+';
                result += std::format("{:.9g}", operation == "add" ? std::abs(value) : value);
                if (key == "healthRegeneration") {
                    result += "/s";
                }
            }
            return result;
        }
    } // namespace

    CustomBuffRegistry &CustomBuffRegistry::Get() {
        static CustomBuffRegistry registry;
        return registry;
    }

    const CustomBuffDefinition &CustomBuffRegistry::Register(const std::string &id, const nlohmann::json &definition) {
        std::unique_lock lock(_mutex);
        if (id.empty() || id.size() > 128 || !std::all_of(id.begin(), id.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ':' || c == '.';
            })) {
            throw std::invalid_argument("Invalid custom buff id");
        }
        for (const auto &buff : g_Buffs) {
            if (buff.name == id || buff.guid == id) {
                throw std::invalid_argument("Custom buff id collides with a native buff");
            }
        }
        if (_definitions.contains(id)) {
            throw std::invalid_argument("Custom buff id collides with a registered GUID");
        }
        if (!definition.is_object() || !definition.contains("name") || !Text(definition.at("name"), 256) || definition.at("name").get_ref<const std::string &>().empty() || !definition.contains("visual") || !definition.at("visual").is_string()) {
            throw std::invalid_argument("Custom buffs require name and visual");
        }
        for (const auto &[key, value] : definition.items()) {
            if (key != "name" && key != "description" && key != "visual" && key != "duration" && key != "tickInterval" && key != "stackable" && key != "refresh" && key != "maxStacks" && key != "modifiers") {
                throw std::invalid_argument("Unknown custom buff property: " + key);
            }
        }
        if (definition.contains("description") && !Text(definition.at("description"), 4096)) {
            throw std::invalid_argument("Invalid buff description");
        }
        for (const auto *key : {"stackable", "refresh"}) {
            if (definition.contains(key) && !definition.at(key).is_boolean()) {
                throw std::invalid_argument("Invalid buff flag");
            }
        }
        const auto visual = definition.at("visual").get<std::string>();
        const auto found  = std::find_if(g_Buffs.begin(), g_Buffs.end(), [&visual](const BuffEntry &entry) {
            return entry.name == visual || entry.guid == visual;
        });
        if (found == g_Buffs.end()) {
            throw std::invalid_argument("Custom buff visual must name an existing game buff");
        }
        const double duration = definition.contains("duration") ? Number(definition.at("duration"), -1, 604800) : -1;
        if (duration != -1 && (duration <= 0 || static_cast<float>(duration) == 0)) {
            throw std::invalid_argument("Buff duration must be positive representable seconds or -1 for permanent");
        }
        const double tick = definition.contains("tickInterval") ? Number(definition.at("tickInterval"), 0, 86400) : 0;
        if (tick > 0 && tick < 0.1) {
            throw std::invalid_argument("Buff tickInterval must be zero or at least 0.1 seconds");
        }
        const bool stackable = definition.value("stackable", false);
        const double stacks  = definition.contains("maxStacks") ? Number(definition.at("maxStacks"), 1, 16) : 1;
        if (stacks != std::floor(stacks) || (!stackable && stacks != 1)) {
            throw std::invalid_argument("Invalid buff maxStacks");
        }
        const auto modifiers  = definition.value("modifiers", nlohmann::json::object());
        const auto parameters = Parameters(modifiers);
        const auto guid       = GuidFor("KCDC/custom-buff/" + id);
        for (const auto &buff : g_Buffs) {
            if (buff.guid == guid) {
                throw std::invalid_argument("Custom buff GUID collides with a native buff");
            }
        }
        for (const auto &[registeredGuid, buff] : _definitions) {
            if (buff.id == guid) {
                throw std::invalid_argument("Custom buff GUID collides with a registered id");
            }
        }
        const nlohmann::json normalized = {{"name", definition.at("name")}, {"description", definition.value("description", std::string())}, {"visual", found->guid}, {"duration", duration}, {"tickInterval", tick}, {"stackable", stackable},
            {"refresh", definition.value("refresh", true)}, {"maxStacks", static_cast<std::uint32_t>(stacks)}, {"modifiers", modifiers}};
        if (const auto match = _definitions.find(guid); match != _definitions.end()) {
            if (match->second.id != id || match->second.definition != normalized) {
                throw std::invalid_argument("Custom buff id is already registered with another definition");
            }
            return match->second;
        }
        if (_definitions.size() >= 512) {
            throw std::invalid_argument("Custom buff registry capacity exceeded");
        }
        const auto nativeGuid = GuidFor("KCDC/custom-buff/native/" + id + '/' + normalized.dump());
        auto &buff            = _definitions[guid];
        buff.id               = id;
        buff.guid             = guid;
        buff.nativeGuid       = nativeGuid;
        buff.name             = normalized.at("name").get<std::string>();
        buff.description      = normalized.at("description").get<std::string>();
        buff.visual           = std::string(found->guid);
        buff.parameters       = parameters;
        buff.tickInterval     = tick;
        _hasTicking           = _hasTicking || tick > 0;
        buff.stackable        = stackable;
        buff.refresh          = normalized.at("refresh");
        buff.maxStacks        = static_cast<std::uint32_t>(stacks);
        buff.definition       = normalized;
        if (modifiers.contains("maxHealth")) {
            const auto &input           = modifiers.at("maxHealth");
            const std::string operation = input.is_number() ? "add" : input.at("operation").get<std::string>();
            buff.maximumHealth          = CustomBuffMaximum {operation == "multiply" ? CustomBuffMaximum::Operation::Multiply
                                                             : operation == "set"    ? CustomBuffMaximum::Operation::Set
                                                                                     : CustomBuffMaximum::Operation::Add,
                input.is_number() ? input.get<float>() : input.at("value").get<float>()};
        }
        CryGuidFromText(guid, buff.entry.id.low, buff.entry.id.high);
        buff.entry.name        = buff.id;
        buff.entry.guid        = buff.guid;
        buff.entry.uiName      = buff.name;
        buff.entry.classId     = BuffClass::kPotion;
        buff.entry.aiTag       = -1;
        buff.entry.family      = -1;
        buff.entry.exclusivity = stackable ? 0 : 1;
        buff.entry.uiType      = 1;
        buff.entry.duration    = static_cast<float>(duration);
        buff.rule              = {buff.guid, "Cpp:BasicTimed", buff.parameters, true};
        return buff;
    }

    const CustomBuffDefinition *CustomBuffRegistry::Find(std::string_view query) const {
        std::shared_lock lock(_mutex);
        if (const auto match = _definitions.find(query); match != _definitions.end()) {
            return &match->second;
        }
        for (const auto &[guid, buff] : _definitions) {
            if (buff.id == query) {
                return &buff;
            }
        }
        return nullptr;
    }
    bool CustomBuffRegistry::HasTicking() const {
        std::shared_lock lock(_mutex);
        return _hasTicking;
    }
    std::vector<std::string> CustomBuffRegistry::ModifierNames() {
        std::vector<std::string> names;
        for (const auto &[alias, native] : g_CustomBuffModifiers) {
            names.emplace_back(alias);
        }
        for (const auto &track : g_ProgressionTracks) {
            names.emplace_back(track.name);
        }
        return names;
    }
    std::vector<std::pair<std::string, nlohmann::json>> CustomBuffRegistry::Snapshot() const {
        std::shared_lock lock(_mutex);
        std::vector<std::pair<std::string, nlohmann::json>> definitions;
        for (const auto &[guid, buff] : _definitions) {
            definitions.emplace_back(buff.id, buff.definition);
        }
        return definitions;
    }
    void CustomBuffRegistry::Reset() {
        std::unique_lock lock(_mutex);
        _definitions.clear();
        _hasTicking = false;
    }
    float ApplyCustomHealthMaximum(float base, std::span<const CustomBuffMaximum> modifiers) {
        float value = base;
        // Additions and original-base scales precede assignments. Each stack
        // contributes independently to this mod-owned maximum.
        for (const auto operation : {CustomBuffMaximum::Operation::Add, CustomBuffMaximum::Operation::Multiply, CustomBuffMaximum::Operation::Set}) {
            for (const auto &modifier : modifiers) {
                if (modifier.operation != operation) {
                    continue;
                }
                if (operation == CustomBuffMaximum::Operation::Add) {
                    value += modifier.value;
                }
                else if (operation == CustomBuffMaximum::Operation::Multiply) {
                    value += base * (modifier.value - 1);
                }
                else {
                    value = modifier.value;
                }
            }
        }
        return std::clamp(value, 1.0F, 1000000.0F);
    }
    const BuffEntry *FindCustomBuff(std::string_view query) {
        const auto *buff = CustomBuffRegistry::Get().Find(query);
        return buff != nullptr ? &buff->entry : nullptr;
    }
    const BuffEntry *FindCustomBuffById(const RPC::BuffId &id) {
        return FindCustomBuff(CryGuidToString(id.low, id.high));
    }
    const BuffRuleDefinition *FindCustomBuffRule(std::string_view guid) {
        const auto *buff = CustomBuffRegistry::Get().Find(guid);
        return buff != nullptr ? &buff->rule : nullptr;
    }
} // namespace KCDC::Shared::Game
