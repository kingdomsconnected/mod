#include "local_events.h"
#include "features/items/custom_item_module.h"
#include "features/items/inventory_use_service.h"

#include "core/application.h"
#include "core/obfuscation.h"

#include <utils/safe_win32.h>

#include "features/cook_pot/cook_pot_service.h"
#include "features/ground_item/ground_item_service.h"
#include "features/npc/npc_inventory_module.h"
#include "features/npc/npc_service.h"
#include "features/player/player.h"
#include "features/player/player_inventory_service.h"
#include "features/player/player_scripting.h"
#include "features/player/player_service.h"
#include "common/module_support.h"

#include "shared/features/crafting/alchemy_message.h"
#include "shared/features/inventory/inventory_message.h"
#include "shared/features/marker/marker_rpc.h"
#include "shared/features/player/player_carry.h"
#include "shared/features/player/player_damage.h"
#include "shared/features/player/player_ready.h"
#include "shared/features/quest/quest_rpc.h"
#include "shared/game/body_part_catalog.h"

#include <core_modules.h>
#include <fmt/format.h>
#include <logging/logger.h>
#include <mafianet/get_time.h>
#include <networking/network_peer.h>

#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/wh/entitymodule/c_human.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/entitymodule/s_item_transfer_descriptor.h"

namespace KCDC::Core {
    // Both run on the game's worker threads: they read only what the game
    // thread published, never the replication manager.
    bool LocalEvents::AllowNativeItemDecay(const SDK::wh::entitymodule::C_Item *item) const {
        return !gApplication->GetPlayerInventoryService().ManagesNativeItem(item) && !gApplication->GetStashService().ManagesNativeItem(item) && !KCDC::Features::Npc::NpcInventoryModule::Get().ManagesItem(item);
    }

    bool LocalEvents::AllowNativeItemMerge(SDK::wh::entitymodule::C_Item *source, SDK::wh::entitymodule::C_Item *candidate) const {
        if (!KCDC::Features::Items::CustomItemModule::Get().AllowMerge(source, candidate)) {
            return false;
        }
        // The game merges stacks whose health is within 0.01 of each other.
        // A stack the server holds only merges with its exact equal, or two
        // rows' properties would blur into one native stack.
        auto &inventory = gApplication->GetPlayerInventoryService();
        auto &stashes   = gApplication->GetStashService();
        if (source == nullptr || candidate == nullptr
            || (!inventory.ManagesNativeItem(source) && !inventory.ManagesNativeItem(candidate) && !stashes.ManagesNativeItem(source) && !stashes.ManagesNativeItem(candidate) && !KCDC::Features::Npc::NpcInventoryModule::Get().ManagesItem(source)
                && !KCDC::Features::Npc::NpcInventoryModule::Get().ManagesItem(candidate))) {
            return true;
        }
        return KCDC::Features::Items::NativeInventoryProjection::SameProperties(*source, *candidate);
    }

    void LocalEvents::NativeRepairStarting(SDK::wh::entitymodule::C_Item *kit, SDK::wh::entitymodule::C_Item *equipment) {
        gApplication->GetPlayerInventoryService().ObserveNativeRepair(kit, equipment);
    }

    bool LocalEvents::InterceptFoodUse(SDK::wh::entitymodule::C_Item *item, const KCDC::Features::Items::NativeFoodUse &use) {
        if (KCDC::Features::CookPot::CookPotService::Get().IsReplayingFood(item)) {
            return false;
        }
        return gApplication->GetPlayerInventoryService().InterceptFoodUse(item, use);
    }

    void LocalEvents::FoodUseFinished(bool eaten) {
        gApplication->GetPlayerInventoryService().FoodUseFinished(eaten);
        KCDC::Features::CookPot::CookPotService::Get().FoodUseFinished(eaten);
    }

    bool LocalEvents::AllowsFoodEffects() const {
        return gApplication->GetPlayerInventoryService().AllowsFoodEffects() && KCDC::Features::CookPot::CookPotService::Get().AllowsFoodEffects();
    }

    void LocalEvents::NativeRepairFinished(SDK::wh::entitymodule::C_Item *kit, SDK::wh::entitymodule::C_Item *equipment) {
        gApplication->GetPlayerInventoryService().CaptureNativeRepairResult(kit, equipment);
    }

