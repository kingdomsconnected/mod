#include "core/server.h"
#include "shared/features/inventory/inventory_message.h"
#include "shared/features/stash/stash_inventory_message.h"
#include "shared/features/stash/stash_rpc.h"
#include "shared/features/world/world_entity.h"

#include <core_modules.h>
#include <networking/network_client.h>
#include <networking/network_server.h>
#include <networking/replication/replication_manager.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace KCDC::Features::Stash {
    // Exercise the private snapshot boundary independently of its callers.
    class StashServiceTestAccess {
      public:
        static void Snapshot(StashService &service, std::uint64_t player, std::uint64_t stash, bool preOpen = false) {
            service.SendSnapshot(player, stash, "", preOpen ? StashService::SnapshotAccess::OpenGrant : StashService::SnapshotAccess::HeldView);
        }

        static void ExpireGrants(StashService &service) {
            for (auto &grant : service._grants) {
                grant.expiresAt = 0;
            }
        }
    };
} // namespace KCDC::Features::Stash

namespace {
    void Require(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    class StashRPCClient final {
      public:
        Framework::Networking::NetworkClient peer {"127.0.0.1"};
        KCDC::Shared::Entities::HumanEntity *player = nullptr;
        std::vector<nlohmann::json> snapshots;
        std::vector<nlohmann::json> virtualOpens;
        std::vector<std::uint64_t> closes;
        std::vector<KCDC::Shared::RPC::StashInteractVerdict> verdicts;
        nlohmann::json inventorySync;
        std::optional<std::uint64_t> definitionRevision;

        void Init() {
            Require(static_cast<bool>(peer.Init()), "client peer starts");
            peer.SetBuildToken(Framework::Networking::NetworkPeer::kBuildVerificationDisabledToken);
            peer.RegisterRPC<KCDC::Shared::RPC::StashInventorySnapshot>([this](const auto &message, MafiaNet::Packet *) {
                Require(message.valid, "valid snapshot envelope");
                snapshots.push_back(nlohmann::json::parse(message.body));
            });
            peer.RegisterRPC<KCDC::Shared::RPC::StashVirtualOpen>([this](const auto &message, MafiaNet::Packet *) {
                Require(message.valid, "valid virtual envelope");
                virtualOpens.push_back(nlohmann::json::parse(message.body));
            });
            peer.RegisterRPC<KCDC::Shared::RPC::StashClosed>([this](const auto &message, MafiaNet::Packet *) {
                closes.push_back(message.stashNetworkId);
            });
            peer.RegisterRPC<KCDC::Shared::RPC::StashInteractVerdict>([this](const auto &message, MafiaNet::Packet *) {
                verdicts.push_back(message);
            });
            peer.RegisterRPC<KCDC::Shared::RPC::CustomItemDefinitions>([this](const auto &message, MafiaNet::Packet *) {
                Require(message.valid, "valid custom item definition envelope");
                const auto body = nlohmann::json::parse(message.body);
                if (body.value("begin", false)) {
                    definitionRevision.reset();
                }
                if (body.value("end", false)) {
                    definitionRevision = body.at("revision").template get<std::uint64_t>();
                }
            });
            peer.RegisterRPC<KCDC::Shared::RPC::InventorySync>([this](const auto &message, MafiaNet::Packet *packet) {
                const auto body = nlohmann::json::parse(message.body);
                inventorySync   = body;
                Require(definitionRevision.has_value(), "custom item definitions arrive before inventory projection");
                KCDC::Shared::RPC::InventoryApplied applied;
                applied.body = nlohmann::json({{"revision", body.at("revision")}, {"applied", true}, {"definitionRevision", *definitionRevision}}).dump();
                peer.SendRPC(applied, packet->guid);
            });
        }

        void Clear() {
            snapshots.clear();
            virtualOpens.clear();
            closes.clear();
            verdicts.clear();
        }
    };

    class StashRPCFixture final {
      public:
        static constexpr int kPort              = 28975;
        static constexpr std::uint64_t kBarrier = std::numeric_limits<std::uint64_t>::max();
        KCDC::Server server;
        StashRPCClient holder;
        StashRPCClient other;
        std::uint32_t requestId = 1;

        StashRPCFixture() {
            std::filesystem::create_directories("resources");
            // The working directory is owned by this test beneath the canonical
            // build tree. The fake level has no catalogued world containers.
            std::ofstream("server.json") << R"({"mod":{"level":"stash-rpc-tests","required_dlc":[],"npcEnabled":false,"scoreboard":false}})";
            Framework::Integrations::Server::InstanceOptions options {};
            options.modSlug          = "stash-rpc-tests";
            options.modName          = "Stash RPC tests";
            options.modVersion       = "2.0.0";
            options.gameName         = "stash-rpc-tests";
            options.gameVersion      = "1";
            options.bindHost         = "127.0.0.1";
            options.bindPort         = kPort;
            options.maxPlayers       = 4;
            options.bindPublicServer = false;
            options.verifyBuildToken = false;
            options.enableSignals    = false;
            options.modConfigSchema  = {{"level", Framework::Utils::ConfigFieldType::String, "stash-rpc-tests"}, {"required_dlc", Framework::Utils::ConfigFieldType::Json, nlohmann::json::array()}, {"npcEnabled", Framework::Utils::ConfigFieldType::Bool, false},
                 {"scoreboard", Framework::Utils::ConfigFieldType::Bool, false}};
            char executable[]        = "stash-rpc-tests";
            char *argv[]             = {executable};
            options.argc             = 1;
            options.argv             = argv;
            const auto initialized   = server.Init(options);
            if (!initialized) {
                throw std::runtime_error("server starts: " + initialized.GetError().message);
            }

            // Admission/build identity is outside this regression. Give the two
            // local peers real connections; the production stash RPC handlers
            // still resolve the sender from the packet's GUID and viewer.
            Network().SetOnSessionRequestCallback([this](MafiaNet::RakNetGUID guid, const auto &) {
                Network().AcceptSession(guid);
            });
            Network().SetOnClientAuthenticatedCallback([](MafiaNet::RakNetGUID) {});
            // Provide the world normally created by the gamemode.
            Require(Framework::CoreModules::GetReplication()->CreateEntity<KCDC::Shared::Entities::WorldEntity>() != nullptr, "world is created before players");
            holder.Init();
            other.Init();
            Require(static_cast<bool>(holder.peer.Connect("127.0.0.1", kPort)), "holder connects");
            Require(static_cast<bool>(other.peer.Connect("127.0.0.1", kPort)), "other connects");
            Wait([this] {
                return holder.peer.GetConnectionState() == Framework::Networking::PeerState::CONNECTED && other.peer.GetConnectionState() == Framework::Networking::PeerState::CONNECTED;
            });
            AddPlayer(holder);
            AddPlayer(other);
            Wait([this] {
                return server.GetInventory().IsInitialized(holder.player->GetNetworkID()) && server.GetInventory().IsInitialized(other.player->GetNetworkID()) && server.GetInventory().IsReady(holder.player->GetNetworkID())
                       && server.GetInventory().IsReady(other.player->GetNetworkID());
            });
            Barrier(holder);
            Barrier(other);
            holder.Clear();
            other.Clear();
        }

        ~StashRPCFixture() {
            holder.peer.Shutdown();
            other.peer.Shutdown();
            server.Shutdown();
        }

        Framework::Networking::NetworkServer &Network() {
            return *static_cast<Framework::Networking::NetworkServer *>(Framework::CoreModules::GetNetworkPeer());
        }

        void Pump() {
            Network().Update();
            server.GetInventory().Update();
            holder.peer.Update();
            other.peer.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        template <typename Predicate>
        void Wait(Predicate predicate) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!predicate()) {
                Require(std::chrono::steady_clock::now() < deadline, "RPC response timed out");
                Pump();
            }
        }

        void Barrier(StashRPCClient &client) {
            const auto before = client.closes.size();
            KCDC::Shared::RPC::StashClosed marker;
            marker.stashNetworkId = kBarrier;
            Network().SendRPC(marker, client.peer.GetPeer()->GetMyGUID());
            Wait([&] {
                return client.closes.size() > before && client.closes.back() == kBarrier;
            });
        }

        void Snapshot(StashRPCClient &client, KCDC::Shared::Entities::StashEntity &stash, bool preOpen = false) {
            KCDC::Features::Stash::StashServiceTestAccess::Snapshot(server.GetStashes(), client.player->GetNetworkID(), stash.GetNetworkID(), preOpen);
            Barrier(client); // Same reliable ordered channel as any snapshot.
        }

        void Transfer(StashRPCClient &client, KCDC::Shared::Entities::StashEntity &stash, bool stale = false) {
            KCDC::Shared::RPC::StashInventoryTransfer request;
            const auto *stock = server.GetStashes().ContentsOf(stash);
            request.body      = nlohmann::json({{"stash", stash.GetNetworkID()}, {"deposit", false}, {"units", {{{"id", stock->Rows().begin()->first}, {"amount", 1}}}}, {"playerRevision", server.GetInventory().Find(client.player->GetNetworkID())->Revision()},
                                                   {"stashRevision", stock->Revision() + (stale ? 1 : 0)}})
                               .dump();
            client.peer.SendRPC(request, Network().GetPeer()->GetMyGUID());
        }

        void RejectTransfer(StashRPCClient &client, KCDC::Shared::Entities::StashEntity &stash, std::uint64_t closedId = 0) {
            const auto snapshots = client.snapshots.size();
            const auto closes    = client.closes.size();
            Transfer(client, stash);
            Wait([&] {
                return client.closes.size() > closes || client.snapshots.size() > snapshots;
            });
            Require(client.snapshots.size() == snapshots, "unauthorized transfer returns no inventory rows");
            Require(client.closes.back() == (closedId != 0 ? closedId : stash.GetNetworkID()), "unauthorized transfer closes the requested or active view");
        }

        void Interact(StashRPCClient &client, KCDC::Shared::Entities::StashEntity &stash, KCDC::Shared::RPC::StashAction action = KCDC::Shared::RPC::StashAction::Open) {
            // Respect the production prompt limiter between uses.
            const auto readyAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(110);
            while (std::chrono::steady_clock::now() < readyAt) {
                Pump();
            }
            KCDC::Shared::RPC::StashInteractRequest request;
            request.stashNetworkId = stash.GetNetworkID();
            request.requestId      = requestId++;
            request.action         = action;
            client.peer.SendRPC(request, Network().GetPeer()->GetMyGUID());
            Wait([&] {
                return !client.verdicts.empty() && client.verdicts.back().requestId == request.requestId;
            });
            Require(client.verdicts.back().allowed, "physical prompt is granted");
        }

        void Close(StashRPCClient &client, KCDC::Shared::Entities::StashEntity &stash) {
            KCDC::Shared::RPC::StashClosed closed;
            closed.stashNetworkId = stash.GetNetworkID();
            client.peer.SendRPC(closed, Network().GetPeer()->GetMyGUID());
            Wait([&] {
                return server.GetStashes().HolderOf(stash.GetNetworkID()) == 0;
            });
        }

        void Fill(KCDC::Shared::Entities::StashEntity &stash) {
            const auto result = KCDC::Features::Inventory::InventoryService::ScriptAdd(server.GetStashes().ContentsOf(stash), {{"item", "bread"}, {"amount", 5U}, {"metadata", {{"health", 0.75}}}});
            if (!result.value("ok", false)) {
                throw std::runtime_error("secret stock is created: " + result.dump());
            }
            server.GetStashes().ContentsChanged(stash);
        }

      private:
        void AddPlayer(StashRPCClient &client) {
            auto *replication = Framework::CoreModules::GetReplication();
            client.player     = replication->CreateEntity<KCDC::Shared::Entities::HumanEntity>();
            Require(client.player != nullptr, "player is created");
            Require(client.player->GetNetworkID() != 0, "player has a nonzero identity");
            // This fixture supplies avatar state directly. Its transport-only
            // clients must not publish default avatar replicas over that state.
            client.player->streaming.visible = false;
            client.player->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
            client.player->ownerGUID                                                           = MafiaNet::ToPeerGuid(client.peer.GetPeer()->GetMyGUID());
            client.player->soul.valid                                                          = true;
            client.player->soul.resources[KCDC::Shared::Game::HumanSoulState::kHealth]         = {100.0F, 100.0F};
            client.player->soul.conditions[KCDC::Shared::Game::HumanSoulState::kConsciousness] = 1.0F;
            replication->SetViewer(client.player->ownerGUID, client.player);
            server.GetInventory().OnPlayerConnect(client.player->ownerGUID, client.player->GetNetworkID());
            server.GetInventory().OnPlayerReady(client.player->ownerGUID);
        }
    };

