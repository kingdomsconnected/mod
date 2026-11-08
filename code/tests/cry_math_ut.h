#pragma once

#include "features/player/player_nametag_policy.h"
#include "sdk/types.h"

#include <external/imgui/widgets/nametag.h>

#include <algorithm>
#include <cmath>
#include <limits>

MODULE(cry_affine_transform, {
    IT("transforms a joint with slot translation, rotation and nonuniform scale", {
        const SDK::Matrix34 slotWorld {{{0.0F, -3.0F, 0.0F, 10.0F}, {2.0F, 0.0F, 0.0F, 20.0F}, {0.0F, 0.0F, 4.0F, 30.0F}}};
        const SDK::Vec3 world = slotWorld.TransformPoint(SDK::Vec3 {1.0F, 2.0F, 3.0F});
        EQUALS(world.x, 4.0F);
        EQUALS(world.y, 22.0F);
        EQUALS(world.z, 42.0F);
    });

    IT("preserves animated joint displacement in character-local space", {
        const SDK::Matrix34 identity {{{1.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F, 0.0F}}};
        const SDK::Vec3 world = identity.TransformPoint(SDK::Vec3 {-0.25F, 0.5F, 1.25F});
        EQUALS(world.x, -0.25F);
        EQUALS(world.y, 0.5F);
        EQUALS(world.z, 1.25F);
    });
});

namespace NametagTests {
    struct DrawFrame {
        ImGuiContext *previous = ImGui::GetCurrentContext();
        ImGuiContext *context  = ImGui::CreateContext();

        DrawFrame() {
            ImGui::SetCurrentContext(context);
            auto &io              = ImGui::GetIO();
            io.IniFilename        = nullptr;
            io.DisplaySize        = ImVec2(1264.0F, 921.0F);
            unsigned char *pixels = nullptr;
            int width = 0, height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            ImGui::NewFrame();
        }

        ~DrawFrame() {
            ImGui::EndFrame();
            ImGui::DestroyContext(context);
            ImGui::SetCurrentContext(previous);
        }

        float DrawBottom(float anchorY, float distance, Framework::External::ImGUI::Widgets::NameTagAnchor anchor, float fontSize = 13.0F, std::uint8_t components = 3, const char *text = "Player", float health = 100.0F) {
            Framework::External::ImGUI::Widgets::NameTagLayout layout;
            layout.drawDistance = 60.0F;
            layout.fadeStart    = 42.0F;
            Framework::External::ImGUI::Widgets::NameTagStyle style;
            style.anchor   = anchor;
            style.fontSize = fontSize;
            auto *drawList = ImGui::GetBackgroundDrawList();
            Framework::External::ImGUI::Widgets::DrawNameTagAt(drawList, ImVec2(846.0F, anchorY), distance, text, components, IM_COL32_WHITE, health, layout, style);
            float bottom = -std::numeric_limits<float>::infinity();
            for (const auto &vertex : drawList->VtxBuffer) {
                bottom = std::max(bottom, vertex.pos.y);
            }
            return bottom;
        }
    };
} // namespace NametagTests

MODULE(nametag_layout, {
    IT("reproduces the captured hill overlap and clears it by anchoring the full widget", {
        // Live capture: head Y=409.65, neck+0.5m projects to Y=399.30.
        // At 22.73m the old text-center layout puts the bar below the head.
        {
            NametagTests::DrawFrame frame;
            EQUALS(frame.DrawBottom(399.30F, 22.73F, Framework::External::ImGUI::Widgets::NameTagAnchor::TextCenter) > 417.0F, true);
        }
        {
            NametagTests::DrawFrame frame;
            const float bottom          = frame.DrawBottom(399.30F, 22.73F, Framework::External::ImGUI::Widgets::NameTagAnchor::BottomCenter);
            const float anchorAfterLift = 399.30F - (22.73F - 15.0F) / 45.0F * 20.0F;
            EQUALS(std::fabs(bottom - anchorAfterLift) <= 1.0F, true);
            EQUALS(bottom < 409.65F, true);
        }
    });

    IT("keeps all emitted geometry above the anchor across captured distances and font sizes", {
        for (const float distance : {3.91F, 22.73F, 36.33F, 59.0F}) {
            for (const float fontSize : {13.0F, 26.0F}) {
                NametagTests::DrawFrame frame;
                const float bottom          = frame.DrawBottom(400.0F, distance, Framework::External::ImGUI::Widgets::NameTagAnchor::BottomCenter, fontSize, 3, "Player\nGuild");
                const float anchorAfterLift = 400.0F - std::clamp((distance - 15.0F) / 45.0F, 0.0F, 1.0F) * 20.0F;
                // Rounded-rectangle antialiasing can extend half a pixel.
                EQUALS(std::fabs(bottom - anchorAfterLift) <= 1.0F, true);
            }
        }
    });

    IT("anchors the name background when no health bar is drawn", {
        for (const auto components : {std::uint8_t {1}, std::uint8_t {3}}) {
            NametagTests::DrawFrame frame;
            const float bottom = frame.DrawBottom(400.0F, 5.0F, Framework::External::ImGUI::Widgets::NameTagAnchor::BottomCenter, 13.0F, components, "Player", components == 1 ? 100.0F : -1.0F);
            EQUALS(std::fabs(bottom - 400.0F) <= 1.0F, true);
        }
    });
});

