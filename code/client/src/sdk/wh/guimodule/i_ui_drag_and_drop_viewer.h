#pragma once

namespace SDK::wh::guimodule {
    /** Drag-and-drop viewing contract mixed into native inventory-style UIs. */
    class I_UIDragAndDropViewer {
      public:
        virtual void DragOpaque00() = 0;
        virtual void DragOpaque01() = 0;
        virtual void DragOpaque02() = 0;
        virtual void DragOpaque03() = 0;
        virtual void DragOpaque04() = 0;
        virtual void DragOpaque05() = 0;
        virtual void DragOpaque06() = 0;
    };

    static_assert(sizeof(I_UIDragAndDropViewer) == 0x8,
                  "I_UIDragAndDropViewer size mismatch");
} // namespace SDK::wh::guimodule
