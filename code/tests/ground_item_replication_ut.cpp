#include <utils/safe_win32.h>

#include "core/interest_config.h"
#include "shared/features/ground_item/ground_item_entity.h"
#include "shared/features/player/player_entity.h"

#include <networking/network_peer.h>
#include <networking/replication/replication_connection.h>

#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace {
    void RequireGroundReplication(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    class GroundReplicationPeer final: public Framework::Networking::NetworkPeer {
      public:
        bool HandlePacket(std::uint8_t, MafiaNet::Packet *) override {
            return false;
        }
    };

    void CollectInterest(Framework::Networking::Replication::ReplicationManager &server, Framework::Networking::Replication::NetworkEntity *viewer, MafiaNet::PeerGuid viewerGuid, const std::unordered_set<Framework::Networking::Replication::NetworkEntity *> &previous, std::unordered_set<Framework::Networking::Replication::NetworkEntity *> &out) {
        const Framework::Networking::Replication::InterestGrid &interest = server.Interest();
        Framework::Networking::Replication::SlotSet previousSlots;
        for (Framework::Networking::Replication::NetworkEntity *entity : previous) {
            previousSlots.Set(interest.SlotOf(entity));
        }
        std::vector<std::uint32_t> slots;
        interest.CollectVisible(viewer, viewerGuid, previousSlots, server.MainScratch(), slots);
        for (const std::uint32_t slot : slots) {
            out.insert(interest.EntityAt(slot));
        }
    }
} // namespace

