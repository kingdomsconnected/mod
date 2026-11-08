#pragma once

#include "c_blacksmithing_tool_attachment.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * A smithery tool the smith holds as an item rather than wears as a
     * mesh. Vtable 0x183A7BA48; allocated 0x30 by 0x180B67820, constructed
     * by 0x180B67A4C.
     *
     * The tool is an item of the smithery, never of the smith. `Attach`
     * (0x18193F148) moves it through the smith's attachment manager with
     * `CompleteItemTransfer(descriptor, true)` -- immediate, direct, amount
     * one, no destination holder -- so it passes through the smith's own
     * inventory into a hand. `Detach` (0x180B67CBC) finds the smith again by
     * `m_actorEntityId` and gives it back with `BeginItemTransfer`. The
     * destructor (0x180B67AD4) detaches first.
     */
    class C_BlacksmithingSlotToolAttachment final: public SDK::wh::playermodule::C_BlacksmithingToolAttachment {
      public:
        /** The tool's item; set by the constructor from the resolved tool entity. */
        std::uint64_t GetToolItemWuid() const {
            return m_toolItemWuid;
        }

      private:
        std::uint64_t m_toolItemWuid; // 0x28

        static void AssertLayout() {
            static_assert(offsetof(C_BlacksmithingSlotToolAttachment, m_toolItemWuid) == 0x28, "C_BlacksmithingSlotToolAttachment::m_toolItemWuid offset mismatch");
        }
    };

    static_assert(sizeof(C_BlacksmithingSlotToolAttachment) == 0x30, "C_BlacksmithingSlotToolAttachment size mismatch");
} // namespace SDK::wh::playermodule
