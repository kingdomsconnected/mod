#include "features/player/soul_evaluator.h"
#include "shared/features/crafting/alchemy_native_ids.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
    void RequireSoulEvaluator(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }
    KCDC::Shared::Game::SoulScalarTerm SkillTerm(KCDC::Shared::Game::SoulModifierOperation operation, float value) {
        return {KCDC::Shared::Game::SoulScalarDomain::Skill, 6, operation, value, true, false, false, std::nullopt, "test-skill-effect"};
    }
} // namespace

void SoulEvaluatorTests() {
    std::string error;
    bool dailyActive = false;
    RequireSoulEvaluator(KCDC::Features::Player::EvaluateSoulDailyWindow(0, 0.0F, 4.5F, dailyActive, error) && dailyActive, "Black Arts includes midnight start");
    RequireSoulEvaluator(KCDC::Features::Player::EvaluateSoulDailyWindow(16200000, 0.0F, 4.5F, dailyActive, error) && dailyActive, "Black Arts includes exact 04:30 boundary");
    RequireSoulEvaluator(KCDC::Features::Player::EvaluateSoulDailyWindow(16201000, 0.0F, 4.5F, dailyActive, error) && !dailyActive, "Black Arts turns off after inclusive boundary");
    RequireSoulEvaluator(KCDC::Features::Player::EvaluateSoulDailyWindow(102600001, 0.0F, 4.5F, dailyActive, error) && dailyActive, "native float calendar rounding retains boundary after integer milliseconds have crossed it");
    RequireSoulEvaluator(KCDC::Features::Player::EvaluateSoulDailyWindow(86400000, 0.0F, 4.5F, dailyActive, error) && dailyActive, "next world day reactivates the time-conditioned rule");
    RequireSoulEvaluator(KCDC::Features::Player::EvaluateSoulDailyWindow(82800000, 22.0F, 3.0F, dailyActive, error) && dailyActive, "authored wrapping window includes previous evening");
    RequireSoulEvaluator(KCDC::Features::Player::EvaluateSoulDailyWindow(14400000, 22.0F, 3.0F, dailyActive, error) && !dailyActive, "wrapping window excludes daytime");
    RequireSoulEvaluator(KCDC::Features::Player::EvaluateSoulDailyWindow(43200000, 4.5F, 4.5F, dailyActive, error) && dailyActive, "native equal endpoints form a full-day wrapping window");
    RequireSoulEvaluator(!KCDC::Features::Player::EvaluateSoulDailyWindow(std::numeric_limits<std::uint64_t>::max(), 0.0F, 4.5F, dailyActive, error), "unsigned time outside native signed calendar range refuses");
    KCDC::Shared::Game::SoulScalarQuery query {KCDC::Shared::Game::SoulScalarDomain::Skill, 6, 10.0F, KCDC::Shared::Game::SoulQueryReason::Default};
    std::vector<KCDC::Shared::Game::SoulScalarTerm> terms {SkillTerm(KCDC::Shared::Game::SoulModifierOperation::Add, 4.0F), SkillTerm(KCDC::Shared::Game::SoulModifierOperation::ScaleOriginalBase, 1.5F)};
    auto value = KCDC::Shared::Game::EvaluateResolvedSoulScalar(query, terms, error);
    RequireSoulEvaluator(value && value->effective == 19.0F && value->relative == 19.0F / 30.0F, "star uses original base after additive effect, then relative skill divides by native cap");
    terms[1].operation = KCDC::Shared::Game::SoulModifierOperation::ScaleCurrent;
    value              = KCDC::Shared::Game::EvaluateResolvedSoulScalar(query, terms, error);
    RequireSoulEvaluator(value && value->effective == 21.0F, "percent scales the accumulated value");
    std::swap(terms[0], terms[1]);
    value = KCDC::Shared::Game::EvaluateResolvedSoulScalar(query, terms, error);
    RequireSoulEvaluator(value && value->effective == 19.0F, "reordering noncommutative native terms changes the result");

    terms = {SkillTerm(KCDC::Shared::Game::SoulModifierOperation::Subtract, 3.0F), SkillTerm(KCDC::Shared::Game::SoulModifierOperation::LowerBound, 8.0F), SkillTerm(KCDC::Shared::Game::SoulModifierOperation::UpperBound, 6.0F),
        SkillTerm(KCDC::Shared::Game::SoulModifierOperation::ReflectCurrent, 15.0F)};
    value = KCDC::Shared::Game::EvaluateResolvedSoulScalar(query, terms, error);
    RequireSoulEvaluator(value && value->effective == 9.0F, "subtract, lower bound, upper bound and reflection preserve native operation order");
    terms                  = {SkillTerm(KCDC::Shared::Game::SoulModifierOperation::Assign, 40.0F)};
    terms[0].contextual    = true;
    terms[0].contextWeight = 0.25F;
    value                  = KCDC::Shared::Game::EvaluateResolvedSoulScalar(query, terms, error);
    RequireSoulEvaluator(value && value->effective == 30.0F && value->relative == 1.0F, "nonzero contextual weight gates assignment rather than interpolating; final cap applies");
    terms[0].contextWeight = 0.0F;
    value                  = KCDC::Shared::Game::EvaluateResolvedSoulScalar(query, terms, error);
    RequireSoulEvaluator(value && value->effective == 10.0F, "zero contextual weight excludes assignment");
    terms[0].contextWeight.reset();
    RequireSoulEvaluator(!KCDC::Shared::Game::EvaluateResolvedSoulScalar(query, terms, error) && error == "unsupportedContextWeight:test-skill-effect", "missing contributing buff context cannot be replaced by weight one");
    query.context.reset();
    value = KCDC::Shared::Game::EvaluateResolvedSoulScalar(query, terms, error);
    RequireSoulEvaluator(value && value->effective == 10.0F, "native null query context suppresses contextual modifier");

    query.context = KCDC::Shared::Game::SoulQueryReason::BaseValue;
    terms[0].operation.reset();
    terms[0].value = std::numeric_limits<float>::quiet_NaN();
    value          = KCDC::Shared::Game::EvaluateResolvedSoulScalar(query, terms, error);
    RequireSoulEvaluator(value && value->effective == 10.0F, "base-value query never evaluates contributing modifier or context");
    query.context      = KCDC::Shared::Game::SoulQueryReason::LongTermValue;
    terms[0].shortTerm = true;
    value              = KCDC::Shared::Game::EvaluateResolvedSoulScalar(query, terms, error);
    RequireSoulEvaluator(value && value->effective == 10.0F, "long-term query excludes unsupported short-term modifier");
    terms[0].shortTerm = false;
    terms[0].selector  = 7;
    value              = KCDC::Shared::Game::EvaluateResolvedSoulScalar(query, terms, error);
    RequireSoulEvaluator(value && value->effective == 10.0F, "unsupported unrelated skill effect does not block requested skill");
    terms[0].selector   = query.selector;
    terms[0].contextual = false;
    RequireSoulEvaluator(!KCDC::Shared::Game::EvaluateResolvedSoulScalar(query, terms, error) && error == "unsupportedContributingOperator:test-skill-effect", "unsupported active contributing operator omits result");
    query.resolvedBase = 10.5F;
    RequireSoulEvaluator(!KCDC::Shared::Game::EvaluateResolvedSoulScalar(query, {}, error) && error == "nonIntegralNativeBaseLevel", "legacy fractional profile cannot silently round native base level");
    query.resolvedBase = 40.0F;
    value              = KCDC::Shared::Game::EvaluateResolvedSoulScalar(query, {}, error);
    RequireSoulEvaluator(value && value->base == 40.0F && value->effective == 30.0F, "base input and clamped effective value remain distinct");
    query = {KCDC::Shared::Game::SoulScalarDomain::Adjustment, 83, 5.0F, std::nullopt};
    RequireSoulEvaluator(!KCDC::Shared::Game::EvaluateResolvedSoulScalar(query, {}, error) && error == "unsupportedAdjustmentBounds", "unimplemented special adjustment bounds cannot use a generic clamp");

    query = {KCDC::Shared::Game::SoulScalarDomain::DerivedStat, KCDC::Shared::Crafting::AlchemyNative::kSessionQualityBonusStat, KCDC::Shared::Crafting::AlchemyNative::kSessionQualityBonusBase, std::nullopt};
    terms = {{query.domain, query.selector, KCDC::Shared::Game::SoulModifierOperation::Add, 0.1F, true, false, false, std::nullopt, "quality-bonus"}};
    value = KCDC::Shared::Game::EvaluateResolvedSoulScalar(query, terms, error);
    RequireSoulEvaluator(value && value->base == 0.0F && value->effective == 0.1F, "table entry bonus evaluates derived modifiers on the proven native zero base");
    terms[0].value = 2.0F;
    value          = KCDC::Shared::Game::EvaluateResolvedSoulScalar(query, terms, error);
    RequireSoulEvaluator(value && value->effective == 2.0F, "table entry bonus is not prematurely clamped to final potion quality");
    terms[0].value = -0.1F;
    value          = KCDC::Shared::Game::EvaluateResolvedSoulScalar(query, terms, error);
    RequireSoulEvaluator(value && value->effective == 0.0F, "derived quality bonus applies native nonnegative lower bound");
}
