#pragma once

#include "c_compass_mark.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    /** One entry of the compass's mark vector, borrowed rather than owned. */
    struct SCompassMarkSlot {
        C_CompassMark *mark;
        SNativeRefCount *control;
    };

    static_assert(sizeof(SCompassMarkSlot) == 0x10, "SCompassMarkSlot must match std::shared_ptr's two-pointer layout");

    /**
     * The HUD compass, and the owner of every mark on it.
     *
     * There is one for the process: `wh::guimodule::GetUICompass` walks the
     * GUI module's page vector once, then memoizes the hit in a global it
     * never clears. What does change is the marks -- `RemoveAllMarks` runs on
     * UI and world lifecycle edges and empties the vector, after which the
     * marks the mod still holds report `IsRegistered() == false`.
     *
     * The factory (sub_1817869EC) allocates 0x110 bytes. The class is still
     * never constructed, copied or arrayed by the mod, only pointed at.
     */
    class C_UICompass final {
      public:
        C_UICompass()                               = delete;
        ~C_UICompass()                              = delete;
        C_UICompass(const C_UICompass &)            = delete;
        C_UICompass &operator=(const C_UICompass &) = delete;

        /** Null until the HUD is built, which is before any level loads. */
        static C_UICompass *GetInstance();

        /**
         * Takes the mark over: the compass keeps its own reference and the
         * handle passed in is consumed. Clone() one first to keep a handle.
         * There is no rejection path.
         */
        void AddMark(CompassMarkRef &&mark);

        /**
         * Drops the mark, matching by object pointer. False when this compass
         * does not own it, which is the ordinary answer after a
         * `RemoveAllMarks`; the handle is consumed either way.
         *
         * The caller must still hold a reference of its own across the call:
         * the native releases the compass's before it reads the mark again to
         * unregister it from flash.
         */
        bool RemoveMark(CompassMarkRef &&mark);

        void RemoveAllMarks();

        std::size_t GetMarkCount() const {
            return static_cast<std::size_t>(m_marksEnd - m_marksBegin);
        }

        /** Borrowed; valid only until the next add, remove or reset. */
        const SCompassMarkSlot *GetMarks() const {
            return m_marksBegin;
        }

      private:
        // Constructor sub_181786B0C over a 0x110-byte allocation
        // (sub_1817869EC).
        std::byte m_flashBase[0x58];       // 0x00; the C_UIFlashBase layer (sub_180BC86C4)
        const void *m_pSecondaryVTable;    // 0x58; C_UICompass

        // std::vector<std::shared_ptr<C_CompassMark>> at 0x60.
        SCompassMarkSlot *m_marksBegin;       // 0x60
        SCompassMarkSlot *m_marksEnd;         // 0x68
        SCompassMarkSlot *m_marksCapacityEnd; // 0x70
        std::byte m_hashMap0078[0x40];        // 0x78; std::unordered_map (load factor 1.0, bucket mask 7/8)
        std::byte m_hashMap00B8[0x40];        // 0xB8; std::unordered_map (sub_180AD7AD4)
        bool m_flag00F8;                      // 0xF8; ctor 1
        std::byte m_padding00F9[0x7];
        std::uint64_t m_value0100;            // 0x100; ctor copies qword_184932280
        std::uint32_t m_value0108;            // 0x108; ctor copies dword_184932288
        std::byte m_padding010C[0x4];

        static void AssertLayout() {
            static_assert(offsetof(C_UICompass, m_marksBegin) == 0x60, "C_UICompass::m_marksBegin offset mismatch");
            static_assert(offsetof(C_UICompass, m_marksEnd) == 0x68, "C_UICompass::m_marksEnd offset mismatch");
            static_assert(offsetof(C_UICompass, m_marksCapacityEnd) == 0x70, "C_UICompass::m_marksCapacityEnd offset mismatch");
            static_assert(offsetof(C_UICompass, m_hashMap0078) == 0x78, "C_UICompass::m_hashMap0078 offset mismatch");
            static_assert(offsetof(C_UICompass, m_flag00F8) == 0xF8, "C_UICompass::m_flag00F8 offset mismatch");
            static_assert(offsetof(C_UICompass, m_value0108) == 0x108, "C_UICompass::m_value0108 offset mismatch");
        }
    };

    static_assert(sizeof(C_UICompass) == 0x110, "C_UICompass size mismatch");
} // namespace SDK::wh::guimodule
