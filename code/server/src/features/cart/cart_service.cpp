#include "cart_service.h"

#include "features/cart/cart_verbs.h"
#include "features/player/player_roster.h"

#include "shared/features/cart/cart_entity.h"
#include "shared/features/cart/cart_seat.h"

#include <core_modules.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/glm.hpp>

namespace KCDC::Features::Cart {
    namespace {
        // How far from a cart a player may be to ask for a seat in it. The
        // game's own prompt is raised from a couple of metres off the bench;
        // this is the length of a wagon on top.
        constexpr float kMaxBoardingDistance = 8.0F;

        /** How often one peer's seat requests are answered. The client asks at most once a second on its own. */
        constexpr MafiaNet::Time kSeatRequestIntervalMs = 250;
    } // namespace

    CartService &CartService::Get() {
        static CartService instance;
        return instance;
    }

    bool CartService::TakeTurn(std::unordered_map<MafiaNet::PeerGuid, MafiaNet::Time> &turns, MafiaNet::PeerGuid guid, MafiaNet::Time interval) {
        const MafiaNet::Time now = MafiaNet::GetTime();
        MafiaNet::Time &next     = turns[guid];
        if (now < next) {
            return false;
        }
        next = now + interval;
        return true;
    }

    void CartService::Forget(MafiaNet::PeerGuid guid) {
        _nextSeatRequest.erase(guid);
    }

    bool CartService::BeginDestroy(std::uint64_t cartNetworkId) {
        return _destroying.insert(cartNetworkId).second;
    }

    void CartService::EndDestroy(std::uint64_t cartNetworkId) {
        _destroying.erase(cartNetworkId);
    }

    bool CartService::IsBeingDestroyed(std::uint64_t cartNetworkId) const {
        return _destroying.contains(cartNetworkId);
    }

    void CartService::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        // A player at a mount point, one climbing, or one getting out. The
        // seat is the server's choice: the bench prompt is the reins, or the
        // bench beside them; the rail prompt a rail on the side the player
        // stands. A climb report names the seat it is about, and
        // counts only while the player still sits there -- one in flight when
        // a script moved them must not climb the copies of the seat they left.
        net->RegisterRPC<Shared::RPC::CartSeatRequest>([this](const Shared::RPC::CartSeatRequest &msg, MafiaNet::Packet *packet) {
            auto *player = KCDC::Features::Player::Players::FromPacket(packet);
            auto *cart   = Carts::Find(msg.cartNetworkId);
            if (!msg.IsSane() || player == nullptr || cart == nullptr) {
                return;
            }

            const auto kind = static_cast<Shared::RPC::CartSeatRequest::Kind>(msg.kind);
            switch (kind) {
            case Shared::RPC::CartSeatRequest::Kind::Leave:
                if (cart->SeatOf(player->GetNetworkID()) < Shared::Entities::CartEntity::kSeatCount) {
                    Carts::Unseat(player->GetNetworkID());
                }
                return;
            case Shared::RPC::CartSeatRequest::Kind::Boarding:
            case Shared::RPC::CartSeatRequest::Kind::Seated:
            case Shared::RPC::CartSeatRequest::Kind::Leaving:
                if (msg.seat < Shared::Entities::CartEntity::kSeatCount && cart->seats[msg.seat] == player->GetNetworkID()) {
                    Carts::MarkStage(cart, msg.seat, kind);
                }
                return;
            case Shared::RPC::CartSeatRequest::Kind::Front:
            case Shared::RPC::CartSeatRequest::Kind::Back: break;
            }

            if (!TakeTurn(_nextSeatRequest, player->ownerGUID, kSeatRequestIntervalMs) || !MafiaNet::VirtualWorldsCanSee(cart->GetVirtualWorld(), player->GetVirtualWorld()) || glm::distance(cart->position, player->position) > kMaxBoardingDistance) {
                return;
            }
            // The side they stand on, across the body's heading from the lead point.
            const glm::vec2 ahead = Shared::Game::CartHeading(cart->bodyYaw);
            const glm::vec2 right = glm::vec2(ahead.y, -ahead.x);
            const bool rightSide  = glm::dot(glm::vec2(player->position) - glm::vec2(cart->position), right) >= 0.0F;
            const auto seat       = Shared::Game::ChooseCartSeat(cart->Seats(), kind == Shared::RPC::CartSeatRequest::Kind::Front, rightSide, [cart](std::uint8_t candidate) {
                return cart->seats[candidate] != 0;
            });
            if (seat) {
                Carts::Seat(cart, *seat, player->GetNetworkID());
            }
        });
    }
} // namespace KCDC::Features::Cart
