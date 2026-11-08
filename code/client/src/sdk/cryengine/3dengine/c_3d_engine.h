#pragma once

#include "i_3d_engine.h"
#include "i_stat_obj.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * The level's static object table: every mesh `terrain.dat` names, in
     * file order, as an MSVC `std::vector<IStatObj *>` the game allocated.
     * Read in place -- the mod's own vector does not share the game's layout
     * in a debug build, and must never free the game's buffer.
     */
    struct LevelStatObjTableLayout {
        IStatObj **m_begin;    // 0x00
        IStatObj **m_end;      // 0x08
        IStatObj **m_capacity; // 0x10

        /** The mesh at `index`, or null off either end, as the layer physics builder bounds it. */
        IStatObj *At(std::int32_t index) const {
            return index >= 0 && index < m_end - m_begin ? m_begin[index] : nullptr;
        }
    };

    static_assert(sizeof(LevelStatObjTableLayout) == 0x18, "LevelStatObjTableLayout size mismatch");

    /**
     * Concrete CryEngine 3D-engine implementation.
     *
     * The primary table at 0x1840205E8 implements all 295 I3DEngine entries
     * and appends eleven concrete operations. Matching allocation and scalar
     * deletion paths prove the complete 0x1418-byte size. Additional RTTI
     * bases beginning at +0x08 are retained inside the opaque state.
     */
    class C3DEngine: public I3DEngine {
      public:
        ~C3DEngine() override = default;

        virtual void unk_fn0295() = 0;
        virtual void unk_fn0296() = 0;
        virtual void unk_fn0297() = 0;
        virtual void unk_fn0298() = 0;
        virtual void unk_fn0299() = 0;
        virtual void unk_fn0300() = 0;
        virtual void unk_fn0301() = 0;
        virtual void unk_fn0302() = 0;
        virtual void unk_fn0303() = 0;
        virtual void unk_fn0304() = 0;
        virtual void unk_fn0305() = 0; // +0x988

        /** The one 3D engine, which `I3DEngine::GetInstance` hands out through its primary base. */
        static C3DEngine *GetInstance() {
            return static_cast<C3DEngine *>(I3DEngine::GetInstance());
        }

        /**
         * The level's static object table, or null before a level has loaded
         * one. Stock CryEngine's `m_pLevelStatObjTable`: the index each layer
         * physics record carries resolves through it, bounds-checked, in
         * `C_PhysicalizationManager::BuildLayerColliders` (0x180845740).
         */
        const LevelStatObjTableLayout *GetLevelStatObjTable() const {
            return m_pLevelStatObjTable;
        }

        /**
         * The zoom state the main view's passes compare against. Building a
         * general pass (`SRenderingPassInfo::CreateGeneralPassRenderingInfo`
         * 0x18043E940) writes both as if the pass were the main view's, so a
         * secondary pass saves them first and puts them back after; left
         * changed, the next main pass reads a zoom in progress.
         */
        struct ZoomState {
            float prevZoomFactor;
            std::uint32_t zoomMode;
        };

        ZoomState GetZoomState() const {
            return ZoomState {m_prevZoomFactor, m_zoomMode};
        }

        void RestoreZoomState(const ZoomState &state) {
            m_prevZoomFactor = state.prevZoomFactor;
            m_zoomMode       = state.zoomMode;
        }

      private:
        std::byte m_opaque0008[0x18];
        LevelStatObjTableLayout *m_pLevelStatObjTable; // 0x20
        std::byte m_opaque0028[0x81C];                 // from 0x28, the level's material table, which a record's material index resolves through
        float m_prevZoomFactor;                        // 0x844, I3DEngine slots 275/277 (+0x898 get, +0x8A8 set)
        std::byte m_opaque0848[0x4];
        std::uint32_t m_zoomMode; // 0x84C, I3DEngine slots 274/276 (+0x890 get, +0x8A0 set)
        std::byte m_opaque0850[0xBC8];

        static void AssertLayout() {
            static_assert(offsetof(C3DEngine, m_pLevelStatObjTable) == 0x20, "C3DEngine::m_pLevelStatObjTable offset mismatch");
            static_assert(offsetof(C3DEngine, m_prevZoomFactor) == 0x844, "C3DEngine::m_prevZoomFactor offset mismatch");
            static_assert(offsetof(C3DEngine, m_zoomMode) == 0x84C, "C3DEngine::m_zoomMode offset mismatch");
        }
    };

    static_assert(sizeof(C3DEngine) == 0x1418, "C3DEngine size mismatch");
} // namespace SDK
