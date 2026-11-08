#pragma once

#include "../../types.h"
#include "c_ui_flash_object.h"
#include "e_compass_mark_type.h"
#include "native_shared_ref.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    class C_UIMap;
    class C_UICompass;

    /**
     * One mark on the HUD compass: a quest objective, a point of interest, or
     * the marker the player drops on the map. Constructed at 0x181F479C0.
     */
    class alignas(void *) C_CompassMark {
      public:
        C_CompassMark()                                 = delete;
        ~C_CompassMark()                                = delete;
        C_CompassMark(const C_CompassMark &)            = delete;
        C_CompassMark &operator=(const C_CompassMark &) = delete;

        const Vec3 &GetWorldPosition() const {
            return m_worldPosition;
        }

        /**
         * The whole of a move: `C_UICompass::UpdateMarks` re-reads the Vec3
         * every frame and recomputes bearing and distance from it.
         */
        void SetWorldPosition(const Vec3 &position) {
            m_worldPosition = position;
        }

        E_CompassMarkType GetType() const {
            return static_cast<E_CompassMarkType>(m_markType);
        }

        std::int32_t GetState() const {
            return m_state;
        }

        /**
         * Writes the state and tells flash. Reaches through the owning
         * compass, so it is only legal while IsRegistered().
         */
        void SetState(std::int32_t state);

        /**
         * `AddToFlashCompass` stores the owner here and `RemoveFromFlashCompass`
         * clears it, so this goes false on its own when the game runs
         * `C_UICompass::RemoveAllMarks` across a level edge.
         */
        bool IsRegistered() const {
            return m_pFlashOwner != nullptr;
        }

        /**
         * What marks this as belonging to a followed quest's objective: the
         * quest's tracking slot and the objective's index, which the compass
         * colours the mark by. `AddObjectiveCompassMarks` (0x180DC5F24)
         * writes both between building the mark and adding it, so they are
         * set before AddMark, not after.
         */
        void SetQuestObjective(std::int32_t trackingId, std::int32_t objectiveIndex) {
            m_questTrackingId     = trackingId;
            m_questObjectiveIndex = objectiveIndex;
        }

        /** Null for a static mark, i.e. one the mod built. */
        const void *GetSourceObject() const {
            return m_pSourceObject;
        }

      private:
        // The constructor installs I_AreaTracker and
        // I_ObjectManagerBaseListener tables first, then builds the
        // C_UIFlashObject at +0x10 (0x1807EA8AC) and overwrites all three
        // with C_CompassMark's own.
        const void *m_pAreaTrackerVTable;     // 0x00
        const void *m_pObjectListenerVTable;  // 0x08
        C_UIFlashObject m_flashObject;        // 0x10; its id at +0x18 keys every flash call
        void *m_weakThis[2];                  // 0x28; enable_shared_from_this {pointer, control block}, ctor 0

        C_UICompass *m_pFlashOwner; // 0x38
        void *m_pSourceObject;      // 0x40, the constructor's owner argument

        std::uint8_t m_sourceAttached; // 0x48, listening to m_pSourceObject
        std::uint8_t m_followsSource;  // 0x49, position comes from it, not from the Vec3
        std::byte m_padding004A[0x2];

        std::int32_t m_markType; // 0x4C
        Vec3 m_worldPosition;    // 0x50

        // All three recomputed every frame by
        // C_CompassMark::UpdateCompassData; the constructor zeroes them.
        float m_bearing;           // 0x5C
        float m_verticalDirection; // 0x60
        float m_distance;          // 0x64

        std::int32_t m_questTrackingId;     // 0x68
        std::int32_t m_questObjectiveIndex; // 0x6C
        std::uint16_t m_value0070;          // 0x70; ctor 0
        std::uint8_t m_value0072;           // 0x72; ctor 0
        std::byte m_padding0073[0x1];

        std::int32_t m_state; // 0x74

        static void AssertLayout() {
            static_assert(offsetof(C_CompassMark, m_flashObject) == 0x10, "C_CompassMark::m_flashObject offset mismatch");
            static_assert(offsetof(C_CompassMark, m_weakThis) == 0x28, "C_CompassMark::m_weakThis offset mismatch");
            static_assert(offsetof(C_CompassMark, m_pFlashOwner) == 0x38, "C_CompassMark::m_pFlashOwner offset mismatch");
            static_assert(offsetof(C_CompassMark, m_bearing) == 0x5C, "C_CompassMark::m_bearing offset mismatch");
            static_assert(offsetof(C_CompassMark, m_value0070) == 0x70, "C_CompassMark::m_value0070 offset mismatch");
            static_assert(offsetof(C_CompassMark, m_pSourceObject) == 0x40, "C_CompassMark::m_pSourceObject offset mismatch");
            static_assert(offsetof(C_CompassMark, m_sourceAttached) == 0x48, "C_CompassMark::m_sourceAttached offset mismatch");
            static_assert(offsetof(C_CompassMark, m_followsSource) == 0x49, "C_CompassMark::m_followsSource offset mismatch");
            static_assert(offsetof(C_CompassMark, m_markType) == 0x4C, "C_CompassMark::m_markType offset mismatch");
            static_assert(offsetof(C_CompassMark, m_worldPosition) == 0x50, "C_CompassMark::m_worldPosition offset mismatch");
            static_assert(offsetof(C_CompassMark, m_questTrackingId) == 0x68, "C_CompassMark::m_questTrackingId offset mismatch");
            static_assert(offsetof(C_CompassMark, m_questObjectiveIndex) == 0x6C, "C_CompassMark::m_questObjectiveIndex offset mismatch");
            static_assert(offsetof(C_CompassMark, m_state) == 0x74, "C_CompassMark::m_state offset mismatch");
        }
    };

    static_assert(sizeof(C_CompassMark) == 0x78, "C_CompassMark size mismatch");

    /** One reference to a compass mark, owned by whoever holds it. */
    using CompassMarkRef = NativeSharedRef<C_CompassMark>;

    /**
     * Builds a mark with no source object, whose position is the Vec3 it is
     * handed and nothing else, and hands back the only reference to it.
     * Adding it to a compass is a separate step. Empty on allocation failure,
     * which is the native's only failure.
     */
    CompassMarkRef MakeCompassMark(E_CompassMarkType type, const Vec3 &position, std::int32_t state);

    /**
     * The marker the player drops on the map -- the game calls it a
     * checkpoint. At most one per map, all of them held by C_UIMap.
     *
     * Its factory at 0x182AFB3DC allocates 0x90 for a
     * _Ref_count_obj2<C_CheckpointMark>: a 0x10 control block, then the object.
     */
    class C_CheckpointMark final: public C_CompassMark {
      public:
        C_CheckpointMark()                                    = delete;
        ~C_CheckpointMark()                                   = delete;
        C_CheckpointMark(const C_CheckpointMark &)            = delete;
        C_CheckpointMark &operator=(const C_CheckpointMark &) = delete;

        /** Maps reuse one coordinate range, so a position needs its map. */
        std::int32_t GetMapId() const {
            return m_mapId;
        }

      private:
        std::int32_t m_mapId; // 0x78

        static void AssertLayout() {
            static_assert(offsetof(C_CheckpointMark, m_mapId) == 0x78, "C_CheckpointMark::m_mapId offset mismatch");
        }
    };

    static_assert(sizeof(C_CheckpointMark) == 0x80, "C_CheckpointMark size mismatch");

    /** What C_UIMap::FindCheckpointForCurrentMap answers, owned by the caller. */
    using CheckpointMarkRef = NativeSharedRef<C_CheckpointMark>;
} // namespace SDK::wh::guimodule
