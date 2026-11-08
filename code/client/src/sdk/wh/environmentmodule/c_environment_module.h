#pragma once

#include "c_respawn_manager.h"
#include "i_weather_system.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::environmentmodule {

    // Primary vtable: 0x183A840E8; secondary module-listener vptr at +0x10.
    class C_EnvironmentModule {
      public:
        virtual ~C_EnvironmentModule()       = default;
        virtual void unk_fn0001()            = 0;
        virtual void unk_fn0002()            = 0;
        virtual void unk_fn0003()            = 0;
        virtual void unk_fn0004()            = 0;
        virtual void unk_fn0005()            = 0;
        virtual void unk_fn0006()            = 0;
        virtual void unk_fn0007()            = 0;
        virtual IWeatherSystem *unk_fn0008() = 0; // +0x040, weather-system accessor
        virtual void unk_fn0009()            = 0;
        virtual void unk_fn0010()            = 0;
        virtual void unk_fn0011()            = 0; // +0x058

        IWeatherSystem *GetWeatherSystem() {
            return unk_fn0008();
        }
        C_RespawnManager *GetRespawnManager() {
            return m_pRespawnManager;
        }

        // Disabling wh_env_WeatherUpdateEnabled also stops rain submission.
        bool IsWeatherUpdateEnabled() const {
            return m_weatherUpdateEnabled != 0;
        }
        void SetWeatherUpdateEnabled(bool enabled) {
            m_weatherUpdateEnabled = enabled ? 1 : 0;
        }

        static C_EnvironmentModule *GetInstance();

      private:
        std::uint32_t m_moduleState0008;      // +0x008, C_BaseModule state; the ctor (0x180BE889C) sets 0
        std::byte m_padding000C[0x4];         // +0x00C
        std::byte m_parallelUpdater0010[0x70]; // +0x010, framework::C_ParallelModuleUpdater base (abstract), its vptr is the secondary one
        C_RespawnManager *m_pRespawnManager; // +0x080
        void *m_pScriptBind;                 // +0x088, C_ScriptBindEnvironment (0x70 bytes), built by Init (0x180E3C398)
        void *m_pState0090;                  // +0x090, a 0x18-byte object Init zero-fills and builds through 0x180E3C34C
        IWeatherSystem *m_weatherSystem;     // +0x098
        void *m_pState00A0;                  // +0x0A0, a 0x28-byte module-owned object
        std::int32_t m_parallelUpdate;       // +0x0A8, wh_env_ParallelUpdate
        std::int32_t m_weatherUpdateEnabled; // +0x0AC, wh_env_WeatherUpdateEnabled

        static void AssertLayout() {
            static_assert(offsetof(C_EnvironmentModule, m_parallelUpdater0010) == 0x10, "C_EnvironmentModule::m_parallelUpdater0010 offset mismatch");
            static_assert(offsetof(C_EnvironmentModule, m_pRespawnManager) == 0x80, "C_EnvironmentModule::m_pRespawnManager offset mismatch");
            static_assert(offsetof(C_EnvironmentModule, m_weatherSystem) == 0x98, "C_EnvironmentModule::m_weatherSystem offset mismatch");
            static_assert(offsetof(C_EnvironmentModule, m_weatherUpdateEnabled) == 0xAC, "C_EnvironmentModule::m_weatherUpdateEnabled offset mismatch");
        }
    };

    static_assert(sizeof(C_EnvironmentModule) == 0xB0, "C_EnvironmentModule size mismatch");
} // namespace SDK::wh::environmentmodule
