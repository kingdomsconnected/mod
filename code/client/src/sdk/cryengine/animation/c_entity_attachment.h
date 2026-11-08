#pragma once

#include "../../patterns.h"
#include "../../types.h"
#include "../entitysystem/i_entity.h"
#include "i_attachment_object.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete CryAnimation entity attachment payload.
     *
     * RTTI identifies this global-namespace type as a struct and proves the
     * hierarchy CEntityAttachment -> IAttachmentObject. Its table at
     * 0x183A68F68 implements exactly the 22 inherited entries and adds no
     * operations. Matching allocation and sized-deletion paths prove 0x28.
     */
    struct CEntityAttachment : IAttachmentObject {
        ~CEntityAttachment() override = default;

        /**
         * The entity this socket carries.
         *
         * An entity attachment is the one payload that renders on its own:
         * `ProcessAttachment` only writes the socket transform into it, and
         * the type exposes no `IRenderNode`, so the visibility a character
         * propagates to its payloads cannot reach it. Hiding what a body
         * carries therefore means hiding these entities -- KCD2 holsters and
         * hands hold `CharHolsterItem` and pickable item entities this way.
         */
        EntityId GetEntityId() const { return m_id; }

        /**
         * Builds one carrying `bound` on a socket of `holder`'s character, as
         * `C_BlacksmithingWorkpieceAttachment` builds the one that carries a
         * workpiece in the tongs (0x180B66624): 0x28 bytes from the game's
         * heap, constructed natively (0x1809DFD44), with no flags. The game's
         * deleting destructor frees through `FreeActionStorage`, the pair of
         * `CryActionAllocate`. Hand the result to `IAttachment::AddBinding`,
         * which owns it from then on.
         */
        static CEntityAttachment *Create(IEntity *bound, IEntity *holder) {
            void *storage = hook::call<void *>(gPatterns.CryActionAllocate, sizeof(CEntityAttachment));
            if (storage == nullptr) {
                return nullptr;
            }
            return hook::this_call<CEntityAttachment *>(gPatterns.CEntityAttachment__Construct, storage, bound, holder, static_cast<std::uint8_t>(0));
        }

      private:
        std::uint32_t m_id;       // 0x08, the bound entity's GetId() in the constructor at 0x1809DFD44
        Vec3 m_scale;             // 0x0C, (1, 1, 1)
        void *m_pointer0018;      // 0x18, null; KCD2 addition after CryEngine's m_scale
        std::uint8_t m_flags0020; // 0x20, constructor flags: bit 0 and bit 1 copy two render settings from the parent
        std::byte m_pad0021[0x7]; // 0x21

        static void AssertLayout() {
            static_assert(offsetof(CEntityAttachment, m_id) == 0x08, "CEntityAttachment::m_id offset mismatch");
            static_assert(offsetof(CEntityAttachment, m_scale) == 0x0C, "CEntityAttachment::m_scale offset mismatch");
            static_assert(offsetof(CEntityAttachment, m_flags0020) == 0x20, "CEntityAttachment::m_flags0020 offset mismatch");
        }
    };

    static_assert(sizeof(CEntityAttachment) == 0x28,
                  "CEntityAttachment size mismatch");
} // namespace SDK
