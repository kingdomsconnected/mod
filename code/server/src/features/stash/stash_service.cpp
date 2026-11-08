#include "stash_service.h"
#include "stash_scripting.h"

#include "core/server.h"
#include "features/player/player_roster.h"

#include "shared/features/player/player_entity.h"
#include "shared/features/stash/stash_entity.h"
#include "shared/features/stash/stash_inventory_message.h"
#include "shared/features/stash/stash_policy.h"
#include "shared/features/stash/stash_rpc.h"
#include "shared/game/stash_catalog.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/get_time.h>

#include <fmt/format.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace KCDC::Features::Stash {
    namespace {
        constexpr const char *kLogger = "Stash";

        /** How far past its use distance a holder may step before the container closes on them. */
        constexpr float kAccessMargin = 2.0F;

        /** Wider than a dropped item: a container is scenery. */
        constexpr float kStreamRange = 200.0F;

        /** How far in front of a body its death drop is set down. */
        constexpr float kDropOffset = 1.2F;

        /**
         * A level container is one of thousands and is only ever used from
         * arm's length, so it streams no further than it takes to have its
         * contents in hand before the player reaches it. The densest 50 m of
         * Kuttenberg holds 144 of them; 100 m would hold 412.
         */
        constexpr float kLevelStreamRange = 50.0F;

        /** An open or a key turned follows its grant within a round trip and a Lua frame. */
        constexpr MafiaNet::Time kGrantTimeoutMs = 10000;

        /**
         * The shortest gap between two requests from one connection. A player
         * presses a prompt a few times a second at most; anything faster is
         * refused without reaching the scripts.
         */
        constexpr MafiaNet::Time kMinRequestIntervalMs = 100;

        /** A lockpick grant spans the whole minigame, which a patient player can stretch. */
        constexpr MafiaNet::Time kLockpickGrantTimeoutMs = 180000;
    } // namespace

    std::size_t StashService::CatalogKeyHash::operator()(const CatalogKey &key) const {
        const std::size_t world = std::hash<MafiaNet::VirtualWorldId> {}(key.virtualWorld);
        return std::hash<std::uint64_t> {}(key.guid) ^ (world + 0x9E3779B9U + (world << 6U) + (world >> 2U));
    }

    void StashService::Install(const std::string &level) {
        _level    = level;
        _contents = std::make_unique<StashContents>(Server::Get()->GetInventory().Ids());

        for (const Shared::Game::StashPlacement &placement : Shared::Game::g_StashPlacements) {
            if (level == placement.level) {
                _catalog.emplace(placement.guid, &placement);
            }
        }

        // Every container the level places is live in the global world from
        // boot, masters first so a linked child finds the one whose contents
        // it opens already built. Another virtual world builds each from the
        // same row the first time anything there asks for it.
        for (const bool children : {false, true}) {
            for (const auto &[guid, placement] : _catalog) {
                if ((placement->masterGuid != 0) == children) {
                    Materialize(*placement, MafiaNet::VIRTUAL_WORLD_GLOBAL);
                }
            }
        }
        Framework::Logging::GetLogger(kLogger)->info("Seeded {} catalogued stash(es) for '{}'", _index.size(), level);

        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        net->RegisterRPC<Shared::RPC::StashInventoryTransfer>([this](const Shared::RPC::StashInventoryTransfer &transfer, MafiaNet::Packet *packet) {
            OnTransfer(transfer, packet);
        });
        net->RegisterRPC<Shared::RPC::StashOpened>([this](const Shared::RPC::StashOpened &opened, MafiaNet::Packet *packet) {
            OnOpened(opened, packet);
        });
        net->RegisterRPC<Shared::RPC::StashClosed>([this](const Shared::RPC::StashClosed &closed, MafiaNet::Packet *packet) {
            OnClosed(closed, packet);
        });
        net->RegisterRPC<Shared::RPC::StashInteractRequest>([this](const Shared::RPC::StashInteractRequest &request, MafiaNet::Packet *packet) {
            OnInteractRequest(request, packet);
        });
        net->RegisterRPC<Shared::RPC::StashLockReport>([this](const Shared::RPC::StashLockReport &report, MafiaNet::Packet *packet) {
            OnLockReport(report, packet);
        });
    }

    Shared::Entities::StashEntity *StashService::Spawn(const glm::vec3 &position, const glm::quat &rotation, std::uint32_t virtualWorld, const std::string &model, bool announce) {
        if (!model.empty() && !IsSpawnableModel(model)) {
            return nullptr;
        }

        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return nullptr;
        }

        auto *entity = replication->CreateEntity<Shared::Entities::StashEntity>();
        if (entity == nullptr) {
            return nullptr;
        }

        entity->stashId         = _nextStashId++;
        entity->model           = model;
        entity->position        = position;
        entity->rotation        = glm::normalize(rotation);
        entity->streaming.range = kStreamRange;
        entity->SetVirtualWorld(virtualWorld);

        _contents->Create(entity->GetNetworkID());

        // Appended to the catalog under the GUID every client builds it with,
        // so it is found, measured and gated as a level container is. Its use
        // axis is its own forward, as a level container's is.
        const glm::vec3 forward = entity->rotation * glm::vec3(0.0F, 1.0F, 0.0F);
        const float flat        = std::sqrt(forward.x * forward.x + forward.y * forward.y);

        auto runtime                    = std::make_unique<RuntimePlacement>();
        runtime->name                   = fmt::format("spawned_stash_{}", entity->stashId);
        runtime->virtualWorld           = virtualWorld;
        runtime->placement.level        = _level.c_str();
        runtime->placement.guid         = entity->GetEntityGuid();
        runtime->placement.name         = runtime->name.c_str();
        runtime->placement.positionX    = position.x;
        runtime->placement.positionY    = position.y;
        runtime->placement.positionZ    = position.z;
        runtime->placement.forwardX     = flat > 1e-4F ? forward.x / flat : 0.0F;
        runtime->placement.forwardY     = flat > 1e-4F ? forward.y / flat : 1.0F;
        runtime->placement.useDistance  = Shared::Game::kDefaultStashUseDistance;
        runtime->placement.keyItemClass = "";
        runtime->placement.masterGuid   = 0;
        runtime->placement.startsLocked = false;
        runtime->placement.lockpickable = true;
        runtime->placement.dynamicKey   = false;

        CatalogKey key;
        key.virtualWorld = virtualWorld;
        key.guid         = runtime->placement.guid;
        CatalogRecord record;
        record.entity    = entity;
        record.placement = &runtime->placement;
        _index.insert_or_assign(key, record);
        _runtimeCatalog.insert_or_assign(runtime->placement.guid, std::move(runtime));

        Framework::Logging::GetLogger(kLogger)->info("Spawned stash {} (id {}) at {:.2f}, {:.2f}, {:.2f}", entity->GetNetworkID(), entity->stashId, position.x, position.y, position.z);
        if (announce) {
            KCDC::Scripting::Events::StashSpawned(entity->GetNetworkID());
        }
        return entity;
    }

    Shared::Entities::StashEntity *StashService::Materialize(const Shared::Game::StashPlacement &placement, MafiaNet::VirtualWorldId virtualWorld) {
        if (auto *existing = Find(placement.guid, virtualWorld)) {
            return existing;
        }

        Shared::Entities::StashEntity *master = nullptr;
        if (placement.masterGuid != 0) {
            master = Resolve(placement.masterGuid, virtualWorld);
            if (master == nullptr) {
                Framework::Logging::GetLogger(kLogger)->error("Stash {:016x} links master {:016x}, which the catalog does not place", placement.guid, placement.masterGuid);
                return nullptr;
            }
        }

        auto *replication = Framework::CoreModules::GetReplication();
        auto *entity      = replication != nullptr ? replication->CreateEntity<Shared::Entities::StashEntity>() : nullptr;
        if (entity == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("Could not create a stash entity for {:016x}; is '{}' registered?", placement.guid, Shared::Entities::StashEntity::kTypeName);
            return nullptr;
        }

        // Locked exactly when AnimStash:Reset would lock it, so a client
        // binding the chest finds it already agreeing.
        entity->levelGuid         = placement.guid;
        entity->contentsNetworkId = master != nullptr ? master->GetNetworkID() : 0;
        entity->hasContents       = master != nullptr && master->hasContents;
        entity->locked            = placement.startsLocked;
        entity->lockpickable      = placement.lockpickable;
        entity->position          = glm::vec3(placement.positionX, placement.positionY, placement.positionZ);
        entity->streaming.range   = kLevelStreamRange;
        entity->SetVirtualWorld(virtualWorld);

        if (master == nullptr) {
            _contents->Create(entity->GetNetworkID());
        }

        CatalogKey key;
        key.virtualWorld = virtualWorld;
        key.guid         = placement.guid;
        CatalogRecord record;
        record.entity    = entity;
        record.placement = &placement;
        _index.emplace(key, record);
        KCDC::Scripting::Events::StashSpawned(entity->GetNetworkID());
        return entity;
    }

    Shared::Entities::StashEntity *StashService::CreateVirtual(std::uint32_t virtualWorld) {
        auto *replication = Framework::CoreModules::GetReplication();
        auto *stash       = replication != nullptr ? replication->CreateEntity<Shared::Entities::StashEntity>() : nullptr;
        if (stash == nullptr) {
            return nullptr;
        }
        stash->stashId                 = _nextStashId++;
        stash->isVirtual               = true;
        stash->lockpickable            = false;
        stash->streaming.visible       = false;
        stash->streaming.alwaysVisible = true;
        stash->SetVirtualWorld(virtualWorld);
        _contents->Create(stash->GetNetworkID());
        const std::uint64_t networkId = stash->GetNetworkID();
        KCDC::Scripting::Events::StashSpawned(networkId);
        return replication->GetEntity<Shared::Entities::StashEntity>(networkId);
    }

    bool StashService::CanOpenVirtual(Shared::Entities::StashEntity &stash, std::uint64_t playerNetworkId) const {
        auto *player      = Features::Player::Players::Find(playerNetworkId);
        auto &inventories = Server::Get()->GetInventory();
        if (!stash.isVirtual || stash.locked || player == nullptr || !inventories.IsInitialized(playerNetworkId) || !inventories.IsReady(playerNetworkId) || !CanAccessContainer(*player, stash)) {
            return false;
        }
        const std::uint64_t holder = HolderOf(stash.GetNetworkID());
        return holder == 0 || holder == playerNetworkId;
    }

    bool StashService::OpenVirtual(Shared::Entities::StashEntity &stash, std::uint64_t playerNetworkId) {
        const std::uint64_t networkId = stash.GetNetworkID();
        auto *net                     = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr || !CanOpenVirtual(stash, playerNetworkId)) {
            return false;
        }
        if (HolderOf(networkId) != playerNetworkId) {
            if (!_virtualOpenRequests.insert(networkId).second) {
                return false;
            }
            const bool allowed = KCDC::Scripting::Events::StashInteract(playerNetworkId, networkId, "open");
            _virtualOpenRequests.erase(networkId);
            if (!allowed) {
                return false;
            }
        }

        // A synchronous handler can destroy, lock or move either handle.
        auto *live   = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::StashEntity>(networkId);
        auto *player = Features::Player::Players::Find(playerNetworkId);
        if (live == nullptr || player == nullptr || _contents->Find(networkId) == nullptr || !CanOpenVirtual(*live, playerNetworkId)) {
            return false;
        }
        const std::uint64_t now = static_cast<std::uint64_t>(MafiaNet::GetTime());
        if (HolderOf(networkId) == playerNetworkId) {
            // Already held: the client may have closed its screen with the close
            // still in flight, so the open is sent again rather than assumed.
            _contents->Renew(networkId, playerNetworkId, now);
        }
        else {
            for (const auto &[view, viewer] : _contents->Views()) {
                if (viewer == playerNetworkId && view.networkId != networkId) {
                    SendClosed(viewer, view.networkId);
                    _contents->Close(view, viewer, "closed");
                    Released(view);
                }
            }
            if (!_contents->Open(IdentityOf(*live), playerNetworkId, now)) {
                return false;
            }
            live->isOpen               = true;
            live->streaming.targetGUID = player->ownerGUID;
            live->streaming.visible    = true;
        }
        Shared::RPC::StashVirtualOpen open;
        open.body = StashContents::Snapshot(networkId, *_contents->Find(networkId), "").dump();
        net->SendRPC(open, MafiaNet::ToGuid(player->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        DispatchEvents();
        return true;
    }

    bool StashService::Despawn(Shared::Entities::StashEntity *stash) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr || stash == nullptr) {
            return false;
        }

        // The level owns its containers: a script can lock one, not remove it.
        const std::uint64_t networkId = stash->GetNetworkID();
        if (stash->levelGuid != 0 || _contents->Find(networkId) == nullptr) {
            return false;
        }

        // Raised while it can still be read: closed, emptied, then gone.
        NotifyHolderClosed(IdentityOf(*stash));
        _contents->Destroy(IdentityOf(*stash));
        DispatchEvents();
        if (auto *live = replication->GetEntity<Shared::Entities::StashEntity>(networkId)) {
            ForgetSpawned(*live);
            replication->DestroyEntity(live);
        }
        Framework::Logging::GetLogger(kLogger)->info("Despawned stash {}", networkId);
        return true;
    }

    std::vector<Shared::Entities::StashEntity *> StashService::Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld) const {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return {};
        }

        std::vector<Shared::Entities::StashEntity *> stashes;
        stashes.reserve(_index.size());
        replication->ForEach<Shared::Entities::StashEntity>([&](Shared::Entities::StashEntity *entity) {
            if (!virtualWorld.has_value() || entity->GetVirtualWorld() == *virtualWorld) {
                stashes.push_back(entity);
            }
        });
        return stashes;
    }

    std::size_t StashService::ContentCount(std::uint64_t stashNetworkId) const {
        auto *replication = Framework::CoreModules::GetReplication();
        auto *entity      = replication != nullptr ? replication->GetEntity<Shared::Entities::StashEntity>(stashNetworkId) : nullptr;
        const auto *stock = entity != nullptr ? _contents->Find(ContentsIdOf(*entity)) : nullptr;
        return stock != nullptr ? stock->Rows().size() : 0;
    }

    std::uint64_t StashService::HolderOf(std::uint64_t stashNetworkId) const {
        auto *replication = Framework::CoreModules::GetReplication();
        auto *entity      = replication != nullptr ? replication->GetEntity<Shared::Entities::StashEntity>(stashNetworkId) : nullptr;
        return entity != nullptr ? _contents->HolderOf(ContentsIdOf(*entity)) : 0;
    }

    KCDC::Features::Inventory::Inventory *StashService::ContentsOf(Shared::Entities::StashEntity &stash) {
        return _contents->Find(ContentsIdOf(stash));
    }

    Shared::Entities::StashEntity *StashService::DropInventory(std::uint64_t playerId, bool keepEquipped) {
        auto &inventories     = Server::Get()->GetInventory();
        auto *inventory       = inventories.Find(playerId);
        const auto *equipment = inventories.Equipped(playerId);
        const auto *body      = Features::Player::Players::Find(playerId);
        if (inventory == nullptr || equipment == nullptr || body == nullptr) {
            return nullptr;
        }
        const auto units = KCDC::Features::Inventory::InventoryGameplay::DropUnits(*inventory, *equipment, keepEquipped, KCDC::Shared::RPC::kMaxStashItems);
        if (units.empty()) {
            return nullptr;
        }
        // Beside the body, not under it: a player revived where they fell
        // would otherwise stand on their own drop.
        const glm::vec3 beside = body->position + glm::normalize(body->rotation) * glm::vec3(0.0F, kDropOffset, 0.0F);
        auto *stash            = Spawn(beside, body->rotation, body->GetVirtualWorld(), {}, false);
        if (stash == nullptr) {
            return nullptr;
        }
        if (!_contents->Deposit(IdentityOf(*stash), playerId, *inventory, units).ok) {
            Despawn(stash);
            return nullptr;
        }
        // Announced once it holds the drop: spawned, then the removal and the deposit.
        KCDC::Scripting::Events::StashSpawned(stash->GetNetworkID());
        DispatchEvents();
        return stash;
    }

    void StashService::ContentsChanged(Shared::Entities::StashEntity &stash) {
        const auto identity = IdentityOf(stash);
        _contents->Changed(identity, 0);
        if (const auto *view = _contents->ViewOf(identity.contentsId)) {
            SendSnapshot(_contents->HolderOf(identity.contentsId), view->networkId, "");
        }
        DispatchEvents();
    }

    std::size_t StashService::DespawnAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return 0;
        }

        // Collected first: destroying while iterating would mutate the set the
        // walk is standing in. Level containers are the level's and stay.
        // Event handlers can destroy other entries, so retain identities and
        // resolve each again rather than keeping pointers across callbacks.
        std::vector<std::uint64_t> doomed;
        replication->ForEach<Shared::Entities::StashEntity>([&doomed, virtualWorld](Shared::Entities::StashEntity *entity) {
            if (entity->levelGuid == 0 && (!virtualWorld.has_value() || entity->GetVirtualWorld() == *virtualWorld)) {
                doomed.push_back(entity->GetNetworkID());
            }
        });

        std::size_t removed = 0;
        for (const auto networkId : doomed) {
            removed += Despawn(replication->GetEntity<Shared::Entities::StashEntity>(networkId)) ? 1 : 0;
        }

        Framework::Logging::GetLogger(kLogger)->info("Despawned {} stash(es)", removed);
        return removed;
    }

    void StashService::ForgetSpawned(Shared::Entities::StashEntity &stash) {
        CatalogKey key;
        key.virtualWorld = stash.GetVirtualWorld();
        key.guid         = stash.GetEntityGuid();
        _index.erase(key);
        _runtimeCatalog.erase(key.guid);
    }

    Shared::Entities::StashEntity *StashService::Find(std::uint64_t guid, MafiaNet::VirtualWorldId virtualWorld) const {
        CatalogKey key;
        key.virtualWorld = virtualWorld;
        key.guid         = guid;
        const auto found = _index.find(key);
        return found != _index.end() ? found->second.entity : nullptr;
    }

    Shared::Entities::StashEntity *StashService::Resolve(std::uint64_t guid, MafiaNet::VirtualWorldId virtualWorld) {
        if (auto *stash = Find(guid, virtualWorld)) {
            return stash;
        }
        const auto catalogued = _catalog.find(guid);
        return catalogued != _catalog.end() ? Materialize(*catalogued->second, virtualWorld) : nullptr;
    }

    Shared::Entities::StashEntity *StashService::Nearest(const glm::vec3 &position, MafiaNet::VirtualWorldId virtualWorld, float maxDistance) {
        // The catalogs rather than the index: in a virtual world other than the
        // global one a level container is only built once something reaches it.
        const Shared::Game::StashPlacement *nearest = nullptr;
        float nearestDistance                       = maxDistance * maxDistance;
        const auto consider                         = [&](const Shared::Game::StashPlacement &placement) {
            const glm::vec3 offset = glm::vec3(placement.positionX, placement.positionY, placement.positionZ) - position;
            const float distance   = glm::dot(offset, offset);
            if (distance <= nearestDistance) {
                nearestDistance = distance;
                nearest         = &placement;
            }
        };
        for (const auto &[guid, placement] : _catalog) {
            consider(*placement);
        }
        for (const auto &[guid, runtime] : _runtimeCatalog) {
            if (runtime->virtualWorld == virtualWorld) {
                consider(runtime->placement);
            }
        }
        return nearest != nullptr ? Resolve(nearest->guid, virtualWorld) : nullptr;
    }

    const Shared::Game::StashPlacement *StashService::PlacementOf(const Shared::Entities::StashEntity &stash) const {
        CatalogKey key;
        key.virtualWorld = stash.GetVirtualWorld();
        key.guid         = stash.GetEntityGuid();
        const auto found = _index.find(key);
        return found != _index.end() && found->second.entity == &stash ? found->second.placement : nullptr;
    }

    std::uint64_t StashService::ContentsIdOf(Shared::Entities::StashEntity &stash) {
        return StashContents::ContentsIdOf(stash.GetNetworkID(), stash.contentsNetworkId);
    }

    StashIdentity StashService::IdentityOf(Shared::Entities::StashEntity &stash) {
        return {stash.GetNetworkID(), ContentsIdOf(stash), stash.GetEntityGuid(), stash.GetVirtualWorld()};
    }

    void StashService::SetLock(Shared::Entities::StashEntity &stash, bool locked) {
        stash.locked         = locked;
        stash.lockedByServer = locked;
        Framework::Logging::GetLogger(kLogger)->info("Stash {} set locked={}", stash.GetNetworkID(), locked);
    }

    void StashService::SetLockpickable(Shared::Entities::StashEntity &stash, bool lockpickable) {
        stash.lockpickable = lockpickable;
    }

    void StashService::SetLockDifficulty(Shared::Entities::StashEntity &stash, float difficulty) {
        stash.lockDifficulty = difficulty;
    }

    void StashService::SetLockFanciness(Shared::Entities::StashEntity &stash, Shared::Entities::StashEntity::LockFanciness fanciness) {
        stash.lockFanciness = fanciness;
    }

    bool StashService::IsSpawnableModel(const std::string &model) {
        // AnimStash.lua hands it to LoadObject, which picks a mesh or a
        // character by the extension; anything else loads nothing.
        constexpr std::size_t kMaxModelPath = 255;
        if (model.size() < 5 || model.size() > kMaxModelPath) {
            return false;
        }
        std::string extension = model.substr(model.size() - 4);
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return extension == ".cgf" || extension == ".cga" || extension == ".cdf";
    }

    void StashService::SetKeyItem(Shared::Entities::StashEntity &stash, const Shared::RPC::ItemClassId &keyItemClass) {
        stash.keyItemClass = keyItemClass.IsValid() ? keyItemClass : Shared::RPC::ItemClassId {};
    }

    void StashService::OnOpened(const Shared::RPC::StashOpened &opened, MafiaNet::Packet *packet) {
        if (packet == nullptr || !opened.IsSane()) {
            return;
        }

        auto *replication             = Framework::CoreModules::GetReplication();
        auto *entity                  = replication != nullptr ? replication->GetEntity<Shared::Entities::StashEntity>(opened.stashNetworkId) : nullptr;
        const MafiaNet::PeerGuid guid = MafiaNet::ToPeerGuid(packet->guid);
        auto *opener                  = replication != nullptr ? replication->GetViewerAs<Shared::Entities::HumanEntity>(guid) : nullptr;
        if (entity == nullptr || opener == nullptr) {
            return;
        }

        // The holder's game repeats its open while the screen is up: that
        // keeps the view, and is not another open.
        if (_contents->Renew(ContentsIdOf(*entity), opener->GetNetworkID(), static_cast<std::uint64_t>(MafiaNet::GetTime()))) {
            return;
        }

        // Every open is asked for first. One nobody granted is a client that
        // went around the prompt, and it gets no view and no contents. A key
        // or pick grant only becomes an open once its unlock was reported, so
        // a container still locked here is never opened on one.
        Grant *grant = FindGrant(guid, ContentsIdOf(*entity), true);
        if (grant == nullptr || grant->action != Shared::RPC::StashAction::Open) {
            Framework::Logging::GetLogger(kLogger)->warn("Player {} opened stash {} without a grant; ignoring it", opener->GetNetworkID(), opened.stashNetworkId);
            return;
        }
        grant->expiresAt = 0;

        // The lid that opens is the one the player used, a linked child's own
        // even when the report names the master whose stock it opens.
        auto *used = replication->GetEntity<Shared::Entities::StashEntity>(grant->stashNetworkId);
        if (used == nullptr) {
            return;
        }

        // The grant refused a held container, so a refusal here is two opens
        // crossing inside a round trip. The view stays with the holder.
        if (!_contents->Open(IdentityOf(*used), opener->GetNetworkID(), static_cast<std::uint64_t>(MafiaNet::GetTime()))) {
            Framework::Logging::GetLogger(kLogger)->warn("Player {} opened stash {} while {} holds it", opener->GetNetworkID(), opened.stashNetworkId, HolderOf(opened.stashNetworkId));
            return;
        }
        used->isOpen = true;
        SendSnapshot(opener->GetNetworkID(), used->GetNetworkID(), "");
        DispatchEvents();
    }

    void StashService::OnClosed(const Shared::RPC::StashClosed &closed, MafiaNet::Packet *packet) {
        auto *closer = packet != nullptr && closed.IsSane() ? Features::Player::Players::FromPacket(packet) : nullptr;
        auto *entity = closer != nullptr ? Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::StashEntity>(closed.stashNetworkId) : nullptr;
        if (entity == nullptr) {
            return;
        }
        const auto *view = _contents->ViewOf(ContentsIdOf(*entity));
        if (view == nullptr) {
            return;
        }
        const auto identity = *view;
        if (_contents->Close(identity, closer->GetNetworkID(), "closed")) {
            Released(identity);
            DispatchEvents();
        }
    }

    void StashService::OnTransfer(const Shared::RPC::StashInventoryTransfer &message, MafiaNet::Packet *packet) {
        auto *player = packet != nullptr && message.valid ? Features::Player::Players::FromPacket(packet) : nullptr;
        StashTransfer transfer;
        if (player == nullptr || !StashContents::ReadTransfer(message.body, transfer)) {
            return;
        }
        auto &inventories          = Server::Get()->GetInventory();
        const auto playerNetworkId = player->GetNetworkID();
        auto *inventory            = inventories.Find(playerNetworkId);
        auto *entity               = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::StashEntity>(transfer.stash);
        if (entity == nullptr || inventory == nullptr || !inventories.IsReady(player->GetNetworkID()) || !inventories.Admit(player->GetNetworkID())) {
            return;
        }
        // The chest the holder opened, not the one the request names: a linked
        // child and its master share one view.
        const auto *view = _contents->ViewOf(ContentsIdOf(*entity));
        if (view == nullptr || _contents->HolderOf(view->contentsId) != player->GetNetworkID()) {
            SendClosed(player->GetNetworkID(), transfer.stash);
            return;
        }
        const auto identity = *view;
        const auto *used    = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::StashEntity>(identity.networkId);
        if (used == nullptr || entity->GetVirtualWorld() != player->GetVirtualWorld() || !CanAccessContainer(*player, *used)) {
            SendClosed(player->GetNetworkID(), identity.networkId);
            _contents->Close(identity, player->GetNetworkID(), "lostAccess");
            Released(identity);
            DispatchEvents();
            return;
        }
        const auto result = _contents->Transfer(identity, player->GetNetworkID(), *inventory, transfer);
        if (result.ok) {
            _contents->Renew(identity.contentsId, player->GetNetworkID(), static_cast<std::uint64_t>(MafiaNet::GetTime()));
        }
        inventories.DispatchChanges();
        // Accepted or not, the holder's screen shows what the server holds.
        SendSnapshot(playerNetworkId, transfer.stash, result.ok ? "" : result.code);
        DispatchEvents();
    }

    StashService::Grant *StashService::FindGrant(MafiaNet::PeerGuid peer, std::uint64_t stashNetworkId, bool byContents) {
        const MafiaNet::Time now = MafiaNet::GetTime();
        for (Grant &grant : _grants) {
            const std::uint64_t matched = byContents ? grant.contentsNetworkId : grant.stashNetworkId;
            if (grant.peer == peer && matched == stashNetworkId && grant.expiresAt > now) {
                return &grant;
            }
        }
        return nullptr;
    }

    void StashService::OnInteractRequest(const Shared::RPC::StashInteractRequest &request, MafiaNet::Packet *packet) {
        if (packet == nullptr || !request.IsSane()) {
            return;
        }

        const auto logger             = Framework::Logging::GetLogger(kLogger);
        const MafiaNet::PeerGuid guid = MafiaNet::ToPeerGuid(packet->guid);

        const MafiaNet::Time now = MafiaNet::GetTime();
        const auto lastRequest   = _lastRequestAt.find(guid);
        if (lastRequest != _lastRequestAt.end() && now - lastRequest->second < kMinRequestIntervalMs) {
            SendVerdict(guid, request, false);
            return;
        }
        _lastRequestAt.insert_or_assign(guid, now);

        auto *replication = Framework::CoreModules::GetReplication();
        auto *actor       = replication != nullptr ? replication->GetViewerAs<Shared::Entities::HumanEntity>(guid) : nullptr;
        auto *stash       = replication != nullptr ? replication->GetEntity<Shared::Entities::StashEntity>(request.stashNetworkId) : nullptr;
        if (actor == nullptr || stash == nullptr || stash->isVirtual || !Server::Get()->GetInventory().IsInitialized(actor->GetNetworkID()) || !Server::Get()->GetInventory().IsReady(actor->GetNetworkID()) || !CanAccessContainer(*actor, *stash)) {
            SendVerdict(guid, request, false);
            return;
        }

        const std::uint64_t contentsNetworkId         = ContentsIdOf(*stash);
        const std::uint64_t holder                    = _contents->HolderOf(contentsNetworkId);
        const Shared::Game::StashPlacement *placement = PlacementOf(*stash);
        const glm::vec3 offset                        = actor->position - stash->position;

        Shared::Game::StashRequest evaluated;
        evaluated.action          = request.action;
        evaluated.locked          = stash->locked;
        evaluated.lockpickable    = stash->lockpickable;
        evaluated.heldByOther     = holder != 0 && holder != actor->GetNetworkID();
        evaluated.useDistance     = placement != nullptr ? placement->useDistance : Shared::Game::kDefaultStashUseDistance;
        evaluated.distanceSquared = glm::dot(offset, offset);

        const char *actionName = Shared::RPC::StashActionName(request.action);
        const char *refusal    = Shared::Game::EvaluateStashRequest(evaluated);
        if (refusal != nullptr) {
            logger->debug("Refused stash {} {} by actor {}: {}", request.stashNetworkId, actionName, actor->GetNetworkID(), refusal);
            SendVerdict(guid, request, false);
            return;
        }

        if (!KCDC::Scripting::Events::StashInteract(actor->GetNetworkID(), request.stashNetworkId, actionName)) {
            logger->debug("Stash {} {} by actor {} was refused by a script", request.stashNetworkId, actionName, actor->GetNetworkID());
            SendVerdict(guid, request, false);
            return;
        }

        // A handler may have removed the container while it decided.
        if (replication->GetEntity<Shared::Entities::StashEntity>(request.stashNetworkId) == nullptr) {
            SendVerdict(guid, request, false);
            return;
        }

        std::erase_if(_grants, [guid, &request](const Grant &grant) {
            return grant.peer == guid && grant.stashNetworkId == request.stashNetworkId;
        });
        Grant grant;
        grant.peer              = guid;
        grant.stashNetworkId    = request.stashNetworkId;
        grant.contentsNetworkId = contentsNetworkId;
        grant.action            = request.action;
        grant.expiresAt         = static_cast<std::uint64_t>(now + (request.action == Shared::RPC::StashAction::Lockpick ? kLockpickGrantTimeoutMs : kGrantTimeoutMs));
        _grants.push_back(grant);
        logger->debug("Granted stash {} {} to actor {}", request.stashNetworkId, actionName, actor->GetNetworkID());
        // Ahead of the verdict, so the screen the game opens on it is already
        // filled. A key or a pick shows nothing until the lock is beaten.
        if (StashContents::RevealsContents(request.action)) {
            SendSnapshot(actor->GetNetworkID(), request.stashNetworkId, "", SnapshotAccess::OpenGrant);
        }
        SendVerdict(guid, request, true);
    }

    void StashService::OnLockReport(const Shared::RPC::StashLockReport &report, MafiaNet::Packet *packet) {
        if (packet == nullptr || !report.IsSane()) {
            return;
        }

        const MafiaNet::PeerGuid guid           = MafiaNet::ToPeerGuid(packet->guid);
        auto *replication                       = Framework::CoreModules::GetReplication();
        auto *stash                             = replication != nullptr ? replication->GetEntity<Shared::Entities::StashEntity>(report.stashNetworkId) : nullptr;
        Grant *grant                            = FindGrant(guid, report.stashNetworkId, false);
        const Shared::RPC::StashAction expected = report.event == Shared::RPC::StashLockEvent::Lockpicked ? Shared::RPC::StashAction::Lockpick : Shared::RPC::StashAction::Unlock;
        if (stash == nullptr || grant == nullptr || grant->action != expected) {
            Framework::Logging::GetLogger(kLogger)->warn("Ignoring an unlock of stash {} that the server did not grant", report.stashNetworkId);
            return;
        }

        stash->locked         = false;
        stash->lockedByServer = false;

        // The container's own Lua opens it straight after either unlock, and
        // that open is part of what was granted.
        grant->action    = Shared::RPC::StashAction::Open;
        grant->expiresAt = static_cast<std::uint64_t>(MafiaNet::GetTime() + kGrantTimeoutMs);
        if (auto *player = replication->GetViewerAs<Shared::Entities::HumanEntity>(guid)) {
            SendSnapshot(player->GetNetworkID(), grant->stashNetworkId, "", SnapshotAccess::OpenGrant);
        }
        Framework::Logging::GetLogger(kLogger)->info("Stash {} unlocked by {}", report.stashNetworkId, report.event == Shared::RPC::StashLockEvent::Lockpicked ? "a lockpick" : "a key");
    }

    void StashService::SendVerdict(MafiaNet::PeerGuid guid, const Shared::RPC::StashInteractRequest &request, bool allowed) const {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        Shared::RPC::StashInteractVerdict verdict;
        verdict.stashNetworkId = request.stashNetworkId;
        verdict.requestId      = request.requestId;
        verdict.allowed        = allowed;
        net->SendRPC(verdict, MafiaNet::ToGuid(guid), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    bool StashService::CanAccessContainer(const Shared::Entities::HumanEntity &player, const Shared::Entities::StashEntity &stash) const {
        const auto *placement = PlacementOf(stash);
        const float reach     = (placement != nullptr ? placement->useDistance : Shared::Game::kDefaultStashUseDistance) + kAccessMargin;
        return StashContents::CanAccess(stash.GetVirtualWorld(), player.GetVirtualWorld(), player.soul.CanAct(), glm::distance(player.position, stash.position), reach, stash.isVirtual);
    }

    void StashService::SendSnapshot(std::uint64_t playerNetworkId, std::uint64_t stashNetworkId, const std::string &code, SnapshotAccess access) {
        auto *player      = Features::Player::Players::Find(playerNetworkId);
        auto *replication = Framework::CoreModules::GetReplication();
        auto *entity      = replication->GetEntity<Shared::Entities::StashEntity>(stashNetworkId);
        auto *stock       = entity != nullptr ? _contents->Find(ContentsIdOf(*entity)) : nullptr;
        if (player == nullptr || stock == nullptr || player->GetVirtualWorld() != entity->GetVirtualWorld() || !Server::Get()->GetInventory().IsInitialized(playerNetworkId)) {
            return;
        }
        const auto contentsId = ContentsIdOf(*entity);
        const auto holder     = _contents->HolderOf(contentsId);
        if (access == SnapshotAccess::HeldView) {
            const auto *view = _contents->ViewOf(contentsId);
            const auto *used = view != nullptr ? replication->GetEntity<Shared::Entities::StashEntity>(view->networkId) : nullptr;
            if (holder != playerNetworkId || used == nullptr || !CanAccessContainer(*player, *used)) {
                return;
            }
        }
        else {
            // Only the physical prompt/unlock path may show stock ahead of
            // OnOpened. A transfer refusal never falls back to a grant.
            const auto *grant = FindGrant(player->ownerGUID, stashNetworkId, false);
            if (entity->isVirtual || entity->locked || (holder != 0 && holder != playerNetworkId) || grant == nullptr || grant->action != Shared::RPC::StashAction::Open || grant->contentsNetworkId != contentsId || !CanAccessContainer(*player, *entity)) {
                return;
            }
        }
        Shared::RPC::StashInventorySnapshot snapshot;
        snapshot.body = StashContents::Snapshot(stashNetworkId, *stock, code).dump();
        Framework::CoreModules::GetNetworkPeer()->SendRPC(snapshot, MafiaNet::ToGuid(player->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void StashService::Released(const StashIdentity &stash) {
        if (auto *entity = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::StashEntity>(stash.networkId)) {
            entity->isOpen = false;
            if (entity->isVirtual) {
                entity->streaming.visible = false;
            }
        }
    }

    void StashService::NotifyHolderClosed(const StashIdentity &stash) {
        const auto *view = _contents->ViewOf(stash.contentsId);
        if (view != nullptr && (view->networkId == stash.networkId || stash.networkId == stash.contentsId)) {
            SendClosed(_contents->HolderOf(stash.contentsId), view->networkId);
        }
    }

    void StashService::SendClosed(std::uint64_t playerNetworkId, std::uint64_t stashNetworkId) const {
        const auto *player = Features::Player::Players::Find(playerNetworkId);
        auto *net          = Framework::CoreModules::GetNetworkPeer();
        if (player == nullptr || net == nullptr) {
            return;
        }
        Shared::RPC::StashClosed closed;
        closed.stashNetworkId = stashNetworkId;
        net->SendRPC(closed, MafiaNet::ToGuid(player->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void StashService::RefreshHasContents(std::uint64_t contentsId) {
        const bool hasContents = _contents->HasContents(contentsId);
        if (auto *stash = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::StashEntity>(contentsId); stash != nullptr && stash->isVirtual) {
            stash->hasContents = hasContents;
        }
        for (const auto &[key, record] : _index) {
            if (record.entity != nullptr && ContentsIdOf(*record.entity) == contentsId && record.entity->hasContents != hasContents) {
                record.entity->hasContents = hasContents;
            }
        }
    }

    void StashService::DispatchEvents() {
        Server::Get()->GetInventory().DispatchChanges();
        for (const auto &event : _contents->TakeEvents()) {
            if (event.name == "stashInventoryChanged") {
                RefreshHasContents(event.stash.contentsId);
            }
            KCDC::Scripting::Events::StashChanged(event);
        }
    }

    void StashService::Update() {
        const MafiaNet::Time now = MafiaNet::GetTime();
        bool closed              = false;
        // A holder whose game stopped renewing, or who walked off, died or
        // changed world, lets go; their screen is told to close.
        const auto views  = _contents->Views();
        auto *replication = Framework::CoreModules::GetReplication();
        for (const auto &[stash, playerId] : views) {
            auto *holder = Features::Player::Players::Find(playerId);
            auto *entity = replication->GetEntity<Shared::Entities::StashEntity>(stash.networkId);
            if (holder != nullptr && entity != nullptr && CanAccessContainer(*holder, *entity)) {
                continue;
            }
            SendClosed(playerId, stash.networkId);
            if (_contents->Close(stash, playerId, "lostAccess")) {
                Released(stash);
                closed = true;
            }
        }
        for (const auto &stash : _contents->Expire(static_cast<std::uint64_t>(now))) {
            Framework::Logging::GetLogger(kLogger)->warn("View of stash {} expired; releasing it", stash.networkId);
            for (const auto &[view, playerId] : views) {
                if (view.contentsId == stash.contentsId) {
                    SendClosed(playerId, stash.networkId);
                }
            }
            Released(stash);
            closed = true;
        }
        if (closed) {
            DispatchEvents();
        }

        std::erase_if(_grants, [now](const Grant &grant) {
            return grant.expiresAt <= now;
        });
    }

    void StashService::Reset() {
        // The stock outlives this: resources stop after PreShutdown, and a
        // script saving chests on the way out still reads it.
        _grants.clear();
        _virtualOpenRequests.clear();
        _lastRequestAt.clear();
    }

    void StashService::OnPlayerDisconnect(MafiaNet::PeerGuid guid) {
        if (auto *player = Framework::CoreModules::GetReplication()->GetViewerAs<Shared::Entities::HumanEntity>(guid)) {
            for (const auto &stash : _contents->Release(player->GetNetworkID())) {
                Released(stash);
            }
            DispatchEvents();
        }
        std::erase_if(_grants, [guid](const Grant &grant) {
            return grant.peer == guid;
        });
        _lastRequestAt.erase(guid);
    }
} // namespace KCDC::Features::Stash
