#include "features/crafting/alchemy_character_evaluator.h"
#include "shared/features/crafting/alchemy_character_view.h"

#include <stdexcept>

namespace {
    void CharacterRequire(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    KCDC::Features::Player::SoulSnapshot Soul() {
        KCDC::Features::Player::SoulSnapshot soul;
        soul.clock                  = {1000, 15000};
        soul.profile.health         = 100.0F;
        soul.profile.skillLevels[6] = 0;
        return soul;
    }
} // namespace

void AlchemyCharacterEvaluatorTests() {
    std::string error;
    KCDC::Features::Crafting::AlchemyCharacterContext context;

    auto soul = Soul();
    soul.profile.perks.insert("f57098fe-24f5-4850-81f8-36bb0a12e4f9");
    soul.profile.perks.insert("249b10e9-23df-4055-9e2c-93ebf959967d");
    soul.profile.effects.push_back({"a", "ffda734a-763e-4de6-9cac-309c6084513b", std::nullopt});
    soul.profile.effects.push_back({"b", "f1a483b4-a142-4311-87a8-150978216167", std::nullopt});
    CharacterRequire(KCDC::Features::Crafting::AlchemyCharacterEvaluator::Evaluate(soul, context, error), "an in-memory character evaluates");
    CharacterRequire(context.skill == 0.0F && context.relativeSkill == 0.0F && context.boilingTolerance == 0.2F && context.allowedMistakes == 4 && context.failureXPUnits == 640, "native defaults plus authored table overrides produce exact brewing inputs");
    CharacterRequire(context.secretOfSecrets && context.experimentalPowder && !context.trialAndError && !context.bundleAlchemist, "owned perks resolve exact ability edges");
    CharacterRequire(context.sessionQualityBonus == KCDC::Shared::Crafting::AlchemyNative::kSessionQualityBonusBase, "no contributing quality modifier leaves the native table-entry base");
    float yielded = 0.0F;
    CharacterRequire(KCDC::Features::Crafting::AlchemyCharacterEvaluator::ApplyYield(context, 2.0F, yielded, error) && yielded == 3.0F && KCDC::Features::Crafting::AlchemyCharacterEvaluator::ApplyYield(context, 9.0F, yielded, error) && yielded == 10.0F,
        "the yield modifier applies to the actual result base");

    const nlohmann::json knowledge = {{"16", KCDC::Shared::Crafting::AlchemyNative::kCompleteRecipeKnowledge}, {"3", 5U}};
    const auto projection          = nlohmann::json::parse(KCDC::Features::Crafting::AlchemyCharacterEvaluator::EncodeProjection(context, knowledge).dump());
    KCDC::Shared::Crafting::AlchemyCharacterView view;
    CharacterRequire(view.Bind(projection, error) && view.Ability(KCDC::Shared::Crafting::AlchemyNative::kSecretOfSecretsAbility) == true && view.Ability(KCDC::Shared::Crafting::AlchemyNative::kTrialAndErrorAbility) == false, "the owner binds the server's character exactly");
    CharacterRequire(view.KnowledgeRange(16, 0, 28) == true && view.KnowledgeRange(3, 0, 0) == true && view.KnowledgeRange(3, 1, 1) == false && view.KnowledgeRange(4, 0, 0) == false, "the recipe book reads the server's knowledge, partial and unknown kept apart");

    auto skilled                   = Soul();
    skilled.profile.skillLevels[6] = 30;
    CharacterRequire(KCDC::Features::Crafting::AlchemyCharacterEvaluator::Evaluate(skilled, context, error) && context.skill == 30.0F && context.relativeSkill == 1.0F && context.boilingTolerance >= 0.2F, "the progression level is the skill base");

    auto dead           = Soul();
    dead.profile.health = 0.0F;
    CharacterRequire(!KCDC::Features::Crafting::AlchemyCharacterEvaluator::Evaluate(dead, context, error) && error == "soulActorNotAlive", "a dead player does not brew");

    // Black Arts is a daily window on the world clock: 00:00 to 04:30.
    constexpr const char *blackArtsPerk = "234615f0-2e62-4977-aeaf-da2da47e7847";
    auto night                          = Soul();
    night.profile.perks.insert(blackArtsPerk);
    night.clock.worldMilliseconds = 16199000;
    CharacterRequire(KCDC::Features::Crafting::AlchemyCharacterEvaluator::Evaluate(night, context, error) && context.sessionQualityBonus == 0.1F, "Black Arts adds its table bonus inside its window");
    night.clock.worldMilliseconds = 16201000;
    CharacterRequire(KCDC::Features::Crafting::AlchemyCharacterEvaluator::Evaluate(night, context, error) && context.sessionQualityBonus == 0.0F, "and nothing outside it");

    auto timed = Soul();
    timed.profile.effects.push_back({"c", "f1a483b4-a142-4311-87a8-150978216167", 2000});
    CharacterRequire(!KCDC::Features::Crafting::AlchemyCharacterEvaluator::Evaluate(timed, context, error) && error.find("activeEffectSimulationRequired:") == 0, "a timed buff that changes the result is refused, never ignored");

    auto unknown = Soul();
    unknown.profile.effects.push_back({"d", "00000000-0000-0000-0000-000000000001", std::nullopt});
    CharacterRequire(KCDC::Features::Crafting::AlchemyCharacterEvaluator::Evaluate(unknown, context, error), "a reported buff the catalog cannot describe names no brewing selector");

    // Enthusiast's Cpp:Alchemy minigame effect touches exhaustion and
    // digestion only; the general scope still refuses to evaluate it.
    auto enthusiast = Soul();
    enthusiast.profile.perks.insert("47a2cb9d-1932-4eba-aaf5-cd21f3a2ffe2");
    CharacterRequire(KCDC::Features::Crafting::AlchemyCharacterEvaluator::Evaluate(enthusiast, context, error), "resource-only minigame effects do not block brewing");
    const auto brewing = KCDC::Features::Player::SoulEvaluationScope::AlchemyOutcome;
    const KCDC::Shared::Game::SoulScalarQuery alchemySkill {KCDC::Shared::Game::SoulScalarDomain::Skill, KCDC::Shared::Crafting::AlchemyNative::kSkill, 1.0F, std::nullopt};
    const KCDC::Shared::Game::SoulScalarQuery otherSkill {KCDC::Shared::Game::SoulScalarDomain::Skill, 1U, 1.0F, std::nullopt};
    KCDC::Shared::Game::SoulScalarCalculation calculated;
    CharacterRequire(!KCDC::Features::Player::EvaluateCanonicalSoulScalar(enthusiast, enthusiast.clock, alchemySkill, calculated, error) && error.find("unsupportedActiveEffect:") == 0, "the general scope still refuses an unsynchronized C++ effect");

    auto periodic = Soul();
    periodic.profile.effects.push_back({"e", "52e578c8-608f-44e5-b6c0-e79673cfd4a0", std::nullopt});
    CharacterRequire(!KCDC::Features::Player::EvaluateCanonicalSoulScalar(periodic, periodic.clock, alchemySkill, calculated, error) && error.find("activeEffectSimulationRequired:") == 0, "a periodic resource effect blocks the general scope");
    CharacterRequire(KCDC::Features::Player::EvaluateCanonicalSoulScalar(periodic, periodic.clock, alchemySkill, calculated, error, brewing) && calculated.effective == 1.0F, "and not the brewing scope");
    CharacterRequire(!KCDC::Features::Player::EvaluateCanonicalSoulScalar(periodic, periodic.clock, otherSkill, calculated, error, brewing) && error == "unsupportedSoulEvaluationScope", "the brewing scope cannot answer unrelated skill queries");
}