    void LocalEvents::NativeItemsPrepared(SDK::wh::entitymodule::E_PrepareFood::Type preparation, const std::vector<std::uint64_t> &stacks, std::int32_t count) {
        gApplication->GetPlayerInventoryService().ReportPreparation(preparation == SDK::wh::entitymodule::E_PrepareFood::Cook ? "cook" : preparation == SDK::wh::entitymodule::E_PrepareFood::Smoke ? "smoke" : "dry", stacks, count);
    }

    void LocalEvents::NativeInventoryChanged(const SDK::wh::entitymodule::C_ItemHolder *holder, const SDK::wh::entitymodule::C_Item *item) {
        auto &inventory = gApplication->GetPlayerInventoryService();
        if (inventory.ManagesHolder(holder) || inventory.ManagesNativeItem(item)) {
            inventory.NotifyNativeChange();
        }
        gApplication->GetStashService().NotifyNativeChange(holder != nullptr ? holder : (item != nullptr ? item->GetOwner() : nullptr));
        KCDC::Features::Npc::NpcInventoryModule::Get().NotifyNativeChange(holder != nullptr ? holder : (item != nullptr ? item->GetOwner() : nullptr));
    }

    void LocalEvents::NativeEquipmentChanged(const SDK::wh::entitymodule::C_Item *item) {
        KCDC::Features::Npc::NpcInventoryModule::Get().NotifyNativeChange(item != nullptr ? item->GetOwner() : nullptr);
        gApplication->GetPlayerService().NotifyEquipmentChanged();
        if (gApplication->GetPlayerInventoryService().ManagesNativeItem(item)) {
            gApplication->GetPlayerInventoryService().NotifyEquipmentChanged();
        }
    }

    bool LocalEvents::RequestInventoryDrop(std::uint64_t itemWuid, std::uint32_t amount) {
        return gApplication->GetGroundItemService().RequestDrop(itemWuid, amount, FindHandHolding(itemWuid) != kNoHand);
    }

    void LocalEvents::InventoryDropSelectionStarting() {
        gApplication->GetGroundItemService().BeginDropSelection();
    }

    void LocalEvents::InventoryDropSelectionFinished() {
        gApplication->GetGroundItemService().FinishDropSelection();
    }

    bool LocalEvents::RequestContainerTransfer(SDK::wh::entitymodule::C_Inventory *destination, const SDK::StdVector<SDK::wh::entitymodule::C_Item *> &items, std::uint32_t amount) {
        return KCDC::Features::Npc::NpcInventoryModule::Get().RequestTransfer(destination, items, amount) || gApplication->GetStashService().OnTransferRequested(destination, items, amount);
    }

    bool LocalEvents::NpcItemExchange(SDK::wh::entitymodule::C_Actor &actor, SDK::EntityId user) {
        return KCDC::Features::Npc::NpcInventoryModule::Get().RequestExchange(actor, user);
    }
    void LocalEvents::NpcActionRegistering(const SDK::wh::entitymodule::C_PlayerInteractor &interactor, SDK::wh::entitymodule::S_InteractorActionRecord &record, SDK::HScriptFunction function) {
        KCDC::Features::Npc::NpcInventoryModule::Get().RegisterAction(interactor, record, function);
    }
    bool LocalEvents::NpcActionInvoked(const SDK::wh::entitymodule::S_LuaActionInvocation &invocation) {
        return KCDC::Features::Npc::NpcInventoryModule::Get().InterceptAction(invocation);
    }
    bool LocalEvents::NpcButcheringStarting(SDK::wh::entitymodule::C_ActorActionButchering &action) {
        return KCDC::Features::Npc::NpcInventoryModule::Get().StartButchering(action);
    }
    void LocalEvents::NpcButcheringEntered(const SDK::wh::entitymodule::C_ActorActionButchering &action, bool accepted) {
        KCDC::Features::Npc::NpcInventoryModule::Get().EnteredButchering(action, accepted);
    }
    bool LocalEvents::NpcButcheringCompleted(SDK::wh::entitymodule::C_ActorActionButchering &action) {
        return KCDC::Features::Npc::NpcInventoryModule::Get().CompletedButchering(action);
    }
    void LocalEvents::NpcButcheringExited(const SDK::wh::entitymodule::C_ActorActionButchering &action, bool cancelled) {
        KCDC::Features::Npc::NpcInventoryModule::Get().ExitedButchering(action, cancelled);
    }
    bool LocalEvents::NpcCanBeButchered(const SDK::wh::rpgmodule::I_Soul *soul, bool &answer) const {
        return KCDC::Features::Npc::NpcInventoryModule::Get().AnswerCanBeButchered(soul, answer);
    }

