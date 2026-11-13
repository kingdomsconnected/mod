#include <logging/logger.h>

#include "unit.h"

#include "action_hints_ut.h"
#include "animation_ut.h"
#include "area_collision_ut.h"
#include "area_transfer_ut.h"
#include "area_ut.h"
#include "body_action_ut.h"
#include "cart_ut.h"
#include "combat_exchange_ut.h"
#include "combat_pricing_ut.h"
#include "cook_pot_ut.h"
#include "cry_math_ut.h"
#include "custom_buffs_ut.h"
#include "custom_item_registry_ut.h"
#include "damage_ut.h"
#include "death_ut.h"
#include "dice_ut.h"
#include "dice_npc_ut.h"
#include "door_api_ut.h"
#include "door_ut.h"
#include "duel_ut.h"
#include "emote_ut.h"
#include "environment_ut.h"
#include "gate_ut.h"
#include "gathering_api_ut.h"
#include "gathering_area_ut.h"
#include "gathering_projection_ut.h"
#include "gathering_sources_ut.h"
#include "gizmos_ut.h"
#include "head_visibility_ut.h"
#include "horse_buffs_ut.h"
#include "horse_gait_ut.h"
#include "horse_gear_ut.h"
#include "horse_life_ut.h"
#include "human_soul_ut.h"
#include "inventory_ammunition_ut.h"
#include "inventory_core_ut.h"
#include "inventory_repair_ut.h"
#include "inventory_use_ut.h"
#include "item_stats_ut.h"
#include "join_deadlines_ut.h"
#include "locomotion_bands_ut.h"
#include "grass_clearing_ut.h"
#include "level_object_ut.h"
#include "light_api_ut.h"
#include "missile_ut.h"
#include "npc_api_ut.h"
#include "npc_inventory_ut.h"
#include "npc_replication_ut.h"
#include "pickpocket_ut.h"
#include "player_hit_transfer_ut.h"
#include "player_resource_ut.h"
#include "player_rock_ut.h"
#include "player_spawn_ut.h"
#include "population_ut.h"
#include "prop_physics_ut.h"
#include "screen_effects_ut.h"
#include "spawn_ut.h"
#include "stash_ut.h"
#include "station_ut.h"
#include "suites_ut.h"
#include "voice_filter_ut.h"
#include "world_builder_layers_ut.h"
#include "world_builder_mcp_ut.h"
#include "world_objects_ut.h"

#ifdef _WIN32
#include "game_menus_ut.h"
#include "native_keybinds_ut.h"
#include "world_builder_authoring_ut.h"
#include "world_builder_editor_preferences_ut.h"
#include "world_builder_figure_ut.h"
#include "world_builder_poses_ut.h"
#include "world_builder_keymap_ut.h"
#include "world_builder_grass_ut.h"
#include "world_builder_lights_ut.h"
#include "world_builder_plants_ut.h"
#include "world_builder_selection_ut.h"
#endif

#include "features/npc/npc_locomotion_service.h"
#include "features/npc/npc_movement_progress.h"
#include "common/replication/pose_snapshot.h"

#include <cmath>
#include <string>
#include <vector>

namespace {

    // One table for the whole module, as the file-scope one it replaces was.
    KCDC::Features::Npc::NpcLocomotionService gLocomotion;

    struct OverrideFixture {
        std::vector<SDK::EntityId> ids;

        ~OverrideFixture() {
            for (const auto id : ids) {
                Withdraw(id);
            }
        }

        void Publish(SDK::EntityId id, std::uint32_t state, std::uint8_t stance, const glm::vec3 &velocity = glm::vec3(0.0F)) {
            ids.push_back(id);
            gLocomotion.PublishLocomotionOverride(id, state, 3, 4, stance, velocity, glm::identity<glm::quat>());
        }

        static void Withdraw(SDK::EntityId id) {
            gLocomotion.PublishLocomotionOverride(id, 0, 0, 0, 0, glm::vec3(0.0F), glm::identity<glm::quat>());
        }
    };
} // namespace

