#include "gizmos_service.h"

#include "core/application.h"
#include "features/camera/camera_view.h"
#include "common/module_support.h"
#include "sdk/cryengine/renderer/i_renderer.h"

namespace KCDC::Features::Gizmos {
    GizmosService &GizmosService::Get() {
        static GizmosService service;
        return service;
    }

    bool GizmosService::Set(const std::string &resource, const std::string &id, Drawing drawing) {
        std::lock_guard lock(_mutex);
        return _drawings.Set(resource, id, std::move(drawing));
    }

    bool GizmosService::SetTransform(const std::string &resource, const std::string &id, const glm::vec3 &position, const glm::quat &rotation) {
        std::lock_guard lock(_mutex);
        return _drawings.SetTransform(resource, id, position, rotation);
    }

    bool GizmosService::Remove(const std::string &resource, const std::string &id) {
        std::lock_guard lock(_mutex);
        return _drawings.Remove(resource, id);
    }

    void GizmosService::Clear(const std::string &resource) {
        std::lock_guard lock(_mutex);
        _drawings.Clear(resource);
    }

    void GizmosService::Reset() {
        std::lock_guard lock(_mutex);
        _drawings.Reset();
        _editorOverlay.clear();
        _level.clear();
    }

    void GizmosService::SetEditorOverlay(std::vector<Drawing> drawings) {
        for (Drawing &drawing : drawings) {
            drawing.ComputeBounds();
        }
        std::lock_guard lock(_mutex);
        _editorOverlay = std::move(drawings);
    }

    bool GizmosService::InstallResourceCleanup() {
        return !std::exchange(_cleanupInstalled, true);
    }

    void GizmosService::Update() {
        std::lock_guard lock(_mutex);
        _drawings.Expire(std::chrono::steady_clock::now());

        const std::string level = Core::gApplication->GetCurrentLevelName();
        if (level != _level) {
            if (!_level.empty()) {
                _drawings.Reset();
                _editorOverlay.clear();
            }
            _level = level;
        }

        if ((_drawings.Entries().empty() && _editorOverlay.empty()) || Core::gApplication->IsGameLoading() || Core::gApplication->IsMultiplayerFrontendVisible()) {
            return;
        }

        // A drawing is skipped when its bounding sphere is wholly outside the
        // cone around the view frustum's corners. The pose is the render
        // camera's own, so World Builder's free camera culls correctly too.
        Camera::ViewPose pose;
        const bool culling     = Camera::ReadViewPose(pose);
        const glm::vec3 eye    = KCDC::Common::ToGlm(pose.position);
        const glm::vec3 ahead  = KCDC::Common::ToGlm(pose.forward);
        const float tanHalf    = std::tan(glm::radians(pose.fovDegrees) * 0.5F);
        const float coneAngle  = std::atan(tanHalf * std::sqrt(1.0F + pose.aspectRatio * pose.aspectRatio));
        const float coneSin    = std::sin(coneAngle);
        const float coneCos    = std::cos(coneAngle);
        const auto visible     = [&](const Drawing &drawing) {
            if (!culling) {
                return true;
            }
            const glm::vec3 offset = drawing.Place(drawing.boundsCentre) - eye;
            const float along      = glm::dot(offset, ahead);
            const float across     = glm::length(offset - ahead * along);
            return along >= -drawing.boundsRadius && across * coneCos - along * coneSin <= drawing.boundsRadius;
        };

        SDK::IRenderAuxGeom *aux = SDK::IRenderer::GetInstance()->GetIRenderAuxGeom();
        SDK::SAuxGeomRenderFlags previous;
        aux->GetRenderFlags(&previous);

        const auto submit = [&](const Drawing &drawing) {
            const bool placed      = drawing.Placed();
            const auto at          = [&](const glm::vec3 &point) {
                return KCDC::Common::ToNative(placed ? drawing.Place(point) : point);
            };
            const SDK::ColorB fill = SDK::ColorB::FromARGB(drawing.fillColor);
            for (const Triangle &triangle : drawing.triangles) {
                aux->DrawTriangle(at(triangle.a), fill, at(triangle.b), fill, at(triangle.c), fill);
            }
            for (const Segment &segment : drawing.segments) {
                const SDK::ColorB color = SDK::ColorB::FromARGB(segment.color.value_or(drawing.color));
                if (color.a != 0) {
                    aux->DrawLine(at(segment.from), color, at(segment.to), color, drawing.width * segment.weight);
                }
            }
        };

        // Two render states only, so the flags change at most twice a frame.
        for (const bool depthTest : {true, false}) {
            SDK::SAuxGeomRenderFlags ignored;
            const SDK::SAuxGeomRenderFlags flags {SDK::SAuxGeomRenderFlags::DepthWriteOff | SDK::SAuxGeomRenderFlags::AlphaBlended | (depthTest ? 0U : SDK::SAuxGeomRenderFlags::DepthTestOff)};
            aux->SetRenderFlags(&ignored, flags);
            for (const auto &[key, drawing] : _drawings.Entries()) {
                if (drawing.depthTest == depthTest && visible(drawing)) {
                    submit(drawing);
                }
            }
            for (const Drawing &drawing : _editorOverlay) {
                if (drawing.depthTest == depthTest && visible(drawing)) {
                    submit(drawing);
                }
            }
        }

        SDK::SAuxGeomRenderFlags ignored;
        aux->SetRenderFlags(&ignored, previous);
    }
} // namespace KCDC::Features::Gizmos
