#pragma once

#include "features/prop/prop_editor_poses.h"

#include <algorithm>
#include <cmath>
#include <vector>

MODULE(world_builder_poses, {
    IT("reads a blueprint's pose anchors off the parts that carry a helper class", {
        const KCDC::Shared::Game::SmartObjectBlueprint *bench = KCDC::Shared::Game::FindSmartObjectBlueprint("Bench/bench_2places_low");
        EQUALS(bench != nullptr, true);
        std::vector<KCDC::Features::Prop::PoseAnchor> anchors;
        KCDC::Features::Prop::AppendPoseAnchors(*bench, glm::vec3(0.0F), glm::quat(1.0F, 0.0F, 0.0F, 0.0F), 7, anchors);

        // Two seats, each a part with a helper class; its triggers pose nobody.
        EQUALS(anchors.size(), static_cast<std::size_t>(2));
        std::size_t posed = 0;
        for (std::size_t index = 0; index < bench->partCount; ++index) {
            const KCDC::Shared::Game::SmartObjectPart &part = bench->parts[index];
            if (KCDC::Features::Prop::PoseHelperClass(part).empty()) {
                continue;
            }
            const KCDC::Features::Prop::PoseAnchor &anchor = anchors[posed++];
            EQUALS((anchor.label == KCDC::Features::Prop::PoseHelperClass(part) && anchor.partClass == part.entityClass && anchor.partName == part.name && anchor.placedId == 7), true);
            EQUALS((std::fabs(anchor.position.x - part.posX) < 1e-4F && std::fabs(anchor.position.y - part.posY) < 1e-4F && std::fabs(anchor.position.z - part.posZ) < 1e-4F), true);
        }
        EQUALS(posed, static_cast<std::size_t>(2));
    });

    IT("turns and moves the anchors with the object", {
        const KCDC::Shared::Game::SmartObjectBlueprint *bench = KCDC::Shared::Game::FindSmartObjectBlueprint("Bench/bench_2places_low");
        EQUALS(bench != nullptr, true);
        std::vector<KCDC::Features::Prop::PoseAnchor> still;
        std::vector<KCDC::Features::Prop::PoseAnchor> turned;
        KCDC::Features::Prop::AppendPoseAnchors(*bench, glm::vec3(0.0F), glm::quat(1.0F, 0.0F, 0.0F, 0.0F), 0, still);
        // A quarter turn counter-clockwise seen from above, then moved.
        const glm::quat quarter = glm::angleAxis(glm::radians(90.0F), glm::vec3(0.0F, 0.0F, 1.0F));
        KCDC::Features::Prop::AppendPoseAnchors(*bench, glm::vec3(100.0F, 200.0F, 30.0F), quarter, 0, turned);
        EQUALS(still.size() == turned.size() && !still.empty(), true);
        for (std::size_t index = 0; index < still.size(); ++index) {
            // (x, y) turns to (-y, x).
            EQUALS((std::fabs(turned[index].position.x - (100.0F - still[index].position.y)) < 1e-3F && std::fabs(turned[index].position.y - (200.0F + still[index].position.x)) < 1e-3F && std::fabs(turned[index].position.z - (30.0F + still[index].position.z)) < 1e-3F),
                true);
            float delta = turned[index].yawDegrees - still[index].yawDegrees;
            while (delta < 0.0F) {
                delta += 360.0F;
            }
            EQUALS(std::fabs(delta - 90.0F) < 1e-2F, true);
        }
    });

    IT("reads a body's yaw counter-clockwise from +Y, as the editor camera's", {
        EQUALS(std::fabs(KCDC::Features::Prop::PoseYawDegrees(glm::quat(1.0F, 0.0F, 0.0F, 0.0F))) < 1e-4F, true);
        EQUALS(std::fabs(KCDC::Features::Prop::PoseYawDegrees(glm::angleAxis(glm::radians(90.0F), glm::vec3(0.0F, 0.0F, 1.0F))) - 90.0F) < 1e-3F, true);
        EQUALS(std::fabs(KCDC::Features::Prop::PoseYawDegrees(glm::angleAxis(glm::radians(-90.0F), glm::vec3(0.0F, 0.0F, 1.0F))) + 90.0F) < 1e-3F, true);
    });

    IT("names the entity classes the shipped blueprints pose bodies with", {
        const std::vector<std::string_view> classes = KCDC::Features::Prop::PoseAnchorClasses();
        EQUALS(std::find(classes.begin(), classes.end(), std::string_view {"StanceSmartObject"}) != classes.end(), true);
        // A trigger offers the seat; it does not pose the body.
        EQUALS(std::find(classes.begin(), classes.end(), std::string_view {"BedTrigger"}) == classes.end(), true);
        EQUALS(std::adjacent_find(classes.begin(), classes.end()) == classes.end(), true);
    });
});
