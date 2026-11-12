#pragma once

#include "../../types.h"
#include "../system/i_stream_callback.h"
#include "cry_3d_engine_base.h"
#include "i_merged_mesh_render_node.h"
#include "i_render_node.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * One plant of a merged-mesh sector, as it stands in memory: sixteen
     * bytes, where the sector file and stock CryEngine keep twelve.
     *
     * The loader (0x180CFAB6C) copies the three position words, then the
     * file's scale byte to +0x0C and again to +0x0E, and writes 0xFE at
     * +0x0D. +0x0E is KCD2's own: the scale the plant came with, which is
     * what makes hiding one reversible. `IMergedMeshRenderNode`'s walker
     * (0x18043C258, tail at 0x1821EACEE) hides a gathered herb by writing
     * zero to +0x0C and puts it back by copying +0x0E over it; it touches
     * nothing else. The sector's mesh builder (0x1834E91B0) reads the byte
     * as the plant's scale, and a sector in view is rebuilt without the
     * plant within the second -- seen in game, with no call to ask for it.
     *
     * A write is one byte, the same one the game's main thread writes while
     * the sector's jobs read it.
     */
    struct SMMRMInstance {
        std::uint16_t pos_x; // 0x00, 0..65535 across the sector's cube
        std::uint16_t pos_y; // 0x02
        std::uint16_t pos_z; // 0x04
        std::byte m_opaque0006[0x6];
        std::uint8_t scale; // 0x0C, in 1/64ths; zero draws nothing
        std::byte m_opaque000D;
        std::uint8_t loadedScale; // 0x0E, what `scale` was read as
        std::byte m_opaque000F;

        bool IsHidden() const {
            return scale == 0;
        }
        void Hide() {
            scale = 0;
        }
        void Restore() {
            scale = loadedScale;
        }
    };

    static_assert(offsetof(SMMRMInstance, scale) == 0x0C, "SMMRMInstance::scale offset mismatch");
    static_assert(offsetof(SMMRMInstance, loadedScale) == 0x0E, "SMMRMInstance::loadedScale offset mismatch");
    static_assert(sizeof(SMMRMInstance) == 0x10, "SMMRMInstance size mismatch");

    /**
     * One vegetation group's plants in a sector. 0x80 bytes: every native
     * walk steps the array by `shl 7` (0x18043C258, 0x18043BFEC,
     * 0x180CFAB6C, 0x1834EF368).
     *
     * `instanceCount` is the bound all four loops use over `instances`.
     * Stream-out frees `instances` and nulls it, and the loader leaves a
     * group whose bit 0 of `flags` is set without instances at all -- stock
     * CryEngine's spec mismatch -- so a group is walked only when
     * `HasInstances` says so.
     */
    struct SMMRMGroupHeader {
        SMMRMInstance *instances; // 0x00
        std::byte m_opaque0008[0x44];
        std::int32_t statInstGroupId; // 0x4C, index into the engine's vegetation groups
        std::byte m_opaque0050[0x4];
        std::uint32_t instanceCount; // 0x54
        std::byte m_opaque0058[0x14];
        std::uint8_t flags; // 0x6C
        std::byte m_opaque006D[0x13];

        bool HasInstances() const {
            return instances != nullptr && (flags & 1U) == 0 && instanceCount != 0;
        }
    };

    static_assert(offsetof(SMMRMGroupHeader, statInstGroupId) == 0x4C, "SMMRMGroupHeader::statInstGroupId offset mismatch");
    static_assert(offsetof(SMMRMGroupHeader, instanceCount) == 0x54, "SMMRMGroupHeader::instanceCount offset mismatch");
    static_assert(offsetof(SMMRMGroupHeader, flags) == 0x6C, "SMMRMGroupHeader::flags offset mismatch");
    static_assert(sizeof(SMMRMGroupHeader) == 0x80, "SMMRMGroupHeader size mismatch");

    /**
     * One sixteen-metre cube of the terrain's merged vegetation
     * (`eERType_MergedMesh`, 23): the tall grass and everything else the
     * level bakes out of its `AutoMerged` groups. Not one object per plant
     * but one node per cube, holding every plant as an `SMMRMInstance`.
     *
     * RTTI gives the bases: IRenderNode at +0x00, IMergedMeshRenderNode at
     * +0x50, IStreamCallback at +0x58 and the empty Cry3DEngineBase.
     * `CMergedMeshesManager`'s node lookup (0x1834DB738) allocates exactly
     * 0x2E0 bytes for the constructor (0x1805DE2D0), which writes the three
     * tables and the members named here.
     *
     * The instance arrays exist only while the node is streamed in, and
     * come back as the file has them every time it streams in again: an
     * instance hidden here has to be hidden again from
     * `IMergeMeshStreamListener::OnMergedMeshStreamedIn`. The engine keeps
     * a sector's instances long after the camera has left -- two kilometres
     * off they were still in memory -- so a reload of the level is the sure
     * way to see one stream in.
     */
    class CMergedMeshRenderNode : public IRenderNode, public IMergedMeshRenderNode, public IStreamCallback, public Cry3DEngineBase {
      public:
        /** The states the binary tests: 4 while the stream request is out (0x18043A9B8, 0x180CFAB6C), 5 once the instances are in (0x18043B52C), 3 after stream-out (0x1834EF368). */
        static constexpr std::int32_t kStateStreamedIn = 5;

        ~CMergedMeshRenderNode() override = default;

        /** Whether `GetGroup` may be walked. Main thread: a stream-out on it is what takes the instances away. */
        bool HasInstanceData() const {
            return m_state == kStateStreamedIn && m_groups != nullptr;
        }

        std::uint32_t GetGroupCount() const {
            return m_groupCount;
        }

        SMMRMGroupHeader &GetGroup(std::uint32_t index) const {
            return m_groups[index];
        }

        /** The cube the instance positions are quantized in; the manager finds a node by testing a point against it. */
        const AABB &GetSectorBounds() const {
            return m_sectorBounds;
        }

        /** Whether the sector carries a turn, so that its plants do not stand inside its cube. */
        bool IsTurned() const {
            return m_zRotation != 0.0F;
        }

        /**
         * Where an instance stands in the world, as 0x18043C4D0 decodes it:
         * each word over 65535 times the cube's X extent, from the cube's
         * minimum -- one extent for all three axes -- then, for a sector
         * that carries a turn, rotated about Z around its pivot
         * (0x1821EAD1E).
         */
        Vec3 DecodePosition(const SMMRMInstance &instance) const {
            const float extent = std::max(m_sectorBounds.max.x - m_sectorBounds.min.x, 0.0F);
            Vec3 position {m_sectorBounds.min.x + static_cast<float>(instance.pos_x) / 65535.0F * extent, m_sectorBounds.min.y + static_cast<float>(instance.pos_y) / 65535.0F * extent, m_sectorBounds.min.z + static_cast<float>(instance.pos_z) / 65535.0F * extent};
            if (m_zRotation != 0.0F) {
                const float x = position.x - m_rotationPivot.x, y = position.y - m_rotationPivot.y;
                const float sine = std::sin(m_zRotation), cosine = std::cos(m_zRotation);
                position.x = x * cosine - y * sine + m_rotationPivot.x;
                position.y = x * sine + y * cosine + m_rotationPivot.y;
            }
            return position;
        }

      private:
        std::byte m_opaque0060[0xC];
        std::int32_t m_state; // 0x6C
        std::byte m_opaque0070[0x1C];
        AABB m_sectorBounds;  // 0x8C
        AABB m_visibleBounds; // 0xA4, what FillBBox answers (0x1834E436C)
        Vec3 m_rotationPivot; // 0xBC
        std::byte m_opaque00C8[0xC];
        float m_zRotation; // 0xD4, radians; zero for a sector the terrain owns
        std::byte m_opaque00D8[0x18];
        SMMRMGroupHeader *m_groups; // 0xF0
        std::uint32_t m_groupCount; // 0xF8
        std::byte m_opaque00FC[0x1E4];

        static void AssertLayout() {
            static_assert(offsetof(CMergedMeshRenderNode, m_state) == 0x6C, "CMergedMeshRenderNode::m_state offset mismatch");
            static_assert(offsetof(CMergedMeshRenderNode, m_sectorBounds) == 0x8C, "CMergedMeshRenderNode::m_sectorBounds offset mismatch");
            static_assert(offsetof(CMergedMeshRenderNode, m_visibleBounds) == 0xA4, "CMergedMeshRenderNode::m_visibleBounds offset mismatch");
            static_assert(offsetof(CMergedMeshRenderNode, m_rotationPivot) == 0xBC, "CMergedMeshRenderNode::m_rotationPivot offset mismatch");
            static_assert(offsetof(CMergedMeshRenderNode, m_zRotation) == 0xD4, "CMergedMeshRenderNode::m_zRotation offset mismatch");
            static_assert(offsetof(CMergedMeshRenderNode, m_groups) == 0xF0, "CMergedMeshRenderNode::m_groups offset mismatch");
            static_assert(offsetof(CMergedMeshRenderNode, m_groupCount) == 0xF8, "CMergedMeshRenderNode::m_groupCount offset mismatch");
        }
    };

    static_assert(sizeof(CMergedMeshRenderNode) == 0x2E0, "CMergedMeshRenderNode size mismatch");
} // namespace SDK
