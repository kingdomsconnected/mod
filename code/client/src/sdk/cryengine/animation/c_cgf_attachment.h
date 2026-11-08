#pragma once

#include "i_attachment_object.h"

#include "../../patterns.h"
#include "../3dengine/i_stat_obj.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete CryAnimation static-geometry attachment payload.
     *
     * RTTI identifies this global-namespace type as a struct and proves the
     * hierarchy CCGFAttachment -> IAttachmentObject. Its table at 0x183E5F4C0
     * implements exactly the 22 inherited entries and adds no operations.
     * Default construction, allocation, and sized deletion prove size 0x18.
     *
     * The two members are the construction's own: every inline builder --
     * `CProceduralClipAttachProp::OnEnter` (0x1836DD358),
     * `CScriptBind_Entity::SetAttachmentCGF`, `SetAttachmentObject` -- zeroes
     * +0x08 and +0x10 and then assigns the loaded mesh to +0x08 through
     * `_smart_ptr<IStatObj>::operator=`. KCD2's build has no render node
     * member.
     */
    struct CCGFAttachment : IAttachmentObject {
        ~CCGFAttachment() override = default;

        /**
         * Builds one around `mesh` the way the game does: allocated from the
         * game's own heap, because the attachment it is bound to releases it
         * through its own `delete this`, and holding one share of the mesh.
         * Hand the result to `IAttachment::AddBinding`, which owns it from
         * then on; nothing here keeps a pointer to it.
         */
        static CCGFAttachment *Create(IStatObj *mesh) {
            auto *storage = static_cast<std::byte *>(hook::call<void *>(gPatterns.CryActionAllocate, sizeof(CCGFAttachment)));
            if (storage == nullptr) {
                return nullptr;
            }
            // The game's vtable, written over the front: a C++ construction
            // here would install the mod's own table, whose destructor frees
            // through the mod's heap.
            *reinterpret_cast<std::uint64_t *>(storage) = gPatterns.CCGFAttachment__VTable;
            auto *attachment                            = reinterpret_cast<CCGFAttachment *>(storage);
            attachment->m_pObj                          = mesh;
            attachment->m_pReplacementMaterial          = nullptr;
            if (mesh != nullptr) {
                mesh->AddRef();
            }
            return attachment;
        }

      private:
        IStatObj *m_pObj;              // 0x08, _smart_ptr<IStatObj>
        void *m_pReplacementMaterial; // 0x10, _smart_ptr<IMaterial>

        static void AssertLayout() {
            static_assert(offsetof(CCGFAttachment, m_pObj) == 0x08, "CCGFAttachment::m_pObj offset mismatch");
            static_assert(offsetof(CCGFAttachment, m_pReplacementMaterial) == 0x10, "CCGFAttachment::m_pReplacementMaterial offset mismatch");
        }
    };

    static_assert(sizeof(CCGFAttachment) == 0x18, "CCGFAttachment size mismatch");
} // namespace SDK