    bool LocalEvents::CanStartGathering(SDK::wh::playermodule::C_HerbGathering *minigame, SDK::EntityId proxy) {
        return gApplication->GetGatheringService().CanStart(minigame, proxy);
    }

    void LocalEvents::GatheringStarted(SDK::wh::playermodule::C_HerbGathering *minigame, const SDK::wh::environmentmodule::RespawnInteractionStateLayout &source) {
        gApplication->GetGatheringService().Started(minigame, source);
    }

    void LocalEvents::GatheringCompleted(SDK::wh::environmentmodule::C_RespawnManager *manager) {
        gApplication->GetGatheringService().Completed(manager);
    }

    bool LocalEvents::BeginAlchemyScriptCall(std::uint64_t actor, std::uint64_t station) {
        return gApplication->GetAlchemyService().BeginScriptCall(actor, station);
    }

    bool LocalEvents::EndAlchemyScriptCall(int originalValueCount) {
        return gApplication->GetAlchemyService().EndScriptCall(originalValueCount);
    }

    bool LocalEvents::AlchemyStarting(SDK::wh::playermodule::C_Alchemy *minigame, SDK::EntityId station) {
        return gApplication->GetAlchemyService().Starting(minigame, station);
    }

    bool LocalEvents::AlchemyEntryReady(SDK::wh::playermodule::C_Alchemy *minigame) {
        return gApplication->GetAlchemyService().EntryReady(minigame);
    }

    void LocalEvents::AlchemyStarted(SDK::wh::playermodule::C_Alchemy *minigame, SDK::EntityId station, bool accepted) {
        gApplication->GetAlchemyService().Started(minigame, station, accepted);
    }

    void LocalEvents::AlchemyDestroying(SDK::wh::playermodule::C_Alchemy *minigame) {
        gApplication->GetAlchemyService().Destroying(minigame);
    }

    void LocalEvents::AlchemyDestroyed(SDK::wh::playermodule::C_Alchemy *minigame) {
        gApplication->GetAlchemyService().Destroyed(minigame);
    }

    bool LocalEvents::AlchemySelecting(SDK::wh::playermodule::C_Alchemy *minigame, SDK::wh::playermodule::AlchemyItemType target) {
        return gApplication->GetAlchemyService().Selecting(minigame, target);
    }

    bool LocalEvents::AlchemyReturning(SDK::wh::playermodule::C_Alchemy *minigame) {
        return gApplication->GetAlchemyService().Returning(minigame);
    }

    void LocalEvents::AlchemySelected(SDK::wh::playermodule::C_Alchemy *minigame, SDK::wh::playermodule::AlchemyItemType target) {
        gApplication->GetAlchemyService().Selected(minigame, target);
    }

    void LocalEvents::AlchemyExitRequested(SDK::wh::playermodule::C_Alchemy *minigame) {
        gApplication->GetAlchemyService().RequestingExit(minigame);
    }

    void LocalEvents::AlchemyShuttingDown(SDK::wh::playermodule::C_Alchemy *minigame) {
        gApplication->GetAlchemyService().ShuttingDown(minigame);
    }

    void LocalEvents::AlchemyShutDown(SDK::wh::playermodule::C_Alchemy *minigame) {
        gApplication->GetAlchemyService().ShutDown(minigame);
    }

    bool LocalEvents::AlchemyNativeScopeStarting(SDK::wh::playermodule::C_Alchemy *minigame) {
        return gApplication->GetAlchemyService().EnterNativeScope(minigame);
    }

    bool LocalEvents::AlchemyNativeCleanupScopeStarting(SDK::wh::playermodule::C_Alchemy *minigame) {
        return gApplication->GetAlchemyService().EnterCleanupScope(minigame);
    }

    void LocalEvents::AlchemyNativeCleanupScopeEnded() {
        gApplication->GetAlchemyService().LeaveCleanupScope();
    }

    bool LocalEvents::AlchemyActionScopeStarting(SDK::wh::playermodule::C_Alchemy *minigame, SDK::wh::framework::I_Action *action) {
        return gApplication->GetAlchemyService().EnterActionScope(minigame, action);
    }

    void LocalEvents::AlchemyNativeScopeEnded() {
        gApplication->GetAlchemyService().LeaveNativeScope();
    }

    void LocalEvents::AlchemyResourceConstructing(SDK::wh::playermodule::C_AlchemyResource *resource, SDK::wh::entitymodule::C_Item *item) {
        gApplication->GetAlchemyService().ResourceConstructing(resource, item);
    }

