#include "social_voice.h"

#include <utils/time.h>

#include <algorithm>
#include <cstring>

namespace KCDC::Features::Social {
    namespace {
        // The silent source the channel plays, in PCM samples: one voice
        // frame, looped. Its content never reaches the mix -- the DSP
        // overwrites every block -- so its only job is to keep a 3D channel
        // running at the voice rate.
        constexpr std::uint32_t kSourceSamples = Framework::Voice::kFrameSamples;
        constexpr std::uint32_t kSourceBytes   = kSourceSamples * Framework::Voice::kChannels * sizeof(std::int16_t);

        // The DSP runs at the mixer's rate and the playout buffer holds 48kHz.
        // They agree because the game never calls System::setSoftwareFormat
        // (CWh_FmodStudioWrapper::InitializeFmodStudio), which leaves FMOD at
        // its 48kHz default -- and a 48kHz source on a 48kHz mixer is not
        // resampled on its way into the DSP either.
        static_assert(Framework::Voice::kSampleRate == 48000, "the voice DSP renders at FMOD's default 48kHz mixer rate");

        // How much the DSP pulls from the playout buffer at a time. The mixer
        // asks for its whole block -- 1024 samples in KCD2 -- and a smaller
        // chunk only means another pull.
        constexpr std::uint32_t kRenderChunkSamples = Framework::Voice::kFrameSamples;

        constexpr float kPcmToFloat = 1.0f / 32768.0f;

        // Highest priority. FMOD virtualizes the least important channels once
        // the software channel count is reached, and a virtual channel stops
        // running its DSPs -- the ring would fill, the drift trim would throw
        // the backlog away, and a player would go unheard.
        constexpr int kChannelPriority = 0;

        // CryEngine world space to FMOD's: right-handed Z-up to left-handed
        // Y-up, the same swap the game applies to the listener these are mixed
        // against. Getting it backwards puts every talker on the wrong side of
        // the player, silently.
        SDK::Fmod::FMOD_VECTOR ToFmod(const glm::vec3 &value) {
            return SDK::Fmod::FMOD_VECTOR {value.x, value.z, value.y};
        }

        // The game zeroes the listener's velocity, so doppler is off engine
        // wide; voices state the same rather than letting FMOD infer one.
        constexpr SDK::Fmod::FMOD_VECTOR kNoVelocity {0.0f, 0.0f, 0.0f};
    } // namespace

    Voice::~Voice() {
        Close();
    }