void GroundItemReplicationTests() {
    GroundReplicationPeer serverPeer;
    GroundReplicationPeer clientPeer;
    auto *server = serverPeer.GetReplicationManager();
    auto *client = clientPeer.GetReplicationManager();
    server->Init(&serverPeer, true);
    client->Init(&clientPeer, false);
    KCDC::ConfigureInterest(*server);
    server->ConfigureGrid(100.0F, -4096.0F, 4096.0F);
    // Tests advance the actual grid explicitly, without a wall-clock wait.
    server->SetInterestRebuildInterval(0);
    Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::GroundItemEntity>(KCDC::Shared::Entities::GroundItemEntity::kTypeName);
    Framework::Networking::Replication::EntityRegistry::Get().Register<KCDC::Shared::Entities::HumanEntity>(KCDC::Shared::Entities::HumanEntity::kTypeName);

    const auto firstPeer         = MafiaNet::ToPeerGuid(MafiaNet::RakNetGUID(1001));
    const auto latePeer          = MafiaNet::ToPeerGuid(MafiaNet::RakNetGUID(1002));
    auto *firstViewer            = server->CreateEntity<KCDC::Shared::Entities::HumanEntity>();
    auto *lateViewer             = server->CreateEntity<KCDC::Shared::Entities::HumanEntity>();
    firstViewer->position        = {100.0F, 100.0F, 20.0F};
    lateViewer->position         = {110.0F, 100.0F, 20.0F};
    firstViewer->streaming.range = lateViewer->streaming.range = KCDC::Shared::Entities::HumanEntity::kStreamRange;
    firstViewer->SetVirtualWorld(5);
    lateViewer->SetVirtualWorld(5);
    server->SetViewer(firstPeer, firstViewer);

    constexpr std::size_t kGroundItemBudget = 64;
    std::vector<KCDC::Shared::Entities::GroundItemEntity *> drops;
    for (std::uint32_t i = 0; i < 130; ++i) {
        auto *drop                  = server->CreateEntity<KCDC::Shared::Entities::GroundItemEntity>();
        drop->position              = {90.0F + static_cast<float>(i), 102.0F, 20.0F};
        drop->rotation              = glm::identity<glm::quat>();
        drop->restPosition          = drop->position;
        drop->restRotation          = drop->rotation;
        drop->resting               = true;
        drop->item.inventoryItemId  = i + 1;
        drop->item.itemClass        = {1234, 5678};
        drop->item.amount           = i + 1;
        drop->item.quality          = 2;
        drop->item.health           = 0.65F;
        drop->item.condition        = 0.8F;
        drop->sourcePlayerNetworkId = firstViewer->GetNetworkID();
        drop->recoveredItemWuid     = i % 2 == 0 ? 0 : 0xF123456789ABCDEFULL - i;
        drop->carryable             = i % 2 == 0;
        drop->streaming.range       = KCDC::Shared::Entities::GroundItemEntity::kStreamRange;
        drop->SetVirtualWorld(5);
        drops.push_back(drop);
    }
    auto *otherWorld            = server->CreateEntity<KCDC::Shared::Entities::GroundItemEntity>();
    otherWorld->position        = firstViewer->position;
    otherWorld->streaming.range = KCDC::Shared::Entities::GroundItemEntity::kStreamRange;
    otherWorld->SetVirtualWorld(6);
    auto *distant            = server->CreateEntity<KCDC::Shared::Entities::GroundItemEntity>();
    distant->position        = {2000.0F, 2000.0F, 20.0F};
    distant->streaming.range = KCDC::Shared::Entities::GroundItemEntity::kStreamRange;
    distant->SetVirtualWorld(5);

    server->RebuildInterest();
    const std::unordered_set<Framework::Networking::Replication::NetworkEntity *> noPrevious;
    std::unordered_set<Framework::Networking::Replication::NetworkEntity *> firstVisible;
    CollectInterest(*server,firstViewer, firstPeer, noPrevious, firstVisible);
    std::size_t visibleDrops = 0;
    for (auto *drop : drops) {
        visibleDrops += firstVisible.contains(drop) ? 1 : 0;
    }
    RequireGroundReplication(visibleDrops == kGroundItemBudget, "one viewer receives the nearest stacks up to the ground item budget");
    RequireGroundReplication(!firstVisible.contains(otherWorld) && !firstVisible.contains(distant), "drops respect virtual worlds and spatial range");

    // A fresh connection has no past interest or drop events. Its initial
    // construction list must contain the stacks already on the ground.
    server->SetViewer(latePeer, lateViewer);
    auto *lateConnection = server->AllocConnection(MafiaNet::SystemAddress(), MafiaNet::ToGuid(latePeer));
    DataStructures::List<MafiaNet::Replica3 *> construct;
    DataStructures::List<MafiaNet::Replica3 *> destroy;
    lateConnection->QueryReplicaList(construct, destroy);
    std::unordered_set<MafiaNet::Replica3 *> seeded;
    for (unsigned i = 0; i < construct.Size(); ++i) {
        seeded.insert(construct[i]);
    }
    std::size_t seededDrops = 0;
    for (auto *drop : drops) {
        seededDrops += seeded.contains(drop) ? 1 : 0;
    }
    RequireGroundReplication(seededDrops == kGroundItemBudget, "late join construction includes the preexisting in-range drops up to the budget");
    RequireGroundReplication(!seeded.contains(otherWorld) && !seeded.contains(distant), "late join construction does not leak drops from another world or remote area");

    // A stack still falling: a fresh connection seeds its current pose
    // without waiting for it to settle.
    auto *moved     = drops.front();
    moved->position = moved->restPosition = {101.5F, 100.4F, 19.8F};
    moved->rotation = moved->restRotation = glm::angleAxis(1.2F, glm::vec3(0.0F, 1.0F, 0.0F));
    moved->resting                        = false;

    for (auto *drop : drops) {
        KCDC::Shared::Entities::GroundItemEntity observer;
        observer.replicaManager = client;
        MafiaNet::BitStream construction;
        drop->SerializeConstruction(&construction, lateConnection);
        RequireGroundReplication(observer.DeserializeConstruction(&construction, nullptr), "ground construction seed is readable");
        RequireGroundReplication(observer.item.inventoryItemId == drop->item.inventoryItemId && observer.item.itemClass == drop->item.itemClass && observer.item.amount == drop->item.amount, "late join seed includes exact stack identity and count");
        RequireGroundReplication(observer.resting == drop->resting && observer.restPosition == drop->restPosition && observer.restRotation == drop->restRotation,
            "late join seed includes the latest reliable pose without another settle event");
        RequireGroundReplication(observer.item.quality == drop->item.quality && observer.item.health == drop->item.health && observer.item.condition == drop->item.condition, "late join seed preserves item properties");
        RequireGroundReplication(observer.carryable == drop->carryable && observer.sourcePlayerNetworkId == drop->sourcePlayerNetworkId, "late join seed says which stacks are carried props and who put them down");
        RequireGroundReplication(observer.recoveredItemWuid == drop->recoveredItemWuid, "construction preserves the exact source recovery identity, including ordinary drops without one");
        observer.replicaManager = nullptr;
    }

    std::unordered_set<Framework::Networking::Replication::NetworkEntity *> lateVisible;
    CollectInterest(*server,lateViewer, latePeer, noPrevious, lateVisible);
    lateViewer->position = {1800.0F, 100.0F, 20.0F};
    server->RebuildInterest();
    std::unordered_set<Framework::Networking::Replication::NetworkEntity *> streamedOut;
    CollectInterest(*server,lateViewer, latePeer, lateVisible, streamedOut);
    for (auto *drop : drops) {
        RequireGroundReplication(!streamedOut.contains(drop), "leaving range removes drops from observer interest");
    }
    std::size_t returnedDrops = 0;
    lateViewer->position = firstViewer->position;
    server->RebuildInterest();
    std::unordered_set<Framework::Networking::Replication::NetworkEntity *> returned;
    CollectInterest(*server,lateViewer, latePeer, streamedOut, returned);
    for (auto *drop : drops) {
        returnedDrops += returned.contains(drop) ? 1 : 0;
    }
    RequireGroundReplication(returnedDrops == kGroundItemBudget, "returning to the area restores the stacks without a new drop event");
    KCDC::Shared::Entities::GroundItemEntity returnedObserver;
    returnedObserver.replicaManager = client;
    MafiaNet::BitStream returnedSeed;
    moved->SerializeConstruction(&returnedSeed, lateConnection);
    RequireGroundReplication(returnedObserver.DeserializeConstruction(&returnedSeed, nullptr) && returnedObserver.restPosition == moved->restPosition && returnedObserver.restRotation == moved->restRotation && !returnedObserver.resting,
        "stream re-entry receives the newest moving position and rotation");
    returnedObserver.replicaManager = nullptr;
    // The nearest client simulates a stack while it falls and gives it
    // back once it rests; a server write reaches it through the forced
    // state, settled pose included. No simulator can take one off the ground.
    const auto *policy = moved->GetDelegationPolicy();
    RequireGroundReplication(policy != nullptr && policy->acquireRange <= policy->releaseRange && policy->requireSameVirtualWorld, "a falling stack is delegated within its world");
    RequireGroundReplication(!moved->CanOwnerDestroy(), "the client simulating a stack cannot destroy it");
    moved->resting = true;
    RequireGroundReplication(moved->GetDelegationPolicy() == nullptr, "a resting stack is not delegated");
    MafiaNet::BitStream forced;
    Framework::Networking::Replication::FieldSerializer forcedWriter(&forced, true, true);
    moved->SerializeForcedState(forcedWriter);
    KCDC::Shared::Entities::GroundItemEntity simulator;
    Framework::Networking::Replication::FieldSerializer forcedReader(&forced, false, true);
    simulator.SerializeForcedState(forcedReader);
    RequireGroundReplication(forcedReader.Good() && simulator.position == moved->position && simulator.resting && simulator.restPosition == moved->restPosition && simulator.restRotation == moved->restRotation, "the forced state carries the pose and the settled pose");

    lateViewer->SetVirtualWorld(6);
    server->RebuildInterest();
    std::unordered_set<Framework::Networking::Replication::NetworkEntity *> changedWorld;
    CollectInterest(*server,lateViewer, latePeer, returned, changedWorld);
    RequireGroundReplication(changedWorld.contains(otherWorld), "changing virtual world admits its own nearby ground stock");
    for (auto *drop : drops) {
        RequireGroundReplication(!changedWorld.contains(drop), "changing virtual world removes previous world ground stock");
    }

    KCDC::Shared::Entities::GroundItemEntity forged;
    forged.replicaManager = server;
    forged.ownerGUID      = latePeer;
    RequireGroundReplication(!forged.QueryRemoteConstruction(lateConnection), "a client cannot construct a ground stack of its own");
    forged.replicaManager = nullptr;
    server->DeallocConnection(lateConnection);
    for (auto *drop : drops) {
        server->DestroyEntity(drop);
    }
    server->DestroyEntity(otherWorld);
    server->DestroyEntity(distant);
    server->DestroyEntity(firstViewer);
    server->DestroyEntity(lateViewer);
}
