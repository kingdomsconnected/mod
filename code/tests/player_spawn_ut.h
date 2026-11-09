#pragma once

#include "shared/features/player/player_spawn.h"
#include "common/module_support.h"
#include <networking/network_peer.h>

#include <mafianet/bit_stream.h>

#include <limits>
#include <memory>

class PlayerSpawnTestEntity final: public Framework::Networking::Replication::NetworkEntity {
  public:
    static constexpr const char *kTypeName = "KCDC::PlayerSpawnTest";
};

MODULE(player_spawn_protocol, {
    IT("finds the downloaded owner before gameplay replication is published", {
        class DownloadPeer final: public Framework::Networking::NetworkPeer {
          public:
            bool HandlePacket(std::uint8_t, MafiaNet::Packet *) override { return false; }
            ~DownloadPeer() {
                MafiaNet::RakPeerInterface::DestroyInstance(_peer);
                _peer = nullptr;
            }
        } peer;
        auto *downloaded = peer.GetReplicationManager();
        downloaded->Init(&peer, false);
        // Typed lookups compare the registered type id, so the entities come from the registry.
        Framework::Networking::Replication::EntityRegistry::Get().Register<PlayerSpawnTestEntity>(PlayerSpawnTestEntity::kTypeName);
        const std::uint32_t typeId = Framework::Networking::Replication::EntityTypeId<PlayerSpawnTestEntity>();
        std::unique_ptr<Framework::Networking::Replication::NetworkEntity> remote(Framework::Networking::Replication::EntityRegistry::Get().Create(typeId));
        std::unique_ptr<Framework::Networking::Replication::NetworkEntity> owner(Framework::Networking::Replication::EntityRegistry::Get().Create(typeId));
        remote->ownerGUID = MafiaNet::ToPeerGuid(MafiaNet::RakNetGUID(123));
        owner->ownerGUID = downloaded->GetMyGUID();
        remote->SetNetworkID(4210);
        owner->SetNetworkID(4211);
        downloaded->Reference(remote.get());
        downloaded->Reference(owner.get());

        EQUALS(Framework::CoreModules::GetReplication(), nullptr);
        auto isOwner = [](PlayerSpawnTestEntity *entity) { return entity->IsOwner(); };
        EQUALS(KCDC::Common::FindReplica<PlayerSpawnTestEntity>(isOwner), nullptr);
        auto *found = KCDC::Common::FindReplica<PlayerSpawnTestEntity>(isOwner, downloaded);
        EQUALS(static_cast<Framework::Networking::Replication::NetworkEntity *>(found), owner.get());
        KCDC::Shared::RPC::RequestSpawn request;
        request.networkId = found != nullptr ? found->GetNetworkID() : 0;
        EQUALS(request.IsSane(), true);

        downloaded->Dereference(owner.get());
        EQUALS(KCDC::Common::FindReplica<PlayerSpawnTestEntity>(isOwner, downloaded), nullptr);
        downloaded->Dereference(remote.get());
    });

    IT("round trips a placement", {
        KCDC::Shared::RPC::SpawnPlayer spawn;
        spawn.x         = -1423.5F;
        spawn.y         = 2871.25F;
        spawn.z         = 118.0F;
        spawn.rotationZ = 0.7071068F;
        spawn.rotationW = 0.7071068F;
        spawn.placed    = true;
        EQUALS(spawn.IsSane(), true);

        MafiaNet::BitStream stream;
        spawn.Serialize(&stream, true);
        KCDC::Shared::RPC::SpawnPlayer decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.x, spawn.x);
        EQUALS(decoded.y, spawn.y);
        EQUALS(decoded.z, spawn.z);
        EQUALS(decoded.rotationZ, spawn.rotationZ);
        EQUALS(decoded.rotationW, spawn.rotationW);
        EQUALS(decoded.placed, true);
    });

    IT("accepts an answer that names nowhere", {
        // The join barrier releases on this message, so an answer naming
        // nowhere still has to pass the receiver's own check.
        KCDC::Shared::RPC::SpawnPlayer nowhere;
        EQUALS(nowhere.placed, false);
        EQUALS(nowhere.IsSane(), true);

        MafiaNet::BitStream stream;
        nowhere.Serialize(&stream, true);
        KCDC::Shared::RPC::SpawnPlayer decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.placed, false);
    });

    IT("rejects a placement nothing can stand at", {
        KCDC::Shared::RPC::SpawnPlayer spawn;
        spawn.placed = true;
        EQUALS(spawn.IsSane(), true);

        KCDC::Shared::RPC::SpawnPlayer notFinite = spawn;
        notFinite.z                              = std::numeric_limits<float>::infinity();
        EQUALS(notFinite.IsSane(), false);

        KCDC::Shared::RPC::SpawnPlayer notANumber = spawn;
        notANumber.x                              = std::numeric_limits<float>::quiet_NaN();
        EQUALS(notANumber.IsSane(), false);

        // A zero quaternion has no facing to normalize into one.
        KCDC::Shared::RPC::SpawnPlayer noFacing = spawn;
        noFacing.rotationW                      = 0.0F;
        EQUALS(noFacing.IsSane(), false);

        MafiaNet::BitStream empty;
        KCDC::Shared::RPC::SpawnPlayer truncated;
        truncated.Serialize(&empty, false);
        EQUALS(truncated.IsSane(), false);
    });

    IT("round trips the ask", {
        KCDC::Shared::RPC::RequestSpawn request;
        request.networkId = 4211;
        EQUALS(request.IsSane(), true);

        MafiaNet::BitStream stream;
        request.Serialize(&stream, true);
        KCDC::Shared::RPC::RequestSpawn decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.networkId, request.networkId);

        KCDC::Shared::RPC::RequestSpawn noBody;
        EQUALS(noBody.IsSane(), false);
    });
});
