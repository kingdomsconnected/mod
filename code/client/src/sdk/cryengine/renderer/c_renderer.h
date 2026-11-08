#pragma once

#include "c_render_display_context.h"
#include "i_renderer.h"
#include "s_display_context_key.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK {
    /**
     * The renderer behind `IRenderer`: `CRenderer`, whose `CD3D9Renderer`
     * subclass is the one gEnv publishes. Only its non-virtual lookups are
     * modelled.
     */
    class CRenderer: public IRenderer {
      public:
        CRenderer()                             = delete;
        CRenderer(const CRenderer &)            = delete;
        CRenderer &operator=(const CRenderer &) = delete;

        static CRenderer *GetInstance() {
            return static_cast<CRenderer *>(IRenderer::GetInstance());
        }

        /**
         * The display context behind a key (0x1805719C4), or null for a key
         * the renderer does not hold. An empty key answers the base context,
         * the main viewport's.
         */
        CRenderDisplayContext *FindDisplayContext(const SDisplayContextKey &key) {
            return hook::this_call<CRenderDisplayContext *>(gPatterns.CRenderer__FindDisplayContext, this, &key);
        }

      protected:
        ~CRenderer() = default;
    };
} // namespace SDK
