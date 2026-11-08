#include <utils/safe_win32.h>

#include "social_voice_service.h"

#include "core/application.h"
#include "core/input/native_keybinds.h"
#include "features/player/player.h"
#include "features/ui/ui_hud_elements.h"
#include "common/module_support.h"
#include "sdk/wh/c_wh_fmod_studio_wrapper.h"

#include <core_modules.h>
#include <external/imgui/widgets/nametag.h>
#include <imgui.h>
#include <logging/logger.h>
#include <voice/client/voice_client.h>

#include <algorithm>

namespace KCDC::Features::Social {
    namespace {
        // What FMOD reports for the groups in its profiler and channel lists.
        constexpr const char *kChannelGroupName           = "KCDC Voice";
        constexpr const char *kBackgroundChannelGroupName = "KCDC Voice (background)";

        // The transmit indicator's place and size, as fractions of the screen:
        // low on the left, clear of the HUD's own corners.
        constexpr float kIndicatorX      = 0.03f;
        constexpr float kIndicatorY      = 0.78f;
        constexpr float kIndicatorHeight = 0.035f;

        // Every Studio project has a master bus at exactly this path, which is
        // what makes routing into it independent of Warhorse's banks. A
        // dedicated dialogue bus would sit better -- voice would follow the
        // game's speech volume rather than its master -- but that path is bank
        // data, not something FMOD guarantees.
        constexpr const char *kMasterBusPath = "bus:/";

        // Major.minor of the FMOD release every layout here describes. A game
        // update to 2.03 resizes FMOD_CREATESOUNDEXINFO, and the failure would
        // otherwise be FMOD reading fields the mod never wrote.
        constexpr std::uint32_t kSupportedVersionMask = 0xFFFFFF00;

        // FMOD's space back to CryEngine's -- the same y/z swap the game
        // applies on the way out, which is its own inverse.
        glm::vec3 FromFmod(const SDK::Fmod::FMOD_VECTOR &value) {
            return glm::vec3(value.x, value.z, value.y);
        }
    } // namespace

    void VoiceService::Update() {
        // First, and outside the install gate: the profile and the settings
        // page do not wait for the game's audio to come up.
        _settings.Update();

        if (Framework::CoreModules::GetVoiceClient() == nullptr) {
            return;
        }

        if (!_installed) {
            Install();
        }

        // Input, tier changes and placement also drive the fallback device.
        // FMOD refusing installation must not disable H and Y along with it.
        UpdateTransmit();
        UpdateTier();
        PublishListener();

        if (_installed) {
            SyncVolume();
            UpdateBackground();
        }
    }

    void VoiceService::Shutdown() {
        _settings.Shutdown();
        Reset();
    }

    bool VoiceService::Install() {
        if (_refused) {
            return false;
        }

        SDK::Fmod::Api &api = SDK::Fmod::Api::Get();
        if (!api.Resolve()) {
            return false;
        }

        SDK::wh::CWh_FmodStudioWrapper *wrapper = SDK::wh::CWh_FmodStudioWrapper::GetInstance();
        if (wrapper == nullptr || !wrapper->IsReady()) {
            if (!_waitingLogged) {
                _waitingLogged = true;
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Voice: waiting for the game's audio system before taking FMOD over");
            }

            return false;
        }

        _system       = wrapper->GetCoreSystem();
        _studioSystem = wrapper->GetStudioSystem();

        unsigned int version = 0;
        if (api.System_GetVersion(_system, &version) != SDK::Fmod::FMOD_OK || (version & kSupportedVersionMask) != (SDK::Fmod::kFmodVersion & kSupportedVersionMask)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Voice: the game ships FMOD {:#x}, but the mod's structures describe {:#x}; leaving voice on the framework's own device", version, SDK::Fmod::kFmodVersion);
            _refused = true;
            Reset();
            return false;
        }

        if (api.System_CreateChannelGroup(_system, kChannelGroupName, &_group) != SDK::Fmod::FMOD_OK) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Voice: FMOD refused a channel group; leaving voice on the framework's own device");
            _refused = true;
            Reset();
            return false;
        }

        // Created under the core master and left there: a new group outputs to
        // the master channel group until re-parented.
        if (api.System_CreateChannelGroup(_system, kBackgroundChannelGroupName, &_backgroundGroup) != SDK::Fmod::FMOD_OK) {
            _backgroundGroup = nullptr;
        }

        RouteThroughMasterBus();

        for (Voice &slot : _voices) {
            if (!slot.Open(_system, _group, &VoiceService::DspRead, &VoiceService::DspShouldProcess)) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Voice: FMOD refused a voice channel; leaving voice on the framework's own device");
                _refused = true;
                Reset();
                return false;
            }
        }

