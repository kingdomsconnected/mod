#pragma once

#include "features/ui/ui_hud_screen_effect_stack.h"

#include <cmath>
#include <string>

namespace {
    const KCDC::Features::UI::ScreenEffectSpec &ScreenEffect(const char *name) {
        return *KCDC::Features::UI::ScreenEffectStack::FindEffect(name);
    }

    /** Only `name`, set to `value`; every other parameter at its default. */
    KCDC::Features::UI::ScreenEffectStack::ParamValues ScreenParam(const char *effect, const char *name, float value) {
        KCDC::Features::UI::ScreenEffectStack::ParamValues values;
        const KCDC::Features::UI::ScreenEffectSpec &spec = ScreenEffect(effect);
        for (std::size_t i = 0; i < spec.params.size(); ++i) {
            if (spec.params[i].name == name) {
                values.values[i] = value;
                values.given[i]  = true;
            }
        }
        return values;
    }

    float ScreenChannelValue(const KCDC::Features::UI::ScreenEffectStack::Composition &composition, KCDC::Features::UI::ScreenChannel channel) {
        return composition.value[static_cast<std::size_t>(channel)];
    }

    bool ScreenChannelHeld(const KCDC::Features::UI::ScreenEffectStack::Composition &composition, KCDC::Features::UI::ScreenChannel channel) {
        return composition.held[static_cast<std::size_t>(channel)];
    }

    bool ScreenNear(float a, float b) {
        return std::fabs(a - b) < 1.0e-4F;
    }
} // namespace