    void LocalEvents::AlchemyResourceConstructed(SDK::wh::playermodule::C_AlchemyResource *resource, SDK::wh::entitymodule::C_Item *item) {
        gApplication->GetAlchemyService().ResourceConstructed(resource, item);
    }

    void LocalEvents::AlchemyBaseResourceConstructed(SDK::wh::playermodule::C_AlchemyResource *resource, SDK::wh::playermodule::AlchemyItemType selection) {
        gApplication->GetAlchemyService().BaseResourceConstructed(resource, selection);
    }

    bool LocalEvents::AlchemySuppressNativeOutcome(SDK::wh::playermodule::C_Alchemy *minigame, SDK::wh::playermodule::AlchemyItemType container) {
        return gApplication->GetAlchemyService().SuppressNativeOutcome(minigame, container);
    }

    bool LocalEvents::AlchemyBellowsPumping(SDK::wh::playermodule::C_UseBellowsAction *action) {
        return gApplication->GetAlchemyService().BellowsPumping(action);
    }

    bool LocalEvents::AlchemyBellowsReturning(SDK::wh::playermodule::C_UseBellowsAction *action) {
        return gApplication->GetAlchemyService().BellowsReturning(action);
    }

    bool LocalEvents::AlchemyRetortPumping(SDK::wh::playermodule::C_UseRetortAction *action) {
        return gApplication->GetAlchemyService().RetortPumping(action);
    }

    bool LocalEvents::AlchemyRetortFinishing(SDK::wh::playermodule::C_UseRetortAction *action) {
        return gApplication->GetAlchemyService().RetortFinishing(action);
    }

    bool LocalEvents::AlchemyRetortRequiredPulses(SDK::wh::playermodule::C_UseRetortAction *action, std::uint32_t &value) {
        return gApplication->GetAlchemyService().RetortRequiredPulses(action, value);
    }

    void LocalEvents::AlchemyMortarStarted(SDK::wh::playermodule::C_UseMortarAction *action, bool accepted) {
        gApplication->GetAlchemyService().MortarStarted(action, accepted);
    }

    void LocalEvents::AlchemyResetNative(SDK::wh::playermodule::C_Alchemy *minigame) {
        gApplication->GetAlchemyService().ResetNative(minigame);
    }

    bool LocalEvents::SmithingRecipeAccepting(SDK::wh::playermodule::C_Blacksmithing *minigame) {
        return gApplication->GetSmithingService().AcceptingRecipe(minigame);
    }

    void LocalEvents::SmithingWorkpieceFinishing(SDK::wh::playermodule::C_Blacksmithing *minigame, bool success) {
        gApplication->GetSmithingService().FinishingWorkpiece(minigame, success);
    }

    void LocalEvents::SmithingCancelling(SDK::wh::playermodule::C_Blacksmithing *minigame) {
        gApplication->GetSmithingService().Cancelling(minigame);
    }

    void LocalEvents::MinigameActionReplaced(SDK::wh::playermodule::C_Minigame *minigame) {
        gApplication->GetSmithingService().ActionReplaced(minigame);
    }

    void LocalEvents::SmithingWorkpieceFinished(SDK::wh::playermodule::C_Blacksmithing *minigame) {
        gApplication->GetSmithingService().FinishedWorkpiece(minigame);
    }

    bool LocalEvents::SmithingSuppressNativeSettlement(SDK::wh::playermodule::C_Blacksmithing *minigame) {
        return gApplication->GetSmithingService().SuppressNativeSettlement(minigame);
    }

    bool LocalEvents::SmithingSuppressNativeOutcome() {
        return gApplication->GetSmithingService().SuppressNativeOutcome();
    }

    bool LocalEvents::SmithingToolAttaching(SDK::wh::playermodule::C_BlacksmithingSlotToolAttachment *tool, SDK::wh::entitymodule::C_Actor *actor) {
        return gApplication->GetSmithingService().LendingTool(tool, actor);
    }

    bool LocalEvents::SmithingToolDetaching(SDK::wh::playermodule::C_BlacksmithingSlotToolAttachment *tool) {
        return gApplication->GetSmithingService().ReturningTool(tool);
    }

    void LocalEvents::SmithingToolDetached(SDK::wh::playermodule::C_BlacksmithingSlotToolAttachment *tool) {
        gApplication->GetSmithingService().ReturnedTool(tool);
    }