MODULE(locomotion_override, {
    IT("publishes stance changes without changing the requested idle state", {
        OverrideFixture fixture;
        fixture.Publish(1, 1, 5);
        fixture.Publish(1, 1, 6);
        const auto pose = gLocomotion.GetLocomotionOverride(1);
        EQUALS(pose.state, 1U);
        EQUALS(pose.stanceTag, 6);
        EQUALS(gLocomotion.GetLocomotionOverride(1).stanceTag, 6);
    });

    IT("withdraws an entity before reusing its slot or ID", {
        OverrideFixture fixture;
        fixture.Publish(1, 4, 5, glm::vec3(10.0F, 20.0F, 30.0F));
        fixture.Withdraw(1);
        fixture.Publish(2, 1, 6);
        EQUALS(gLocomotion.GetLocomotionOverride(1).state, 0U);
        const auto replacement = gLocomotion.GetLocomotionOverride(2);
        EQUALS(replacement.state, 1U);
        EQUALS(replacement.velocityX, 0.0F);
        EQUALS(replacement.stanceTag, 6);
        fixture.Publish(1, 4, 7, glm::vec3(2.0F, 3.0F, 4.0F));
        const auto reused = gLocomotion.GetLocomotionOverride(1);
        EQUALS(reused.velocityZ, 4.0F);
        EQUALS(reused.stanceTag, 7);
        EQUALS(gLocomotion.GetLocomotionOverride(2).state, 1U);
    });

    IT("recovers from full capacity when a slot is withdrawn", {
        OverrideFixture fixture;
        SDK::EntityId rejected = 0;
        for (SDK::EntityId id = 1; id <= 1024; ++id) {
            fixture.Publish(id, 1, 5);
            if (gLocomotion.GetLocomotionOverride(id).state == 0) {
                rejected = id;
                break;
            }
        }
        GREATER(rejected, 2U);
        fixture.Withdraw(1);
        fixture.Publish(rejected, 4, 6, glm::vec3(1.0F));
        EQUALS(gLocomotion.GetLocomotionOverride(rejected).state, 4U);
        EQUALS(gLocomotion.GetLocomotionOverride(1).state, 0U);
        EQUALS(gLocomotion.GetLocomotionOverride(2).state, 1U);
    });
});

