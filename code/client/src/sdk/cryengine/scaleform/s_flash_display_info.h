#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    /** Native 0x38-byte partial display update; only flagged fields change. */
    struct SFlashDisplayInfo {
        void SetX(float value) {
            m_x = value;
            m_varsSet |= 0x1;
        }
        float GetX() const { return m_x; }
        void SetY(float value) {
            m_y = value;
            m_varsSet |= 0x2;
        }
        float GetY() const { return m_y; }
        void SetZ(float value) {
            m_z = value;
            m_varsSet |= 0x4;
        }
        float GetZ() const { return m_z; }
        void SetXScale(float value) {
            m_xscale = value;
            m_varsSet |= 0x8;
        }
        float GetXScale() const { return m_xscale; }
        void SetYScale(float value) {
            m_yscale = value;
            m_varsSet |= 0x10;
        }
        float GetYScale() const { return m_yscale; }
        void SetZScale(float value) {
            m_zscale = value;
            m_varsSet |= 0x20;
        }
        float GetZScale() const { return m_zscale; }
        void SetRotation(float value) {
            m_rotation = value;
            m_varsSet |= 0x40;
        }
        float GetRotation() const { return m_rotation; }
        void SetXRotation(float value) {
            m_xrotation = value;
            m_varsSet |= 0x80;
        }
        float GetXRotation() const { return m_xrotation; }
        void SetYRotation(float value) {
            m_yrotation = value;
            m_varsSet |= 0x100;
        }
        float GetYRotation() const { return m_yrotation; }
        void SetAlpha(float value) {
            m_alpha = value;
            m_varsSet |= 0x200;
        }
        float GetAlpha() const { return m_alpha; }
        void SetVisible(bool value) {
            m_visible = value;
            m_varsSet |= 0x400;
        }
        bool GetVisible() const { return m_visible; }
        void Clear() { m_varsSet = 0; }
        bool HasField(std::uint16_t mask) const { return (m_varsSet & mask) != 0; }

      private:
        float m_x         = 0;
        float m_y         = 0;
        float m_z         = 0;
        float m_xscale    = 0;
        float m_yscale    = 0;
        float m_zscale    = 0;
        float m_rotation  = 0;
        float m_xrotation = 0;
        float m_yrotation = 0;
        float m_alpha     = 0;
        bool m_visible    = false;
        std::byte m_opaque0029[0x3] {};
        float m_fov            = 0;       // 0x2C, field of view
        std::int32_t m_edgeAA  = 0;       // 0x30, EdgeAAMode
        std::uint16_t m_varsSet = 0;
        std::byte m_opaque0036[2] {};

        static void AssertLayout() {
            static_assert(offsetof(SFlashDisplayInfo, m_x) == 0x0, "SFlashDisplayInfo::m_x offset mismatch");
            static_assert(offsetof(SFlashDisplayInfo, m_y) == 0x4, "SFlashDisplayInfo::m_y offset mismatch");
            static_assert(offsetof(SFlashDisplayInfo, m_z) == 0x8, "SFlashDisplayInfo::m_z offset mismatch");
            static_assert(offsetof(SFlashDisplayInfo, m_xscale) == 0xc, "SFlashDisplayInfo::m_xscale offset mismatch");
            static_assert(offsetof(SFlashDisplayInfo, m_yscale) == 0x10, "SFlashDisplayInfo::m_yscale offset mismatch");
            static_assert(offsetof(SFlashDisplayInfo, m_zscale) == 0x14, "SFlashDisplayInfo::m_zscale offset mismatch");
            static_assert(offsetof(SFlashDisplayInfo, m_rotation) == 0x18, "SFlashDisplayInfo::m_rotation offset mismatch");
            static_assert(offsetof(SFlashDisplayInfo, m_xrotation) == 0x1c, "SFlashDisplayInfo::m_xrotation offset mismatch");
            static_assert(offsetof(SFlashDisplayInfo, m_yrotation) == 0x20, "SFlashDisplayInfo::m_yrotation offset mismatch");
            static_assert(offsetof(SFlashDisplayInfo, m_alpha) == 0x24, "SFlashDisplayInfo::m_alpha offset mismatch");
            static_assert(offsetof(SFlashDisplayInfo, m_visible) == 0x28, "SFlashDisplayInfo::m_visible offset mismatch");
            static_assert(offsetof(SFlashDisplayInfo, m_fov) == 0x2C, "SFlashDisplayInfo::m_fov offset mismatch");
            static_assert(offsetof(SFlashDisplayInfo, m_edgeAA) == 0x30, "SFlashDisplayInfo::m_edgeAA offset mismatch");
            static_assert(offsetof(SFlashDisplayInfo, m_varsSet) == 0x34, "SFlashDisplayInfo::m_varsSet offset mismatch");
        }
    };
    static_assert(sizeof(SFlashDisplayInfo) == 0x38, "SFlashDisplayInfo size mismatch");
} // namespace SDK
