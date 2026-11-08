#include "c_physical_world.h"

#include "../system/s_system_global_environment.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstdint>

namespace SDK {
    CPhysicalWorld *CPhysicalWorld::GetInstance() {
        return SSystemGlobalEnvironment::Get().pPhysicalWorld;
    }

    int CPhysicalWorld::RayWorldIntersection(const Vec3 &origin, const Vec3 &direction, int objectTypes, unsigned int flags, SRayHit *hits, int maxHits, IPhysicalEntity **skip, int skipCount) {
        return hook::this_call<int>(gPatterns.CPhysicalWorld__RayWorldIntersection, this, &origin, &direction, objectTypes, flags, hits, maxHits, skip, skipCount, nullptr, 0, "KCDC");
    }

    bool CPhysicalWorld::ResolveGround(const Vec3 &position, float probeUp, float probeDown, Vec3 &ground, IPhysicalEntity **skip, int skipCount) {
        const Vec3 origin {position.x, position.y, position.z + probeUp};
        const Vec3 direction {0.0F, 0.0F, -(probeUp + probeDown)};

        SRayHit hit {};
        if (RayWorldIntersection(origin, direction, kGroundObjectTypes, kGroundRayFlags, &hit, 1, skipCount != 0 ? skip : nullptr, skipCount) <= 0) {
            return false;
        }

        ground = hit.point;
        return true;
    }
} // namespace SDK
