#include "features/player/player_roster.h"
#include "shared/game/custom_item_registry.h"
#include "shared/game/mod_item_tables.h"

#include "core/server.h"

#include "shared/features/player/player_appearance.h"
#include "shared/features/player/player_carry.h"
#include "shared/features/player/player_heal.h"
#include "shared/features/player/player_movement_mode.h"
#include "shared/features/player/player_teleport.h"
#include "shared/features/player/player_visibility.h"
#include "shared/features/world_builder/world_builder_access.h"
#include "shared/game/body_part_catalog.h"

#include <core_modules.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>
#include <utils/string_utils.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <system_error>
#include <utility>

namespace KCDC::Features::Player {
    Shared::Entities::HumanEntity *Players::Find(std::uint64_t networkId) {
        auto *replication = Framework::CoreModules::GetReplication();
        return replication != nullptr && networkId != 0 ? replication->GetEntity<Shared::Entities::HumanEntity>(networkId) : nullptr;
    }

    Shared::Entities::HumanEntity *Players::FromPacket(MafiaNet::Packet *packet) {
        auto *replication = Framework::CoreModules::GetReplication();
        return replication != nullptr && packet != nullptr ? replication->GetViewerAs<Shared::Entities::HumanEntity>(MafiaNet::ToPeerGuid(packet->guid)) : nullptr;
    }

