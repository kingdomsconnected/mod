#include "audio_service.h"

#include "sdk/cryengine/audio/i_audio_proxy.h"
#include "sdk/cryengine/audio/i_audio_system.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_audio_proxy.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"

#include <logging/logger.h>

namespace KCDC::Features::Audio {
    namespace {
        constexpr const char *kLogger = "Audio";

        // What a proxy this feature borrows calls itself in the ATL's debug
        // listings; the game names its material-effect ones "MFXAudioEffect".
        constexpr const char *kScriptedProxyName = "KCDCScriptedTrigger";

        SDK::IAudioSystem *AudioSystem() {
            SDK::IAudioSystem *audio = SDK::IAudioSystem::GetInstance();
            if (audio == nullptr) {
                Framework::Logging::GetLogger(kLogger)->debug("The audio system is not up yet");
            }
            return audio;
        }

        // Name to control id through one of the audio system's lookups, 0 when
        // the name is not declared. `kind` names the control in the log line
        // and nowhere else.
        SDK::AudioControlId Resolve(SDK::IAudioSystem &audio, const std::string &name, const char *kind, bool (SDK::IAudioSystem::*lookUp)(const char *, SDK::AudioControlId &) const) {
            SDK::AudioControlId id = SDK::kInvalidAudioControlId;
            if (name.empty() || !(audio.*lookUp)(name.c_str(), id)) {
                Framework::Logging::GetLogger(kLogger)->warn("No audio {} named \"{}\"", kind, name);
                return SDK::kInvalidAudioControlId;
            }
            return id;
        }

        // Entity ids carry a generation, so a stale one resolves to nothing
        // rather than to whatever reused the slot.
        SDK::IEntityAudioProxy *EntityAudio(SDK::EntityId entityId) {
            SDK::IEntitySystem *entities = SDK::IEntitySystem::GetInstance();
            if (entities == nullptr) {
                return nullptr;
            }

            SDK::IEntity *entity = entities->GetEntity(entityId);
            if (entity == nullptr) {
                Framework::Logging::GetLogger(kLogger)->warn("No entity {} to play audio on", entityId);
                return nullptr;
            }

            SDK::IEntityAudioProxy *proxy = SDK::IEntityAudioProxy::GetOrCreate(entity);
            if (proxy == nullptr) {
                Framework::Logging::GetLogger(kLogger)->warn("Entity {} would not take an audio component", entityId);
            }
            return proxy;
        }
    } // namespace

    SDK::AudioControlId AudioService::ResolveTrigger(const std::string &name) {
        SDK::IAudioSystem *audio = SDK::IAudioSystem::GetInstance();
        if (audio == nullptr || name.empty()) {
            return SDK::kInvalidAudioControlId;
        }

        SDK::AudioControlId triggerId = SDK::kInvalidAudioControlId;
        return audio->GetAudioTriggerId(name.c_str(), triggerId) ? triggerId : SDK::kInvalidAudioControlId;
    }

    bool AudioService::Play(const std::string &trigger) {
        SDK::IAudioSystem *audio = AudioSystem();
        if (audio == nullptr) {
            return false;
        }

        const SDK::AudioControlId triggerId = Resolve(*audio, trigger, "trigger", &SDK::IAudioSystem::GetAudioTriggerId);
        return triggerId != SDK::kInvalidAudioControlId && SDK::ExecuteGlobalAudioTrigger(triggerId, false);
    }

    bool AudioService::PlayAt(const std::string &trigger, const glm::vec3 &position) {
        SDK::IAudioSystem *audio = AudioSystem();
        if (audio == nullptr) {
            return false;
        }

        const SDK::AudioControlId triggerId = Resolve(*audio, trigger, "trigger", &SDK::IAudioSystem::GetAudioTriggerId);
        if (triggerId == SDK::kInvalidAudioControlId) {
            return false;
        }

        SDK::IAudioProxy *proxy = audio->GetFreeAudioProxy();
        if (proxy == nullptr) {
            Framework::Logging::GetLogger(kLogger)->warn("The audio system had no free proxy for \"{}\"", trigger);
            return false;
        }

        // Releasing straight after the trigger is the game's own one-shot: init
        // is async, so everything here queues on the proxy and replays when the
        // object id lands, and the ATL defers the release until the trigger ends.
        proxy->Initialize(kScriptedProxyName, true);
        proxy->SetPosition(SDK::Vec3 {position.x, position.y, position.z});
        proxy->SetOcclusionType(SDK::EAudioOcclusionType::Ignore);
        proxy->SetCurrentEnvironments(0);
        proxy->ExecuteTrigger(triggerId, SDK::ELipSyncMethod::None, SDK::SAudioCallBackInfo::Empty());
        proxy->Release();
        return true;
    }

