#pragma once

#include "buff_catalog.h"
#include "buff_rule_catalog.h"
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>

namespace KCDC::Shared::Game {
    struct CustomBuffMaximum {
        enum class Operation : std::uint8_t {
            Add,
            Multiply,
            Set
        };
        Operation operation = Operation::Add;
        float value         = 0;
    };
    float ApplyCustomHealthMaximum(float base, std::span<const CustomBuffMaximum> modifiers);
    struct CustomBuffDefinition {
        std::string id;
        std::string guid;
        std::string nativeGuid;
        std::string name;
        std::string description;
        std::string visual;
        std::string parameters;
        double tickInterval     = 0;
        bool stackable          = false;
        bool refresh            = true;
        std::uint32_t maxStacks = 1;
        BuffEntry entry {};
        nlohmann::json definition;
        std::optional<CustomBuffMaximum> maximumHealth;
        BuffRuleDefinition rule {};
    };

    /** Immutable definitions for one connection/server lifetime. */
    class CustomBuffRegistry final {
      public:
        static CustomBuffRegistry &Get();
        const CustomBuffDefinition &Register(const std::string &id, const nlohmann::json &definition);
        const CustomBuffDefinition *Find(std::string_view query) const;
        /** Whether any definition asks for tick events, so the server can skip its scan. */
        bool HasTicking() const;
        std::vector<std::pair<std::string, nlohmann::json>> Snapshot() const;
        static std::vector<std::string> ModifierNames();
        void Reset();

      private:
        std::map<std::string, CustomBuffDefinition, std::less<>> _definitions;
        mutable std::shared_mutex _mutex;
        bool _hasTicking = false;
    };
} // namespace KCDC::Shared::Game