MODULE(nametag_visibility, {
    IT("gives noon the full reach and the small hours the floor", {
        EQUALS(KCDC::Features::Player::NametagLightCurve::DrawDistanceFor(12.0F), KCDC::Features::Player::NametagLightCurve::kDaylightDistance);
        EQUALS(KCDC::Features::Player::NametagLightCurve::DrawDistanceFor(0.0F), KCDC::Features::Player::NametagLightCurve::kDarknessDistance);

        // The hour the failing capture was actually at: world time 94176765 ms
        // is 02:10, and the engine parameter read 1.0 there.
        EQUALS(KCDC::Features::Player::NametagLightCurve::DrawDistanceFor(2.16F), KCDC::Features::Player::NametagLightCurve::kDarknessDistance);
        EQUALS(KCDC::Features::Player::NametagLightCurve::DrawDistanceFor(23.5F), KCDC::Features::Player::NametagLightCurve::kDarknessDistance);
    });

    IT("ramps across dawn and dusk rather than switching", {
        EQUALS(KCDC::Features::Player::NametagLightCurve::DaylightFractionAt(KCDC::Features::Player::NametagLightCurve::kDawnStart), 0.0F);
        EQUALS(KCDC::Features::Player::NametagLightCurve::DaylightFractionAt(KCDC::Features::Player::NametagLightCurve::kDawnEnd), 1.0F);
        EQUALS(KCDC::Features::Player::NametagLightCurve::DaylightFractionAt(KCDC::Features::Player::NametagLightCurve::kDuskStart), 1.0F);
        EQUALS(KCDC::Features::Player::NametagLightCurve::DaylightFractionAt(KCDC::Features::Player::NametagLightCurve::kDuskEnd), 0.0F);

        const float dawnMiddle = 0.5F * (KCDC::Features::Player::NametagLightCurve::kDawnStart + KCDC::Features::Player::NametagLightCurve::kDawnEnd);
        EQUALS(std::fabs(KCDC::Features::Player::NametagLightCurve::DaylightFractionAt(dawnMiddle) - 0.5F) < 0.01F, true);

        float previous = -1.0F;
        for (float hour = KCDC::Features::Player::NametagLightCurve::kDawnStart; hour <= KCDC::Features::Player::NametagLightCurve::kDawnEnd; hour += 0.25F) {
            const float fraction = KCDC::Features::Player::NametagLightCurve::DaylightFractionAt(hour);
            EQUALS(fraction >= previous, true);
            previous = fraction;
        }
    });

    IT("wraps hours outside the day and answers daylight for an hour it cannot read", {
        EQUALS(KCDC::Features::Player::NametagLightCurve::DrawDistanceFor(36.0F), KCDC::Features::Player::NametagLightCurve::DrawDistanceFor(12.0F));
        EQUALS(KCDC::Features::Player::NametagLightCurve::DrawDistanceFor(-2.0F), KCDC::Features::Player::NametagLightCurve::DrawDistanceFor(22.0F));

        // An unreadable clock must never be the thing that hides a tag.
        EQUALS(KCDC::Features::Player::NametagLightCurve::DrawDistanceFor(std::numeric_limits<float>::quiet_NaN()), KCDC::Features::Player::NametagLightCurve::kDaylightDistance);
        EQUALS(KCDC::Features::Player::NametagLightCurve::DrawDistanceFor(std::numeric_limits<float>::infinity()), KCDC::Features::Player::NametagLightCurve::kDaylightDistance);
    });

    IT("asks for a trace when it has none and when its answer has aged out", {
        EQUALS(KCDC::Features::Player::NametagRayPolicy::WantsTrace(-1.0, 0.0), true);
        EQUALS(KCDC::Features::Player::NametagRayPolicy::WantsTrace(10.0, 10.0), false);
        EQUALS(KCDC::Features::Player::NametagRayPolicy::WantsTrace(10.0, 10.0 + KCDC::Features::Player::NametagRayPolicy::kInterval * 0.5), false);
        EQUALS(KCDC::Features::Player::NametagRayPolicy::WantsTrace(10.0, 10.0 + KCDC::Features::Player::NametagRayPolicy::kInterval), true);
    });

    IT("lets an unrefreshed clear answer expire rather than outlive the cover it was taken through", {
        EQUALS(KCDC::Features::Player::NametagRayPolicy::Authorises(true, 10.0, 10.0), true);
        EQUALS(KCDC::Features::Player::NametagRayPolicy::Authorises(true, 10.0, 10.0 + KCDC::Features::Player::NametagRayPolicy::kMaxAge * 0.5), true);
        EQUALS(KCDC::Features::Player::NametagRayPolicy::Authorises(true, 10.0, 10.0 + KCDC::Features::Player::NametagRayPolicy::kMaxAge), false);
        EQUALS(KCDC::Features::Player::NametagRayPolicy::Authorises(false, 10.0, 10.0), false);
        EQUALS(KCDC::Features::Player::NametagRayPolicy::Authorises(true, -1.0, 10.0), false);
    });

    IT("carries a full session before the budget starts fading anything", {
        // Below this the budget would begin hiding tags nothing is occluding.
        constexpr double kFrameSeconds = 1.0 / 60.0;
        EQUALS(KCDC::Features::Player::NametagRayPolicy::kBudgetPerFrame * (KCDC::Features::Player::NametagRayPolicy::kMaxAge / kFrameSeconds) > 128.0, true);
    });
});
