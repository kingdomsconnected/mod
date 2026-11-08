#pragma once

#include "social_voice.h"
#include "social_voice_microphone.h"
#include "social_voice_settings.h"

#include "sdk/fmod/fmod_api.h"
#include "sdk/fmod/fmod_types.h"

#include <voice/client/i_voice_sink.h>
#include <voice/voice_config.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace KCDC::Features::Social {
    /**
     * The mod's grip on the game's audio, and the framework's voice output.
     *
     * The framework ships a complete voice path -- RakVoice, Opus, proximity
     * admission -- and ends it in a miniaudio device with a stereo mixer of
     * its own. Everything before that is game-agnostic and is kept; the device
     * is not, because it opens a second output stream beside the game's and
     * knows nothing about where the player's ears are.
     *
     * This replaces it with the game's FMOD: each audible talker gets a 3D
     * channel fed by a DSP, mixed by the same system, into the same output,
     * against the same listener as every other sound in the game.
     *
     * What lives here is what spans more than one talker -- the FMOD grip, the
     * channel group, the slots, the microphone. Everything about a single
     * talker is on `Voice`.
     *
     * Game thread throughout, except `DspRead` and `DspShouldProcess`.
     */
    class VoiceService final: public Framework::Voice::IVoiceSink {
      public:
        /**
         * Hands the audio back if nothing else did.
         *
         * DspRead runs on FMOD's mixer thread and walks `_voices` through
         * `gApplication`, which is still live while this object is being
         * destroyed -- so the DSPs have to be released before the slots
         * are. PreShutdown already resets, and Reset is idempotent; this is
         * for the teardown that does not get there. It does not leave the
         * profile manager: by then the game may be gone, and Shutdown is the
         * path that has to.
         */
        ~VoiceService() {
            Reset();
        }

        /** PreShutdown's: leaves the profile manager, then hands the audio back. */
        void Shutdown();

        /**
         * One tick on the game thread. Also where the FMOD grip is taken,
         * retried until the game's audio system is up -- an injected client
         * runs during CRT startup, long before it is.
         */
        void Update();

        /**
         * Hands the game's audio back and returns the framework to its own
         * device. Safe when nothing was ever taken.
         */
        void Reset();

        bool IsInstalled() const {
            return _installed;
        }

        const VoiceMicrophone &GetMicrophone() const {
            return _microphone;
        }

        /** The player's voice settings: profile, Sound page, and what they apply to. */
        VoiceSettings &GetSettings() {
            return _settings;
        }

        /**
         * A microphone in the corner of the screen while the local player is
         * sending, so voice activation never transmits unseen -- unless a
         * resource that draws its own hid it (`Voice.setIndicatorVisible`).
         * Game thread, inside the ImGui callback.
         */
        void RenderTransmitIndicator() const;

        /** Whether voice mixes through the game's master bus or straight out. */
        bool IsRoutedThroughMasterBus() const {
            return _busGroup != nullptr;
        }

        // --- Framework::Voice::IVoiceSink ---------------------------------

        void Submit(std::uint64_t speaker, const std::int16_t *mono, std::uint32_t samples) override;
        void ReleaseSpeaker(std::uint64_t speaker) override;
        void PublishWorld(const Framework::Voice::ListenerTransform &listener, const Framework::Voice::SpeakerPlacement *speakers, std::size_t count) override;

        /**
         * FMOD's mixer thread. Resolves the DSP back to the slot that owns it
         * and lets that slot render the block.
         *
         * Here rather than on `Voice` because the resolution is a question
         * about every slot at once, and because it must not call into FMOD to
         * ask a DSP who owns it.
         */
        static SDK::Fmod::FMOD_RESULT __stdcall DspRead(SDK::Fmod::FMOD_DSP_STATE *state, float *inbuffer, float *outbuffer, unsigned int length, int inchannels, int *outchannels);

        /** FMOD's mixer thread. Asks for every block, idle inputs or not. */
        static SDK::Fmod::FMOD_RESULT __stdcall DspShouldProcess(SDK::Fmod::FMOD_DSP_STATE *state, int inputsidle, unsigned int length, std::uint32_t inmask, int inchannels, std::int32_t speakermode);

      private:
        /** Takes the FMOD grip and installs the sink and source. */
        bool Install();

        /**
         * Puts the voice channel group under the game's master bus.
         *
         * `bus:/` is the one bus path FMOD Studio guarantees in every project,
         * so this needs no knowledge of Warhorse's bank layout. It buys the
         * player's master volume; failing it, the group stays directly under
         * the core master group and only the output device's volume applies.
         */
        void RouteThroughMasterBus();

        /**
         * The slot holding `speaker`, or nullptr. 0 never matches: it is the
         * framework's "no speaker" and also what a free slot holds, so a
         * lookup taking it literally would hand back an idle slot.
         */
        Voice *Find(std::uint64_t speaker);

        /** A free slot the mixer has finished draining, or nullptr. */
        Voice *Acquire(std::uint64_t speaker);

        /** Push-to-talk and the conditions that override it. */
        void UpdateTransmit();

        /** The voice tier key: each press steps whisper, normal, shout and round again. */
        void UpdateTier();

        /**
         * The player's volume, which lives on the framework's VoiceClient so
         * a script's Voice.setVolume and the settings page agree, onto both
         * channel groups.
         */
        void SyncVolume();

        /**
         * Moves every talker between the master-bus group and the background
         * group as the game gains and loses focus, when the player asked to
         * keep hearing voice in the background.
         */
        void UpdateBackground();

        /** Hands the framework the listener the game's own audio is using. */
        void PublishListener();

        std::array<Voice, Framework::Voice::kMaxAudibleTalkers> _voices;
        VoiceMicrophone _microphone;
        VoiceSettings _settings;

        SDK::Fmod::FMOD_SYSTEM *_system              = nullptr;
        SDK::Fmod::FMOD_STUDIO_SYSTEM *_studioSystem = nullptr;
        SDK::Fmod::FMOD_CHANNELGROUP *_group         = nullptr;

        // A second group under the core master channel group rather than the
        // Studio master bus, for while the game's lose-focus snapshot is
        // running. Talkers move to it with Channel::setChannelGroup, which
        // FMOD documents as moving a playing channel.
        SDK::Fmod::FMOD_CHANNELGROUP *_backgroundGroup = nullptr;
        bool _inBackground                             = false;

        // The master bus's channel group, held locked while it is our parent.
        SDK::Fmod::FMOD_STUDIO_BUS *_masterBus  = nullptr;
        SDK::Fmod::FMOD_CHANNELGROUP *_busGroup = nullptr;

        bool _installed      = false;
        float _appliedVolume = -1.0f;

        // Logged once rather than every tick while the audio system comes up.
        bool _waitingLogged = false;

        // A failure retrying cannot fix: the wrong FMOD, or an FMOD that
        // refused a channel group or a voice channel. Voice stays on the
        // framework's own device for the rest of the session, said once.
        bool _refused = false;
    };
} // namespace KCDC::Features::Social