MODULE(pose_locomotion, {
    IT("extrapolates measured vertical velocity during ascent and descent", {
        KCDC::Common::Replication::PoseSnapshot pose;
        pose.hasPhysicsVelocity = true;
        pose.inAir              = true;
        pose.position           = glm::vec3(0.0F, 0.0F, 1.0F);
        pose.velocity           = glm::vec3(5.0F, 0.0F, -2.0F);
        const auto descending   = KCDC::Common::Replication::PoseSnapshotPolicy::Extrapolate(pose, 0.05F);
        LESSER(std::fabs(descending.position.z - 0.9F), 0.0001F);
        pose.velocity.z      = 2.0F;
        const auto ascending = KCDC::Common::Replication::PoseSnapshotPolicy::Extrapolate(pose, 0.05F);
        LESSER(std::fabs(ascending.position.z - 1.1F), 0.0001F);
        pose.hasPhysicsVelocity = false;
        EQUALS(KCDC::Common::Replication::PoseSnapshotPolicy::Extrapolate(pose, 0.05F).position.z, 1.0F);
    });
    IT("holds stance until its timestamp while preserving interval gait", {
        Framework::Utils::SnapshotBuffer<KCDC::Common::Replication::PoseSnapshot, KCDC::Common::Replication::PoseSnapshotPolicy> buffer;
        KCDC::Common::Replication::PoseSnapshot a, b, c;
        a.stanceTag    = 5;
        b.stanceTag    = 6;
        b.moveSpeedTag = 3;
        b.moveDirTag   = 4;
        b.position     = glm::vec3(1.0F, 0.0F, 0.0F);
        c              = b;
        c.stanceTag    = 5;
        c.position     = glm::vec3(2.0F, 0.0F, 0.0F);
        buffer.Push(a, 1000);
        buffer.Push(b, 1100);
        buffer.Push(c, 1200);
        KCDC::Common::Replication::PoseSnapshot out;
        EQUALS(buffer.Sample(1001, out), true);
        EQUALS(out.stanceTag, 5);
        EQUALS(out.moveSpeedTag, 3);
        EQUALS(out.moveDirTag, 4);
        EQUALS(buffer.Sample(1099, out), true);
        EQUALS(out.stanceTag, 5);
        EQUALS(buffer.Sample(1100, out), true);
        EQUALS(out.stanceTag, 6);
        EQUALS(buffer.Sample(1199, out), true);
        EQUALS(out.stanceTag, 6);
        EQUALS(buffer.Sample(1200, out), true);
        EQUALS(out.stanceTag, 5);
    });

    IT("holds a fall until the pose that counted it", {
        // A body drawn behind the wire falls where its owner did, not a
        // bracket early.
        Framework::Utils::SnapshotBuffer<KCDC::Common::Replication::PoseSnapshot, KCDC::Common::Replication::PoseSnapshotPolicy> buffer;
        KCDC::Common::Replication::PoseSnapshot a, b;
        b.fallSequence = 1;
        b.position     = glm::vec3(1.0F, 0.0F, 0.0F);
        buffer.Push(a, 1000);
        buffer.Push(b, 1100);
        KCDC::Common::Replication::PoseSnapshot out;
        EQUALS(buffer.Sample(1099, out), true);
        EQUALS(out.fallSequence, 0);
        EQUALS(buffer.Sample(1100, out), true);
        EQUALS(out.fallSequence, 1);
    });

    IT("stops held locomotion without changing buffered movement or stance", {
        Framework::Utils::SnapshotBuffer<KCDC::Common::Replication::PoseSnapshot, KCDC::Common::Replication::PoseSnapshotPolicy> buffer;
        KCDC::Common::Replication::PoseSnapshot a, b;
        b.position     = glm::vec3(1.0F, 0.0F, 0.0F);
        b.velocity     = glm::vec3(10.0F, 0.0F, 0.0F);
        b.rotation     = glm::angleAxis(1.0F, glm::vec3(0.0F, 0.0F, 1.0F));
        b.moveSpeedTag = 3;
        b.moveDirTag   = 4;
        b.stanceTag    = 6;
        buffer.Push(a, 1000);
        buffer.Push(b, 1100);
        KCDC::Common::Replication::PoseSnapshot out;
        bool held = false;
        EQUALS(buffer.Sample(5000, out, &held), true);
        EQUALS(held, true);
        const auto heldPosition = out.position;
        KCDC::Common::Replication::HoldLocomotion(out);
        EQUALS(out.position, heldPosition);
        EQUALS(out.rotation, b.rotation);
        EQUALS(out.velocity, glm::vec3(0.0F));
        EQUALS(out.moveSpeedTag, KCDC::Shared::Game::kNoAnimationTag);
        EQUALS(out.moveDirTag, KCDC::Shared::Game::kNoAnimationTag);
        EQUALS(out.stanceTag, 6);

        // A fresh sample resumes movement; the held result never writes back.
        b.position = glm::vec3(3.0F, 0.0F, 0.0F);
        buffer.Push(b, 5100);
        EQUALS(buffer.Sample(5150, out, &held), true);
        EQUALS(held, false);
        EQUALS(out.velocity, b.velocity);
        EQUALS(out.moveSpeedTag, b.moveSpeedTag);
    });

    IT("carries the owner's own animation velocity rather than deriving one", {
        // Use commanded velocity with its MoveSpeed tag; physics velocity
        // lags pace changes.
        Framework::Utils::SnapshotBuffer<KCDC::Common::Replication::PoseSnapshot, KCDC::Common::Replication::PoseSnapshotPolicy> buffer;
        KCDC::Common::Replication::PoseSnapshot a, b;
        a.velocity = glm::vec3(1.0F, 0.0F, 0.0F);
        b.position = glm::vec3(4.0F, 0.0F, 0.0F);
        b.velocity = glm::vec3(3.0F, 0.0F, 0.0F);
        buffer.Push(a, 1000);
        buffer.Push(b, 1100);

        KCDC::Common::Replication::PoseSnapshot out;
        EQUALS(buffer.Sample(1050, out), true);

        // Halfway between what the owner reported, and nothing to do with the
        // 40 m/s the displacement over this bracket would have implied.
        EQUALS(out.velocity, glm::vec3(2.0F, 0.0F, 0.0F));
        EQUALS(out.position, glm::vec3(2.0F, 0.0F, 0.0F));
    });

    IT("interpolates facing across the bracket and reaches the reported heading", {
        Framework::Utils::SnapshotBuffer<KCDC::Common::Replication::PoseSnapshot, KCDC::Common::Replication::PoseSnapshotPolicy> buffer;
        KCDC::Common::Replication::PoseSnapshot a, b;
        a.rotation = glm::angleAxis(0.0F, glm::vec3(0.0F, 0.0F, 1.0F));
        b.rotation = glm::angleAxis(glm::quarter_pi<float>(), glm::vec3(0.0F, 0.0F, 1.0F));
        buffer.Push(a, 1000);
        buffer.Push(b, 1100);

        KCDC::Common::Replication::PoseSnapshot out;
        EQUALS(buffer.Sample(1050, out), true);

        // Halfway through a 45-degree turn, the body faces 22.5 degrees.
        const auto facing = out.rotation * glm::vec3(1.0F, 0.0F, 0.0F);
        LESSER(std::fabs(facing.x - std::cos(glm::quarter_pi<float>() * 0.5F)), 0.0001F);
        LESSER(std::fabs(facing.y - std::sin(glm::quarter_pi<float>() * 0.5F)), 0.0001F);
        EQUALS(buffer.Sample(1100, out), true);
        LESSER(glm::length(out.rotation * glm::vec3(1.0F, 0.0F, 0.0F) - b.rotation * glm::vec3(1.0F, 0.0F, 0.0F)), 0.0001F);
    });

    IT("keeps the newest heading during extrapolation and while held", {
        Framework::Utils::SnapshotBuffer<KCDC::Common::Replication::PoseSnapshot, KCDC::Common::Replication::PoseSnapshotPolicy> buffer;
        KCDC::Common::Replication::PoseSnapshot a, b;
        a.rotation = glm::angleAxis(0.0F, glm::vec3(0.0F, 0.0F, 1.0F));
        b.rotation = glm::angleAxis(glm::quarter_pi<float>(), glm::vec3(0.0F, 0.0F, 1.0F));
        b.velocity = glm::vec3(1.0F, 0.0F, 0.0F);
        buffer.Push(a, 1000);
        buffer.Push(b, 1100);

        // Past the newest, reported travel continues without inventing a turn.
        KCDC::Common::Replication::PoseSnapshot out;
        bool held = false;
        EQUALS(buffer.Sample(1130, out, &held), true);
        EQUALS(held, false);
        EQUALS(out.rotation, b.rotation);
        LESSER(std::fabs(out.position.x - 0.03F), 0.0001F);

        EQUALS(buffer.Sample(5000, out, &held), true);
        EQUALS(held, true);
        EQUALS(out.rotation, b.rotation);
        const auto heldPosition = out.position;
        KCDC::Common::Replication::HoldLocomotion(out);
        EQUALS(out.rotation, b.rotation);
        EQUALS(out.position, heldPosition);
        EQUALS(out.velocity, glm::vec3(0.0F));
    });

    IT("hands a stopped owner's zero straight through", {
        // Commanded velocity reaches zero with the input; physics velocity
        // decays later.
        Framework::Utils::SnapshotBuffer<KCDC::Common::Replication::PoseSnapshot, KCDC::Common::Replication::PoseSnapshotPolicy> buffer;
        KCDC::Common::Replication::PoseSnapshot moving, stopped;
        moving.velocity     = glm::vec3(1.4F, 0.0F, 0.0F);
        moving.moveSpeedTag = 3;
        stopped.position    = glm::vec3(0.2F, 0.0F, 0.0F);
        stopped.velocity    = glm::vec3(0.0F);
        buffer.Push(moving, 1000);
        buffer.Push(stopped, 1100);

        KCDC::Common::Replication::PoseSnapshot out;
        EQUALS(buffer.Sample(1100, out), true);
        EQUALS(out.velocity, glm::vec3(0.0F));
        EQUALS(out.moveSpeedTag, KCDC::Shared::Game::kNoAnimationTag);

        // And the body still finishes travelling to where the owner stopped,
        // because that is the position channel's business, not the pace's.
        EQUALS(out.position, glm::vec3(0.2F, 0.0F, 0.0F));
    });
});