        // Only now, with every slot able to take audio: installed earlier, a
        // frame of speech would arrive with nowhere to play it.
        Framework::Voice::VoiceClient *voice = Framework::CoreModules::GetVoiceClient();
        voice->SetSink(this);
        voice->SetSource(&_microphone);

        // Again, now the microphone that will honour the chosen device is the
        // one installed.
        _settings.Apply();

        _installed     = true;
        _appliedVolume = -1.0f;
        _inBackground  = false;
        SyncVolume();
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Voice: playing through the game's FMOD, {} talkers, {}", _voices.size(), IsRoutedThroughMasterBus() ? "under the Studio master bus" : "under the core master channel group");
        return true;
    }

    void VoiceService::RouteThroughMasterBus() {
        SDK::Fmod::Api &api = SDK::Fmod::Api::Get();

        if (api.Studio_System_GetBus(_studioSystem, kMasterBusPath, &_masterBus) != SDK::Fmod::FMOD_OK) {
            _masterBus = nullptr;
            return;
        }

        // A bus only has a channel group while something is playing on it, so
        // it has to be pinned into existence first. The lock is a queued
        // command; flushing is what makes the group readable on the next line.
        if (api.Studio_Bus_LockChannelGroup(_masterBus) != SDK::Fmod::FMOD_OK) {
            _masterBus = nullptr;
            return;
        }

        api.Studio_System_FlushCommands(_studioSystem);

        if (api.Studio_Bus_GetChannelGroup(_masterBus, &_busGroup) != SDK::Fmod::FMOD_OK || _busGroup == nullptr) {
            api.Studio_Bus_UnlockChannelGroup(_masterBus);
            _masterBus = nullptr;
            _busGroup  = nullptr;
            return;
        }

        // Propagating the parent's DSP clock is FMOD's default for a sub-mix.
        if (api.ChannelGroup_AddGroup(_busGroup, _group, 1, nullptr) != SDK::Fmod::FMOD_OK) {
            api.Studio_Bus_UnlockChannelGroup(_masterBus);
            _masterBus = nullptr;
            _busGroup  = nullptr;
        }
    }

    void VoiceService::Reset() {
        // The framework goes back to its own device before anything it might
        // still be submitting into is torn down.
        Framework::Voice::VoiceClient *voice = Framework::CoreModules::GetVoiceClient();
        if (voice != nullptr) {
            voice->SetSink(nullptr);
            voice->SetSource(nullptr);
        }

        for (Voice &slot : _voices) {
            slot.Close();
        }

        _microphone.Stop();

        SDK::Fmod::Api &api = SDK::Fmod::Api::Get();
        if (api.IsResolved()) {
            // Unlocked before the group is released: while the bus holds our
            // group as a child, FMOD may keep it alive.
            if (_masterBus != nullptr) {
                api.Studio_Bus_UnlockChannelGroup(_masterBus);
            }

            if (_group != nullptr) {
                api.ChannelGroup_Release(_group);
            }

            if (_backgroundGroup != nullptr) {
                api.ChannelGroup_Release(_backgroundGroup);
            }
        }

        _masterBus       = nullptr;
        _busGroup        = nullptr;
        _group           = nullptr;
        _backgroundGroup = nullptr;
        _inBackground    = false;
        _system          = nullptr;
        _studioSystem    = nullptr;
        _installed       = false;
    }

    void VoiceService::SyncVolume() {
        const float volume = Framework::CoreModules::GetVoiceClient()->GetMasterVolume();
        if (volume == _appliedVolume) {
            return;
        }
        _appliedVolume = volume;

        SDK::Fmod::Api &api = SDK::Fmod::Api::Get();
        api.ChannelGroup_SetVolume(_group, volume);
        if (_backgroundGroup != nullptr) {
            api.ChannelGroup_SetVolume(_backgroundGroup, volume);
        }
    }

    void VoiceService::UpdateBackground() {
        SDK::wh::CWh_FmodStudioWrapper *wrapper = SDK::wh::CWh_FmodStudioWrapper::GetInstance();

        // Only the snapshot's lifetime is a fact here: what it does to each bus
        // is bank data. Leaving the master bus for its duration keeps voice out
        // of whatever it does, at the cost of the game's master volume, which
        // has no effect while the game is silenced anyway.
        const bool background = _backgroundGroup != nullptr && _busGroup != nullptr && _settings.KeepsVoiceInBackground() && wrapper != nullptr && wrapper->IsLoseFocusSnapshotActive();
        if (background == _inBackground) {
            return;
        }
        _inBackground = background;

        SDK::Fmod::FMOD_CHANNELGROUP *target = background ? _backgroundGroup : _group;
        for (Voice &slot : _voices) {
            slot.SetGroup(target);
        }
    }

    void VoiceService::RenderTransmitIndicator() const {
        if (!KCDC::Features::UI::HudElements::Get().IsVoiceIndicatorVisible()) {
            return;
        }

        Framework::Voice::VoiceClient *voice = Framework::CoreModules::GetVoiceClient();
        const float alpha                    = voice != nullptr ? voice->GetIndicatorAlpha() : 0.0F;
        if (alpha <= 0.0F) {
            return;
        }

        const ImGuiIO &io = ImGui::GetIO();
        ImDrawList *draw  = ImGui::GetBackgroundDrawList();
        if (draw == nullptr || io.DisplaySize.y <= 0.0F) {
            return;
        }

        const float height = io.DisplaySize.y * kIndicatorHeight;
        const ImVec2 center(io.DisplaySize.x * kIndicatorX, io.DisplaySize.y * kIndicatorY);

        // The same icon a remote talker gets over their head, so the two read
        // as one signal: one wave for a whisper, two for normal, three for a
        // shout.
        Framework::External::ImGUI::Widgets::DrawVoiceIndicator(draw, center, height, voice->GetLocalLevel(), static_cast<int>(voice->GetTier()) + 1, alpha);
    }

    void VoiceService::UpdateTier() {
        // Several presses between two ticks step that many tiers, as the
        // player pressed them.
        const std::uint32_t presses = KCDC::Core::NativeKeybinds::Get().ConsumePresses(KCDC::Core::ModKeybind::VoiceTier);
        if (presses == 0) {
            return;
        }

        // A request: the framework paces it and the server decides. The
        // profile keeps it as the player's choice for the next session.
        Framework::Voice::VoiceClient *voice = Framework::CoreModules::GetVoiceClient();
        voice->CycleTier(presses);
        _settings.SetTier(voice->GetTier());
    }

    Voice *VoiceService::Find(std::uint64_t speaker) {
        if (speaker == 0) {
            return nullptr;
        }

        for (Voice &slot : _voices) {
            if (slot.GetSpeaker() == speaker) {
                return &slot;
            }
        }

        return nullptr;
    }

    Voice *VoiceService::Acquire(std::uint64_t speaker) {
        for (Voice &slot : _voices) {
            if (!slot.IsAvailable()) {
                continue;
            }

            slot.Bind(speaker);
            return &slot;
        }

        return nullptr;
    }

    void VoiceService::Submit(std::uint64_t speaker, const std::int16_t *mono, std::uint32_t samples) {
        if (speaker == 0 || mono == nullptr || samples == 0) {
            return;
        }

        Voice *slot = Find(speaker);
        if (slot == nullptr) {
            slot = Acquire(speaker);
        }

        // No slot means every one is still draining the talker it just let go
        // of. The framework admits at most as many speakers as there are
        // slots, so this is a tick or two of overlap, not a shortage.
        if (slot != nullptr) {
            slot->Submit(mono, samples);
        }
    }

    void VoiceService::ReleaseSpeaker(std::uint64_t speaker) {
        Voice *slot = Find(speaker);
        if (slot != nullptr) {
            slot->Release();
        }
    }

    void VoiceService::PublishWorld(const Framework::Voice::ListenerTransform &listener, const Framework::Voice::SpeakerPlacement *speakers, std::size_t count) {
        // Unused: FMOD already mixes against the listener the game pushed, and
        // PublishListener hands that same pose back to the framework. Stating
        // a listener of our own here would start a disagreement.
        (void)listener;

        Framework::Voice::VoiceClient *voice = Framework::CoreModules::GetVoiceClient();
        const float fullVolumeFraction       = voice != nullptr ? voice->GetFullVolumeFraction() : Framework::Voice::kDefaultFullVolumeFraction;

        std::array<bool, Framework::Voice::kMaxAudibleTalkers> placed {};
        std::array<bool, Framework::Voice::kMaxAudibleTalkers> muffled {};

        // Resolve each voice by its owning peer, exactly as the talk icon
        // does. Read replicated state on the game thread, never from FMOD.
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<KCDC::Features::Player::Human>([&](KCDC::Features::Player::Human *human) {
                if (!human->IsOwner() && human->soul.valid && human->visorClosed) {
                    if (Voice *slot = Find(static_cast<std::uint64_t>(human->ownerGUID))) {
                        muffled[static_cast<std::size_t>(slot - _voices.data())] = true;
                    }
                }
            });
        }

        for (std::size_t i = 0; i < count; i++) {
            Voice *slot = Find(speakers[i].speaker);
            if (slot == nullptr) {
                continue;
            }

            slot->Place(speakers[i].position, speakers[i].range, fullVolumeFraction, muffled[static_cast<std::size_t>(slot - _voices.data())]);
            placed[static_cast<std::size_t>(slot - _voices.data())] = true;
        }

        // A bound talker nobody placed has no position worth mixing against:
        // they streamed out, or their body has not replicated yet.
        for (std::size_t i = 0; i < _voices.size(); i++) {
            if (!placed[i] && _voices[i].GetSpeaker() != 0) {
                _voices[i].Mute();
            }
        }
    }

    void VoiceService::PublishListener() {
        SDK::wh::CWh_FmodStudioWrapper *wrapper = SDK::wh::CWh_FmodStudioWrapper::GetInstance();
        if (wrapper == nullptr || !wrapper->IsReady()) {
            return;
        }

        const SDK::Fmod::FMOD_3D_ATTRIBUTES &attributes = wrapper->GetListenerAttributes();

        // Still zero until the game pushes its first listener, which is a real
        // state during a level load. Publishing it would put the listener at
        // the origin facing nowhere, and would silence the framework's own
        // warning about a game that never publishes one.
        if (attributes.forward.x == 0.0f && attributes.forward.y == 0.0f && attributes.forward.z == 0.0f) {
            return;
        }

        // The framework selects talkers against this pose, and also uses it
        // for spatial playback when voice stays on its fallback device.
        Framework::Voice::ListenerTransform listener;
        listener.position = FromFmod(attributes.position);
        listener.forward  = FromFmod(attributes.forward);
        listener.up       = FromFmod(attributes.up);

        Framework::CoreModules::GetVoiceClient()->SetListenerTransform(listener);
    }

    void VoiceService::UpdateTransmit() {
        Framework::Voice::VoiceClient *voice = Framework::CoreModules::GetVoiceClient();

        KCDC::Core::Application *application = KCDC::Core::gApplication.get();
        if (application == nullptr) {
            voice->SetTransmitBlocked(true);
            return;
        }

        // The game's own binding, rebindable under Controls > Keybinds >
        // Multiplayer and saved with the game's other keys. The action map
        // releases it when it is disabled, so a menu cannot leave it stuck.
        voice->SetPushToTalk(KCDC::Core::NativeKeybinds::Get().IsHeld(KCDC::Core::ModKeybind::PushToTalk));

        // Blocked rather than folded into push-to-talk: the framework cuts the
        // release delay short on a block and lets it run out on a plain key
        // release, which is the difference between a menu opening mid-sentence
        // and a sentence being allowed to finish.
        voice->SetTransmitBlocked(KCDC::Common::LocalPlayer() == nullptr);
        voice->SetPushToTalkBlocked(application->AreControlsLocked() || application->HasExclusiveWebInput());
    }

    SDK::Fmod::FMOD_RESULT __stdcall VoiceService::DspRead(SDK::Fmod::FMOD_DSP_STATE *state, float *inbuffer, float *outbuffer, unsigned int length, int inchannels, int *outchannels) {
        // Only the silent source arrives here; the voice replaces it.
        (void)inbuffer;
        (void)inchannels;

        // A linear walk over six slots, on our own memory: the alternative is
        // asking FMOD whose DSP this is from inside an FMOD callback. No
        // application means no slot was ever opened, so there is nothing to
        // walk and nothing this DSP could belong to.
        if (KCDC::Core::gApplication != nullptr) {
            for (Voice &slot : KCDC::Core::gApplication->GetVoiceService()._voices) {
                if (slot.GetDsp() == state->instance) {
                    slot.Render(outbuffer, length, *outchannels);
                    return SDK::Fmod::FMOD_OK;
                }
            }
        }

        // A DSP being torn down, between its retraction and its removal from
        // the channel.
        std::fill_n(outbuffer, static_cast<std::size_t>(length) * static_cast<std::size_t>(*outchannels), 0.0f);
        return SDK::Fmod::FMOD_OK;
    }

    SDK::Fmod::FMOD_RESULT __stdcall VoiceService::DspShouldProcess(SDK::Fmod::FMOD_DSP_STATE *state, int inputsidle, unsigned int length, std::uint32_t inmask, int inchannels, std::int32_t speakermode) {
        (void)state;
        (void)inputsidle;
        (void)length;
        (void)inmask;
        (void)inchannels;
        (void)speakermode;

        // Every block: the voice comes from the playout buffer, not from the
        // inputs, so idle inputs say nothing about whether there is any.
        return SDK::Fmod::FMOD_OK;
    }
} // namespace KCDC::Features::Social
