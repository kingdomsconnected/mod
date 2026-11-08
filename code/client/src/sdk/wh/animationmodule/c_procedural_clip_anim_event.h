#pragma once

#include "../../cryengine/animation/i_procedural_clip.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::animationmodule {
    /**
     * The parameters of an `AnimEvent` procedural clip: the event name the
     * ADB gives it (`<EventName value="AttachHammer" />`).
     *
     * RTTI names the struct. Its factory (0x180E0DBC8) allocates 0x10 bytes,
     * writes the vtable and zeroes the name; the clip's OnEnter (0x18085731C)
     * reads +0x08 as the name, falling back to an empty string when it is
     * null.
     */
    struct S_ProcClipcAnimEventParams {
        virtual ~S_ProcClipcAnimEventParams() = default;

        /** The event name, or null for a clip that names none. */
        const char *GetEventName() const {
            return m_eventName;
        }

      private:
        const char *m_eventName; // 0x08

        static void AssertLayout() {
            static_assert(offsetof(S_ProcClipcAnimEventParams, m_eventName) == 0x08, "S_ProcClipcAnimEventParams::m_eventName offset mismatch");
        }
    };

    static_assert(sizeof(S_ProcClipcAnimEventParams) == 0x10, "S_ProcClipcAnimEventParams size mismatch");

    /**
     * The `AnimEvent` procedural clip (registered at 0x181AD558C), a
     * `TProceduralContextualClip<C_ProceduralClipAnimEventContext,
     * S_ProcClipcAnimEventParams>`.
     *
     * It plays no animation. Entering, it raises its event name with `true`
     * on the body's `AnimEventContext` signal (0x18085731C, through
     * 0x18085738C); leaving, the same name with `false` (0x180857374). The
     * native smithing action subscribes to that signal while it runs
     * (0x18148A1F8) and moves the smithery's tools on what it hears
     * (0x180857500 -> 0x180857548). Its factory (0x180E0DCB0) allocates
     * 0x48 bytes: the `IProceduralClip` base, a copy of its params at +0x28,
     * the context at +0x38 and the raised name at +0x40.
     */
    class C_ProceduralClipAnimEvent: public SDK::IProceduralClip {
      private:
        std::byte m_opaque0028[0x20]; // 0x28

        static void AssertLayout() {
            static_assert(offsetof(C_ProceduralClipAnimEvent, m_opaque0028) == 0x28, "C_ProceduralClipAnimEvent::m_opaque0028 offset mismatch");
        }
    };

    static_assert(sizeof(C_ProceduralClipAnimEvent) == 0x48, "C_ProceduralClipAnimEvent size mismatch");
} // namespace SDK::wh::animationmodule
