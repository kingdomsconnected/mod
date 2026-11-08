#pragma once

#include "features/social/social_voice_filter.h"
#include "human_soul_ut.h"

#include <cmath>

namespace VoiceFilterTests {
    inline float Gain(float frequency) {
        KCDC::Features::Social::VoiceFilter filter;
        filter.BeginBlock(1, true);
        double inputEnergy  = 0.0;
        double outputEnergy = 0.0;
        for (int i = 0; i < 48000; ++i) {
            const float sample = 0.5F * std::sin(6.283185307F * frequency * static_cast<float>(i) / 48000.0F);
            const float output = filter.Process(sample);
            if (i >= 4800) {
                inputEnergy += sample * sample;
                outputEnergy += output * output;
            }
        }
        return static_cast<float>(std::sqrt(outputEnergy / inputEnergy));
    }
} // namespace VoiceFilterTests

MODULE(voice_visor_filter, {
    IT("passes an open visor through exactly", {
        KCDC::Features::Social::VoiceFilter filter;
        filter.BeginBlock(1, false);
        for (float sample : {0.0F, 1.0F, -1.0F, 0.37F, -0.1F, 0.0F}) {
            EQUALS(filter.Process(sample), sample);
        }
    });

    IT("retains low speech frequencies and attenuates high frequencies", {
        const float low  = VoiceFilterTests::Gain(300.0F);
        const float mid  = VoiceFilterTests::Gain(3000.0F);
        const float high = VoiceFilterTests::Gain(8000.0F);
        GREATER(low, 0.5F);
        LESSER(low, 0.6F);
        // Require strong attenuation of upper speech frequencies while
        // preserving the low end, independent of the overall gain.
        LESSER(mid / low, 0.13F);
        LESSER(high / low, 0.025F);
    });

    IT("fades visor changes and mid-fade reversals without a gain step", {
        KCDC::Features::Social::VoiceFilter filter;
        filter.BeginBlock(1, false);
        for (int i = 0; i < 480; ++i) {
            filter.Process(1.0F);
        }
        float previous = 1.0F;
        filter.BeginBlock(1, true);
        for (int i = 0; i < 2400; ++i) {
            const float value = filter.Process(1.0F);
            LESSER(std::abs(value - previous), 0.001F);
            previous = value;
        }
        LESSER(previous, 0.83F);
        GREATER(previous, 0.77F);
        filter.BeginBlock(1, false);
        for (int i = 0; i < 5000; ++i) {
            const float value = filter.Process(1.0F);
            LESSER(std::abs(value - previous), 0.001F);
            previous = value;
        }
        EQUALS(previous, 1.0F);
    });

    IT("clears old speech and muffling when a slot changes speakers", {
        KCDC::Features::Social::VoiceFilter filter;
        filter.BeginBlock(1, true);
        for (int i = 0; i < 480; ++i) {
            filter.Process(1.0F);
        }
        filter.BeginBlock(2, false);
        EQUALS(filter.Process(0.0F), 0.0F);
        EQUALS(filter.Process(0.5F), 0.5F);
        filter.BeginBlock(3, true);
        EQUALS(filter.Process(0.0F), 0.0F);
        filter.Process(1.0F);
        filter.BeginBlock(0, false);
        filter.BeginBlock(3, true);
        EQUALS(filter.Process(0.0F), 0.0F);
    });

    IT("keeps simultaneous speakers independent and full-scale input bounded", {
        KCDC::Features::Social::VoiceFilter closed;
        KCDC::Features::Social::VoiceFilter open;
        closed.BeginBlock(1, true);
        open.BeginBlock(2, false);
        for (int i = 0; i < 12000; ++i) {
            const float sample = (i % 2) != 0 ? -1.0F : 1.0F;
            const float output = closed.Process(sample);
            EQUALS(std::isfinite(output), true);
            LESSER(std::abs(output), 1.001F);
            EQUALS(open.Process(sample), sample);
        }
    });

    IT("converges to exact silence after speech", {
        KCDC::Features::Social::VoiceFilter filter;
        filter.BeginBlock(1, true);
        filter.Process(1.0F);
        float sample = 1.0F;
        for (int i = 0; i < 48000; ++i) {
            sample = filter.Process(0.0F);
        }
        EQUALS(sample, 0.0F);
    });

    IT("replicates closed state to a late listener and reopening without changing equipment", {
        KCDC::Shared::Entities::HumanEntity source;
        source.soul        = HumanSoulTests::Sample();
        source.visorClosed = true;
        KCDC::Shared::Entities::HumanEntity receiver;
        HumanSoulTests::Channel channel;
        EQUALS(channel.Send(source, receiver, true), true);
        EQUALS(receiver.visorClosed, true);
        EQUALS(channel.Send(source, receiver), false);
        source.visorClosed = false;
        EQUALS(channel.Send(source, receiver), true);
        EQUALS(receiver.visorClosed, false);
        source.visorClosed = true;
        EQUALS(channel.Send(source, receiver), true);
        EQUALS(receiver.visorClosed, true);
        KCDC::Shared::Entities::HumanEntity late;
        HumanSoulTests::Channel lateChannel;
        EQUALS(lateChannel.Send(source, late, true), true);
        EQUALS(late.visorClosed, true);
    });
});
