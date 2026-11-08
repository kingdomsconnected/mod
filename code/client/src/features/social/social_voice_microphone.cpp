#include "social_voice_microphone.h"

#include "sdk/wh/c_wh_fmod_studio_wrapper.h"

#include <logging/logger.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace KCDC::Features::Social {
    namespace {
        // FMOD's default recording device, which on Windows is the endpoint
        // the player picked in the sound control panel.
        constexpr int kDefaultRecordDriver = 0;

        // How FMOD's WASAPI output names the capture of an output device.
        constexpr const char *kLoopbackSuffix = "[loopback]";

        bool IsLoopback(const char *name) {
            const std::size_t length = std::strlen(name);
            const std::size_t suffix = std::strlen(kLoopbackSuffix);
            return length >= suffix && std::strcmp(name + length - suffix, kLoopbackSuffix) == 0;
        }

        // Long enough that a level-load hitch does not lap the buffer, short
        // enough that a lap costs a word rather than a sentence.
        constexpr std::uint32_t kRecordSeconds = 2;

        // Below the target's 24kHz Nyquist with room for the transition, and
        // far above anything speech carries.
        constexpr double kAntiAliasCutoffHz = 20000.0;

        // The Q values that make two cascaded biquads a fourth-order
        // Butterworth. Two sections at 0.7071 would instead build a peak at
        // the cutoff, audible as a whistle on sibilants.
        constexpr double kButterworthQ[] = {0.54119610, 1.30656296};

        // Bounds the stack, not the work: a longer block is split.
        constexpr std::uint32_t kConvertChunkFrames = 2048;

        constexpr double kPi = 3.14159265358979323846;

        // Cubic rather than linear, because linear interpolation is a
        // low-pass whose corner moves with the fractional phase -- on speech
        // that reads as a warble on held vowels.
        float CubicHermite(float y0, float y1, float y2, float y3, float t) {
            const float c0 = y1;
            const float c1 = 0.5f * (y2 - y0);
            const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * t + c2) * t + c1) * t + c0;
        }

        std::int16_t ToPcm16(float value) {
            return static_cast<std::int16_t>(std::lrintf(std::clamp(value, -32768.0f, 32767.0f)));
        }
    } // namespace

    void VoiceMicrophone::Biquad::Configure(double sampleRate, double cutoffHz, double q) {
        const double w0    = 2.0 * kPi * cutoffHz / sampleRate;
        const double cosW0 = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        const double a0    = 1.0 + alpha;

        _b0 = static_cast<float>(((1.0 - cosW0) * 0.5) / a0);
        _b1 = static_cast<float>((1.0 - cosW0) / a0);
        _b2 = _b0;
        _a1 = static_cast<float>((-2.0 * cosW0) / a0);
        _a2 = static_cast<float>((1.0 - alpha) / a0);

        Reset();
    }

    VoiceMicrophone::~VoiceMicrophone() {
        Stop();
    }

    bool VoiceMicrophone::Start() {
        if (IsRunning()) {
            return true;
        }

        SDK::Fmod::Api &api = SDK::Fmod::Api::Get();
        if (!api.IsResolved()) {
            return false;
        }

        SDK::wh::CWh_FmodStudioWrapper *wrapper = SDK::wh::CWh_FmodStudioWrapper::GetInstance();
        if (wrapper == nullptr || !wrapper->IsReady()) {
            return false;
        }

        _system = wrapper->GetCoreSystem();

        int driverCount    = 0;
        int connectedCount = 0;
        if (api.System_GetRecordNumDrivers(_system, &driverCount, &connectedCount) != SDK::Fmod::FMOD_OK || connectedCount <= 0) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Voice: FMOD reports no connected recording device; continuing listen-only");
            _system = nullptr;
            return false;
        }

        _driver = ResolveDriver();

        char name[256] = {};
        int rate       = 0;
        int channels   = 0;
        if (api.System_GetRecordDriverInfo(_system, _driver, name, static_cast<int>(sizeof(name)), nullptr, &rate, nullptr, &channels, nullptr) != SDK::Fmod::FMOD_OK || rate <= 0 || channels <= 0) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Voice: could not read the recording device's format; continuing listen-only");
            _system = nullptr;
            return false;
        }

        _rate         = rate;
        _channels     = channels;
        _recordFrames = static_cast<std::uint32_t>(rate) * kRecordSeconds;

        // No read callback: FMOD fills this itself, and the sound is storage
        // the record device writes into and Sound::lock hands back.
        // FMOD_CREATESAMPLE is what System::recordStart documents as required.
        SDK::Fmod::FMOD_CREATESOUNDEXINFO exinfo;
        exinfo.cbsize           = static_cast<std::int32_t>(sizeof(exinfo));
        exinfo.numchannels      = channels;
        exinfo.defaultfrequency = rate;
        exinfo.format           = SDK::Fmod::FMOD_SOUND_FORMAT_PCM16;
        exinfo.length           = _recordFrames * static_cast<std::uint32_t>(channels) * sizeof(std::int16_t);

        const std::uint32_t mode = SDK::Fmod::FMOD_OPENUSER | SDK::Fmod::FMOD_CREATESAMPLE | SDK::Fmod::FMOD_LOOP_NORMAL;
        if (api.System_CreateSound(_system, nullptr, mode, &exinfo, &_sound) != SDK::Fmod::FMOD_OK) {
            _sound  = nullptr;
            _system = nullptr;
            return false;
        }

        if (api.System_RecordStart(_system, _driver, _sound, 1) != SDK::Fmod::FMOD_OK) {
            api.Sound_Release(_sound);
            _sound  = nullptr;
            _system = nullptr;
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Voice: FMOD refused to start recording on '{}'; continuing listen-only", name);
            return false;
        }

        _ratio       = static_cast<double>(rate) / static_cast<double>(Framework::Voice::kSampleRate);
        _passthrough = rate == static_cast<std::int32_t>(Framework::Voice::kSampleRate) && channels == static_cast<std::int32_t>(Framework::Voice::kChannels);
        _phase       = 0.0;
        _readCursor  = 0;
        _overruns    = 0;
        _history.fill(0.0f);
        _out.Clear();

        for (std::size_t i = 0; i < _antiAlias.size(); i++) {
            _antiAlias[i].Configure(rate, kAntiAliasCutoffHz, kButterworthQ[i]);
        }

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Voice: recording through the game's FMOD on '{}' at {}Hz, {} channel(s){}", name, rate, channels, _passthrough ? "" : " (converted)");
        return true;
    }

    void VoiceMicrophone::Stop() {
        if (_sound == nullptr) {
            return;
        }

        SDK::Fmod::Api &api = SDK::Fmod::Api::Get();

        // Stopped before the buffer it writes into is released, or FMOD is
        // left recording into freed memory.
        api.System_RecordStop(_system, _driver);
        api.Sound_Release(_sound);

        _sound    = nullptr;
        _system   = nullptr;
        _rate     = 0;
        _channels = 0;
        _out.Clear();
    }

    std::vector<std::string> VoiceMicrophone::ListDevices() const {
        std::vector<std::string> names;

        SDK::Fmod::Api &api = SDK::Fmod::Api::Get();
        if (!api.IsResolved()) {
            return names;
        }

        SDK::wh::CWh_FmodStudioWrapper *wrapper = SDK::wh::CWh_FmodStudioWrapper::GetInstance();
        if (wrapper == nullptr || !wrapper->IsReady()) {
            return names;
        }

        SDK::Fmod::FMOD_SYSTEM *system = wrapper->GetCoreSystem();
        int driverCount                = 0;
        int connectedCount             = 0;
        if (api.System_GetRecordNumDrivers(system, &driverCount, &connectedCount) != SDK::Fmod::FMOD_OK) {
            return names;
        }

        for (int driver = 0; driver < driverCount; driver++) {
            char name[256]     = {};
            unsigned int state = 0;
            if (api.System_GetRecordDriverInfo(system, driver, name, static_cast<int>(sizeof(name)), nullptr, nullptr, nullptr, nullptr, &state) != SDK::Fmod::FMOD_OK) {
                continue;
            }

            // Unplugged endpoints stay in the list; offering one is offering silence.
            if ((state & SDK::Fmod::FMOD_DRIVER_STATE_CONNECTED) == 0 || IsLoopback(name)) {
                continue;
            }

            names.emplace_back(name);
        }

        return names;
    }

    int VoiceMicrophone::ResolveDriver() const {
        if (_deviceName.empty()) {
            return kDefaultRecordDriver;
        }

        SDK::Fmod::Api &api = SDK::Fmod::Api::Get();
        int driverCount     = 0;
        int connectedCount  = 0;
        if (api.System_GetRecordNumDrivers(_system, &driverCount, &connectedCount) == SDK::Fmod::FMOD_OK) {
            for (int driver = 0; driver < driverCount; driver++) {
                char name[256]     = {};
                unsigned int state = 0;
                if (api.System_GetRecordDriverInfo(_system, driver, name, static_cast<int>(sizeof(name)), nullptr, nullptr, nullptr, nullptr, &state) == SDK::Fmod::FMOD_OK && (state & SDK::Fmod::FMOD_DRIVER_STATE_CONNECTED) != 0 && _deviceName == name) {
                    return driver;
                }
            }
        }

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Voice: microphone '{}' is not connected; recording from the default device", _deviceName);
        return kDefaultRecordDriver;
    }

    bool VoiceMicrophone::ReadFrame(std::int16_t *out) {
        if (!IsRunning()) {
            return false;
        }

        // Pulled on demand rather than on a tick of its own, so a caller that
        // drains in a loop cannot outrun the record cursor.
        if (_out.Available() < Framework::Voice::kFrameSamples) {
            Pump();
        }

        return _out.Pop(out, Framework::Voice::kFrameSamples);
    }

    void VoiceMicrophone::Pump() {
        SDK::Fmod::Api &api = SDK::Fmod::Api::Get();

        unsigned int position = 0;
        if (api.System_GetRecordPosition(_system, _driver, &position) != SDK::Fmod::FMOD_OK) {
            return;
        }

        std::uint32_t available = (position + _recordFrames - _readCursor) % _recordFrames;
        if (available == 0) {
            return;
        }

        // What is read has to fit the ring that carries it, and after a stall
        // the record buffer holds several times that. Skipping the cursor
        // forward keeps the words just spoken; pushing the whole block would
        // keep the wrong end of it, since the ring fills oldest-first.
        const std::size_t freeSamples      = kRingSamples - 1 - _out.Available();
        const std::uint32_t maxInputFrames = static_cast<std::uint32_t>(static_cast<double>(freeSamples) * _ratio);
        if (available > maxInputFrames) {
            _overruns++;
            _readCursor = (position + _recordFrames - maxInputFrames) % _recordFrames;
            available   = maxInputFrames;
        }

        if (available == 0) {
            return;
        }

        void *block1         = nullptr;
        void *block2         = nullptr;
        unsigned int length1 = 0;
        unsigned int length2 = 0;

        // Sound::lock takes byte offsets and returns byte lengths.
        const unsigned int bytesPerFrame = static_cast<unsigned int>(_channels) * sizeof(std::int16_t);
        if (api.Sound_Lock(_sound, _readCursor * bytesPerFrame, available * bytesPerFrame, &block1, &block2, &length1, &length2) != SDK::Fmod::FMOD_OK) {
            return;
        }

        // Two blocks when the read wraps the end of the ring.
        if (block1 != nullptr && length1 != 0) {
            Consume(static_cast<const std::int16_t *>(block1), length1 / bytesPerFrame);
        }

        if (block2 != nullptr && length2 != 0) {
            Consume(static_cast<const std::int16_t *>(block2), length2 / bytesPerFrame);
        }

        api.Sound_Unlock(_sound, block1, block2, length1, length2);

        _readCursor = (_readCursor + available) % _recordFrames;
    }

    void VoiceMicrophone::Consume(const std::int16_t *interleaved, std::uint32_t frames) {
        // The endpoint already speaks the voice format, which is the usual
        // case on Windows: nothing to convert, and no interpolation error to
        // introduce by pretending otherwise.
        if (_passthrough) {
            _out.Push(interleaved, frames);
            return;
        }

        const bool filtering     = _rate > static_cast<std::int32_t>(Framework::Voice::kSampleRate);
        const float channelScale = 1.0f / static_cast<float>(_channels);

        std::array<std::int16_t, kConvertChunkFrames> converted {};
        std::uint32_t produced = 0;

        for (std::uint32_t frame = 0; frame < frames; frame++) {
            // Averaged rather than taking the first channel: a headset that
            // carries the voice on one side only would otherwise be silent
            // half the time, depending on which side that is.
            float sample = 0.0f;
            for (std::int32_t channel = 0; channel < _channels; channel++) {
                sample += static_cast<float>(interleaved[frame * _channels + channel]);
            }
            sample *= channelScale;

            if (filtering) {
                for (Biquad &section : _antiAlias) {
                    sample = section.Process(sample);
                }
            }

            _history[0] = _history[1];
            _history[1] = _history[2];
            _history[2] = _history[3];
            _history[3] = sample;

            // One output per _ratio inputs: the loop runs several times when
            // upsampling and skips inputs when decimating.
            while (_phase < 1.0) {
                converted[produced++] = ToPcm16(CubicHermite(_history[0], _history[1], _history[2], _history[3], static_cast<float>(_phase)));
                _phase += _ratio;

                if (produced == converted.size()) {
                    _out.Push(converted.data(), produced);
                    produced = 0;
                }
            }

            _phase -= 1.0;
        }

        if (produced != 0) {
            _out.Push(converted.data(), produced);
        }
    }
} // namespace KCDC::Features::Social
