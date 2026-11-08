#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * CryAnimation's pendulum-row simulation state embedded in CAttachmentPROW.
     *
     * The constructor at 0x183743AD8 (run at CAttachmentPROW+0x38) matches
     * CryEngine's CPendulaRow member for member: it derives the angle
     * cosines at +0x74..+0x90 from the inherited RowSimulationParams angles
     * at +0x10..+0x1C, sets the pitch/roll half-angle pairs to (1, 0), the
     * direction-transform joint to -1, and points both DynArrays at their
     * empty sentinels. The inherited RowSimulationParams block (0x70 bytes,
     * built by 0x180CBD620) is not mapped member by member.
     */
    struct CPendulaRow {
        std::byte m_rowSimulationParams[0x70];  // 0x00, RowSimulationParams base
        std::uint16_t m_nParticlesPerRow;       // 0x70
        std::int16_t m_nAnimOverrideJoint;      // 0x72, -1
        float m_fMaxAngleCos;                   // 0x74
        float m_fMaxAngleHCos;                  // 0x78
        Vec3 m_vConeRotHCos;                    // 0x7C
        Vec3 m_vConeRotHSin;                    // 0x88
        float m_pitchc;                         // 0x94, 1.0
        float m_pitchs;                         // 0x98
        float m_rollc;                          // 0x9C, 1.0
        float m_rolls;                          // 0xA0
        float m_fTimeAccumulator;               // 0xA4
        std::int32_t m_idxDirTransJoint;        // 0xA8, -1
        std::byte m_pad00AC[0x4];               // 0xAC
        void *m_arrProxyIndex;                  // 0xB0, DynArray<int16> header
        void *m_arrParticles;                   // 0xB8, DynArray<Particle> header
    };

    static_assert(sizeof(CPendulaRow) == 0xC0, "CPendulaRow size mismatch");
    static_assert(offsetof(CPendulaRow, m_fMaxAngleCos) == 0x74, "CPendulaRow::m_fMaxAngleCos offset mismatch");
    static_assert(offsetof(CPendulaRow, m_pitchc) == 0x94, "CPendulaRow::m_pitchc offset mismatch");
    static_assert(offsetof(CPendulaRow, m_idxDirTransJoint) == 0xA8, "CPendulaRow::m_idxDirTransJoint offset mismatch");
    static_assert(offsetof(CPendulaRow, m_arrParticles) == 0xB8, "CPendulaRow::m_arrParticles offset mismatch");
} // namespace SDK
