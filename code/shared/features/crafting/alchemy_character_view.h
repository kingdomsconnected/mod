#pragma once

#include "alchemy_protocol.h"

#include <array>
#include <bit>
#include <cmath>
#include <map>
#include <optional>

namespace KCDC::Shared::Crafting {
    /**
     * The server's `character` object of an alchemy response, validated.
     *
     * Skills and perks are the owner's own through progression, so the
     * client reads only two things out of it: the table-entry quality bonus
     * the server pinned for the visit, and the recipe knowledge it holds for
     * this player, which the native recipe book is shown instead of the
     * soul's. The rest is admitted so a malformed view is refused whole.
     */
    class AlchemyCharacterView final {
      public:
        bool Bind(const nlohmann::json &view, std::string &error) try {
            error = "invalidAlchemyCharacterView";
            if (!HasOnly(view, {"skill", "relativeSkill", "sessionQualityBonus", "abilities", "knowledge"}) || !view.contains("skill") || !view.at("skill").is_number() || !view.contains("relativeSkill") || !view.at("relativeSkill").is_number()
                || !view.contains("sessionQualityBonus") || !view.at("sessionQualityBonus").is_number()) {
                return false;
            }
            AlchemyCharacterView candidate;
            candidate._skill               = view.at("skill").get<float>();
            candidate._relativeSkill       = view.at("relativeSkill").get<float>();
            candidate._sessionQualityBonus = view.at("sessionQualityBonus").get<float>();
            if (!std::isfinite(candidate._sessionQualityBonus) || candidate._sessionQualityBonus < 0.0F || !std::isfinite(candidate._skill) || candidate._skill < 0.0F || !std::isfinite(candidate._relativeSkill) || candidate._relativeSkill < 0.0F) {
                return false;
            }
            if (!view.contains("abilities") || !HasOnly(view.at("abilities"), {"19", "30", "63", "92"}) || view.at("abilities").size() != kAbilities.size()) {
                return false;
            }
            for (std::size_t index = 0; index < kAbilities.size(); ++index) {
                const auto &value = view.at("abilities").at(std::to_string(kAbilities[index]));
                if (!value.is_boolean()) {
                    return false;
                }
                candidate._abilities[index] = value.get<bool>();
            }
            if (!view.contains("knowledge") || !view.at("knowledge").is_object() || view.at("knowledge").size() > kMaximumRecipeKnowledgeRows) {
                return false;
            }
            for (const auto &[recipe, mask] : view.at("knowledge").items()) {
                if (recipe.empty() || recipe.size() > 160 || !mask.is_number_unsigned() || mask.get<std::uint64_t>() > AlchemyNative::kCompleteRecipeKnowledge) {
                    return false;
                }
                candidate._knowledge.emplace(recipe, mask.get<std::uint32_t>());
            }
            candidate._valid = true;
            *this            = std::move(candidate);
            error.clear();
            return true;
        }
        catch (const nlohmann::json::exception &) {
            error = "invalidAlchemyCharacterView";
            return false;
        }

        bool Valid() const {
            return _valid;
        }
        void Reset() {
            *this = AlchemyCharacterView {};
        }
        float Skill(bool relative) const {
            return relative ? _relativeSkill : _skill;
        }
        float SessionQualityBonus() const {
            return _sessionQualityBonus;
        }
        std::optional<bool> Ability(std::uint32_t id) const {
            for (std::size_t index = 0; index < kAbilities.size(); ++index) {
                if (kAbilities[index] == id) {
                    return _valid ? std::make_optional(_abilities[index]) : std::nullopt;
                }
            }
            return std::nullopt;
        }
        std::optional<std::uint32_t> Knowledge(const std::string &recipe) const {
            if (!_valid) {
                return std::nullopt;
            }
            const auto found = _knowledge.find(recipe);
            return found == _knowledge.end() ? std::make_optional(0U) : std::make_optional(found->second);
        }
        std::optional<bool> KnowledgeRange(std::uint32_t recipe, std::uint32_t first, std::uint32_t last) const {
            if (!_valid || first > last || last >= std::bit_width(AlchemyNative::kCompleteRecipeKnowledge)) {
                return std::nullopt;
            }
            const std::uint32_t mask = ((std::uint32_t {1} << (last + 1)) - 1) & ~((std::uint32_t {1} << first) - 1);
            return (*Knowledge(std::to_string(recipe)) & mask) == mask;
        }

      private:
        // Wire admission budget; a protocol limit, not a game rule.
        static constexpr std::size_t kMaximumRecipeKnowledgeRows = 256;
        static constexpr std::array<std::uint32_t, 4> kAbilities {AlchemyNative::kTrialAndErrorAbility, AlchemyNative::kBundleAlchemistAbility, AlchemyNative::kSecretOfSecretsAbility, AlchemyNative::kExperimentalPowderAbility};
        float _skill               = 0.0F;
        float _relativeSkill       = 0.0F;
        float _sessionQualityBonus = 0.0F;
        std::array<bool, 4> _abilities {};
        std::map<std::string, std::uint32_t> _knowledge;
        bool _valid = false;
    };
} // namespace KCDC::Shared::Crafting