    Shared::Entities::HumanEntity *Players::CarrierOf(std::uint64_t carriedNetworkId) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr || carriedNetworkId == 0) {
            return nullptr;
        }

        Shared::Entities::HumanEntity *carrier = nullptr;
        replication->ForEach<Shared::Entities::HumanEntity>([&carrier, carriedNetworkId](Shared::Entities::HumanEntity *human) {
            if (human->carriedNetworkId == carriedNetworkId) {
                carrier = human;
            }
        });
        return carrier;
    }

    bool Players::PutDown(Shared::Entities::HumanEntity *carrier, bool immediate) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (carrier == nullptr || net == nullptr) {
            return false;
        }

        Shared::RPC::PutDownCarried order;
        order.immediate = immediate;
        net->SendRPC(order, MafiaNet::ToGuid(carrier->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        return true;
    }

    bool Players::CarryItem(Shared::Entities::HumanEntity *player, const Shared::RPC::ItemClassId &itemClass, std::uint64_t groundItemNetworkId) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (player == nullptr || net == nullptr || !itemClass.IsValid()) {
            return false;
        }

        Shared::RPC::CarryItem order;
        order.itemClass           = itemClass;
        order.groundItemNetworkId = groundItemNetworkId;
        net->SendRPC(order, MafiaNet::ToGuid(player->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        return true;
    }

    bool Players::PutDownItem(Shared::Entities::HumanEntity *carrier, bool immediate, bool refused) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (carrier == nullptr || net == nullptr) {
            return false;
        }

        Shared::RPC::PutDownCarriedItem order;
        order.immediate = immediate;
        order.refused   = refused;
        net->SendRPC(order, MafiaNet::ToGuid(carrier->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        return true;
    }

    Shared::Entities::HumanEntity *Players::FindByName(std::string_view query, bool &ambiguous) {
        ambiguous         = false;
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr || query.empty()) {
            return nullptr;
        }

        std::uint16_t slot = 0;
        const auto parsed  = std::from_chars(query.data(), query.data() + query.size(), slot);
        const bool isSlot  = parsed.ec == std::errc {} && parsed.ptr == query.data() + query.size();

        Shared::Entities::HumanEntity *exact  = nullptr;
        Shared::Entities::HumanEntity *prefix = nullptr;
        Shared::Entities::HumanEntity *bySlot = nullptr;
        std::size_t prefixCount               = 0;

        replication->ForEach<Shared::Entities::HumanEntity>([&](Shared::Entities::HumanEntity *human) {
            if (isSlot && human->playerIndex == slot) {
                bySlot = human;
            }
            if (Framework::Utils::StringUtils::EqualsIgnoreCase(human->nickname, query)) {
                exact = human;
            }
            else if (Framework::Utils::StringUtils::StartsWithIgnoreCase(human->nickname, query)) {
                prefix = human;
                ++prefixCount;
            }
        });

        if (exact != nullptr) {
            return exact;
        }
        if (prefixCount == 1) {
            return prefix;
        }
        ambiguous = prefixCount > 1;
        return ambiguous ? nullptr : bySlot;
    }

    const Shared::Game::ItemClassEntry *Players::ResolveItemClass(std::string_view query) {
        const auto custom = Shared::Game::CustomItemRegistry::Get().Resolve(query);
        if (!custom.empty()) {
            return Shared::Game::FindItemClass(custom);
        }
        if (const auto *named = Shared::Game::FindItemClassByName(query)) {
            return named;
        }
        for (const auto &item : Shared::Game::g_ItemClasses) {
            if (Framework::Utils::StringUtils::EqualsIgnoreCase(item.guid, query)) {
                return &item;
            }
        }
        std::string lowered(query);
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return Shared::Game::ModItemRegistry::Get().Find(lowered);
    }

    bool Players::Teleport(Shared::Entities::HumanEntity *player, const glm::vec3 &position, const std::string &label) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (player == nullptr || net == nullptr) {
            return false;
        }

        Shared::RPC::TeleportPlayer teleport;
        teleport.x = position.x;
        teleport.y = position.y;
        teleport.z = position.z;
        teleport.label.assign(label, 0, Shared::RPC::kMaxTeleportLabel);

        // The receiver drops anything failing its own sanity check, so a request that would be dropped is refused here rather than sent and silently lost.
        if (!teleport.IsSane()) {
            return false;
        }

        net->SendRPC(teleport, MafiaNet::ToGuid(player->ownerGUID));
        return true;
    }

    bool Players::SetWorldBuilderEnabled(Shared::Entities::HumanEntity *player, bool enabled) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (player == nullptr || net == nullptr) {
            return false;
        }

        Shared::RPC::SetWorldBuilderAccess access;
        access.enabled = enabled;
        net->SendRPC(access, MafiaNet::ToGuid(player->ownerGUID));
        return true;
    }

    bool Players::SetMovementMode(Shared::Entities::HumanEntity *player, bool walkByDefault, bool walkEnforced) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (player == nullptr || net == nullptr) {
            return false;
        }

        Shared::RPC::SetPlayerMovementMode mode;
        mode.walkByDefault = walkByDefault;
        mode.walkEnforced  = walkEnforced;

        net->SendRPC(mode, MafiaNet::ToGuid(player->ownerGUID));
        return true;
    }

    bool Players::SetAppearance(Shared::Entities::HumanEntity *player, const Shared::Game::CharacterAppearance &appearance) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (player == nullptr || net == nullptr) {
            return false;
        }

        Shared::RPC::SetPlayerAppearance request;
        request.appearance = appearance;

        // The receiver drops anything failing its own check, so refuse it here rather than send it to be lost.
        // It checks the beard against the face the body ends up wearing; only a named pair can be ruled out here.
        if (!request.IsSane() || !appearance.BeardFitsNamedHead()) {
            return false;
        }

        net->SendRPC(request, MafiaNet::ToGuid(player->ownerGUID));
        return true;
    }

    bool Players::SetDisguise(Shared::Entities::HumanEntity *player, const Shared::Game::PlayerDisguise &disguise) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (player == nullptr || net == nullptr) {
            return false;
        }

        Shared::RPC::SetPlayerDisguise request;
        request.disguise = disguise;

        // The receiver drops anything failing its own check, so refuse it here rather than send it to be lost.
        if (!request.IsSane()) {
            return false;
        }

        net->SendRPC(request, MafiaNet::ToGuid(player->ownerGUID));
        return true;
    }

    bool Players::SetVisible(Shared::Entities::HumanEntity *player, bool visible) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (player == nullptr || net == nullptr) {
            return false;
        }

        Shared::RPC::SetPlayerVisible request;
        request.visible = visible;
        net->SendRPC(request, MafiaNet::ToGuid(player->ownerGUID));
        return true;
    }

    bool Players::GiveItem(Shared::Entities::HumanEntity *player, const Shared::Game::ItemClassEntry &item, std::uint32_t amount) {
        auto *server = Server::Get();
        return player != nullptr && server != nullptr && server->GetInventory().Grant(player->GetNetworkID(), std::string(item.guid), amount).ok;
    }

    bool Players::TakeItem(Shared::Entities::HumanEntity *player, const Shared::Game::ItemClassEntry &item, std::uint32_t amount, ItemRequests::OutcomeHandler handler) {
        auto *server = Server::Get();
        if (player == nullptr || server == nullptr) {
            return false;
        }

        const auto result = server->GetInventory().Take(player->GetNetworkID(), std::string(item.guid), amount);
        ItemRequests::Outcome outcome;
        outcome.answered  = true;
        outcome.reason    = result.code;
        outcome.removed   = result.ok ? amount : 0;
        outcome.requested = amount;
        handler(outcome);
        return true;
    }

    const Shared::Game::BuffEntry *Players::ResolveBuff(std::string_view query) {
        return Shared::Game::ResolveBuff(query);
    }

    bool Players::AddBuff(Shared::Entities::HumanEntity *player, const Shared::Game::BuffEntry &buff) {
        auto *server = Server::Get();
        return server != nullptr && server->GetBuffs().Apply(player, buff);
    }

    bool Players::RemoveBuff(Shared::Entities::HumanEntity *player, const Shared::Game::BuffEntry &buff) {
        auto *server = Server::Get();
        return server != nullptr && server->GetBuffs().Retract(player, buff);
    }

    bool Players::ClearBuffsByTag(Shared::Entities::HumanEntity *player, std::string_view tagName) {
        auto *server      = Server::Get();
        const int aiTagId = Shared::Game::FindBuffAiTag(tagName);
        return server != nullptr && aiTagId >= 0 && server->GetBuffs().ClearByTag(player, aiTagId);
    }

    bool Players::HasBuff(Shared::Entities::HumanEntity *player, const Shared::Game::BuffEntry &buff) {
        auto *server = Server::Get();
        return player != nullptr && server != nullptr && server->GetBuffs().Has(player->GetNetworkID(), Shared::Game::BuffIdOf(buff));
    }

    const std::vector<Shared::RPC::ObservedBuff> *Players::Buffs(Shared::Entities::HumanEntity *player) {
        auto *server = Server::Get();
        return player != nullptr && server != nullptr ? server->GetBuffs().Report(player->GetNetworkID()) : nullptr;
    }

    std::uint32_t Players::InjuredLimbs(Shared::Entities::HumanEntity *player) {
        const std::vector<Shared::RPC::ObservedBuff> *reported = Buffs(player);
        if (reported == nullptr) {
            return 0;
        }
        std::uint32_t limbs = 0;
        for (const Shared::RPC::ObservedBuff &entry : *reported) {
            if (const Shared::Game::BodyPartEntry *limb = Shared::Game::FindHumanLimbByInjuryBuff(entry.buff.buff)) {
                limbs |= 1U << limb->id;
            }
        }
        return limbs;
    }

    bool Players::SetResource(Shared::Entities::HumanEntity *player, Shared::RPC::SetPlayerResource request) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (player == nullptr || net == nullptr || !player->transformReady) {
            return false;
        }
        request.networkId   = player->GetNetworkID();
        request.incarnation = player->soul.incarnation;
        request.epoch       = player->stateEpoch;
        if (!request.AppliesTo(player->GetNetworkID(), player->stateEpoch, player->soul)) {
            return false;
        }
        net->SendRPC(request, MafiaNet::ToGuid(player->ownerGUID));
        return true;
    }

    bool Players::Heal(Shared::Entities::HumanEntity *player, std::optional<float> health, bool clearInjuries, bool clearPoisons, bool clearBleeding) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        Shared::RPC::HealPlayer heal;
        heal.health = health.value_or(-1.0F);
        if (player == nullptr || net == nullptr || !heal.IsSane()) {
            return false;
        }

        // Each cure is a one-shot buff of the class it clears, with
        // exclusivity 3: landing it evicts every other instance of that class
        // from the soul (ResolveBuffExclusivity18063B300).
        if (clearInjuries) {
            AddBuff(player, *Shared::Game::FindBuffByName("remove_injuries"));
        }
        if (clearPoisons) {
            AddBuff(player, *Shared::Game::FindBuffByName("remove_all_posions"));
        }
        if (clearBleeding) {
            ClearBuffsByTag(player, "bleed");
        }
        if (health.has_value()) {
            net->SendRPC(heal, MafiaNet::ToGuid(player->ownerGUID));
        }
        return true;
    }
} // namespace KCDC::Features::Player
