#include "features/player/player_combat_modifiers.h"

#include "core/server.h"
#include "shared/game/buff_catalog.h"
#include "shared/game/buff_rule_catalog.h"
#include "shared/game/progression_catalog.h"
#include "shared/game/soul_rule_catalog.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string>

namespace KCDC::Features::Player {
    std::string_view CombatSoulTerms::Token(KCDC::Shared::Game::CombatModifier modifier) {
        switch (modifier) {
        case KCDC::Shared::Game::CombatModifier::ArmorDefense: return "ade";
        case KCDC::Shared::Game::CombatModifier::UnarmedAttack: return "uat";
        case KCDC::Shared::Game::CombatModifier::WeaponAttack: return "wat";
        case KCDC::Shared::Game::CombatModifier::HorseMountedAttack: return "ahm";
        case KCDC::Shared::Game::CombatModifier::HealthLoss: return "hlh";
        case KCDC::Shared::Game::CombatModifier::StaminaLoss: return "slh";
        case KCDC::Shared::Game::CombatModifier::ShieldStaminaLoss: return "sls";
        case KCDC::Shared::Game::CombatModifier::OpponentStaminaBlocked: return "osb";
        case KCDC::Shared::Game::CombatModifier::HeadDamageDealt: return "dtd";
        case KCDC::Shared::Game::CombatModifier::HeadDamageTaken: return "tdh";
        case KCDC::Shared::Game::CombatModifier::SlashAttack: return "sla";
        case KCDC::Shared::Game::CombatModifier::StabAttack: return "sta";
        case KCDC::Shared::Game::CombatModifier::SmashAttack: return "ssa";
        case KCDC::Shared::Game::CombatModifier::WeaponStrengthReq: return "wsr";
        case KCDC::Shared::Game::CombatModifier::WeaponStrength: return "wrs";
        case KCDC::Shared::Game::CombatModifier::WeaponDefense: return "wde";
        case KCDC::Shared::Game::CombatModifier::EnemyWeaponDefense: return "ewd";
        case KCDC::Shared::Game::CombatModifier::CounterAttack: return "cat";
        case KCDC::Shared::Game::CombatModifier::AttackChargeMod: return "acm";
        case KCDC::Shared::Game::CombatModifier::OpponentDefense: return "ode";
        case KCDC::Shared::Game::CombatModifier::OpponentDefenseSkill: return "opd";
        }
        return {};
    }

    const std::unordered_map<std::string_view, CombatSoulTerms::BuffTerms> &CombatSoulTerms::ParsedBuffs() {
        static const std::unordered_map<std::string_view, BuffTerms> parsed = [] {
            std::unordered_map<std::string_view, BuffTerms> buffs;
            for (const KCDC::Shared::Game::BuffRuleDefinition &rule : KCDC::Shared::Game::g_BuffRules) {
                if (rule.parametersAuthored) {
                    buffs.emplace(rule.guid, Parse(rule));
                }
            }
            return buffs;
        }();
        return parsed;
    }

    const CombatSoulTerms::PerkBuffs &CombatSoulTerms::PerkBuffSources() {
        static const PerkBuffs sources = [] {
            PerkBuffs perks;
            for (const KCDC::Shared::Game::PerkBuffRule &edge : KCDC::Shared::Game::g_PerkBuffRules) {
                perks.buffs.insert(edge.buff);
                if (const KCDC::Shared::Game::PerkEntry *perk = KCDC::Shared::Game::FindPerkByGuid(edge.perk)) {
                    perks.grants.emplace_back(KCDC::Shared::Game::PerkIndexOf(*perk), edge.buff);
                }
            }
            for (const KCDC::Shared::Game::SoulPerkAbility &edge : KCDC::Shared::Game::g_SoulPerkAbilities) {
                if (const KCDC::Shared::Game::PerkEntry *perk = KCDC::Shared::Game::FindPerkByGuid(edge.perk)) {
                    perks.abilities.emplace_back(KCDC::Shared::Game::PerkIndexOf(*perk), edge.ability);
                }
            }
            return perks;
        }();
        return sources;
    }