MODULE(screen_effects, {
    IT("lists the four effects and finds them by name", {
        EQUALS(KCDC::Features::UI::ScreenEffectStack::Effects().size(), static_cast<std::size_t>(4));
        EQUALS(KCDC::Features::UI::ScreenEffectStack::FindEffect("colorGrade") != nullptr, true);
        EQUALS(KCDC::Features::UI::ScreenEffectStack::FindEffect("blur") != nullptr, true);
        EQUALS(KCDC::Features::UI::ScreenEffectStack::FindEffect("ghosting") != nullptr, true);
        EQUALS(KCDC::Features::UI::ScreenEffectStack::FindEffect("screenBlood") != nullptr, true);
        EQUALS(KCDC::Features::UI::ScreenEffectStack::FindEffect("vignette") == nullptr, true);
    });

    IT("keeps every parameter's default inside its range and gives each at most the parameter budget", {
        for (const KCDC::Features::UI::ScreenEffectSpec &effect : KCDC::Features::UI::ScreenEffectStack::Effects()) {
            LESSEREQ(effect.params.size(), KCDC::Features::UI::ScreenEffectStack::kMaxParams);
            for (const KCDC::Features::UI::ScreenEffectParam &param : effect.params) {
                GREATEREQ(param.fallback, param.min);
                LESSEREQ(param.fallback, param.max);
                GREATEREQ(param.neutral, param.min);
                LESSEREQ(param.neutral, param.max);
                EQUALS(param.neutral, KCDC::Features::UI::ScreenEffectStack::NeutralOf(param.channel));
            }
        }
    });

    IT("composes nothing and holds nothing while empty", {
        KCDC::Features::UI::ScreenEffectStack stack;
        const KCDC::Features::UI::ScreenEffectStack::Composition composition = stack.Compose(1000);
        for (std::size_t c = 0; c < KCDC::Features::UI::kScreenChannelCount; ++c) {
            EQUALS(composition.held[c], false);
            EQUALS(composition.value[c], KCDC::Features::UI::ScreenEffectStack::NeutralOf(static_cast<KCDC::Features::UI::ScreenChannel>(c)));
        }
    });

    IT("clamps values into their range", {
        KCDC::Features::UI::ScreenEffectStack stack;
        stack.Create("a", ScreenEffect("blur"), ScreenParam("blur", "amount", 7.0F), {}, 1000);
        stack.Create("a", ScreenEffect("colorGrade"), ScreenParam("colorGrade", "saturation", -3.0F), {}, 1000);
        const KCDC::Features::UI::ScreenEffectStack::Composition composition = stack.Compose(1000);
        EQUALS(ScreenChannelValue(composition, KCDC::Features::UI::ScreenChannel::Blur), 1.0F);
        EQUALS(ScreenChannelValue(composition, KCDC::Features::UI::ScreenChannel::Saturation), 0.0F);
    });

    IT("multiplies saturation, adds the colour cast and takes the strongest blur, whatever the order", {
        KCDC::Features::UI::ScreenEffectStack forward;
        forward.Create("a", ScreenEffect("colorGrade"), ScreenParam("colorGrade", "saturation", 0.5F), {}, 1000);
        forward.Create("b", ScreenEffect("colorGrade"), ScreenParam("colorGrade", "saturation", 0.4F), {}, 1000);
        forward.Create("a", ScreenEffect("colorGrade"), ScreenParam("colorGrade", "cyan", 0.2F), {}, 1000);
        forward.Create("b", ScreenEffect("colorGrade"), ScreenParam("colorGrade", "cyan", 0.1F), {}, 1000);
        forward.Create("a", ScreenEffect("blur"), ScreenParam("blur", "amount", 0.3F), {}, 1000);
        forward.Create("b", ScreenEffect("blur"), ScreenParam("blur", "amount", 0.6F), {}, 1000);

        KCDC::Features::UI::ScreenEffectStack backward;
        backward.Create("b", ScreenEffect("blur"), ScreenParam("blur", "amount", 0.6F), {}, 1000);
        backward.Create("a", ScreenEffect("blur"), ScreenParam("blur", "amount", 0.3F), {}, 1000);
        backward.Create("b", ScreenEffect("colorGrade"), ScreenParam("colorGrade", "cyan", 0.1F), {}, 1000);
        backward.Create("a", ScreenEffect("colorGrade"), ScreenParam("colorGrade", "cyan", 0.2F), {}, 1000);
        backward.Create("b", ScreenEffect("colorGrade"), ScreenParam("colorGrade", "saturation", 0.4F), {}, 1000);
        backward.Create("a", ScreenEffect("colorGrade"), ScreenParam("colorGrade", "saturation", 0.5F), {}, 1000);

        const KCDC::Features::UI::ScreenEffectStack::Composition one = forward.Compose(1000);
        const KCDC::Features::UI::ScreenEffectStack::Composition two = backward.Compose(1000);
        EQUALS(ScreenNear(ScreenChannelValue(one, KCDC::Features::UI::ScreenChannel::Saturation), 0.2F), true);
        EQUALS(ScreenNear(ScreenChannelValue(one, KCDC::Features::UI::ScreenChannel::Cyan), 0.3F), true);
        EQUALS(ScreenNear(ScreenChannelValue(one, KCDC::Features::UI::ScreenChannel::Blur), 0.6F), true);
        for (std::size_t c = 0; c < KCDC::Features::UI::kScreenChannelCount; ++c) {
            EQUALS(ScreenNear(one.value[c], two.value[c]), true);
            EQUALS(one.held[c], two.held[c]);
        }
    });

    IT("fades in, holds, and fades out after its duration", {
        KCDC::Features::UI::ScreenEffectStack stack;
        KCDC::Features::UI::ScreenEffectStack::Timing timing;
        timing.fadeInMs   = 1000;
        timing.durationMs = 3000;
        timing.fadeOutMs  = 1000;
        const std::uint32_t handle = stack.Create("a", ScreenEffect("blur"), ScreenParam("blur", "amount", 0.8F), timing, 10000);

        EQUALS(ScreenNear(ScreenChannelValue(stack.Compose(10000), KCDC::Features::UI::ScreenChannel::Blur), 0.0F), true);
        EQUALS(ScreenNear(ScreenChannelValue(stack.Compose(10500), KCDC::Features::UI::ScreenChannel::Blur), 0.4F), true);
        EQUALS(ScreenNear(ScreenChannelValue(stack.Compose(12000), KCDC::Features::UI::ScreenChannel::Blur), 0.8F), true);
        EQUALS(ScreenNear(ScreenChannelValue(stack.Compose(13500), KCDC::Features::UI::ScreenChannel::Blur), 0.4F), true);
        EQUALS(stack.IsActive(handle, 13500), true);

        const KCDC::Features::UI::ScreenEffectStack::Composition gone = stack.Compose(14000);
        EQUALS(ScreenChannelHeld(gone, KCDC::Features::UI::ScreenChannel::Blur), false);
        EQUALS(stack.IsActive(handle, 14000), false);
        EQUALS(stack.Empty(), true);
    });

    IT("times a lapsed duration from when it ended, not from when it is noticed", {
        KCDC::Features::UI::ScreenEffectStack stack;
        KCDC::Features::UI::ScreenEffectStack::Timing timing;
        timing.durationMs = 1000;
        timing.fadeOutMs  = 1000;
        stack.Create("a", ScreenEffect("blur"), ScreenParam("blur", "amount", 1.0F), timing, 0);
        EQUALS(ScreenNear(ScreenChannelValue(stack.Compose(1500), KCDC::Features::UI::ScreenChannel::Blur), 0.5F), true);
    });

    IT("fades a grade towards neutral rather than towards zero", {
        KCDC::Features::UI::ScreenEffectStack stack;
        KCDC::Features::UI::ScreenEffectStack::Timing timing;
        timing.fadeInMs = 1000;
        stack.Create("a", ScreenEffect("colorGrade"), ScreenParam("colorGrade", "saturation", 0.0F), timing, 0);
        const KCDC::Features::UI::ScreenEffectStack::Composition half = stack.Compose(500);
        EQUALS(ScreenNear(ScreenChannelValue(half, KCDC::Features::UI::ScreenChannel::Saturation), 0.5F), true);
        EQUALS(ScreenNear(ScreenChannelValue(half, KCDC::Features::UI::ScreenChannel::Brightness), 1.0F), true);
        EQUALS(ScreenChannelHeld(half, KCDC::Features::UI::ScreenChannel::Brightness), true);
    });

    IT("retunes from wherever the parameter is, over the fade asked for", {
        KCDC::Features::UI::ScreenEffectStack stack;
        const std::uint32_t handle = stack.Create("a", ScreenEffect("screenBlood"), ScreenParam("screenBlood", "amount", 0.2F), {}, 0);
        EQUALS(stack.Retune("a", handle, ScreenParam("screenBlood", "amount", 1.0F), 1000, 1000), true);
        EQUALS(ScreenNear(ScreenChannelValue(stack.Compose(1500), KCDC::Features::UI::ScreenChannel::ScreenBlood), 0.6F), true);

        // Turned around half way: starts from 0.6, not from 0.2 or 1.0.
        EQUALS(stack.Retune("a", handle, ScreenParam("screenBlood", "amount", 0.0F), 1000, 1500), true);
        EQUALS(ScreenNear(ScreenChannelValue(stack.Compose(1500), KCDC::Features::UI::ScreenChannel::ScreenBlood), 0.6F), true);
        EQUALS(ScreenNear(ScreenChannelValue(stack.Compose(2000), KCDC::Features::UI::ScreenChannel::ScreenBlood), 0.3F), true);
    });

    IT("lets only the owning resource retune or remove an effect", {
        KCDC::Features::UI::ScreenEffectStack stack;
        const std::uint32_t handle = stack.Create("owner", ScreenEffect("ghosting"), ScreenParam("ghosting", "amount", 0.5F), {}, 0);
        EQUALS(stack.Retune("other", handle, ScreenParam("ghosting", "amount", 1.0F), 0, 10), false);
        EQUALS(stack.Remove("other", handle, false, 0, 10), false);
        EQUALS(stack.EffectOf("other", handle) == nullptr, true);
        EQUALS(stack.EffectOf("owner", handle) == &ScreenEffect("ghosting"), true);
        EQUALS(stack.IsActive(handle, 10), true);
        EQUALS(stack.Remove("owner", handle, false, 0, 10), true);
        EQUALS(stack.IsActive(handle, 10), false);
    });

    IT("continues a fade-out from where it is when removed again", {
        KCDC::Features::UI::ScreenEffectStack stack;
        const std::uint32_t handle = stack.Create("a", ScreenEffect("blur"), ScreenParam("blur", "amount", 1.0F), {}, 0);
        stack.Remove("a", handle, false, 1000, 1000);
        EQUALS(ScreenNear(ScreenChannelValue(stack.Compose(1500), KCDC::Features::UI::ScreenChannel::Blur), 0.5F), true);

        // A slower second fade starts from 0.5 rather than jumping back to 1.
        stack.Remove("a", handle, false, 2000, 1500);
        EQUALS(ScreenNear(ScreenChannelValue(stack.Compose(1500), KCDC::Features::UI::ScreenChannel::Blur), 0.5F), true);
        EQUALS(ScreenNear(ScreenChannelValue(stack.Compose(2000), KCDC::Features::UI::ScreenChannel::Blur), 0.25F), true);
    });

    IT("uses the fade-out an effect was created with when asked to", {
        KCDC::Features::UI::ScreenEffectStack stack;
        KCDC::Features::UI::ScreenEffectStack::Timing timing;
        timing.fadeOutMs           = 2000;
        const std::uint32_t handle = stack.Create("a", ScreenEffect("blur"), ScreenParam("blur", "amount", 1.0F), timing, 0);
        stack.Remove("a", handle, true, 0, 1000);
        EQUALS(ScreenNear(ScreenChannelValue(stack.Compose(2000), KCDC::Features::UI::ScreenChannel::Blur), 0.5F), true);
    });

    IT("drops one resource's effects and leaves the others composed", {
        KCDC::Features::UI::ScreenEffectStack stack;
        stack.Create("drink", ScreenEffect("colorGrade"), ScreenParam("colorGrade", "saturation", 0.5F), {}, 0);
        stack.Create("drink", ScreenEffect("blur"), ScreenParam("blur", "amount", 0.7F), {}, 0);
        const std::uint32_t wound = stack.Create("combat", ScreenEffect("colorGrade"), ScreenParam("colorGrade", "saturation", 0.8F), {}, 0);

        EQUALS(stack.RemoveAll("drink"), static_cast<std::size_t>(2));
        const KCDC::Features::UI::ScreenEffectStack::Composition left = stack.Compose(0);
        EQUALS(ScreenNear(ScreenChannelValue(left, KCDC::Features::UI::ScreenChannel::Saturation), 0.8F), true);
        EQUALS(ScreenChannelHeld(left, KCDC::Features::UI::ScreenChannel::Blur), false);
        EQUALS(stack.IsActive(wound, 0), true);
        EQUALS(stack.RemoveAll("drink"), static_cast<std::size_t>(0));
    });

    IT("refuses effects past the budget and hands out unique handles", {
        KCDC::Features::UI::ScreenEffectStack stack;
        std::uint32_t last = KCDC::Features::UI::ScreenEffectStack::kNoHandle;
        for (std::size_t i = 0; i < KCDC::Features::UI::ScreenEffectStack::kMaxEffects; ++i) {
            const std::uint32_t handle = stack.Create("a", ScreenEffect("blur"), {}, {}, 0);
            GREATER(handle, last);
            last = handle;
        }
        EQUALS(stack.Create("a", ScreenEffect("blur"), {}, {}, 0), KCDC::Features::UI::ScreenEffectStack::kNoHandle);
        stack.Clear();
        EQUALS(stack.Empty(), true);
        GREATER(stack.Create("a", ScreenEffect("blur"), {}, {}, 0), last);
    });
});
