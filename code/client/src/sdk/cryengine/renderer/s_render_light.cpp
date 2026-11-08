#include "s_render_light.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK {
    SRenderLight::SRenderLight() {
        hook::this_call<void>(gPatterns.SRenderLight__Construct, this);
    }

    SRenderLight::~SRenderLight() {
        hook::this_call<void>(gPatterns.SRenderLight__Destruct, this);
    }

    void SRenderLight::SetLightColor(const ColorF &color) {
        hook::this_call<void>(gPatterns.SRenderLight__SetLightColor, this, &color);
    }

    void SRenderLight::SetRadius(float radius, float bulbRadius) {
        hook::this_call<void>(gPatterns.SRenderLight__SetRadius, this, radius, bulbRadius);
    }
} // namespace SDK
