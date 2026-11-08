#pragma once

#include "../3dengine/i_render_node.h"
#include "i_entity_render_proxy.h"

#include <cstddef>

namespace SDK {
    /**
     * An entity's render component, and the render node the 3D engine draws
     * it -- characters, their attachments and all -- through.
     *
     * RTTI gives this global-namespace class two tables: the primary one at
     * 0x183A49FD8 is `IRenderNode`'s (`Render` at slot 3, `GetRenderNodeType`
     * at slot 7 answering 19, eERType_RenderProxy), and the one at
     * 0x183A4A268 is `IEntityRenderProxy`'s, at +0x50 -- which is exactly
     * `sizeof(IRenderNode)`, so the declared base order reproduces it and a
     * `static_cast` from the proxy `IEntity::GetProxy` hands out lands on the
     * render node. The entity system's render-proxy pool (0x1854999E0,
     * built in 0x180D25458) is sized 0x120 per element.
     */
    class CRenderProxy : public IRenderNode, public IEntityRenderProxy {
      public:
        /** The render node behind the proxy `IEntity::GetProxy(EEntityProxy::Render)` returns. */
        static CRenderProxy *FromProxy(IEntityProxy *proxy) {
            return static_cast<CRenderProxy *>(proxy);
        }

      private:
        std::byte m_opaque0058[0x120 - 0x58];
    };

    static_assert(sizeof(CRenderProxy) == 0x120, "CRenderProxy size mismatch");
} // namespace SDK
