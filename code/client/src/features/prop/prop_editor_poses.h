#pragma once

#include "sdk/types.h"
#include "shared/game/smart_object_blueprints.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace KCDC::Features::Prop {
    /**
     * The prefab property naming the helper class a smart object poses
     * bodies by -- `Bed_1Place_Bench`, `Bench_2Places`. A part carrying it
     * is where the game seats, lays or stands a body: a pose anchor. The
     * helper points themselves live in the game's own tables, so an anchor
     * marks the part's origin and facing, not each seat's exact spot.
     */
    inline constexpr std::string_view kPoseHelperProperty = "soclass_SmartObjectHelpers";

    /** One place the game poses a body at. */
    struct PoseAnchor {
        SDK::Vec3 position {};
        float yawDegrees = 0.0F; // the editor camera's yaw: counter-clockwise from +Y, seen from above
        std::string label;       // the helper class
        std::string partClass;   // the entity class carrying it: `StanceSmartObject`
        std::string partName;    // the prefab's own name for the part: `smartObjectLeftSide`
        std::uint64_t placedId = 0; // the editor's object it belongs to; 0 for one of the level's own
    };

    /** The helper class a blueprint part poses bodies by, or empty when it poses nobody. */
    inline std::string_view PoseHelperClass(const Shared::Game::SmartObjectPart &part) {
        for (std::size_t index = 0; index < part.propertyCount; ++index) {
            if (part.properties[index].key == kPoseHelperProperty) {
                return part.properties[index].value;
            }
        }
        return {};
    }

    /** The yaw of a body turned by `rotation`, as the editor camera reads its own: where its +Y points on the ground, counter-clockwise from the world's +Y. */
    inline float PoseYawDegrees(const glm::quat &rotation) {
        const glm::vec3 forward = rotation * glm::vec3(0.0F, 1.0F, 0.0F);
        return std::atan2(-forward.x, forward.y) * 180.0F / 3.14159265358979F;
    }

    /**
     * Every pose anchor of a smart object standing at `position`, turned by
     * `rotation`, appended to `anchors`. A part is posed as the assembly
     * poses it: its own offset turned by the object, its own turn after the
     * object's.
     */
    inline void AppendPoseAnchors(const Shared::Game::SmartObjectBlueprint &blueprint, const glm::vec3 &position, const glm::quat &rotation, std::uint64_t placedId, std::vector<PoseAnchor> &anchors) {
        const glm::quat facing = glm::normalize(rotation);
        for (std::size_t index = 0; index < blueprint.partCount; ++index) {
            const Shared::Game::SmartObjectPart &part = blueprint.parts[index];
            const std::string_view helper             = PoseHelperClass(part);
            if (helper.empty()) {
                continue;
            }
            const glm::vec3 at = position + facing * glm::vec3(part.posX, part.posY, part.posZ);
            PoseAnchor anchor;
            anchor.position   = SDK::Vec3 {at.x, at.y, at.z};
            anchor.yawDegrees = PoseYawDegrees(facing * glm::quat(part.rotW, part.rotX, part.rotY, part.rotZ));
            anchor.label      = std::string {helper};
            anchor.partClass  = std::string {part.entityClass};
            anchor.partName   = std::string {part.name};
            anchor.placedId   = placedId;
            anchors.push_back(std::move(anchor));
        }
    }

    /**
     * The entity classes that carry a pose helper in any shipped blueprint:
     * what one of the level's own entities has to be to count as an anchor.
     * Read off the catalog, so it follows the game's data.
     */
    inline std::vector<std::string_view> PoseAnchorClasses() {
        std::vector<std::string_view> classes;
        for (const Shared::Game::SmartObjectBlueprint &blueprint : Shared::Game::g_SmartObjectBlueprints) {
            for (std::size_t index = 0; index < blueprint.partCount; ++index) {
                const Shared::Game::SmartObjectPart &part = blueprint.parts[index];
                if (!PoseHelperClass(part).empty() && std::find(classes.begin(), classes.end(), part.entityClass) == classes.end()) {
                    classes.push_back(part.entityClass);
                }
            }
        }
        return classes;
    }
} // namespace KCDC::Features::Prop