    void InventoryQuickslotsPersistAndValidate(StashRPCFixture &fixture) {
        auto &service       = fixture.server.GetInventory();
        const auto playerId = fixture.holder.player->GetNetworkID();
        KCDC::Shared::Inventory::Quickslots slots;
        slots.weapons[2][0] = "sword";
        slots.items[3]      = "potion";
        slots.activeWeapon  = 2;
        slots.activeItem    = 3;
        const nlohmann::json request {{"items", {{{"id", "belt"}, {"item", "7da54a04-67c4-4767-8b40-ee9211cc465c"}, {"amount", 1U}, {"equipped", 1U}}, {{"id", "pouch"}, {"item", "0d40444a-c79e-4406-b71f-9574a856cef2"}, {"amount", 1U}, {"equipped", 1U}},
                                                    {{"id", "sword"}, {"item", "ce1953df-9f10-4138-8fb1-393a11474553"}, {"amount", 1U}, {"equipped", 1U}}, {{"id", "potion"}, {"item", "9872a67f-e235-4641-913a-737681f52870"}, {"amount", 3U}}}},
            {"quickslots", slots.Encode()}};
        Require(service.ScriptSet(playerId, request).at("ok").get<bool>(), "restore accepts quickslot containers and row references");
        const auto saved = service.ScriptGet(playerId);
        Require(saved.at("quickslots") == slots.Encode() && service.Equipped(playerId)->contains("belt") && service.Equipped(playerId)->contains("pouch"), "scripts can save the containers and complete quickslot snapshot");
        fixture.Wait([&] {
            return fixture.holder.inventorySync.contains("quickslots") && fixture.holder.inventorySync.at("quickslots") == slots.Encode();
        });
        const auto generation             = fixture.holder.inventorySync.at("equipmentRestore");
        auto invalid                      = request;
        invalid["items"][0]["amount"]     = 2U;
        invalid["quickslots"]["items"][3] = "another-player-row";
        Require(service.ScriptSet(playerId, invalid).at("code") == "invalidQuickslots" && service.ScriptGet(playerId).at("items") == saved.at("items"), "a bad quickslot reference refuses the entire inventory replacement");
        auto moved       = slots;
        moved.items[0]   = std::exchange(moved.items[3], {});
        moved.activeItem = 0;
        KCDC::Shared::RPC::InventoryEquipment report;
        report.body = nlohmann::json {{"revision", saved.at("revision")}, {"equipmentRestore", generation}, {"items", {{"belt", 1U}, {"pouch", 1U}, {"sword", 1U}}}, {"quickslots", moved.Encode()}}.dump();
        fixture.holder.peer.BroadcastRPC(report, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        fixture.Wait([&] {
            return service.ScriptGet(playerId).at("quickslots") == moved.Encode();
        });
        Require(service.ScriptGet(playerId).at("revision") == saved.at("revision"), "quickslot reports do not alter item revisions or quantities");
        Require(service.ScriptSet(playerId, {{"items", saved.at("items")}, {"quickslots", saved.at("quickslots")}}).at("ok").get<bool>(), "saved scripting state restores with its row IDs");
        service.Receive(report, fixture.holder.player->ownerGUID);
        service.Update();
        Require(service.ScriptGet(playerId).at("quickslots") == slots.Encode(), "a report from before the restore cannot overwrite restored quickslots");
        Require(service.ScriptSet(playerId, {{"items", nlohmann::json::array()}}).at("ok").get<bool>(), "clear the test inventory");
        Require(service.ScriptSet(playerId, {{"items", saved.at("items")}, {"quickslots", saved.at("quickslots")}}).at("ok").get<bool>() && service.ScriptGet(playerId).at("quickslots") == slots.Encode(), "saved quickslots survive replacing an empty inventory");
        Require(service.ScriptRemove(playerId, {{"units", {{{"id", "potion"}, {"amount", 3U}}}}}).at("ok").get<bool>(), "consume the assigned row");
        Require(service.ScriptGet(playerId).at("quickslots").at("items")[3].is_null(), "scripts never see dangling references after a row is removed");
    }

    void VirtualRowsArePrivate(StashRPCFixture &fixture) {
        auto &service = fixture.server.GetStashes();
        auto *stash   = service.CreateVirtual(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        fixture.Fill(*stash);
        const auto original = KCDC::Features::Inventory::InventoryService::EncodeInventory(*service.ContentsOf(*stash));
        fixture.RejectTransfer(fixture.other, *stash);
        fixture.Snapshot(fixture.other, *stash);
        Require(fixture.other.snapshots.empty(), "closed virtual stock cannot be read at the snapshot boundary");
        Require(service.OpenVirtual(*stash, fixture.holder.player->GetNetworkID()), "virtual stock opens for the holder");
        fixture.Wait([&] {
            return !fixture.holder.virtualOpens.empty();
        });
        auto &inventory = fixture.server.GetInventory();
        inventory.OnPlayerDisconnect(fixture.holder.player->ownerGUID);
        inventory.OnPlayerConnect(fixture.holder.player->ownerGUID, fixture.holder.player->GetNetworkID());
        inventory.OnPlayerReady(fixture.holder.player->ownerGUID);
        inventory.Update(); // Publish revision zero without pumping the client acknowledgement.
        Require(!inventory.IsReady(fixture.holder.player->GetNetworkID()) && !inventory.IsInitialized(fixture.holder.player->GetNetworkID()), "revision zero cannot mark an inventory ready before its first projection acknowledgement");
        Require(!service.OpenVirtual(*stash, fixture.holder.player->GetNetworkID()), "virtual opening waits for the first projection acknowledgement");
        fixture.Snapshot(fixture.holder, *stash);
        Require(fixture.holder.snapshots.empty(), "an uninitialized holder receives no snapshot");
        fixture.Wait([&] {
            return inventory.IsInitialized(fixture.holder.player->GetNetworkID()) && inventory.IsReady(fixture.holder.player->GetNetworkID());
        });
        fixture.RejectTransfer(fixture.other, *stash);
        fixture.Snapshot(fixture.other, *stash);
        Require(fixture.other.snapshots.empty() && service.HolderOf(stash->GetNetworkID()) == fixture.holder.player->GetNetworkID(), "non-holder learns nothing and does not revoke the holder");
        fixture.other.player->SetVirtualWorld(42);
        fixture.RejectTransfer(fixture.other, *stash);
        fixture.Snapshot(fixture.other, *stash, true);
        Require(fixture.other.snapshots.empty(), "another world gets no rows through either snapshot path");
        fixture.other.player->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        fixture.holder.player->SetVirtualWorld(42);
        fixture.Snapshot(fixture.holder, *stash);
        Require(fixture.holder.snapshots.empty(), "a holder who changed world cannot read rows");
        fixture.RejectTransfer(fixture.holder, *stash);
        Require(service.HolderOf(stash->GetNetworkID()) == 0 && KCDC::Features::Inventory::InventoryService::EncodeInventory(*service.ContentsOf(*stash)) == original, "lost access releases the view without moving stock");
        fixture.holder.player->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        Require(service.OpenVirtual(*stash, fixture.holder.player->GetNetworkID()), "virtual stock reopens");
        fixture.Transfer(fixture.holder, *stash, true);
        fixture.Wait([&] {
            return !fixture.holder.snapshots.empty();
        });
        Require(fixture.holder.snapshots.back().at("code") == "staleRevision" && fixture.holder.snapshots.back().at("rows").size() == 1, "an authorized stale transfer receives stock for resync");
        const auto before = fixture.holder.snapshots.size();
        fixture.Transfer(fixture.holder, *stash);
        fixture.Wait([&] {
            return fixture.holder.snapshots.size() > before;
        });
        Require(fixture.holder.snapshots.back().at("rows").at(0).at("amount") == 4, "an accepted withdrawal receives updated rows while player sync is in flight");
        fixture.Wait([&] {
            return fixture.server.GetInventory().IsReady(fixture.holder.player->GetNetworkID());
        });
        fixture.Close(fixture.holder, *stash);
        fixture.holder.Clear();
        fixture.RejectTransfer(fixture.holder, *stash);
        fixture.Snapshot(fixture.holder, *stash);
        Require(fixture.holder.snapshots.empty(), "a former holder cannot read a closed stash");
        service.Despawn(stash);
        fixture.holder.Clear();
        fixture.other.Clear();
    }

    void PhysicalGrantsDoNotAuthorizeTransfers(StashRPCFixture &fixture) {
        auto &service            = fixture.server.GetStashes();
        auto *master             = service.Spawn({100.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F, 0.0F}, MafiaNet::VIRTUAL_WORLD_GLOBAL);
        auto *child              = service.Spawn({}, {1.0F, 0.0F, 0.0F, 0.0F}, MafiaNet::VIRTUAL_WORLD_GLOBAL);
        child->contentsNetworkId = master->GetNetworkID();
        fixture.Fill(*master);
        fixture.RejectTransfer(fixture.other, *master);
        fixture.Snapshot(fixture.other, *child, true);
        Require(fixture.other.snapshots.empty(), "a closed physical stash without an open grant is private");
        fixture.other.player->SetVirtualWorld(42);
        fixture.RejectTransfer(fixture.other, *master);
        fixture.Snapshot(fixture.other, *child);
        fixture.Snapshot(fixture.other, *child, true);
        Require(fixture.other.snapshots.empty(), "physical stock cannot be read from another world");
        fixture.other.player->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        fixture.Interact(fixture.holder, *child);
        Require(fixture.holder.snapshots.size() == 1 && service.HolderOf(child->GetNetworkID()) == 0, "the granted physical prompt receives its linked stock before OnOpened");
        fixture.Snapshot(fixture.other, *child, true);
        Require(fixture.other.snapshots.empty(), "another connection cannot use the holder's pre-open grant");
        const auto before = fixture.holder.snapshots.size();
        fixture.RejectTransfer(fixture.holder, *master);
        Require(fixture.holder.snapshots.size() == before, "a pre-open grant cannot obtain a transfer failure snapshot");
        KCDC::Shared::RPC::StashOpened opened;
        opened.stashNetworkId = master->GetNetworkID();
        fixture.holder.peer.SendRPC(opened, fixture.Network().GetPeer()->GetMyGUID());
        fixture.Wait([&] {
            return service.HolderOf(child->GetNetworkID()) == fixture.holder.player->GetNetworkID() && fixture.holder.snapshots.size() > before;
        });
        fixture.RejectTransfer(fixture.other, *master);
        fixture.Snapshot(fixture.other, *child);
        Require(fixture.other.snapshots.empty(), "a held physical stash cannot be read by another connection");
        const auto heldSnapshots = fixture.holder.snapshots.size();
        fixture.Snapshot(fixture.holder, *master);
        Require(fixture.holder.snapshots.size() == heldSnapshots + 1, "a linked view checks reach at the chest actually opened");
        fixture.holder.player->soul.conditions[KCDC::Shared::Game::HumanSoulState::kConsciousness] = 0.0F;
        fixture.Snapshot(fixture.holder, *master);
        Require(fixture.holder.snapshots.size() == heldSnapshots + 1, "an incapacitated holder cannot read rows");
        fixture.holder.player->soul.conditions[KCDC::Shared::Game::HumanSoulState::kConsciousness] = 1.0F;
        fixture.holder.player->position                                                            = {20.0F, 0.0F, 0.0F};
        const auto distant                                                                         = fixture.holder.snapshots.size();
        fixture.Snapshot(fixture.holder, *master);
        Require(fixture.holder.snapshots.size() == distant, "a holder out of physical reach cannot read rows");
        fixture.RejectTransfer(fixture.holder, *master, child->GetNetworkID());
        fixture.holder.player->position = {};
        fixture.Interact(fixture.holder, *child);
        const auto granted = fixture.holder.snapshots.size();
        fixture.holder.player->SetVirtualWorld(42);
        fixture.Snapshot(fixture.holder, *child, true);
        Require(fixture.holder.snapshots.size() == granted, "a valid pre-open grant does not survive a world change");
        fixture.holder.player->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        KCDC::Features::Stash::StashServiceTestAccess::ExpireGrants(service);
        const auto expired = fixture.holder.snapshots.size();
        fixture.Snapshot(fixture.holder, *child, true);
        Require(fixture.holder.snapshots.size() == expired, "an expired pre-open grant cannot read rows");
        child->locked = true;
        fixture.Interact(fixture.holder, *child, KCDC::Shared::RPC::StashAction::Unlock);
        const auto locked = fixture.holder.snapshots.size();
        fixture.Snapshot(fixture.holder, *child, true);
        Require(fixture.holder.snapshots.size() == locked, "an unlock grant discloses nothing before the lock is beaten");
        KCDC::Shared::RPC::StashLockReport unlocked;
        unlocked.stashNetworkId = child->GetNetworkID();
        fixture.holder.peer.SendRPC(unlocked, fixture.Network().GetPeer()->GetMyGUID());
        fixture.Wait([&] {
            return fixture.holder.snapshots.size() > locked;
        });
        Require(!child->locked && service.HolderOf(child->GetNetworkID()) == 0, "a successful unlock retains the legitimate pre-open snapshot");
        fixture.Interact(fixture.other, *child);
        fixture.other.peer.SendRPC(opened, fixture.Network().GetPeer()->GetMyGUID());
        fixture.Wait([&] {
            return service.HolderOf(child->GetNetworkID()) == fixture.other.player->GetNetworkID();
        });
        const auto superseded = fixture.holder.snapshots.size();
        fixture.Snapshot(fixture.holder, *child, true);
        Require(fixture.holder.snapshots.size() == superseded, "a still-valid pre-open grant cannot read another holder's stock");
        service.Despawn(child);
        service.Despawn(master);
    }

    void PeriodicAccessChecksRevokeViews(StashRPCFixture &fixture) {
        auto &service = fixture.server.GetStashes();
        auto *stash   = service.CreateVirtual(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        fixture.Fill(*stash);
        Require(service.OpenVirtual(*stash, fixture.holder.player->GetNetworkID()), "periodic check starts with a virtual view");
        fixture.Barrier(fixture.holder);
        fixture.holder.Clear();
        fixture.holder.player->position = {500.0F, 0.0F, 0.0F};
        service.Update();
        Require(service.HolderOf(stash->GetNetworkID()) == fixture.holder.player->GetNetworkID(), "periodic checks keep virtual access regardless of distance");
        fixture.holder.player->SetVirtualWorld(42);
        service.Update();
        Require(service.HolderOf(stash->GetNetworkID()) == 0, "periodic checks revoke a virtual holder who changes world");
        fixture.Barrier(fixture.holder);
        Require(fixture.holder.closes.size() == 2 && fixture.holder.closes.front() == stash->GetNetworkID() && fixture.holder.snapshots.empty(), "periodic virtual revocation sends a close without stock rows");
        Require(service.ContentsOf(*stash)->Rows().size() == 1, "periodic revocation preserves virtual stock");
        fixture.holder.player->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        fixture.holder.player->position = {};
        service.Despawn(stash);

        auto *master             = service.Spawn({100.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F, 0.0F}, MafiaNet::VIRTUAL_WORLD_GLOBAL);
        auto *child              = service.Spawn({}, {1.0F, 0.0F, 0.0F, 0.0F}, MafiaNet::VIRTUAL_WORLD_GLOBAL);
        child->contentsNetworkId = master->GetNetworkID();
        fixture.Fill(*master);
        KCDC::Shared::RPC::StashOpened opened;
        opened.stashNetworkId = master->GetNetworkID();
        const auto openChild  = [&] {
            fixture.Interact(fixture.holder, *child);
            fixture.holder.peer.SendRPC(opened, fixture.Network().GetPeer()->GetMyGUID());
            fixture.Wait([&] {
                return service.HolderOf(child->GetNetworkID()) == fixture.holder.player->GetNetworkID();
            });
            fixture.Barrier(fixture.holder);
            fixture.holder.Clear();
        };
        openChild();
        service.Update();
        Require(service.HolderOf(child->GetNetworkID()) == fixture.holder.player->GetNetworkID(), "periodic checks use the nearby linked chest rather than its distant master");
        fixture.holder.player->position = {20.0F, 0.0F, 0.0F};
        service.Update();
        Require(service.HolderOf(child->GetNetworkID()) == 0, "periodic checks revoke a holder out of physical reach");
        fixture.Barrier(fixture.holder);
        Require(fixture.holder.closes.size() == 2 && fixture.holder.closes.front() == child->GetNetworkID() && fixture.holder.snapshots.empty(), "periodic physical revocation closes the linked view without stock rows");
        fixture.holder.player->position = {};
        openChild();
        fixture.holder.player->soul.conditions[KCDC::Shared::Game::HumanSoulState::kConsciousness] = 0.0F;
        service.Update();
        Require(service.HolderOf(child->GetNetworkID()) == 0, "periodic checks revoke an incapacitated holder");
        fixture.Barrier(fixture.holder);
        Require(fixture.holder.closes.size() == 2 && fixture.holder.closes.front() == child->GetNetworkID() && fixture.holder.snapshots.empty(), "periodic incapacity revocation sends a close without stock rows");
        fixture.holder.player->soul.conditions[KCDC::Shared::Game::HumanSoulState::kConsciousness] = 1.0F;
        service.Despawn(child);
        service.Despawn(master);
    }
} // namespace

int RunStashRpcSuite() {
    try {
        StashRPCFixture fixture;
        InventoryQuickslotsPersistAndValidate(fixture);
        VirtualRowsArePrivate(fixture);
        PhysicalGrantsDoNotAuthorizeTransfers(fixture);
        PeriodicAccessChecksRevokeViews(fixture);
        std::cout << "Stash service/RPC privacy regression tests passed\n";
        return 0;
    }
    catch (const std::exception &error) {
        std::cerr << "Stash service/RPC privacy regression failed: " << error.what() << '\n';
        return 1;
    }
}
