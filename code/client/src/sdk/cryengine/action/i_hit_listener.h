#pragma once

#include <cstddef>

namespace SDK {
    struct HitInfo;
    struct ExplosionInfo;

    /** Native four-slot interface at 0x183AA59B0, also used by CProjectile. */
    struct IHitListener {
        virtual ~IHitListener()                                        = default;
        virtual void OnHit(const HitInfo &hit)                         = 0;
        virtual void OnExplosion(const ExplosionInfo &explosion)       = 0;
        virtual void OnServerExplosion(const ExplosionInfo &explosion) = 0;
    };

    static_assert(sizeof(IHitListener) == 0x8, "IHitListener size mismatch");
} // namespace SDK
