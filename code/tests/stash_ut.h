#pragma once

#include "shared/features/stash/stash_entity.h"
#include "shared/features/stash/stash_inventory_message.h"
#include "shared/features/stash/stash_policy.h"
#include "shared/features/stash/stash_rpc.h"
#include "shared/game/stash_catalog.h"

#include <mafianet/bit_stream.h>

#include <cstring>
#include <limits>
#include <string>

MODULE(stash_protocol, {
    IT("constructs virtual stock separately from a world chest", {
        KCDC::Shared::Entities::StashEntity sent;
        sent.stashId      = 9;
        sent.isVirtual    = true;
        sent.isOpen       = true;
        sent.hasContents  = true;
        sent.lockpickable = false;
        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        sent.OnSerializeConstruction(writer);
        KCDC::Shared::Entities::StashEntity received;
        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        received.OnSerializeConstruction(reader);
        EQUALS(received.isVirtual, true);
        EQUALS(received.stashId, 9ULL);
        EQUALS(received.levelGuid, 0ULL);
        EQUALS(received.isOpen && received.hasContents, true);
        EQUALS(received.lockpickable, false);
    });

    IT("constructs a spawned container's model and lock tuning", {
        KCDC::Shared::Entities::StashEntity sent;
        sent.stashId        = 4;
        sent.model          = "objects/manmade/common_furniture/cabinets/cabinet_rustic_a.cgf";
        sent.lockDifficulty = 0.7F;
        sent.lockFanciness  = KCDC::Shared::Entities::StashEntity::LockFanciness::Fancy;
        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        sent.OnSerializeConstruction(writer);
        KCDC::Shared::Entities::StashEntity received;
        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        received.OnSerializeConstruction(reader);
        EQUALS(received.model, sent.model);
        EQUALS(received.lockDifficulty, 0.7F);
        EQUALS(received.lockFanciness == KCDC::Shared::Entities::StashEntity::LockFanciness::Fancy, true);
    });

    IT("starts every container on the game's own lock", {
        KCDC::Shared::Entities::StashEntity stash;
        EQUALS(stash.lockDifficulty < 0.0F, true);
        EQUALS(stash.lockFanciness == KCDC::Shared::Entities::StashEntity::LockFanciness::Native, true);
        EQUALS(stash.model.empty(), true);
    });

    IT("round trips a virtual open snapshot through the bounded inventory envelope", {
        KCDC::Shared::RPC::StashVirtualOpen sent;
        sent.body = R"({"stash":42,"revision":3,"rows":[]})";
        MafiaNet::BitStream stream;
        sent.Serialize(&stream, true);
        KCDC::Shared::RPC::StashVirtualOpen received;
        received.Serialize(&stream, false);
        EQUALS(received.valid, true);
        EQUALS(received.body, sent.body);
        EQUALS(KCDC::Shared::RPC::StashVirtualOpen::kIdentifier == KCDC::Shared::RPC::StashInventorySnapshot::kIdentifier, false);
    });

    IT("allows opening an unlocked container in reach", {
        KCDC::Shared::Game::StashRequest request;
        request.action          = KCDC::Shared::RPC::StashAction::Open;
        request.distanceSquared = 4.0F;
        EQUALS(KCDC::Shared::Game::EvaluateStashRequest(request) == nullptr, true);
    });

    IT("refuses opening a locked container", {
        KCDC::Shared::Game::StashRequest request;
        request.action = KCDC::Shared::RPC::StashAction::Open;
        request.locked = true;
        EQUALS(std::string(KCDC::Shared::Game::EvaluateStashRequest(request)), std::string("the container is locked"));
    });

    IT("refuses a container another player holds", {
        KCDC::Shared::Game::StashRequest request;
        request.heldByOther = true;
        EQUALS(std::string(KCDC::Shared::Game::EvaluateStashRequest(request)), std::string("another player has the container open"));
    });

    IT("refuses a body out of reach, and a position that is not a number", {
        KCDC::Shared::Game::StashRequest request;
        request.distanceSquared = 100.0F;
        EQUALS(KCDC::Shared::Game::EvaluateStashRequest(request) != nullptr, true);
        request.distanceSquared = std::numeric_limits<float>::quiet_NaN();
        EQUALS(KCDC::Shared::Game::EvaluateStashRequest(request) != nullptr, true);
    });

    IT("only unlocks or picks a container that is locked", {
        KCDC::Shared::Game::StashRequest request;
        request.action = KCDC::Shared::RPC::StashAction::Unlock;
        EQUALS(KCDC::Shared::Game::EvaluateStashRequest(request) != nullptr, true);
        request.locked = true;
        EQUALS(KCDC::Shared::Game::EvaluateStashRequest(request) == nullptr, true);

        request.action = KCDC::Shared::RPC::StashAction::Lockpick;
        EQUALS(KCDC::Shared::Game::EvaluateStashRequest(request) == nullptr, true);
        request.lockpickable = false;
        EQUALS(std::string(KCDC::Shared::Game::EvaluateStashRequest(request)), std::string("the container cannot be lockpicked"));
    });

    IT("round trips an interaction request and refuses an unknown action", {
        KCDC::Shared::RPC::StashInteractRequest request;
        request.stashNetworkId = 42;
        request.requestId      = 7;
        request.action         = KCDC::Shared::RPC::StashAction::Lockpick;

        MafiaNet::BitStream stream;
        request.Serialize(&stream, true);
        KCDC::Shared::RPC::StashInteractRequest decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.stashNetworkId, 42ULL);
        EQUALS(decoded.requestId, 7U);
        EQUALS(decoded.action == KCDC::Shared::RPC::StashAction::Lockpick, true);

        MafiaNet::BitStream bad;
        const std::uint64_t id    = 42;
        const std::uint32_t reqId = 7;
        const std::uint8_t action = 9;
        bad.Write(id);
        bad.Write(reqId);
        bad.Write(action);
        KCDC::Shared::RPC::StashInteractRequest rejected;
        rejected.Serialize(&bad, false);
        EQUALS(rejected.IsSane(), false);
    });

    IT("keys spawned and level containers by the EntityGuid clients use", {
        KCDC::Shared::Entities::StashEntity spawned;
        spawned.stashId = 7;
        EQUALS(spawned.GetEntityGuid(), 0xF5A5000000000007ULL);
        EQUALS(KCDC::Shared::Entities::StashEntity::SpawnedGuid(0x1234), 0xF5A5000000001234ULL);

        KCDC::Shared::Entities::StashEntity level;
        level.levelGuid = 0x01c63302c59259b5ULL;
        EQUALS(level.GetEntityGuid(), 0x01c63302c59259b5ULL);

        // No placement the level ships can be mistaken for a spawned one.
        for (const KCDC::Shared::Game::StashPlacement &placement : KCDC::Shared::Game::g_StashPlacements) {
            EQUALS((placement.guid & ~KCDC::Shared::Entities::StashEntity::kSpawnedGuidMask) != KCDC::Shared::Entities::StashEntity::kSpawnedGuidTag, true);
        }
    });

    IT("catalogs the level's containers with their authored locks", {
        std::size_t kutnohorsko = 0;
        std::size_t linked      = 0;
        for (const KCDC::Shared::Game::StashPlacement &placement : KCDC::Shared::Game::g_StashPlacements) {
            if (std::strcmp(placement.level, "kutnohorsko") == 0) {
                ++kutnohorsko;
            }
            if (placement.masterGuid != 0) {
                ++linked;
                const KCDC::Shared::Game::StashPlacement *master = KCDC::Shared::Game::FindStashPlacement(placement.level, placement.masterGuid);
                EQUALS(master != nullptr, true);
                EQUALS(master->masterGuid, 0ULL);
            }
        }
        EQUALS(kutnohorsko, static_cast<std::size_t>(2729));
        EQUALS(linked > 0, true);

        const KCDC::Shared::Game::StashPlacement *tunnel = nullptr;
        for (const KCDC::Shared::Game::StashPlacement &placement : KCDC::Shared::Game::g_StashPlacements) {
            if (std::strcmp(placement.name, "papezskyLegat_tunnelStash") == 0) {
                tunnel = &placement;
            }
        }
        EQUALS(tunnel != nullptr, true);
        EQUALS(tunnel->startsLocked, true);
        EQUALS(tunnel->dynamicKey, false);
    });
});
