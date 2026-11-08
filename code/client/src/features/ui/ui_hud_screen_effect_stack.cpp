#include "ui_hud_screen_effect_stack.h"

#include <algorithm>

namespace KCDC::Features::UI {
    namespace {
        /** 0 at `startMs`, 1 from `startMs + lengthMs` on; a zero length is already complete. */
        float Progress(std::uint64_t nowMs, std::uint64_t startMs, std::uint32_t lengthMs) {
            if (lengthMs == 0 || nowMs >= startMs + lengthMs) {
                return 1.0F;
            }
            if (nowMs <= startMs) {
                return 0.0F;
            }
            return static_cast<float>(nowMs - startMs) / static_cast<float>(lengthMs);
        }

        // Each parameter is one channel; colorGrade covers all eight of the
        // game's tint values, so a grade composes with the game's own as a
        // whole rather than channel by channel.
        constexpr std::array<ScreenEffectParam, 8> kColorGradeParams {{
            {"saturation", ScreenChannel::Saturation, 0.0F, 2.0F, 1.0F, 1.0F, "Colour saturation, multiplied into the game's own: 0 is grey, 1 leaves it, 2 doubles it."},
            {"brightness", ScreenChannel::Brightness, 0.0F, 2.0F, 1.0F, 1.0F, "Brightness, multiplied into the game's own: 0 is black, 1 leaves it."},
            {"contrast", ScreenChannel::Contrast, 0.0F, 2.0F, 1.0F, 1.0F, "Contrast around mid-grey, multiplied into the game's own: below 1 flattens the picture, above 1 hardens it."},
            {"cyan", ScreenChannel::Cyan, 0.0F, 1.0F, 0.0F, 0.0F, "Takes this much out of the red channel, which pushes the picture towards cyan. Added to the game's own."},
            {"magenta", ScreenChannel::Magenta, 0.0F, 1.0F, 0.0F, 0.0F, "Takes this much out of the green channel, towards magenta. Added to the game's own."},
            {"yellow", ScreenChannel::Yellow, 0.0F, 1.0F, 0.0F, 0.0F, "Takes this much out of the blue channel, towards yellow. Added to the game's own."},
            {"darkness", ScreenChannel::Darkness, 0.0F, 1.0F, 0.0F, 0.0F, "Takes this much out of every channel at once. Added to the game's own."},
            {"hue", ScreenChannel::Hue, -1.0F, 1.0F, 0.0F, 0.0F, "Rotates every hue by this times 180 degrees. Added to the game's own."},
        }};

        constexpr std::array<ScreenEffectParam, 1> kBlurParams {{
            {"amount", ScreenChannel::Blur, 0.0F, 1.0F, 0.0F, 0.5F, "How far the picture is blended towards a gaussian-blurred copy of itself: 0 is sharp, 1 fully blurred."},
        }};

        constexpr std::array<ScreenEffectParam, 1> kGhostingParams {{
            {"amount", ScreenChannel::Ghosting, 0.0F, 1.0F, 0.0F, 0.5F, "How long earlier frames linger over the current one: 0 is none, 1 leaves long trails behind anything that moves. The game measures it per frame, so it trails less at a low frame rate."},
        }};

        constexpr std::array<ScreenEffectParam, 1> kScreenBloodParams {{
            {"amount", ScreenChannel::ScreenBlood, 0.0F, 1.0F, 0.0F, 0.5F, "How far blood smears in from the edges of the screen: 0 is none, 1 reaches as far as the game's blood overlay goes."},
        }};

        constexpr std::array<ScreenEffectSpec, 4> kEffects {{
            {"colorGrade", "Re-grades the whole picture: saturation, brightness, contrast, a colour cast and a hue shift. Composed with the game's own grade, which already desaturates the screen as health runs low, and with every other grade on screen.", kColorGradeParams},
            {"blur", "Blurs the whole picture. One blur is drawn however many are asked for: the strongest wins.", kBlurParams},
            {"ghosting", "Leaves a fading copy of earlier frames over the picture, so whatever moves smears. One is drawn however many are asked for: the strongest wins.", kGhostingParams},
            {"screenBlood", "The game's blood overlay: red smears creeping in from the edges of the screen. One is drawn however many are asked for: the strongest wins.", kScreenBloodParams},
        }};
    } // namespace