    bool AudioService::PlayOnEntity(const std::string &trigger, SDK::EntityId entityId) {
        SDK::IAudioSystem *audio = AudioSystem();
        if (audio == nullptr) {
            return false;
        }

        const SDK::AudioControlId triggerId = Resolve(*audio, trigger, "trigger", &SDK::IAudioSystem::GetAudioTriggerId);
        if (triggerId == SDK::kInvalidAudioControlId) {
            return false;
        }

        SDK::IEntityAudioProxy *proxy = EntityAudio(entityId);
        return proxy != nullptr && proxy->ExecuteTrigger(triggerId, SDK::ELipSyncMethod::None, SDK::kDefaultAudioProxyId, SDK::SAudioCallBackInfo::Empty());
    }

    bool AudioService::StopOnEntity(const std::string &trigger, SDK::EntityId entityId) {
        SDK::IAudioSystem *audio = AudioSystem();
        if (audio == nullptr) {
            return false;
        }

        const SDK::AudioControlId triggerId = Resolve(*audio, trigger, "trigger", &SDK::IAudioSystem::GetAudioTriggerId);
        SDK::IEntityAudioProxy *proxy = triggerId != SDK::kInvalidAudioControlId ? EntityAudio(entityId) : nullptr;
        if (proxy == nullptr) {
            return false;
        }

        proxy->StopTrigger(triggerId, SDK::kDefaultAudioProxyId);
        return true;
    }

    bool AudioService::PlayFileOnEntity(const std::string &file, SDK::EntityId entityId, const std::string &purpose) {
        // The host event appends the extension itself.
        std::string path = file;
        if (path.size() > 4 && path.compare(path.size() - 4, 4, ".ogg") == 0) {
            path.resize(path.size() - 4);
        }
        if (path.empty() || !SetEntitySwitch(entityId, "standalone_file_purpose", purpose)) {
            return false;
        }

        SDK::IEntityAudioProxy *proxy = EntityAudio(entityId);
        if (proxy == nullptr) {
            return false;
        }
        proxy->PlayFile(path.c_str(), 0.0F, SDK::kDefaultAudioProxyId, SDK::SAudioCallBackInfo::Empty());
        return true;
    }

    bool AudioService::StopAllOnEntity(SDK::EntityId entityId) {
        SDK::IEntityAudioProxy *proxy = EntityAudio(entityId);
        if (proxy == nullptr) {
            return false;
        }

        proxy->StopAllTriggers(SDK::kAllAudioProxyIds);
        return true;
    }

    bool AudioService::SetEntityRtpc(SDK::EntityId entityId, const std::string &rtpc, float value) {
        SDK::IAudioSystem *audio = AudioSystem();
        if (audio == nullptr) {
            return false;
        }

        const SDK::AudioControlId rtpcId = Resolve(*audio, rtpc, "parameter", &SDK::IAudioSystem::GetAudioRtpcId);
        SDK::IEntityAudioProxy *proxy = rtpcId != SDK::kInvalidAudioControlId ? EntityAudio(entityId) : nullptr;
        if (proxy == nullptr) {
            return false;
        }

        proxy->SetRtpcValue(rtpcId, value, SDK::kDefaultAudioProxyId);
        return true;
    }

    bool AudioService::SetEntitySwitch(SDK::EntityId entityId, const std::string &switchName, const std::string &stateName) {
        SDK::IAudioSystem *audio = AudioSystem();
        if (audio == nullptr) {
            return false;
        }

        const SDK::AudioControlId switchId = Resolve(*audio, switchName, "switch", &SDK::IAudioSystem::GetAudioSwitchId);
        if (switchId == SDK::kInvalidAudioControlId) {
            return false;
        }

        // A state belongs to its switch, so it cannot be resolved without one.
        SDK::AudioSwitchStateId stateId = SDK::kInvalidAudioControlId;
        if (stateName.empty() || !audio->GetAudioSwitchStateId(switchId, stateName.c_str(), stateId)) {
            Framework::Logging::GetLogger(kLogger)->warn("Audio switch \"{}\" has no state \"{}\"", switchName, stateName);
            return false;
        }

        SDK::IEntityAudioProxy *proxy = EntityAudio(entityId);
        if (proxy == nullptr) {
            return false;
        }

        proxy->SetSwitchState(switchId, stateId, SDK::kDefaultAudioProxyId);
        return true;
    }
} // namespace KCDC::Features::Audio
