#pragma once

#include "../../patterns.h"
#include "../../types.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Native CryEngine render camera embedded in CView at +0xE8.
     *
     * CCamera::SetFrustum (0x18053909C) writes every scalar below in one pass,
     * which is what pins them, and CView's layout proves the 0x310 extent. The
     * corners and the focal length are derived, so assigning a FOV, near or far
     * plane here leaves culling on the old frustum -- SetFrustum rebuilds them.
     */
    class CCamera {
      public:
        /**
         * Builds a camera in place with the native constructor (0x180584004):
         * identity matrix, a 640x480 frustum. A null name allocates nothing,
         * so a camera built this way needs no destructor.
         */
        void Construct() {
            hook::this_call<void>(gPatterns.CCamera__Construct, this, static_cast<const char *>(nullptr));
        }

        /**
         * Where the camera stands and looks: CryEngine cameras look down +Y
         * with +Z up. There is no compiled SetMatrix, and the frustum is
         * derived from the matrix, so `SetFrustum` follows every change.
         */
        void SetMatrix(const Matrix34 &matrix) {
            m_matrix = matrix;
        }

        /**
         * The viewport size, vertical FOV in radians, planes and pixel aspect
         * (0x18053909C), then the frustum rebuilt from them and the matrix.
         * A pass takes its viewport from the width and height, so they must
         * match the target it renders into.
         */
        void SetFrustum(std::int32_t width, std::int32_t height, float fov, float nearPlane, float farPlane, float pixelAspectRatio) {
            hook::this_call<void>(gPatterns.CCamera__SetFrustum, this, width, height, fov, nearPlane, farPlane, pixelAspectRatio);
        }

        const Matrix34 &GetMatrix() const {
            return m_matrix;
        }
        Matrix34 &GetMatrix() {
            return m_matrix;
        }
        float GetFov() const {
            return m_fov;
        }
        float GetSourceFov() const {
            return m_sourceFov;
        }
        std::int32_t GetWidth() const {
            return m_width;
        }
        std::int32_t GetHeight() const {
            return m_height;
        }
        float GetProjectionRatio() const {
            return m_projectionRatio;
        }
        float GetPixelAspectRatio() const {
            return m_pixelAspectRatio;
        }
        bool AdaptsFovToAspectRatio() const {
            return m_adaptFovToAspectRatio;
        }
        float GetReferenceAspectRatio() const {
            return m_referenceAspectRatio;
        }
        float GetNearPlane() const {
            return m_nearPlane;
        }
        float GetFarPlane() const {
            return m_farPlane;
        }

        /** (height / 2) / tan(fov / 2), which every frustum corner scales. */
        float GetFocalLength() const {
            return m_focalLength;
        }

        /** Camera position, i.e. the translation column of the view matrix. */
        Vec3 GetPosition() const {
            return Vec3 {m_matrix.m[0][3], m_matrix.m[1][3], m_matrix.m[2][3]};
        }

      private:
        Matrix34 m_matrix;            // 0x000
        float m_fov;                  // 0x030, effective vertical FOV
        float m_sourceFov;            // 0x034, input FOV before adaptation
        std::int32_t m_width;         // 0x038
        std::int32_t m_height;        // 0x03C
        float m_projectionRatio;      // 0x040, (width / pixelAspect) / height
        float m_pixelAspectRatio;     // 0x044
        bool m_adaptFovToAspectRatio; // 0x048, fed from SViewParams+0x34
        std::byte m_padding0049[0x3];
        float m_referenceAspectRatio;     // 0x04C, fed from SViewParams+0x38
        float m_nearEdgeLeft;             // 0x050
        float m_nearPlane;                // 0x054
        float m_nearEdgeRight;            // 0x058
        float m_halfWidthOverPixelAspect; // 0x05C, negated
        float m_focalLength;              // 0x060
        float m_halfHeight;               // 0x064
        float m_farEdgeLeft;              // 0x068
        float m_farPlane;                 // 0x06C
        float m_farEdgeRight;             // 0x070
        std::byte m_opaque0074[0x29C];    // 0x074

        static void AssertLayout() {
            static_assert(offsetof(CCamera, m_fov) == 0x30, "CCamera::m_fov offset mismatch");
            static_assert(offsetof(CCamera, m_width) == 0x38, "CCamera::m_width offset mismatch");
            static_assert(offsetof(CCamera, m_adaptFovToAspectRatio) == 0x48, "CCamera adapt-FOV offset mismatch");
            static_assert(offsetof(CCamera, m_nearPlane) == 0x54, "CCamera::m_nearPlane offset mismatch");
            static_assert(offsetof(CCamera, m_focalLength) == 0x60, "CCamera::m_focalLength offset mismatch");
            static_assert(offsetof(CCamera, m_farPlane) == 0x6C, "CCamera::m_farPlane offset mismatch");
        }
    };

    static_assert(sizeof(CCamera) == 0x310, "CCamera size mismatch");
} // namespace SDK
