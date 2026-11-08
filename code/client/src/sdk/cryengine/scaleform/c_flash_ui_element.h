#pragma once

#include "flash_variable_object_ref.h"
#include "i_flash_player_handlers.h"
#include "i_ui_element.h"
#include "i_ui_element_event_listener.h"
#include "i_virtual_keyboard_events.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace SDK {
    /**
     * Engine-owned XML movie element. Constructor: 0x18088DF88.
     * Interface bases occupy 0x00, 0x08, 0x10 and 0x18.
     */
    class CFlashUIElement
        : public IUIElement
        , public IFSCommandHandler
        , public IExternalInterfaceHandler
        , public IVirtualKeyboardEvents {
      public:
        /** Registration is non-owning; unregister before listener destruction. */
        void AddEventListener(IUIElementEventListener *listener, const char *name);
        void RemoveEventListener(IUIElementEventListener *listener);

        /** Borrowed for immediate use only; invalidated by native unload. */
        IFlashPlayer *GetLoadedFlashPlayer() const {
            return m_pFlashPlayer.get();
        }

        /** Resolves against the current display tree without initializing or caching a movie. */
        bool GetLoadedMovieClip(const char *name, FlashVariableObjectRef &clip);

      private:
        std::int32_t m_refCount;                      // 0x20, ctor 0
        std::byte m_opaque0024[0x4];                  // 0x24
        void *m_pFlashUI;                             // 0x28, CFlashUI *, ctor argument
        const char *m_sName;                          // 0x30, CryStringT<char> payload, ctor empty
        const char *m_sGroupName;                     // 0x38, CryStringT<char> payload, ctor empty
        const char *m_sFlashFile;                     // 0x40, CryStringT<char> payload, ctor empty
        float m_fAlpha;                               // 0x48, ctor 0
        std::int32_t m_iLayer;                        // 0x4C, ctor 0
        bool m_bIsValid;                              // 0x50, ctor false
        bool m_bIsHideRequest;                        // 0x51, ctor false
        bool m_bUnloadRequest;                        // 0x52, ctor false
        bool m_bUnloadAll;                            // 0x53, ctor false
        std::byte m_opaque0054[0x4];                  // 0x54
        std::shared_ptr<IFlashPlayer> m_pFlashPlayer; // 0x58
        void *m_pBootStrapper;                        // 0x68, IFlashPlayerBootStrapper *, ctor null
        const void *m_pRoot;                          // 0x70, const SUIMovieClipDesc *
        bool m_bVisible;                              // 0x78, ctor false
        std::byte m_opaque0079[0x7];                  // 0x79
        std::uint64_t m_iFlags;                       // 0x80, ctor 0
        bool m_bCursorVisible;                        // 0x88, ctor false
        bool m_bNeedLazyUpdate;                       // 0x89, ctor false
        bool m_bNeedLazyRender;                       // 0x8A, ctor false
        std::byte m_opaque008B[0x1];                  // 0x8B
        std::byte m_constraints[0x20];                // 0x8C, IUIElement::SUIConstraints: type 2, left/top 0, 1024x768, align 1/1, scale true
        std::uint32_t m_opaque00AC;                   // 0xAC, ctor 0
        std::byte m_opaque00B0[0x8];                  // 0xB0, not written by the ctor
        void *m_baseInfo;                             // 0xB8, XmlNodeRef payload, ctor null
        std::byte m_StringBufferSet[0x10];            // 0xC0, std::set<string>
        CFlashUIElement *m_pBaseInstance;             // 0xD0, ctor argument
        std::uint32_t m_iInstanceID;                  // 0xD8, ctor argument
        std::byte m_opaque00DC[0x4];                  // 0xDC
        void *m_instancesLock;                        // 0xE0, SRWLOCK; not in CryEngine
        std::byte m_instances[0x18];                  // 0xE8, std::vector<CFlashUIElement *>
        std::byte m_variables[0x18];                  // 0x100, TUIParamsLookup
        std::byte m_arrays[0x18];                     // 0x118, TUIParamsLookup
        std::byte m_displayObjects[0x18];             // 0x130, TUIMovieClipLookup
        std::byte m_displayObjectsTmpl[0x18];         // 0x148, TUIMovieClipLookup
        std::byte m_events[0x18];                     // 0x160, TUIEventsLookup
        std::byte m_functions[0x18];                  // 0x178, TUIEventsLookup
        std::int32_t m_firstDynamicDisplObjIndex;     // 0x190, ctor 0
        std::byte m_opaque0194[0x4];                  // 0x194
        std::byte m_opaque0198[0xB8];                 // 0x198, event listeners and KCD2 additions: eight -1 ids at 0x1AC, 2400 at 0x1CC, two lists at 0x1F8/0x208, an SRWLOCK at 0x240

        static void AssertLayout() {
            static_assert(offsetof(CFlashUIElement, m_refCount) == 0x20, "CFlashUIElement::m_refCount offset mismatch");
            static_assert(offsetof(CFlashUIElement, m_pFlashUI) == 0x28, "CFlashUIElement::m_pFlashUI offset mismatch");
            static_assert(offsetof(CFlashUIElement, m_sName) == 0x30, "CFlashUIElement::m_sName offset mismatch");
            static_assert(offsetof(CFlashUIElement, m_fAlpha) == 0x48, "CFlashUIElement::m_fAlpha offset mismatch");
            static_assert(offsetof(CFlashUIElement, m_bIsValid) == 0x50, "CFlashUIElement::m_bIsValid offset mismatch");
            static_assert(offsetof(CFlashUIElement, m_pFlashPlayer) == 0x58, "CFlashUIElement::m_pFlashPlayer offset mismatch");
            static_assert(offsetof(CFlashUIElement, m_pBootStrapper) == 0x68, "CFlashUIElement::m_pBootStrapper offset mismatch");
            static_assert(offsetof(CFlashUIElement, m_bVisible) == 0x78, "CFlashUIElement::m_bVisible offset mismatch");
            static_assert(offsetof(CFlashUIElement, m_iFlags) == 0x80, "CFlashUIElement::m_iFlags offset mismatch");
            static_assert(offsetof(CFlashUIElement, m_constraints) == 0x8C, "CFlashUIElement::m_constraints offset mismatch");
            static_assert(offsetof(CFlashUIElement, m_pBaseInstance) == 0xD0, "CFlashUIElement::m_pBaseInstance offset mismatch");
            static_assert(offsetof(CFlashUIElement, m_iInstanceID) == 0xD8, "CFlashUIElement::m_iInstanceID offset mismatch");
            static_assert(offsetof(CFlashUIElement, m_instances) == 0xE8, "CFlashUIElement::m_instances offset mismatch");
            static_assert(offsetof(CFlashUIElement, m_functions) == 0x178, "CFlashUIElement::m_functions offset mismatch");
            static_assert(offsetof(CFlashUIElement, m_firstDynamicDisplObjIndex) == 0x190, "CFlashUIElement::m_firstDynamicDisplObjIndex offset mismatch");
        }
    };

    static_assert(sizeof(CFlashUIElement) == 0x250, "CFlashUIElement size mismatch");
} // namespace SDK
