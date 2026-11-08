#pragma once

#include "features/gizmos/gizmos_geometry.h"
#include <nlohmann/json.hpp>

namespace KCDC::Features::Prop {
    /** Project-owned viewport preferences. Scaling remains local in ImGuizmo. */
    struct EditorViewport {
        bool localSpace         = false;
        bool pickPreferPhysics  = true;
        bool snapTranslate      = false;
        bool snapRotate         = false;
        bool snapScale          = false;
        float snapTranslateStep = 0.5F;
        float snapRotateStep    = 15.0F;
        float snapScaleStep     = 0.1F;
        bool grid               = false;
        bool gridMatchSnap      = true;
        float gridStep          = 0.5F;
        float gridHeightOffset  = -2.0F;
        float gridLineWidth     = 1.5F;

        bool IsValid() const {
            return std::isfinite(snapTranslateStep) && snapTranslateStep >= 0.01F && snapTranslateStep <= 10.0F && std::isfinite(snapRotateStep) && snapRotateStep >= 1.0F && snapRotateStep <= 90.0F && std::isfinite(snapScaleStep) && snapScaleStep >= 0.01F
                   && snapScaleStep <= 10.0F && std::isfinite(gridStep) && gridStep >= 0.01F && gridStep <= 50.0F && std::isfinite(gridHeightOffset) && gridHeightOffset >= -100.0F && gridHeightOffset <= 100.0F && std::isfinite(gridLineWidth) && gridLineWidth >= 1.0F
                   && gridLineWidth <= 8.0F;
        }
        float GridStep() const {
            return gridMatchSnap ? snapTranslateStep : gridStep;
        }
        nlohmann::json ToJson() const {
            return {{"space", localSpace ? "local" : "world"}, {"pickPreferPhysics", pickPreferPhysics}, {"snapTranslate", snapTranslate}, {"snapRotate", snapRotate}, {"snapScale", snapScale}, {"snapTranslateStep", snapTranslateStep}, {"snapRotateStep", snapRotateStep},
                {"snapScaleStep", snapScaleStep}, {"grid", grid}, {"gridMatchSnap", gridMatchSnap}, {"gridStep", gridStep}, {"gridHeightOffset", gridHeightOffset}, {"gridLineWidth", gridLineWidth}};
        }
        static bool FromJson(const nlohmann::json &json, EditorViewport &out, std::string &error) {
            try {
                if (!json.is_object()) {
                    throw std::invalid_argument("Viewport must be an object.");
                }
                EditorViewport next;
                const auto space = json.value("space", std::string("world"));
                if (space != "world" && space != "local") {
                    throw std::invalid_argument("Gizmo space must be world or local.");
                }
                next.localSpace        = space == "local";
                next.pickPreferPhysics = json.value("pickPreferPhysics", true);
                next.snapTranslate     = json.value("snapTranslate", false);
                next.snapRotate        = json.value("snapRotate", false);
                next.snapScale         = json.value("snapScale", false);
                next.snapTranslateStep = json.value("snapTranslateStep", 0.5F);
                next.snapRotateStep    = json.value("snapRotateStep", 15.0F);
                next.snapScaleStep     = json.value("snapScaleStep", 0.1F);
                next.grid              = json.value("grid", false);
                next.gridMatchSnap     = json.value("gridMatchSnap", true);
                next.gridStep          = json.value("gridStep", 0.5F);
                next.gridHeightOffset  = json.value("gridHeightOffset", -2.0F);
                next.gridLineWidth     = json.value("gridLineWidth", 1.5F);
                if (!next.IsValid()) {
                    throw std::invalid_argument("Viewport increments, grid height or line width are out of range.");
                }
                out = next;
                return true;
            }
            catch (const std::exception &exception) {
                error = exception.what();
                return false;
            }
        }
        /** A bounded horizontal grid, aligned to world multiples even at negative coordinates. */
        Gizmos::Drawing Grid(const glm::vec3 &camera) const {
            Gizmos::Drawing drawing;
            if (!grid || !IsValid()) {
                return drawing;
            }
            const float step       = GridStep();
            const glm::vec3 center = glm::floor((camera + glm::vec3(0, 0, gridHeightOffset)) / step) * step;
            constexpr int half     = 32;
            drawing.color          = 0xDDE2E8F0;
            drawing.width          = gridLineWidth;
            drawing.segments.reserve((half * 2 + 1) * 2);
            for (int line = -half; line <= half; ++line) {
                const float offset = line * step;
                drawing.segments.push_back({center + glm::vec3(offset, -half * step, 0), center + glm::vec3(offset, half * step, 0)});
                drawing.segments.push_back({center + glm::vec3(-half * step, offset, 0), center + glm::vec3(half * step, offset, 0)});
            }
            drawing.ComputeBounds();
            return drawing;
        }
    };
} // namespace KCDC::Features::Prop
