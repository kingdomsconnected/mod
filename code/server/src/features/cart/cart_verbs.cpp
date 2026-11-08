#include "cart_verbs.h"

#include "features/cart/cart_scripting.h"
#include "features/cart/cart_service.h"
#include "features/player/player_roster.h"

#include "shared/entity_filter.h"
#include "shared/features/player/player_entity.h"
#include "shared/pose_math.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace KCDC::Features::Cart {
    namespace {
        constexpr const char *kLogger = "Cart";

        /**
         * A cart standing at `position`, its lead and its body both facing
         * the way `rotation` does: how one is spawned or put somewhere.
         */
        Shared::Game::CartDrive Standing(const glm::vec3 &position, const glm::quat &rotation) {
            const glm::vec3 ahead = glm::normalize(rotation) * glm::vec3(0.0F, 1.0F, 0.0F);
            Shared::Game::CartDrive drive;
            drive.lead    = position;
            drive.leadYaw = Shared::Game::CartYawOf(glm::vec2(ahead));
            drive.bodyYaw = drive.leadYaw;
            drive.speed   = 0.0F;
            return drive;
        }

        /** A cart streams to clients as far out as a horse does: it is as big and it moves. */
        constexpr float kStreamRange = 250.0F;

    } // namespace

    Shared::Entities::CartEntity *Carts::Find(std::uint64_t cartNetworkId) {
        auto *replication = Framework::CoreModules::GetReplication();
        return replication != nullptr ? replication->GetEntity<Shared::Entities::CartEntity>(cartNetworkId) : nullptr;
    }

    std::vector<Shared::Entities::CartEntity *> Carts::Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        std::vector<Shared::Entities::CartEntity *> carts;
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Shared::Entities::CartEntity>([&](Shared::Entities::CartEntity *cart) {
                if (Shared::Entities::InWorld(cart, virtualWorld)) {
                    carts.push_back(cart);
                }
            });
        }
        return carts;
    }

    Shared::Entities::CartEntity *Carts::Spawn(const Shared::Game::CartBlueprint &blueprint, const glm::vec3 &position, const glm::quat &rotation, std::uint32_t virtualWorld) {
        if (!Shared::Game::IsFinite(position) || !Shared::Game::IsUsableRotation(rotation)) {
            return nullptr;
        }

        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return nullptr;
        }

        auto *cart = replication->CreateEntity<Shared::Entities::CartEntity>();
        if (cart == nullptr) {
            return nullptr;
        }

        // All of it before the entity replicates, so it rides the construction snapshot.
        cart->blueprint       = std::string(blueprint.name);
        cart->streaming.range = kStreamRange;
        cart->SetVirtualWorld(virtualWorld);
        cart->SetDrive(Standing(position, rotation));

        const std::uint64_t cartNetworkId = cart->GetNetworkID();
        Framework::Logging::GetLogger(kLogger)->info("Spawned cart {} ('{}') in world {} at {:.2f}, {:.2f}, {:.2f}", cartNetworkId, blueprint.name, virtualWorld, position.x, position.y, position.z);
        Scripting::Events::CartSpawned(cartNetworkId);
        // A handler may have destroyed it already.
        return Find(cartNetworkId);
    }

    bool Carts::Destroy(Shared::Entities::CartEntity *cart) {
        if (cart == nullptr) {
            return false;
        }
        const std::uint64_t cartNetworkId = cart->GetNetworkID();
        if (!CartService::Get().BeginDestroy(cartNetworkId)) {
            // A handler below asked for it again; the outer call finishes it.
            return false;
        }

        // A copy: every `cartExit` handler runs in the middle of this loop.
        const std::array<std::uint64_t, Shared::Entities::CartEntity::kSeatCount> occupants = cart->seats;
        for (std::uint64_t occupant : occupants) {
            if (occupant != 0) {
                Unseat(occupant);
            }
        }

        // While the entity is still here, so a handler can read it.
        Scripting::Events::CartDestroyed(cartNetworkId);

        auto *replication    = Framework::CoreModules::GetReplication();
        const bool destroyed = replication != nullptr && (cart = Find(cartNetworkId)) != nullptr;
        if (destroyed) {
            replication->DestroyEntity(cart);
        }
        CartService::Get().EndDestroy(cartNetworkId);
        return destroyed;
    }

    std::size_t Carts::DestroyAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        // By ID, collected first: destroying while iterating would mutate the
        // set the walk is standing in, and a handler raised by one destroy may
        // destroy another cart on the list.
        std::vector<std::uint64_t> doomed;
        for (auto *cart : Collect(virtualWorld)) {
            doomed.push_back(cart->GetNetworkID());
        }
        std::size_t destroyed = 0;
        for (std::uint64_t cartNetworkId : doomed) {
            if (Destroy(Find(cartNetworkId))) {
                ++destroyed;
            }
        }
        return destroyed;
    }

    Shared::Entities::CartEntity *Carts::Occupied(std::uint64_t playerNetworkId, std::size_t &seat) {
        seat = Shared::Entities::CartEntity::kSeatCount;
        if (playerNetworkId == 0) {
            return nullptr;
        }

        Shared::Entities::CartEntity *found = nullptr;
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Shared::Entities::CartEntity>([&](Shared::Entities::CartEntity *cart) {
                const std::size_t at = found == nullptr ? cart->SeatOf(playerNetworkId) : Shared::Entities::CartEntity::kSeatCount;
                if (at < Shared::Entities::CartEntity::kSeatCount) {
                    found = cart;
                    seat  = at;
                }
            });
        }
        return found;
    }

    bool Carts::Seat(Shared::Entities::CartEntity *cart, std::uint8_t seat, std::uint64_t playerNetworkId) {
        auto *replication = Framework::CoreModules::GetReplication();
        auto *player      = KCDC::Features::Player::Players::Find(playerNetworkId);
        if (cart == nullptr || replication == nullptr || player == nullptr || player->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID || !cart->IsSeat(seat)) {
            return false;
        }
        if (cart->seats[seat] == playerNetworkId) {
            return true;
        }
        const std::uint64_t cartNetworkId = cart->GetNetworkID();
        if (cart->seats[seat] != 0 || CartService::Get().IsBeingDestroyed(cartNetworkId)) {
            return false;
        }
        if (!Scripting::Events::CartEntering(cartNetworkId, playerNetworkId, seat)) {
            Framework::Logging::GetLogger(kLogger)->info("Player '{}' may not sit in cart {}: a cartEntering handler refused it", player->nickname, cartNetworkId);
            return false;
        }

        // Out of wherever they were first. Handlers ran above and run again
        // in there -- any of them may have destroyed this cart, disconnected
        // the player or taken the seat -- so all three are looked at again.
        Unseat(playerNetworkId);
        cart   = Find(cartNetworkId);
        player = KCDC::Features::Player::Players::Find(playerNetworkId);
        if (cart == nullptr || player == nullptr || cart->seats[seat] != 0 || CartService::Get().IsBeingDestroyed(cartNetworkId)) {
            return false;
        }

        cart->seats[seat] = playerNetworkId;
        if (seat == Shared::Entities::CartEntity::kSeatDriver) {
            // Pinned rather than handed over, or the next election -- which
            // never picks anybody for a cart -- would take it off its driver.
            cart->pace = static_cast<std::uint8_t>(Shared::Entities::CartPace::Stand);
            replication->Delegation().Pin(cart, player->ownerGUID);
        }

        // Unconditional: the seats are server fields, and the driver -- the
        // owner, whether or not this seat was the reins -- hears them only
        // from the forced state.
        cart->ForceState();

        Framework::Logging::GetLogger(kLogger)->info("Player '{}' sits in seat {} of cart {}", player->nickname, seat, cart->GetNetworkID());
        Scripting::Events::CartEntered(cart->GetNetworkID(), playerNetworkId, seat);
        return true;
    }

    void Carts::MarkStage(Shared::Entities::CartEntity *cart, std::uint8_t seat, Shared::RPC::CartSeatRequest::Kind stage) {
        std::uint8_t *mask = nullptr;
        switch (stage) {
        case Shared::RPC::CartSeatRequest::Kind::Boarding: mask = &cart->boarded; break;
        case Shared::RPC::CartSeatRequest::Kind::Seated: mask = &cart->seated; break;
        case Shared::RPC::CartSeatRequest::Kind::Leaving: mask = &cart->leaving; break;
        default: return;
        }
        const std::uint8_t bit = static_cast<std::uint8_t>(1U << seat);
        if ((*mask & bit) != 0) {
            return;
        }
        // A later stage implies the earlier ones: a report that arrives out of
        // order still leaves the copies climbing the right way.
        cart->boarded = static_cast<std::uint8_t>(cart->boarded | bit);
        if (stage != Shared::RPC::CartSeatRequest::Kind::Boarding) {
            cart->seated = static_cast<std::uint8_t>(cart->seated | bit);
        }
        *mask = static_cast<std::uint8_t>(*mask | bit);
        cart->ForceState();
    }

    bool Carts::Unseat(std::uint64_t playerNetworkId) {
        std::size_t seat = Shared::Entities::CartEntity::kSeatCount;
        auto *cart       = Occupied(playerNetworkId, seat);
        if (cart == nullptr) {
            return false;
        }

        cart->seats[seat]             = 0;
        const std::uint8_t keepOthers = static_cast<std::uint8_t>(~(1U << seat));
        cart->boarded                 = static_cast<std::uint8_t>(cart->boarded & keepOthers);
        cart->seated                  = static_cast<std::uint8_t>(cart->seated & keepOthers);
        cart->leaving                 = static_cast<std::uint8_t>(cart->leaving & keepOthers);
        if (seat == Shared::Entities::CartEntity::kSeatDriver) {
            cart->pace  = static_cast<std::uint8_t>(Shared::Entities::CartPace::Stand);
            cart->speed = 0.0F;
        }
        // Forced while the driver still owns the cart: the owner hears of its
        // state only this way, and a cart nobody else is near sends them no
        // update once it is the server's.
        cart->ForceState();
        if (seat == Shared::Entities::CartEntity::kSeatDriver) {
            // Nobody at the reins, nobody runs it. Pinned to the server rather
            // than unpinned: an unpin waits for the next election pass, and
            // until then the old driver still owns the cart and their pose
            // uploads undo the stop written here. The cart stands where the
            // driver's was last reported.
            if (auto *replication = Framework::CoreModules::GetReplication()) {
                replication->Delegation().Pin(cart, MafiaNet::UNASSIGNED_PEER_GUID);
            }
        }

        Framework::Logging::GetLogger(kLogger)->info("Player {} left seat {} of cart {}", playerNetworkId, seat, cart->GetNetworkID());
        Scripting::Events::CartExited(cart->GetNetworkID(), playerNetworkId, static_cast<std::uint8_t>(seat));
        return true;
    }

    bool Carts::Teleport(Shared::Entities::CartEntity *cart, const glm::vec3 &position, const glm::quat &rotation) {
        if (cart == nullptr || !Shared::Game::IsFinite(position) || !Shared::Game::IsUsableRotation(rotation)) {
            return false;
        }

        cart->SetDrive(Standing(position, rotation));
        cart->pace = static_cast<std::uint8_t>(Shared::Entities::CartPace::Stand);
        cart->ForceState();
        return true;
    }

    bool Carts::SetMaxSpeed(Shared::Entities::CartEntity *cart, float maxSpeed) {
        if (cart == nullptr || !std::isfinite(maxSpeed) || maxSpeed < 0.0F || maxSpeed > Shared::Game::kCartMaxSpeed) {
            return false;
        }
        cart->maxSpeed = maxSpeed;
        cart->ForceState();
        return true;
    }

} // namespace KCDC::Features::Cart