    CombatSoulTerms::BuffTerms CombatSoulTerms::Parse(const KCDC::Shared::Game::BuffRuleDefinition &rule) {
        BuffTerms buff;
        buff.implementation = rule.implementation;
        std::string parameters;
        for (const char c : rule.parameters) {
            if (c != ' ') {
                parameters.push_back(c);
            }
        }

        // The terms of this buff a blow reads; anything else it does is no
        // business of the price.
        constexpr std::string_view operators = "+-*%<>!=";
        for (std::size_t begin = 0; begin < parameters.size();) {
            const std::size_t end = parameters.find_first_of(",;", begin);
            const std::string_view expression(parameters.data() + begin, (end == std::string::npos ? parameters.size() : end) - begin);
            buff.toggled                = buff.toggled || (end != std::string::npos && parameters[end] == ';');
            const std::size_t operation = expression.find_first_of(operators);
            const std::string_view name = expression.substr(0, operation);
            std::string_view token      = name == "bba" ? std::string_view("bba") : std::string_view();
            for (std::uint8_t id = 0; operation != std::string_view::npos && token.empty() && id <= static_cast<std::uint8_t>(KCDC::Shared::Game::CombatModifier::OpponentDefenseSkill); ++id) {
                const std::string_view candidate = Token(static_cast<KCDC::Shared::Game::CombatModifier>(id));
                if (!candidate.empty() && name == candidate) {
                    token = candidate;
                }
            }
            if (!token.empty()) {
                float value                 = 0.0F;
                const std::string_view text = expression.substr(operation + 1);
                const auto parsed           = std::from_chars(text.data(), text.data() + text.size(), value);
                if (parsed.ec == std::errc() && parsed.ptr == text.data() + text.size() && std::isfinite(value)) {
                    buff.terms.push_back({token, static_cast<KCDC::Shared::Game::SoulModifierOperation>(operators.find(expression[operation])), value});
                }
            }
            if (end == std::string::npos) {
                break;
            }
            begin = end + 1;
        }
        return buff;
    }

    void CombatSoulTerms::Collect(std::uint64_t playerNetworkId, bool mounted) {
        _terms.clear();
        _abilities.clear();
        _skipped       = 0;
        Server *server = Server::Get();
        if (server == nullptr) {
            return;
        }

        // A perk's own buff is a source only through the perk: reported again
        // it would count twice.
        const PerkBuffs &perks = PerkBuffSources();
        if (const auto *digest = server->GetProgression().Accepted(playerNetworkId)) {
            for (const auto &[perk, buff] : perks.grants) {
                if (digest->perks.Has(perk)) {
                    AddBuff(buff, mounted);
                }
            }
            for (const auto &[perk, ability] : perks.abilities) {
                if (digest->perks.Has(perk)) {
                    _abilities.push_back(ability);
                }
            }
        }
        if (const auto *report = server->GetBuffs().Report(playerNetworkId)) {
            std::vector<std::string_view> seen;
            for (const auto &observed : *report) {
                const auto *entry = KCDC::Shared::Game::FindBuffById(observed.buff.buff);
                if (entry == nullptr || perks.buffs.contains(entry->guid) || std::find(seen.begin(), seen.end(), entry->guid) != seen.end()) {
                    continue;
                }
                seen.push_back(entry->guid);
                AddBuff(entry->guid, mounted);
            }
        }

        // 0x18063B7C8 keeps a soul's modifiers ordered by operation.
        std::stable_sort(_terms.begin(), _terms.end(), [](const Term &a, const Term &b) {
            return a.operation < b.operation;
        });
    }

    void CombatSoulTerms::AddBuff(std::string_view buffGuid, bool mounted) {
        const auto &parsed = ParsedBuffs();
        const auto found   = parsed.find(buffGuid);
        if (found == parsed.end() || found->second.terms.empty()) {
            return;
        }
        const BuffTerms &buff                 = found->second;
        const std::string_view implementation = buff.implementation;
        const bool always                     = implementation == "Cpp:Constant" || implementation == "Cpp:BasicTimed";
        if (implementation == "Cpp:VersusAnimal" || (implementation == "Cpp:Mounted" && !mounted)) {
            return;
        }
        // A `;` splits a buff's on and off states, which only its own
        // implementation chooses between.
        if (buff.toggled || (!always && implementation != "Cpp:Mounted")) {
            ++_skipped;
            return;
        }
        _terms.insert(_terms.end(), buff.terms.begin(), buff.terms.end());
    }

    float CombatSoulTerms::ApplyToken(std::string_view token, float value) const {
        const float base = value;
        for (const Term &term : _terms) {
            if (term.token != token) {
                continue;
            }
            switch (term.operation) {
            case KCDC::Shared::Game::SoulModifierOperation::Add: value = term.value + value; break;
            case KCDC::Shared::Game::SoulModifierOperation::Subtract: value = value - term.value; break;
            case KCDC::Shared::Game::SoulModifierOperation::ScaleOriginalBase: value = (term.value - 1.0F) * base + value; break;
            case KCDC::Shared::Game::SoulModifierOperation::ScaleCurrent: value = (term.value - 1.0F) * value + value; break;
            case KCDC::Shared::Game::SoulModifierOperation::UpperBound: value = std::min(value, term.value); break;
            case KCDC::Shared::Game::SoulModifierOperation::LowerBound: value = std::max(value, term.value); break;
            case KCDC::Shared::Game::SoulModifierOperation::ReflectCurrent: value = term.value - value; break;
            case KCDC::Shared::Game::SoulModifierOperation::Assign: value = term.value; break;
            }
        }
        return std::isfinite(value) ? value : base;
    }

    float CombatSoulTerms::Apply(KCDC::Shared::Game::CombatModifier modifier, float value) const {
        return ApplyToken(Token(modifier), value);
    }

    float CombatSoulTerms::BodyBaseArmor(float base) const {
        return std::max(ApplyToken("bba", base), 0.0F);
    }
} // namespace KCDC::Features::Player
