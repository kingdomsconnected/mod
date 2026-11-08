#pragma once

#include "shared/features/dog/dog_entity.h"
#include "shared/features/door/door_entity.h"
#include "shared/features/door/gate_entity.h"
#include "shared/features/ground_item/ground_item_entity.h"
#include "shared/features/horse/horse_entity.h"
#include "shared/features/npc/npc_entity.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/prop/prop_entity.h"
#include "shared/features/stash/stash_entity.h"

#include <networking/replication/replication_manager.h>

namespace KCDC {
    // Use XY distance in this Z-up world. Hysteresis limits streaming
    // churn; look-ahead follows motion. Budgets cap script-spawned types
    // per viewer. Rate bands throttle transforms only; reliable state is
    // unaffected.
    inline void ConfigureInterest(Framework::Networking::Replication::ReplicationManager &replication) {
        replication.SetInterestGroundPlaneXY(true);
        replication.SetInterestStreamOutMargin(25.0F);
        replication.SetInterestLookaheadSeconds(1.0F);
        replication.SetInterestRebuildInterval(100);

        replication.SetSerializeRateBands({50.0F, 150.0F, 33, 50});
        replication.SetSerializeRateBands(Shared::Entities::GroundItemEntity::kTypeName, {50.0F, 150.0F, 50, 100});

        // Every streamed player is a full native C_Player on every client:
        // combat player, interactor, NPC record, Mannequin, living physics.
        // The game itself budgets 70 fully simulated humans, so a client is
        // given the nearest 64 players and no more, however crowded the
        // square. Their poses are input replayed between samples, so they
        // need no more than 30 Hz up close, 20 Hz to 150 m and 10 Hz beyond.
        replication.SetInterestBudget(Shared::Entities::HumanEntity::kTypeName, 64);
        Framework::Networking::Replication::SerializeRateBands playerBands {50.0F, 150.0F, 50, 100};
        playerBands.nearIntervalMs = 33;
        replication.SetSerializeRateBands(Shared::Entities::HumanEntity::kTypeName, playerBands);

        replication.SetInterestBudget(Shared::Entities::HorseEntity::kTypeName, 48);
        // One dog per player, and it is always beside the player it belongs to.
        replication.SetInterestBudget(Shared::Entities::DogEntity::kTypeName, 48);
        // Every streamed NPC is a native actor on the client it reaches, and
        // nothing else bounds how many: the gamemode's wildlife alone is up to
        // 256 bodies, four to a spawn site, and every one of them in range was
        // sent. A tester saw remote players slide on 256
        // and stop at 48. Wildlife and gamemode humans share the type, so the
        // budget is the nearest 48 of either -- the horse budget, for the same
        // cost of a body -- and a vendor across the road outranks a herd on
        // the hill. The bodies a client runs are its own and never counted.
        replication.SetInterestBudget(Shared::Entities::NpcEntity::kTypeName, 48);
        // Every player can drop stacks; the nearest 64 reach each viewer.
        replication.SetInterestBudget(Shared::Entities::GroundItemEntity::kTypeName, 64);
        // Level containers stream at 50 m, and the densest 50 m of
        // Kuttenberg holds 144; the rest is room for spawned ones. Held
        // to each container's own range: under the viewer's 250 m the
        // same spot holds 1,043, and the budget would cut by rank.
        replication.SetInterestBudget(Shared::Entities::StashEntity::kTypeName, 192);
        replication.SetInterestUsesEntityRange(Shared::Entities::StashEntity::kTypeName, true);
        // Props are scenery, and scenery is dense: one furnished room of a
        // World Builder map is a few hundred, and a budget under that cuts by
        // rank, so the nearest few stand and the rest stream in and out with
        // every step. A static prop costs a client one brush, the form the
        // level keeps its own furniture in, so every prop in range is sent.
        // The budget only bounds a resource that spawns without end.
        replication.SetInterestBudget(Shared::Entities::PropEntity::kTypeName, 4096);
        // Every door the level places exists from boot, and the densest
        // 150 m of Kuttenberg holds 361 of them. A budget below that cuts
        // by rank rather than range, and the cut moves with the player,
        // so doors at its edge would stream in and out on every step.
        replication.SetInterestBudget(Shared::Entities::DoorEntity::kTypeName, 384);
        // The shipped game has two of these; the budget is only here so
        // gates are never starved by a street full of doors.
        replication.SetInterestBudget(Shared::Entities::GateEntity::kTypeName, 16);
    }
} // namespace KCDC
