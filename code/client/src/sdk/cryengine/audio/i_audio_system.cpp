#include "i_audio_system.h"

#include "../system/s_system_global_environment.h"

namespace SDK {
    IAudioSystem *IAudioSystem::GetInstance() {
        return SSystemGlobalEnvironment::Get().pAudioSystem;
    }
} // namespace SDK