    std::span<const ScreenEffectSpec> ScreenEffectStack::Effects() {
        return kEffects;
    }

    const ScreenEffectSpec *ScreenEffectStack::FindEffect(std::string_view name) {
        for (const ScreenEffectSpec &effect : kEffects) {
            if (effect.name == name) {
                return &effect;
            }
        }
        return nullptr;
    }

    ScreenFold ScreenEffectStack::FoldOf(ScreenChannel channel) {
        switch (channel) {
        case ScreenChannel::Brightness:
        case ScreenChannel::Contrast:
        case ScreenChannel::Saturation: return ScreenFold::Multiply;
        case ScreenChannel::Blur:
        case ScreenChannel::Ghosting:
        case ScreenChannel::ScreenBlood: return ScreenFold::Strongest;
        default: return ScreenFold::Add;
        }
    }

    float ScreenEffectStack::NeutralOf(ScreenChannel channel) {
        return FoldOf(channel) == ScreenFold::Multiply ? 1.0F : 0.0F;
    }

    float ScreenEffectStack::ParamAt(const Effect &effect, std::size_t index, std::uint64_t nowMs) {
        const float t = Progress(nowMs, effect.retuneStartMs, effect.retuneMs);
        return effect.from[index] + (effect.to[index] - effect.from[index]) * t;
    }

    float ScreenEffectStack::FadeOutWeight(const Effect &effect, std::uint64_t nowMs) {
        if (!effect.leaving) {
            return 1.0F;
        }
        return effect.leaveMs == 0 ? 0.0F : 1.0F - Progress(nowMs, effect.leaveStartMs, effect.leaveMs);
    }

    float ScreenEffectStack::WeightAt(const Effect &effect, std::uint64_t nowMs) {
        return std::min(Progress(nowMs, effect.createdMs, effect.fadeInMs), FadeOutWeight(effect, nowMs));
    }

    void ScreenEffectStack::BeginScheduledLeave(Effect &effect, std::uint64_t nowMs) {
        if (effect.leaving || effect.endsAtMs == 0 || nowMs < effect.endsAtMs) {
            return;
        }
        effect.leaving      = true;
        effect.leaveStartMs = effect.endsAtMs;
        effect.leaveMs      = effect.fadeOutMs;
    }

    ScreenEffectStack::Effect *ScreenEffectStack::Find(std::uint32_t handle) {
        for (Effect &effect : _effects) {
            if (effect.handle == handle) {
                return &effect;
            }
        }
        return nullptr;
    }

    const ScreenEffectStack::Effect *ScreenEffectStack::Find(std::uint32_t handle) const {
        for (const Effect &effect : _effects) {
            if (effect.handle == handle) {
                return &effect;
            }
        }
        return nullptr;
    }

    std::uint32_t ScreenEffectStack::Create(const std::string &resource, const ScreenEffectSpec &effect, const ParamValues &values, const Timing &timing, std::uint64_t nowMs) {
        if (_effects.size() >= kMaxEffects) {
            return kNoHandle;
        }

        Effect created;
        created.handle    = _nextHandle++;
        created.resource  = resource;
        created.spec      = &effect;
        created.createdMs = nowMs;
        created.fadeInMs  = std::min(timing.fadeInMs, kMaxFadeMs);
        created.fadeOutMs = std::min(timing.fadeOutMs, kMaxFadeMs);
        created.endsAtMs  = timing.durationMs == 0 ? 0 : nowMs + std::min(timing.durationMs, kMaxDurationMs);
        for (std::size_t i = 0; i < effect.params.size(); ++i) {
            const ScreenEffectParam &param = effect.params[i];
            created.to[i]                  = std::clamp(values.given[i] ? values.values[i] : param.fallback, param.min, param.max);
            created.from[i]                = created.to[i];
        }

        _effects.push_back(std::move(created));
        return _effects.back().handle;
    }

