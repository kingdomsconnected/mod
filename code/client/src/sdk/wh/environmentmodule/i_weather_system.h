#pragma once

#include "../../types.h"

#include <cstdint>

namespace SDK::wh::environmentmodule {
    // Abstract vtable: 0x183ED1208; C_WeatherSystem: 0x183AB8D20.
    class IWeatherSystem {
      public:
        virtual ~IWeatherSystem() = default;

        // CTimeOfDay preset name; game-time milliseconds, zero for instant.
        virtual void BlendTimeOfDay(const char *timeOfDayPreset, std::int64_t blendTimeMilliseconds, bool force) = 0; // +0x008

        // Sets wh_r_VolumetricCloudsMapOverride to 2.
        virtual void RebuildClouds()       = 0; // +0x010
        virtual void unk_fn0003()          = 0;
        virtual void ResetRainController() = 0; // +0x020

        virtual void ForceImmediateUpdate()    = 0; // +0x028
        virtual void unk_fn0006()              = 0;
        virtual void SetState13D8(float value) = 0; // +0x038
        virtual void unk_fn0008()              = 0;
        virtual float GetRainIntensity()       = 0; // +0x048
        virtual void unk_fn0010()              = 0;
        virtual void unk_fn0011()              = 0;
        virtual void unk_fn0012()              = 0;
        virtual float GetState18E4()           = 0; // +0x068

        /**
         * The global wind, read straight back out of the 3D engine.
         *
         * `0x182AE3958` forwards to `I3DEngine::GetWind(false)` and returns
         * its result; the 12-byte Vec3 comes back through the hidden pointer
         * in RDX. The weather system has no wind setter at all -- nothing in
         * the module writes the wind except `CScriptBind_System::SetWind`.
         */
        virtual Vec3 GetWind()       = 0; // +0x070
        virtual float GetWindSpeed() = 0; // +0x078
        virtual void unk_fn0016()              = 0;
        virtual void unk_fn0017()              = 0;
        virtual void unk_fn0018()              = 0;
        virtual void unk_fn0019()              = 0;
        virtual void unk_fn0020()              = 0; // +0x0A0
    };

    static_assert(sizeof(IWeatherSystem) == 0x8, "IWeatherSystem size mismatch");
} // namespace SDK::wh::environmentmodule
