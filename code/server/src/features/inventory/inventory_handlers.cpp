#include "inventory_service.h"

#include <core_modules.h>
#include <networking/network_peer.h>

namespace KCDC::Features::Inventory {
    void RegisterInventoryHandlers(InventoryService &service) {
        auto *peer = Framework::CoreModules::GetNetworkPeer();
        peer->RegisterRPC<KCDC::Shared::RPC::CustomItemUse>([&service](const KCDC::Shared::RPC::CustomItemUse &message, MafiaNet::Packet *packet) {
            if (packet != nullptr) {
                service.Receive(message, MafiaNet::ToPeerGuid(packet->guid));
            }
        });
        peer->RegisterRPC<KCDC::Shared::RPC::InventoryApplied>([&service](const KCDC::Shared::RPC::InventoryApplied &message, MafiaNet::Packet *packet) {
            if (packet != nullptr) {
                service.Receive(message, MafiaNet::ToPeerGuid(packet->guid));
            }
        });
        peer->RegisterRPC<KCDC::Shared::RPC::InventoryWear>([&service](const KCDC::Shared::RPC::InventoryWear &message, MafiaNet::Packet *packet) {
            if (packet != nullptr) {
                service.Receive(message, MafiaNet::ToPeerGuid(packet->guid));
            }
        });
        peer->RegisterRPC<KCDC::Shared::RPC::InventoryConsume>([&service](const KCDC::Shared::RPC::InventoryConsume &message, MafiaNet::Packet *packet) {
            if (packet != nullptr) {
                service.Receive(message, MafiaNet::ToPeerGuid(packet->guid));
            }
        });
        peer->RegisterRPC<KCDC::Shared::RPC::InventoryEquipment>([&service](const KCDC::Shared::RPC::InventoryEquipment &message, MafiaNet::Packet *packet) {
            if (packet != nullptr) {
                service.Receive(message, MafiaNet::ToPeerGuid(packet->guid));
            }
        });
        peer->RegisterRPC<KCDC::Shared::RPC::InventoryRecover>([&service](const KCDC::Shared::RPC::InventoryRecover &message, MafiaNet::Packet *packet) {
            if (packet != nullptr) {
                service.Receive(message, MafiaNet::ToPeerGuid(packet->guid));
            }
        });
    }
} // namespace KCDC::Features::Inventory