MODULE(npc_movement_progress, {
    IT("flees until separated from the threat, even when starting beside it", {
        KCDC::Shared::Game::NpcIntent intent;
        intent.kind       = KCDC::Shared::Game::NpcIntentKind::Flee;
        intent.point      = glm::vec3(10.0F, 20.0F, 5.0F);
        intent.radius     = 25.0F;
        const auto target = intent.FleeDestination(intent.point);
        EQUALS(intent.HasArrived(intent.point, target), false);
        EQUALS(intent.HasArrived(intent.point + glm::vec3(24.0F, 0.0F, 0.0F), target), false);
        EQUALS(intent.HasArrived(intent.point + glm::vec3(25.0F, 0.0F, 0.0F), target), true);
        EQUALS(glm::distance(glm::vec2(target), glm::vec2(intent.point)) > intent.radius, true);
        // On a slope the destination is at the body's height, not the threat's.
        EQUALS(intent.FleeDestination(intent.point + glm::vec3(1.0F, 0.0F, 3.0F)).z, intent.point.z + 3.0F);
        EQUALS(intent.FleeDestination(intent.point + glm::vec3(0.0F, -1.0F, 0.0F)).y < intent.point.y, true);
    });

    IT("resumes a move or follow when its target leaves the arrival radius", {
        KCDC::Shared::Game::NpcIntent intent;
        intent.radius = 3.0F;
        const glm::vec3 position(10.0F, 20.0F, 5.0F);
        for (const auto kind : {KCDC::Shared::Game::NpcIntentKind::MoveTo, KCDC::Shared::Game::NpcIntentKind::Follow}) {
            intent.kind = kind;
            EQUALS(intent.HasArrived(position, position + glm::vec3(0.0F, 2.0F, 1.0F)), true);
            EQUALS(intent.HasArrived(position, position + glm::vec3(0.0F, 4.0F, 1.0F)), false);
        }
    });

    IT("allows a follower to keep pace without getting closer", {
        KCDC::Features::Npc::MovementProgress progress;
        for (int step = 0; step < 80; ++step) {
            EQUALS(progress.Update(glm::vec3(step * 0.25F, 0.0F, 0.0F), 0.25F), false);
        }
    });

    IT("allows movement away from the goal during a detour", {
        KCDC::Features::Npc::MovementProgress progress;
        for (int step = 0; step < 24; ++step) {
            EQUALS(progress.Update(glm::vec3(-step * 0.25F, step * 0.25F, 0.0F), 0.25F), false);
        }
    });

    IT("reports a stationary body and recovers when it moves", {
        KCDC::Features::Npc::MovementProgress progress;
        const glm::vec3 position(10.0F, 20.0F, 0.0F);
        EQUALS(progress.Update(position, 0.0F), false);
        for (int step = 0; step < 11; ++step) {
            EQUALS(progress.Update(position, 0.25F), false);
        }
        EQUALS(progress.Update(position, 0.25F), true);
        EQUALS(progress.Update(position + glm::vec3(0.6F, 0.0F, 0.0F), 0.25F), false);
    });

    IT("stays blocked when a stuck body shuffles a little", {
        KCDC::Features::Npc::MovementProgress progress;
        const glm::vec3 position(10.0F, 20.0F, 0.0F);
        EQUALS(progress.Update(position, 0.0F), false);
        for (int step = 0; step < 11; ++step) {
            EQUALS(progress.Update(position, 0.25F), false);
        }
        EQUALS(progress.Update(position, 0.25F), true);
        // A nudge is not headway: the report does not flip back to running
        // and then block a second time three seconds later.
        EQUALS(progress.Update(position + glm::vec3(0.1F, 0.0F, 0.0F), 0.25F), true);
        EQUALS(progress.Update(position + glm::vec3(0.3F, 0.2F, 0.0F), 0.25F), true);
        EQUALS(progress.Update(position, 0.25F), true);
    });

    IT("reports a body rocking against a wall as blocked", {
        KCDC::Features::Npc::MovementProgress progress;
        EQUALS(progress.Update(glm::vec3(0.0F), 0.0F), false);
        // Thirty centimetres either way every frame: far more than jitter,
        // and no headway at all.
        for (int step = 0; step < 11; ++step) {
            EQUALS(progress.Update(glm::vec3(step % 2 == 0 ? 0.3F : -0.15F, 0.0F, 0.0F), 0.25F), false);
        }
        EQUALS(progress.Update(glm::vec3(0.3F, 0.0F, 0.0F), 0.25F), true);
    });

    IT("counts slow but steady headway as progress", {
        KCDC::Features::Npc::MovementProgress progress;
        for (int step = 0; step < 40; ++step) {
            EQUALS(progress.Update(glm::vec3(step * 0.1F, 0.0F, 0.0F), 0.25F), false);
        }
    });

    IT("starts a fresh timer after standing or reaching the follow radius", {
        KCDC::Features::Npc::MovementProgress progress;
        EQUALS(progress.Update(glm::vec3(0.0F), 0.0F), false);
        EQUALS(progress.Update(glm::vec3(0.0F), 2.75F), false);
        progress.Reset();
        EQUALS(progress.Update(glm::vec3(0.0F), 0.25F), false);
        EQUALS(progress.Update(glm::vec3(0.0F), 0.25F), false);
        EQUALS(progress.Update(glm::vec3(0.6F, 0.0F, 0.0F), 0.25F), false);
    });

    IT("does not count small pose jitter as movement", {
        KCDC::Features::Npc::MovementProgress progress;
        EQUALS(progress.Update(glm::vec3(0.0F), 0.0F), false);
        for (int step = 0; step < 11; ++step) {
            EQUALS(progress.Update(glm::vec3(step % 2 == 0 ? 0.01F : -0.01F, 0.0F, 0.01F), 0.25F), false);
        }
        EQUALS(progress.Update(glm::vec3(0.0F), 0.25F), true);
    });
});