    bool Voice::Open(SDK::Fmod::FMOD_SYSTEM *system, SDK::Fmod::FMOD_CHANNELGROUP *group, SDK::Fmod::FMOD_DSP_READ_CALLBACK read, SDK::Fmod::FMOD_DSP_SHOULDIPROCESS_CALLBACK shouldProcess) {
        if (IsOpen()) {
            return true;
        }

        SDK::Fmod::Api &api = SDK::Fmod::Api::Get();

        SDK::Fmod::FMOD_CREATESOUNDEXINFO exinfo;
        exinfo.cbsize           = static_cast<std::int32_t>(sizeof(exinfo));
        exinfo.numchannels      = static_cast<std::int32_t>(Framework::Voice::kChannels);
        exinfo.defaultfrequency = static_cast<std::int32_t>(Framework::Voice::kSampleRate);
        exinfo.format           = SDK::Fmod::FMOD_SOUND_FORMAT_PCM16;
        exinfo.length           = kSourceBytes;
        exinfo.channelorder     = SDK::Fmod::FMOD_CHANNELORDER_DEFAULT;

        // A sample, not a stream: nothing feeds it after this, so no stream
        // thread is involved at all. LINEARSQUAREROLLOFF rather than the
        // inverse default: it reaches exactly zero at the max distance, so a
        // talker fades out where the server stops relaying instead of cutting
        // off while still audible. It also ignores the rolloff scale, so
        // whatever the game passed to System::set3DSettings cannot bend it.
        const std::uint32_t mode = SDK::Fmod::FMOD_OPENUSER | SDK::Fmod::FMOD_CREATESAMPLE | SDK::Fmod::FMOD_LOOP_NORMAL | SDK::Fmod::FMOD_3D | SDK::Fmod::FMOD_3D_WORLDRELATIVE | SDK::Fmod::FMOD_3D_LINEARSQUAREROLLOFF;

        if (api.System_CreateSound(system, nullptr, mode, &exinfo, &_sound) != SDK::Fmod::FMOD_OK) {
            _sound = nullptr;
            return false;
        }

        // A user sample's memory is not promised to start zeroed.
        void *block1         = nullptr;
        void *block2         = nullptr;
        unsigned int length1 = 0;
        unsigned int length2 = 0;
        if (api.Sound_Lock(_sound, 0, kSourceBytes, &block1, &block2, &length1, &length2) != SDK::Fmod::FMOD_OK) {
            Close();
            return false;
        }
        std::memset(block1, 0, length1);
        if (block2 != nullptr) {
            std::memset(block2, 0, length2);
        }
        api.Sound_Unlock(_sound, block1, block2, length1, length2);

        _description                  = SDK::Fmod::FMOD_DSP_DESCRIPTION {};
        _description.numinputbuffers  = 1;
        _description.numoutputbuffers = 1;
        _description.read             = read;
        _description.shouldiprocess   = shouldProcess;

        // Kept local until the slot is built: the mixer runs the DSP the
        // moment the channel is unpaused, and a slot that published itself
        // first would be handed a block before its channel is set up.
        SDK::Fmod::FMOD_DSP *dsp = nullptr;
        if (api.System_CreateDSP(system, &_description, &dsp) != SDK::Fmod::FMOD_OK) {
            Close();
            return false;
        }

        // Paused so the channel is configured before it is mixed, and silent
        // so an unplaced slot cannot be heard at the world origin.
        if (api.System_PlaySound(system, _sound, group, 1, &_channel) != SDK::Fmod::FMOD_OK) {
            _channel = nullptr;
            api.DSP_Release(dsp);
            Close();
            return false;
        }

        // At the tail, the input end of the chain: ahead of the fader, so the
        // channel's volume and 3D panning apply to the voice it produces.
        if (api.Channel_AddDSP(_channel, SDK::Fmod::FMOD_CHANNELCONTROL_DSP_TAIL, dsp) != SDK::Fmod::FMOD_OK) {
            api.DSP_Release(dsp);
            Close();
            return false;
        }

        api.Channel_SetPriority(_channel, kChannelPriority);
        api.Channel_SetVolume(_channel, 0.0f);

        _audible = false;
        _dsp.store(dsp, std::memory_order_release);
        api.Channel_SetPaused(_channel, 0);
        return true;
    }

    void Voice::Close() {
        SDK::Fmod::Api &api = SDK::Fmod::Api::Get();

        // Retracted first: from here a block for this DSP no longer resolves
        // to a slot and renders silence instead.
        SDK::Fmod::FMOD_DSP *dsp = _dsp.exchange(nullptr, std::memory_order_acq_rel);

        // Then off the channel. Changing the DSP graph waits for the mix in
        // progress, so once this returns the read callback cannot be running
        // for it, and the ring is this thread's again.
        if (_channel != nullptr) {
            if (dsp != nullptr) {
                api.Channel_RemoveDSP(_channel, dsp);
            }
            api.Channel_Stop(_channel);
            _channel = nullptr;
        }

        if (dsp != nullptr) {
            api.DSP_Release(dsp);
        }

        if (_sound != nullptr) {
            api.Sound_Release(_sound);
            _sound = nullptr;
        }

        _speaker.store(0, std::memory_order_relaxed);
        _audio.Clear();
        _muffled.store(false, std::memory_order_relaxed);
        _filter  = VoiceFilter {};
        _audible = false;
    }

