#include "../../types.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace SDK {
    Quat QuatFromAngles(const Ang3 &anglesRadians) {
        Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};
        hook::this_call<void>(gPatterns.Quat__SetRotationXYZ, &rotation, &anglesRadians);
        return rotation;
    }

    Ang3 AnglesFromQuat(const Quat &rotation) {
        Ang3 angles {};
        hook::this_call<void>(gPatterns.Ang3__FromQuat, &angles, &rotation);
        return angles;
    }

    float RayBoxEntryDistance(const Vec3 &origin, const Vec3 &direction, const AABB &box) {
        // The slab test. Each axis narrows the interval the ray spends inside
        // the box; when the interval closes, the ray missed.
        float nearest  = 0.0F;
        float furthest = std::numeric_limits<float>::max();

        const float originAxis[3] {origin.x, origin.y, origin.z};
        const float directionAxis[3] {direction.x, direction.y, direction.z};
        const float minAxis[3] {box.min.x, box.min.y, box.min.z};
        const float maxAxis[3] {box.max.x, box.max.y, box.max.z};

        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(directionAxis[axis]) < 1e-6F) {
                // Parallel to this pair of planes: either always between them
                // or never.
                if (originAxis[axis] < minAxis[axis] || originAxis[axis] > maxAxis[axis]) {
                    return -1.0F;
                }
                continue;
            }

            const float inverse = 1.0F / directionAxis[axis];
            float entry         = (minAxis[axis] - originAxis[axis]) * inverse;
            float exit          = (maxAxis[axis] - originAxis[axis]) * inverse;
            if (entry > exit) {
                std::swap(entry, exit);
            }

            nearest  = std::max(nearest, entry);
            furthest = std::min(furthest, exit);
            if (furthest < nearest) {
                return -1.0F;
            }
        }

        return nearest;
    }

    Matrix34 Matrix34FromTransform(const Vec3 &position, const Quat &rotation, const Vec3 &scale) {
        const float xx = rotation.x * rotation.x;
        const float yy = rotation.y * rotation.y;
        const float zz = rotation.z * rotation.z;
        const float xy = rotation.x * rotation.y;
        const float xz = rotation.x * rotation.z;
        const float yz = rotation.y * rotation.z;
        const float wx = rotation.w * rotation.x;
        const float wy = rotation.w * rotation.y;
        const float wz = rotation.w * rotation.z;

        // Each column of the 3x3 is a rotated axis, and the scale belongs to
        // the axis rather than to the row -- which is what makes a non-uniform
        // scale come out along the object's own axes.
        Matrix34 matrix {};
        matrix.m[0][0] = (1.0F - 2.0F * (yy + zz)) * scale.x;
        matrix.m[1][0] = (2.0F * (xy + wz)) * scale.x;
        matrix.m[2][0] = (2.0F * (xz - wy)) * scale.x;
        matrix.m[0][1] = (2.0F * (xy - wz)) * scale.y;
        matrix.m[1][1] = (1.0F - 2.0F * (xx + zz)) * scale.y;
        matrix.m[2][1] = (2.0F * (yz + wx)) * scale.y;
        matrix.m[0][2] = (2.0F * (xz + wy)) * scale.z;
        matrix.m[1][2] = (2.0F * (yz - wx)) * scale.z;
        matrix.m[2][2] = (1.0F - 2.0F * (xx + yy)) * scale.z;
        matrix.m[0][3] = position.x;
        matrix.m[1][3] = position.y;
        matrix.m[2][3] = position.z;
        return matrix;
    }

    void Matrix34ToTransform(const Matrix34 &matrix, Vec3 &position, Quat &rotation, Vec3 &scale) {
        position = Vec3 {matrix.m[0][3], matrix.m[1][3], matrix.m[2][3]};

        float axisScale[3] {};
        float basis[3][3] {{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}};
        for (int column = 0; column < 3; ++column) {
            const float x = matrix.m[0][column];
            const float y = matrix.m[1][column];
            const float z = matrix.m[2][column];

            const float length = std::sqrt(x * x + y * y + z * z);
            axisScale[column]  = length;
            if (length <= 1e-6F) {
                continue;
            }

            basis[0][column] = x / length;
            basis[1][column] = y / length;
            basis[2][column] = z / length;
        }
        scale = Vec3 {axisScale[0], axisScale[1], axisScale[2]};

        // Shepperd's method: take the branch whose divisor is largest, so the
        // square root never runs into a near-zero denominator.
        const float trace = basis[0][0] + basis[1][1] + basis[2][2];
        if (trace > 0.0F) {
            const float divisor = std::sqrt(trace + 1.0F) * 2.0F;
            rotation            = Quat {(basis[2][1] - basis[1][2]) / divisor, (basis[0][2] - basis[2][0]) / divisor, (basis[1][0] - basis[0][1]) / divisor, 0.25F * divisor};
        }
        else if (basis[0][0] > basis[1][1] && basis[0][0] > basis[2][2]) {
            const float divisor = std::sqrt(1.0F + basis[0][0] - basis[1][1] - basis[2][2]) * 2.0F;
            rotation            = Quat {0.25F * divisor, (basis[0][1] + basis[1][0]) / divisor, (basis[0][2] + basis[2][0]) / divisor, (basis[2][1] - basis[1][2]) / divisor};
        }
        else if (basis[1][1] > basis[2][2]) {
            const float divisor = std::sqrt(1.0F + basis[1][1] - basis[0][0] - basis[2][2]) * 2.0F;
            rotation            = Quat {(basis[0][1] + basis[1][0]) / divisor, 0.25F * divisor, (basis[1][2] + basis[2][1]) / divisor, (basis[0][2] - basis[2][0]) / divisor};
        }
        else {
            const float divisor = std::sqrt(1.0F + basis[2][2] - basis[0][0] - basis[1][1]) * 2.0F;
            rotation            = Quat {(basis[0][2] + basis[2][0]) / divisor, (basis[1][2] + basis[2][1]) / divisor, 0.25F * divisor, (basis[1][0] - basis[0][1]) / divisor};
        }

        const float length = std::sqrt(rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w);
        if (length > 1e-6F) {
            rotation = Quat {rotation.x / length, rotation.y / length, rotation.z / length, rotation.w / length};
        }
        else {
            rotation = Quat {0.0F, 0.0F, 0.0F, 1.0F};
        }
    }
} // namespace SDK