int RunCookPotRpcSuite();

int main(int argc, char **argv) {
    // The server-booting suites, one per process; RunKCDCTests runs each in its own directory.
    if (argc > 2 && std::string(argv[1]) == "--suite") {
        const std::string suite = argv[2];
        if (suite == "world-builder-mcp") {
            UNIT_CREATE("KCDCTests MCP HTTP");
            UNIT_MODULE(world_builder_mcp_http);
            return UNIT_RUN();
        }
        if (suite == "gathering-api") {
            return RunGatheringApiSuite();
        }
        if (suite == "area-transfer") {
            return RunAreaTransferSuite();
        }
        if (suite == "player-hit-transfer") {
            return RunPlayerHitTransferSuite();
        }
        if (suite == "world-resource") {
            return RunWorldResourceSuite(false);
        }
        if (suite == "world-resource-empty") {
            return RunWorldResourceSuite(true);
        }
        if (suite == "patrol-api") {
            return RunPatrolApiSuite();
        }
        if (suite == "cook-pot-rpc") {
            return RunCookPotRpcSuite();
        }
        if (suite == "npc-api") {
            return RunNpcApiSuite();
        }
        if (suite == "door-api") {
            return RunDoorApiSuite();
        }
        if (suite == "dice-npc") {
            return RunDiceNpcSuite();
        }
        if (suite == "stash-rpc") {
            return RunStashRpcSuite();
        }
        return 1;
    }

    UNIT_CREATE("KCDCTests");
    UNIT_MODULE(world_builder_mcp);
    UNIT_MODULE(world_builder_layers);
    UNIT_MODULE(light_protocol);
#ifdef _WIN32
    UNIT_MODULE(world_builder_authoring);
    UNIT_MODULE(world_builder_lights);
    UNIT_MODULE(world_builder_plants);
    UNIT_MODULE(world_builder_grass);
    UNIT_MODULE(world_builder_selection);
    UNIT_MODULE(world_builder_keymap);
    UNIT_MODULE(world_builder_editor_preferences);
    UNIT_MODULE(world_builder_figure);
    UNIT_MODULE(world_builder_poses);
    UNIT_MODULE(world_builder_mcp_files);
#endif
    Framework::Logging::GetInstance()->PauseLogging(true);
    UNIT_MODULE(action_hint_input);
    UNIT_MODULE(inventory_ammunition);
    UNIT_MODULE(inventory_core);
    UNIT_MODULE(inventory_repairs);
    UNIT_MODULE(animation_protocol);
    UNIT_MODULE(locomotion_override);
    UNIT_MODULE(npc_movement_progress);
    UNIT_MODULE(npc_replication);
    UNIT_MODULE(npc_inventory);
    UNIT_MODULE(missile);
    UNIT_MODULE(pose_locomotion);
    UNIT_MODULE(spawn_contracts);
    UNIT_MODULE(population);
    UNIT_MODULE(human_soul_replication);
    UNIT_MODULE(player_death);
    UNIT_MODULE(player_damage);
    UNIT_MODULE(player_spawn_protocol);
    UNIT_MODULE(player_resource_protocol);
    UNIT_MODULE(player_rock_protocol);
    UNIT_MODULE(player_rock_history);
    UNIT_MODULE(emote_protocol);
    UNIT_MODULE(environment_values);
    UNIT_MODULE(screen_effects);
    UNIT_MODULE(cry_affine_transform);
    UNIT_MODULE(head_visibility);
    UNIT_MODULE(locomotion_bands);
    UNIT_MODULE(door_protocol);
    UNIT_MODULE(grass_clearing);
    UNIT_MODULE(level_objects);
    UNIT_MODULE(world_objects);
    UNIT_MODULE(prop_physics);
    UNIT_MODULE(pickpocket_protocol);
    UNIT_MODULE(dice_rules);
    UNIT_MODULE(dice_table);
    UNIT_MODULE(dice_protocol);
    UNIT_MODULE(combat_exchange);
    UNIT_MODULE(combat_referee);
    UNIT_MODULE(combat_pricing);
    UNIT_MODULE(combat_guard_history);
    UNIT_MODULE(duel_roster);
    UNIT_MODULE(body_actions);
    UNIT_MODULE(stations);
    UNIT_MODULE(duel_roster_ring);
    UNIT_MODULE(forced_attack_speed);
    UNIT_MODULE(swing_generations);
    UNIT_MODULE(swing_hold);
    UNIT_MODULE(duel_engagement);
    UNIT_MODULE(combat_target_replication);
    UNIT_MODULE(inventory_use);
    UNIT_MODULE(door_policy);
    UNIT_MODULE(cook_pots);
    UNIT_MODULE(custom_buffs);
    UNIT_MODULE(custom_item_registry);
    UNIT_MODULE(area_geometry);
    UNIT_MODULE(gathering_areas);
    UNIT_MODULE(gathering_sources);
    UNIT_MODULE(gathering_projection);
    UNIT_MODULE(area_collision);
    UNIT_MODULE(gizmos);
    UNIT_MODULE(gate_protocol);
    UNIT_MODULE(stash_protocol);
    UNIT_MODULE(horse_life);
    UNIT_MODULE(horse_gear);
    UNIT_MODULE(horse_buffs);
    UNIT_MODULE(horse_gait);
    UNIT_MODULE(item_stats);
    UNIT_MODULE(cart_blueprints);
    UNIT_MODULE(cart_rig);
    UNIT_MODULE(cart_motion);
    UNIT_MODULE(cart_seats);
    UNIT_MODULE(cart_protocol);
    UNIT_MODULE(nametag_layout);
    UNIT_MODULE(nametag_visibility);
    UNIT_MODULE(join_deadlines);
    UNIT_MODULE(alchemy_suite);
    UNIT_MODULE(smithing_suite);
    UNIT_MODULE(navigation_suite);
    UNIT_MODULE(npc_order_suite);
    UNIT_MODULE(streamed_asset_suite);
    UNIT_MODULE(mod_item_table_suite);
    UNIT_MODULE(custom_recipe_suite);
    UNIT_MODULE(world_inventory_suite);
#ifdef _WIN32
    UNIT_MODULE(native_keybind_replay);
    UNIT_MODULE(game_menus);
    UNIT_MODULE(hud_elements_suite);
    UNIT_MODULE(world_builder_suite);
#endif
    return UNIT_RUN();
}