    void Voice::Bind(std::uint64_t speaker) {
        _muffled.store(false, std::memory_order_relaxed);
        // A new talker's connection is not the last one's. Still this
        // thread's alone: the mixer only reads the depth.
        _audio.ResetEstimate();

        // Published last: until this lands the mixer treats the slot as free
        // and throws away anything already pushed into it.
        _speaker.store(speaker, std::memory_order_release);
    }

    void Voice::Release() {
        // Cleared first, and the mixer drains the remainder -- clearing the
        // ring from here would race a running consumer.
        _speaker.store(0, std::memory_order_release);
        Mute();
    }

    void Voice::Submit(const std::int16_t *mono, std::uint32_t samples) {
        // A full ring means the mixer has not consumed for a third of a
        // second, which is a stopped or virtual channel rather than a slow
        // one. Dropping beats queueing audio that would burst out later. The
        // arrival time is what sizes this talker's buffer.
        _audio.Push(mono, samples, Framework::Utils::Time::GetTime());
    }

    void Voice::Place(const glm::vec3 &position, float range, float fullVolumeFraction, bool muffled) {
        if (_channel == nullptr) {
            return;
        }

        SDK::Fmod::Api &api = SDK::Fmod::Api::Get();

        _muffled.store(muffled, std::memory_order_relaxed);
        const SDK::Fmod::FMOD_VECTOR fmodPosition = ToFmod(position);
        api.Channel_Set3DAttributes(_channel, &fmodPosition, &kNoVelocity);

        // The near field as a fraction of the audible radius, so the curve
        // keeps its shape whatever range the server hands down.
        const float minDistance = std::max(range * fullVolumeFraction, 0.01f);
        api.Channel_Set3DMinMaxDistance(_channel, minDistance, std::max(range, minDistance));

        if (!_audible) {
            api.Channel_SetVolume(_channel, 1.0f);
            _audible = true;
        }
    }

    void Voice::SetGroup(SDK::Fmod::FMOD_CHANNELGROUP *group) {
        if (_channel == nullptr || group == nullptr) {
            return;
        }

        SDK::Fmod::Api::Get().Channel_SetChannelGroup(_channel, group);
    }

    void Voice::Mute() {
        if (_channel == nullptr || !_audible) {
            return;
        }

        SDK::Fmod::Api::Get().Channel_SetVolume(_channel, 0.0f);
        _audible = false;
    }

    void Voice::Render(float *out, std::uint32_t length, int channels) {
        const std::size_t total = static_cast<std::size_t>(length) * static_cast<std::size_t>(channels);

        const std::uint64_t speaker = _speaker.load(std::memory_order_acquire);
        _filter.BeginBlock(speaker, _muffled.load(std::memory_order_relaxed));

        // Free slot: drop whatever the previous speaker left, remainder
        // included, so the slot is empty before it is bound again.
        if (speaker == 0) {
            _audio.Discard();
            std::fill_n(out, total, 0.0f);
            return;
        }

        // The playout buffer writes the entire request, including a smooth
        // tail on underrun; do not overwrite that tail with hard silence. The
        // clock is the one Submit stamps arrivals with.
        const std::int64_t nowMs = Framework::Utils::Time::GetTime();
        std::int16_t pcm[kRenderChunkSamples];

        std::uint32_t offset = 0;
        while (offset < length) {
            const std::uint32_t chunk = std::min(length - offset, kRenderChunkSamples);
            _audio.Pull(pcm, chunk, nowMs);

            // Mono into however many channels the chain carries here.
            float *frames = out + static_cast<std::size_t>(offset) * static_cast<std::size_t>(channels);
            for (std::uint32_t i = 0; i < chunk; i++) {
                const float sample = _filter.Process(static_cast<float>(pcm[i]) * kPcmToFloat);
                for (int channel = 0; channel < channels; channel++) {
                    frames[static_cast<std::size_t>(i) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(channel)] = sample;
                }
            }

            offset += chunk;
        }
    }
} // namespace KCDC::Features::Social