    void LocalEvents::SmithingBegin(const KCDC::Shared::RPC::SmithingBegin &request) {
        auto message = request;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(message, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void LocalEvents::SmithingFinish(const KCDC::Shared::RPC::SmithingFinish &report) {
        auto message = report;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(message, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void LocalEvents::AlchemyIntent(const std::string &body) {
        if (body.empty() || body.size() > KCDC::Shared::RPC::AlchemyRequest::kMaximumBytes) {
            return;
        }
        KCDC::Shared::RPC::AlchemyRequest request;
        request.body = body;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(request, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    bool LocalEvents::AlchemyStartedQuery(SDK::wh::rpgmodule::I_Soul *soul, float &value, bool &executeNative) {
        return gApplication->GetAlchemyService().ProjectNativeStart(soul, value, executeNative);
    }

    bool LocalEvents::AlchemyFinishedEvent(SDK::wh::rpgmodule::I_Soul *soul) {
        return gApplication->GetAlchemyService().SuppressNativeFinish(soul);
    }

    bool LocalEvents::AlchemyRecipeKnowledgeQuery(SDK::wh::rpgmodule::I_Soul *soul, std::uint32_t recipe, std::uint32_t first, std::uint32_t last, bool &value) {
        auto &alchemy     = gApplication->GetAlchemyService();
        const auto *local = KCDC::Common::LocalHumanActor();
        if (!gApplication->GetPlayerInventoryService().HasAuthority() || local == nullptr || local->GetSoul() != soul || alchemy.CurrentNative() == nullptr) {
            return false;
        }
        // The book reads this outside the native action's Update stack.
        const auto known = alchemy.ProjectedCharacter().KnowledgeRange(recipe, first, last);
        value            = known.value_or(false);
        if (!known.has_value()) {
            alchemy.Fault(std::string(KCDC_HIDE("Unsupported alchemy recipe knowledge query")));
        }
        return true;
    }

    void LocalEvents::MarkerCrossed(std::uint64_t markerNetworkId, bool entered) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr || markerNetworkId == 0) {
            return;
        }

        KCDC::Shared::RPC::MarkerCrossed crossing;
        crossing.markerNetworkId = markerNetworkId;
        crossing.entered         = entered;

        // Ordered as well as reliable: an enter and the exit that follows it
        // are only meaningful in that order, and swapping them would leave the
        // server believing a player is standing in a marker they have left.
        net->BroadcastRPC(crossing, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void LocalEvents::QuestTrackingChanged(const std::string &questKey, bool tracked) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr || questKey.empty()) {
            return;
        }

        KCDC::Shared::RPC::QuestTrackChanged change;
        change.questKey = questKey;
        change.tracked  = tracked;

        // Ordered as well as reliable, like a marker crossing: a start and the
        // stop that follows it only mean anything in that order.
        net->BroadcastRPC(change, MafiaNet::Priority::Medium, MafiaNet::Reliability::ReliableOrdered);
    }

    void LocalEvents::LocalBodyDamaged(std::uint64_t attackerSoulWuid, std::int32_t hitSubpart, std::uint32_t deathReason, float amount, bool fatal) {
        // A remote player or a server NPC is named; a body hurting itself names nobody.
        auto *attacker           = KCDC::Features::Player::Human::FindRemoteSoulWuid(attackerSoulWuid);
        std::uint64_t attackerId = attacker != nullptr ? attacker->GetNetworkID() : 0;
        if (attackerId == 0) {
            if (const auto npc = KCDC::Features::Npc::NpcService::FindCombatBodyOfSoulWuid(attackerSoulWuid)) {
                attackerId = npc->networkId;
            }
        }
        const auto reason = static_cast<std::uint8_t>(KCDC::Shared::Game::ToDamageReason(deathReason));

        if (fatal) {
            _fatalHit.killerNetworkId = attackerId;
            _fatalHit.reason          = reason;
        }

        // A blow the combat referee ruled on: the server raised its damage as
        // it ruled, and only the kill it may cause is this client's to tell.
        if (_applyingRuledDamage) {
            return;
        }

        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        KCDC::Shared::RPC::PlayerDamaged report;
        report.attackerNetworkId = attackerId;
        report.amount            = amount;
        report.bodyPart          = KCDC::Shared::Game::BodyPartOfSubpart(hitSubpart);
        report.reason            = reason;
        if (!report.IsSane()) {
            return;
        }

        // The death report rides the same ordered channel, so a killing blow
        // arrives ahead of the death it caused.
        net->BroadcastRPC(report, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void LocalEvents::HeldItemChanged(std::uint32_t hand, std::uint64_t itemWuid) {
        // The one place a hand index arrives from outside, so it is checked
        // here and nowhere downstream.
        if (hand != kRightHand && hand != kLeftHand) {
            return;
        }

        // A carried item is in a hand but is no held item: it belongs to the
        // carry, which everybody else replays from `carriedItemClass`, and it
        // is in no inventory for PublishHand to name.
        if (itemWuid != 0 && gApplication->GetPlayerService().IsCarriedItem(itemWuid)) {
            itemWuid = 0;
        }

        // A drop owns the clear until its ground replica has taken over, and
        // only for the hand that drop is retiring.
        if (itemWuid == 0 && _pendingHandDropWuid != 0 && _pendingHandDropHand == hand) {
            return;
        }
        if (_handStateKnown[hand] && itemWuid == _desiredHandWuid[hand]) {
            return;
        }

        _desiredHandWuid[hand] = itemWuid;
        _handStateKnown[hand]  = true;
        _handDirty[hand]       = true;
    }

    void LocalEvents::InventoryDropStarting(std::uint64_t itemWuid) {
        _droppingHeldWuid = FindHandHolding(itemWuid) != kHandCount ? itemWuid : 0;
    }

    void LocalEvents::InventoryItemDropped(std::uint64_t sourceWuid, SDK::wh::entitymodule::C_Item *droppedItem) {
        if (droppedItem == nullptr) {
            return;
        }

        // Only a whole held stack retires the hand: a partial drop leaves the
        // rest of it where it was.
        const std::uint64_t droppedWuid = droppedItem->GetId();
        const bool retiresHeldItem      = _droppingHeldWuid != 0 && _droppingHeldWuid == sourceWuid && sourceWuid == droppedWuid;
        _droppingHeldWuid               = 0;

        const std::size_t retiredHand = retiresHeldItem ? FindHandHolding(sourceWuid) : kNoHand;
        if (retiredHand != kNoHand) {
            // Held until the ground replica has taken the item over, so the
            // holster edge does not race it.
            _pendingHandDropWuid          = droppedWuid;
            _pendingHandDropHand          = retiredHand;
            _desiredHandWuid[retiredHand] = sourceWuid;
            _handStateKnown[retiredHand]  = true;
            _handDirty[retiredHand]       = true;
        }

        gApplication->GetGroundItemService().OnLocalDrop(droppedItem, retiresHeldItem);
    }

    bool LocalEvents::PickupStarting(SDK::wh::entitymodule::S_ItemTransferDescriptor *descriptor, bool animated) {
        return gApplication->GetGroundItemService().OnPickupStarting(descriptor, animated);
    }

    bool LocalEvents::IsPickupTrackedFor(const SDK::wh::entitymodule::S_ItemTransferDescriptor *descriptor) const {
        return gApplication->GetGroundItemService().IsPickupTrackedFor(descriptor);
    }

    void LocalEvents::PickupSettled(bool succeeded) {
        gApplication->GetGroundItemService().OnPickupSettled(succeeded);
    }

    void LocalEvents::PickupCancelled() {
        gApplication->GetGroundItemService().OnPickupCancelled();
    }

    void LocalEvents::GroundDropBecamePhysical(std::uint64_t droppedWuid) {
        if (droppedWuid == 0 || droppedWuid != _pendingHandDropWuid) {
            return;
        }

        const std::size_t hand = _pendingHandDropHand;
        _pendingHandDropWuid   = 0;
        _pendingHandDropHand   = kNoHand;
        if (hand == kNoHand || _desiredHandWuid[hand] != droppedWuid) {
            return;
        }

        _desiredHandWuid[hand] = 0;
        _handStateKnown[hand]  = true;
        _handDirty[hand]       = true;
    }

    bool LocalEvents::InventoryItemUsed(SDK::wh::entitymodule::C_Item *item, const char *action) {
        return KCDC::Features::Items::InventoryUseService::Get().Capture(item, action);
    }

    void LocalEvents::CustomItemUsed(SDK::wh::entitymodule::C_Item *item) {
        auto &inventory = gApplication->GetPlayerInventoryService();
        if (!KCDC::Features::Items::CustomItemModule::Get().Usable(item) || !inventory.CanSpend() || !inventory.ManagesNativeItem(item) || _customItemUses.size() >= 16) {
            return;
        }
        // Distinct nonstackable letters can have equal properties. Prefer
        // this native instance's row; split-off stacks may need the fallback.
        auto units = inventory.UnitsOf(item->GetId());
        if (units.empty()) {
            units = inventory.UnwornUnitsOfKind(*item);
        }
        if (units.empty()) {
            return;
        }
        const std::pair<std::string, std::uint64_t> request {units.front().id, inventory.SyncedRevision()};
        if (std::find(_customItemUses.begin(), _customItemUses.end(), request) == _customItemUses.end()) {
            _customItemUses.push_back(request);
        }
    }

    void LocalEvents::Update() {
        for (const auto &[id, revision] : std::exchange(_customItemUses, {})) {
            KCDC::Shared::RPC::CustomItemUse message;
            message.body = nlohmann::json({{"id", id}, {"revision", revision}}).dump();
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(message, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
        // Each hand resolves on its own; one being mid-drop must not hold
        // the other back.
        PublishHand(kRightHand);
        PublishHand(kLeftHand);

        // Ticked only inside an established session, so the latch is all
        // that is left to wait for.
        if (_awaitingReady && _gameplayStarted) {
            ReportReady();
        }
    }

    void LocalEvents::SessionLevelRequested() {
        _awaitingReady   = true;
        _gameplayStarted = false;
    }

    void LocalEvents::ReportReady() {
        _awaitingReady = false;

        if (auto *net = Framework::CoreModules::GetNetworkPeer()) {
            KCDC::Shared::RPC::PlayerReady ready;
            net->BroadcastRPC(ready, MafiaNet::Priority::Medium, MafiaNet::Reliability::ReliableOrdered);
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("The local player can play");

        KCDC::Scripting::Events::LocalPlayerReady();
    }

    void LocalEvents::PublishHand(std::size_t hand) {
        // Edge-driven: the hand hook says what changed, this resolves it once.
        if (!_handStateKnown[hand] || !_handDirty[hand]) {
            return;
        }

        Shared::RPC::ItemClassId itemClass;
        if (_desiredHandWuid[hand] != 0 && !KCDC::Features::Player::PlayerInventoryService::FindLocalItemClass(_desiredHandWuid[hand], itemClass)) {
            // Mid-drop and out of the bags; the ground replica carries it from
            // here and the holster edge follows.
            return;
        }

        if (KCDC::Features::Player::PlayerService::PublishLocalHandItemClass(static_cast<std::uint32_t>(hand), itemClass)) {
            _handDirty[hand] = false;
        }
    }

    void LocalEvents::ObserveStation(const Shared::Game::StationState &state, std::uint32_t epoch, std::uint32_t incarnation) {
        const auto now = MafiaNet::GetTime();
        if (_stationReport.epoch == epoch && _stationReport.incarnation == incarnation && _stationReport.state.SamePose(state) && now < _stationReportAt) {
            return;
        }
        const auto revision           = Shared::Game::StationState::Advance(_stationReport.state.revision);
        _stationReport.state          = state;
        _stationReport.state.revision = revision;
        _stationReport.epoch          = epoch;
        _stationReport.incarnation    = incarnation;
        _stationReportAt              = now + 1000;
        if (auto *net = Framework::CoreModules::GetNetworkPeer()) {
            net->BroadcastRPC(_stationReport, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
    }

    void LocalEvents::ObserveDiceScene(Shared::Game::Dice::Scene scene, std::uint32_t epoch, std::uint32_t incarnation) {
        const auto now = MafiaNet::GetTime();
        if (now < _diceSceneAt) {
            return;
        }
        _diceSceneAt   = now + 50;
        scene.revision = _diceSceneRevision = Shared::Game::StationState::Advance(_diceSceneRevision);
        Shared::RPC::DiceScene report {scene, epoch, incarnation};
        if (auto *net = Framework::CoreModules::GetNetworkPeer()) {
            net->BroadcastRPC(report, MafiaNet::Priority::Medium, MafiaNet::Reliability::Unreliable);
        }
    }

    void LocalEvents::Reset() {
        _customItemUses.clear();
        _diceSceneAt                 = 0;
        _diceSceneRevision           = 0;
        _stationReport               = {};
        _stationReportAt             = 0;
        _handStateKnown[kRightHand]  = false;
        _handStateKnown[kLeftHand]   = false;
        _handDirty[kRightHand]       = false;
        _handDirty[kLeftHand]        = false;
        _desiredHandWuid[kRightHand] = 0;
        _desiredHandWuid[kLeftHand]  = 0;
        _pendingHandDropWuid         = 0;
        _pendingHandDropHand         = kNoHand;
        _droppingHeldWuid            = 0;
        _fatalHit                    = FatalHit {};
        _applyingRuledDamage         = false;
        _awaitingReady               = false;
        _gameplayStarted             = false;
        _reportedCarryNetworkId      = 0;
        _reportedCarryItemClass      = {};
    }

    // --- Carrying ------------------------------------------------------------

    void LocalEvents::ReportCarry(std::uint64_t carriedNetworkId, bool carrying) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        KCDC::Shared::RPC::PlayerCarry report;
        report.carriedNetworkId = carriedNetworkId;
        report.carrying         = carrying;

        // Ordered as well as reliable: a grab and its put-down only mean
        // anything in that order.
        net->BroadcastRPC(report, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void LocalEvents::LocalCarryStarted(SDK::EntityId targetEntityId) {
        KCDC::Features::Player::Human *carried = KCDC::Common::FindReplica<KCDC::Features::Player::Human>([targetEntityId](KCDC::Features::Player::Human *human) {
            return !human->IsOwner() && human->GetEntityId() == targetEntityId;
        });
        const std::uint64_t carriedNetworkId = carried != nullptr ? carried->GetNetworkID() : 0;

        // A re-grab before the last put-down was reported closes that one
        // first, so the server never holds two bodies on one shoulder.
        if (_reportedCarryNetworkId != 0 && _reportedCarryNetworkId != carriedNetworkId) {
            ReportCarry(_reportedCarryNetworkId, false);
        }
        _reportedCarryNetworkId = carriedNetworkId;
        if (carriedNetworkId != 0) {
            Framework::Logging::GetLogger("Carry")->info("Picked up the body of '{}'", carried->nickname);
            ReportCarry(carriedNetworkId, true);
        }
    }

    void LocalEvents::ReportItemCarry(const KCDC::Shared::RPC::PlayerCarryItem &report) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        // Ordered as well as reliable, like a body carry's edges.
        KCDC::Shared::RPC::PlayerCarryItem sent = report;
        net->BroadcastRPC(sent, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void LocalEvents::LocalItemCarryStarted(const KCDC::Shared::RPC::ItemClassId &itemClass, std::uint64_t groundItemNetworkId) {
        // A new pick-up before the last put-down was reported closes that one
        // first, so the server never holds two items in one pair of arms.
        if (_reportedCarryItemClass.IsValid()) {
            KCDC::Shared::RPC::PlayerCarryItem ended;
            ended.itemClass = std::exchange(_reportedCarryItemClass, {});
            ReportItemCarry(ended);
        }
        if (!itemClass.IsValid()) {
            return;
        }
        _reportedCarryItemClass = itemClass;
        Framework::Logging::GetLogger("Carry")->info("Picking up an item of class {:016x}{:016x}{}", itemClass.low, itemClass.high, groundItemNetworkId != 0 ? fmt::format(" from ground item {}", groundItemNetworkId) : std::string());
        KCDC::Shared::RPC::PlayerCarryItem started;
        started.itemClass           = itemClass;
        started.carrying            = true;
        started.groundItemNetworkId = groundItemNetworkId;
        ReportItemCarry(started);
    }

    void LocalEvents::LocalItemCarryEnded(bool dropped, const glm::vec3 *dropPosition, const glm::quat *dropRotation) {
        if (_reportedCarryItemClass.IsValid()) {
            KCDC::Shared::RPC::PlayerCarryItem ended;
            ended.itemClass     = std::exchange(_reportedCarryItemClass, {});
            ended.dropped       = dropped;
            ended.dropPoseKnown = dropped && dropPosition != nullptr && dropRotation != nullptr;
            if (ended.dropPoseKnown) {
                ended.dropPosition = *dropPosition;
                ended.dropRotation = *dropRotation;
            }
            ReportItemCarry(ended);
        }
    }

    void LocalEvents::LocalItemPutDownStarted() {
        if (_reportedCarryItemClass.IsValid()) {
            KCDC::Shared::RPC::PlayerCarryItem putDown;
            putDown.itemClass   = _reportedCarryItemClass;
            putDown.carrying    = true;
            putDown.puttingDown = true;
            ReportItemCarry(putDown);
        }
    }

    void LocalEvents::LocalCarryEnded() {
        // By the id reported rather than the body: the copy may already
        // be gone, which is one of the ways a carry ends.
        if (const std::uint64_t carriedNetworkId = std::exchange(_reportedCarryNetworkId, 0); carriedNetworkId != 0) {
            ReportCarry(carriedNetworkId, false);
        }
    }

} // namespace KCDC::Core