    bool ScreenEffectStack::Retune(const std::string &resource, std::uint32_t handle, const ParamValues &values, std::uint32_t fadeMs, std::uint64_t nowMs) {
        Effect *effect = Find(handle);
        if (effect == nullptr || effect->resource != resource) {
            return false;
        }

        // From wherever each parameter is right now, so a retune during a
        // retune carries on rather than jumping.
        for (std::size_t i = 0; i < effect->spec->params.size(); ++i) {
            const ScreenEffectParam &param = effect->spec->params[i];
            const float current            = ParamAt(*effect, i, nowMs);
            effect->from[i]                = current;
            effect->to[i]                  = values.given[i] ? std::clamp(values.values[i], param.min, param.max) : current;
        }
        effect->retuneStartMs = nowMs;
        effect->retuneMs      = std::min(fadeMs, kMaxFadeMs);
        return true;
    }

    bool ScreenEffectStack::Remove(const std::string &resource, std::uint32_t handle, bool ownFade, std::uint32_t fadeOutMs, std::uint64_t nowMs) {
        Effect *effect = Find(handle);
        if (effect == nullptr || effect->resource != resource) {
            return false;
        }

        BeginScheduledLeave(*effect, nowMs);

        // Started back in time by however far the weight already is from
        // full, so a second remove continues the first instead of snapping
        // the effect back up.
        const std::uint32_t length = std::min(ownFade ? effect->fadeOutMs : fadeOutMs, kMaxFadeMs);
        const float remaining      = FadeOutWeight(*effect, nowMs);
        effect->leaving            = true;
        effect->leaveMs            = length;
        effect->leaveStartMs       = nowMs - static_cast<std::uint64_t>((1.0F - remaining) * static_cast<float>(length));
        return true;
    }

    std::size_t ScreenEffectStack::RemoveAll(const std::string &resource) {
        return std::erase_if(_effects, [&resource](const Effect &effect) {
            return effect.resource == resource;
        });
    }

    void ScreenEffectStack::Clear() {
        _effects.clear();
    }

    bool ScreenEffectStack::IsActive(std::uint32_t handle, std::uint64_t nowMs) const {
        const Effect *effect = Find(handle);
        if (effect == nullptr) {
            return false;
        }

        // Answered for the moment of asking, not for the last compose.
        if (effect->leaving) {
            return FadeOutWeight(*effect, nowMs) > 0.0F;
        }
        return effect->endsAtMs == 0 || nowMs < effect->endsAtMs + effect->fadeOutMs;
    }

    const ScreenEffectSpec *ScreenEffectStack::EffectOf(const std::string &resource, std::uint32_t handle) const {
        const Effect *effect = Find(handle);
        return effect != nullptr && effect->resource == resource ? effect->spec : nullptr;
    }

    ScreenEffectStack::Composition ScreenEffectStack::Compose(std::uint64_t nowMs) {
        // Ended effects go first, so their last frame is not composed.
        for (Effect &effect : _effects) {
            BeginScheduledLeave(effect, nowMs);
        }
        std::erase_if(_effects, [nowMs](const Effect &effect) {
            return effect.leaving && FadeOutWeight(effect, nowMs) <= 0.0F;
        });

        Composition composition;
        for (std::size_t c = 0; c < kScreenChannelCount; ++c) {
            composition.value[c] = NeutralOf(static_cast<ScreenChannel>(c));
        }

        for (const Effect &effect : _effects) {
            const float weight = WeightAt(effect, nowMs);
            for (std::size_t i = 0; i < effect.spec->params.size(); ++i) {
                const ScreenEffectParam &param = effect.spec->params[i];
                const std::size_t c            = static_cast<std::size_t>(param.channel);
                const float value              = param.neutral + (ParamAt(effect, i, nowMs) - param.neutral) * weight;
                composition.held[c]            = true;
                switch (FoldOf(param.channel)) {
                case ScreenFold::Multiply: composition.value[c] *= value; break;
                case ScreenFold::Add: composition.value[c] += value; break;
                case ScreenFold::Strongest: composition.value[c] = std::max(composition.value[c], value); break;
                }
            }
        }
        return composition;
    }
} // namespace KCDC::Features::UI
