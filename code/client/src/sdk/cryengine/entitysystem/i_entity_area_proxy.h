#pragma once

#include "../../types.h"
#include "i_entity_proxy.h"

namespace SDK {
    /**
     * The area an entity carries: a volume the entity system tests bodies
     * against, raising enter and leave on *this* entity.
     *
     * Created with `IEntity::CreateProxy(EEntityProxy::Area)`. `CAreaProxy`'s
     * vtable is 0x183A79E50 and `IEntityProxy`'s prefix is 22 slots, so this
     * interface starts at slot 22. The box and shape setters share the run and
     * are left opaque rather than guessed at.
     */
    class IEntityAreaProxy: public IEntityProxy {
      public:
        virtual void Opaque22() = 0; // +0x0B0

        /**
         * Makes this area a sphere, in the entity's own space -- the entity's
         * pose is what places it. +0x0B8: `CAreaProxy::SetSphere` (0x180A34A00)
         * stores the centre at proxy+120 and the radius at +132, empties the
         * point list, then transforms the centre into the area.
         */
        virtual void SetSphere(const Vec3 &center, float radius) = 0; // +0x0B8
    };
} // namespace SDK
