#pragma once

#include "../3dengine/i_material.h"
#include "../3dengine/i_render_node.h"
#include "i_entity_proxy.h"

namespace SDK {
    /**
     * The render component's own interface on top of `IEntityProxy`, what
     * `IEntity::GetProxy(EEntityProxy::Render)` hands out. RTTI
     * `.?AUIEntityRenderProxy@@`; `CRenderProxy` implements it at +0x50 with
     * the table at 0x183A4A268. Only slots proven against that table are
     * named.
     */
    struct IEntityRenderProxy: public IEntityProxy {
        virtual void unk_fn0022() = 0;

        /**
         * The material slot `slot` renders with (0x18041FDB0); a negative
         * slot answers the first slot that has one. This is the entity's own
         * material, the one the book screen's binder clones a sub-material of
         * in place before writing the page into it.
         */
        virtual IMaterial *GetRenderMaterial(int slot) = 0; // +0x0B8

        virtual void unk_fn0024() = 0;
        virtual void unk_fn0025() = 0;

        /** The `IRenderNode` this interface is part of, `this - 0x50` (0x180441BA0). */
        virtual IRenderNode *GetRenderNode() = 0; // +0x0D0
    };
} // namespace SDK
