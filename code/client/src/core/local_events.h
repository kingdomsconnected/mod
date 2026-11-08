#pragma once

#include "features/items/native_food_use.h"
#include "shared/features/player/player_station.h"
#include "shared/features/dice/dice_scene.h"

#include <utils/safe_win32.h>

#include "core/feature_registry.h"
#include "sdk/types.h"
#include "sdk/wh/framework/i_action.h"
#include "sdk/wh/playermodule/c_alchemy.h"
#include "sdk/wh/playermodule/c_blacksmithing.h"
#include "sdk/wh/playermodule/c_blacksmithing_slot_tool_attachment.h"
#include "sdk/wh/playermodule/c_minigame.h"
#include "sdk/wh/playermodule/c_use_bellows_action.h"
#include "sdk/wh/playermodule/c_use_mortar_action.h"
#include "sdk/wh/playermodule/c_use_retort_action.h"
#include "sdk/wh/rpgmodule/c_inventory_soul.h"
#include "sdk/wh/rpgmodule/c_soul.h"

#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_actor_action_butchering.h"
#include "sdk/wh/entitymodule/c_inventory.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/entitymodule/c_player_interactor.h"
#include "sdk/wh/entitymodule/e_prepare_food.h"
#include "sdk/wh/entitymodule/s_item_transfer_descriptor.h"
#include "sdk/wh/environmentmodule/c_respawn_manager.h"
#include "sdk/wh/playermodule/c_herb_gathering.h"

#include "shared/features/crafting/smithing_rpc.h"
#include "shared/features/player/player_carry.h"
#include "shared/features/player/player_entity.h"
#include "shared/item_stack.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace KCDC::Core {
    // Dispatches local game events and network reports. Game-thread callbacks
    // must not block, allocate through the game heap, or re-enter their
    // caller.
    class LocalEvents final: public KCDC::Core::IFeatureService {
      public:
        void CustomItemUsed(SDK::wh::entitymodule::C_Item *item);
        bool InventoryItemUsed(SDK::wh::entitymodule::C_Item *item, const char *action);
        void ObserveDiceScene(Shared::Game::Dice::Scene scene, std::uint32_t epoch, std::uint32_t incarnation);
        void ObserveStation(const Shared::Game::StationState &state, std::uint32_t epoch, std::uint32_t incarnation);
        bool AllowNativeItemDecay(const SDK::wh::entitymodule::C_Item *item) const;
        bool AllowNativeItemMerge(SDK::wh::entitymodule::C_Item *source, SDK::wh::entitymodule::C_Item *candidate) const;
        /** The game is about to spend `kit` repairing `equipment`. Game thread, inside the combine; never refused. */
        void NativeRepairStarting(SDK::wh::entitymodule::C_Item *kit, SDK::wh::entitymodule::C_Item *equipment);
        bool InterceptFoodUse(SDK::wh::entitymodule::C_Item *item, const KCDC::Features::Items::NativeFoodUse &use);
        bool AllowsFoodEffects() const;
        void FoodUseFinished(bool eaten);
        /** The repair returned; both units still exist until the combine resumes. */
        void NativeRepairFinished(SDK::wh::entitymodule::C_Item *kit, SDK::wh::entitymodule::C_Item *equipment);
        /** A drying rack, smokehouse or fire converted `count` units (negative: every unit) of the local player's `stacks`, now gone. Game thread, inside the conversion. */
        void NativeItemsPrepared(SDK::wh::entitymodule::E_PrepareFood::Type preparation, const std::vector<std::uint64_t> &stacks, std::int32_t count);
        /** The game added `item` to `holder`, moved it out, or took it away. Any thread. */
        void NativeInventoryChanged(const SDK::wh::entitymodule::C_ItemHolder *holder, const SDK::wh::entitymodule::C_Item *item);
        /** A native armor or weapon-slot mutation; sampled on the next tick. */
        void NativeEquipmentChanged(const SDK::wh::entitymodule::C_Item *item);
        /** A native drop of the local player's item; true when it was asked of the server instead. */
        bool RequestInventoryDrop(std::uint64_t itemWuid, std::uint32_t amount);
        void InventoryDropSelectionStarting();
        void InventoryDropSelectionFinished();
        /** The inventory screen moves `items` into `destination`; true when it was asked of the server instead. */
        bool RequestContainerTransfer(SDK::wh::entitymodule::C_Inventory *destination, const SDK::StdVector<SDK::wh::entitymodule::C_Item *> &items, std::uint32_t amount);
        bool NpcItemExchange(SDK::wh::entitymodule::C_Actor &actor, SDK::EntityId user);
        void NpcActionRegistering(const SDK::wh::entitymodule::C_PlayerInteractor &interactor, SDK::wh::entitymodule::S_InteractorActionRecord &record, SDK::HScriptFunction function);
        bool NpcActionInvoked(const SDK::wh::entitymodule::S_LuaActionInvocation &invocation);
        bool NpcButcheringStarting(SDK::wh::entitymodule::C_ActorActionButchering &action);
        void NpcButcheringEntered(const SDK::wh::entitymodule::C_ActorActionButchering &action, bool accepted);
        bool NpcButcheringCompleted(SDK::wh::entitymodule::C_ActorActionButchering &action);
        void NpcButcheringExited(const SDK::wh::entitymodule::C_ActorActionButchering &action, bool cancelled);
        bool NpcCanBeButchered(const SDK::wh::rpgmodule::I_Soul *soul, bool &answer) const;
        bool CanStartGathering(SDK::wh::playermodule::C_HerbGathering *minigame, SDK::EntityId proxy);
        void GatheringStarted(SDK::wh::playermodule::C_HerbGathering *minigame, const SDK::wh::environmentmodule::RespawnInteractionStateLayout &source);
        void GatheringCompleted(SDK::wh::environmentmodule::C_RespawnManager *manager);
        /** Queued native alchemy intent; only the server supplies its verdict. */
        void AlchemyIntent(const std::string &body);
        bool BeginAlchemyScriptCall(std::uint64_t actor, std::uint64_t station);
        bool EndAlchemyScriptCall(int originalValueCount);
        bool AlchemyStarting(SDK::wh::playermodule::C_Alchemy *minigame, SDK::EntityId station);
        bool AlchemyEntryReady(SDK::wh::playermodule::C_Alchemy *minigame);
        void AlchemyStarted(SDK::wh::playermodule::C_Alchemy *minigame, SDK::EntityId station, bool accepted);
        void AlchemyDestroying(SDK::wh::playermodule::C_Alchemy *minigame);
        void AlchemyDestroyed(SDK::wh::playermodule::C_Alchemy *minigame);
        bool AlchemySelecting(SDK::wh::playermodule::C_Alchemy *minigame, SDK::wh::playermodule::AlchemyItemType target);
        bool AlchemyReturning(SDK::wh::playermodule::C_Alchemy *minigame);
        bool AlchemyBellowsPumping(SDK::wh::playermodule::C_UseBellowsAction *action);
        bool AlchemyBellowsReturning(SDK::wh::playermodule::C_UseBellowsAction *action);
        bool AlchemyRetortPumping(SDK::wh::playermodule::C_UseRetortAction *action);
        bool AlchemyRetortFinishing(SDK::wh::playermodule::C_UseRetortAction *action);
        bool AlchemyRetortRequiredPulses(SDK::wh::playermodule::C_UseRetortAction *action, std::uint32_t &value);
        void AlchemyMortarStarted(SDK::wh::playermodule::C_UseMortarAction *action, bool accepted);
        void AlchemySelected(SDK::wh::playermodule::C_Alchemy *minigame, SDK::wh::playermodule::AlchemyItemType target);
        void AlchemyExitRequested(SDK::wh::playermodule::C_Alchemy *minigame);
        void AlchemyShuttingDown(SDK::wh::playermodule::C_Alchemy *minigame);
        void AlchemyShutDown(SDK::wh::playermodule::C_Alchemy *minigame);
        bool AlchemyNativeScopeStarting(SDK::wh::playermodule::C_Alchemy *minigame);
        bool AlchemyNativeCleanupScopeStarting(SDK::wh::playermodule::C_Alchemy *minigame);
        void AlchemyNativeCleanupScopeEnded();
        bool AlchemyActionScopeStarting(SDK::wh::playermodule::C_Alchemy *minigame, SDK::wh::framework::I_Action *action);
        void AlchemyNativeScopeEnded();
        bool AlchemyStartedQuery(SDK::wh::rpgmodule::I_Soul *soul, float &value, bool &executeNative);
        bool AlchemyFinishedEvent(SDK::wh::rpgmodule::I_Soul *soul);
        bool AlchemyRecipeKnowledgeQuery(SDK::wh::rpgmodule::I_Soul *soul, std::uint32_t recipe, std::uint32_t first, std::uint32_t last, bool &value);
        void AlchemyResourceConstructing(SDK::wh::playermodule::C_AlchemyResource *resource, SDK::wh::entitymodule::C_Item *item);
        void AlchemyResourceConstructed(SDK::wh::playermodule::C_AlchemyResource *resource, SDK::wh::entitymodule::C_Item *item);
        void AlchemyBaseResourceConstructed(SDK::wh::playermodule::C_AlchemyResource *resource, SDK::wh::playermodule::AlchemyItemType selection);
        bool AlchemySuppressNativeOutcome(SDK::wh::playermodule::C_Alchemy *minigame, SDK::wh::playermodule::AlchemyItemType container);
        void AlchemyResetNative(SDK::wh::playermodule::C_Alchemy *minigame);
        /** The Craft button at a smithery; false while the server is asked instead. */
        bool SmithingRecipeAccepting(SDK::wh::playermodule::C_Blacksmithing *minigame);
        void SmithingWorkpieceFinishing(SDK::wh::playermodule::C_Blacksmithing *minigame, bool success);
        void SmithingCancelling(SDK::wh::playermodule::C_Blacksmithing *minigame);
        /** Another actor action replaced the one this minigame ran under. */
        void MinigameActionReplaced(SDK::wh::playermodule::C_Minigame *minigame);
        void SmithingWorkpieceFinished(SDK::wh::playermodule::C_Blacksmithing *minigame);
        bool SmithingSuppressNativeSettlement(SDK::wh::playermodule::C_Blacksmithing *minigame);
        bool SmithingSuppressNativeOutcome();
        /**
         * The smithery hands the smith one of its tools, or takes it back.
         * True when it is the local player's and the inventory authority
         * must let the transfer through: the tool is lent, not given.
         */
        bool SmithingToolAttaching(SDK::wh::playermodule::C_BlacksmithingSlotToolAttachment *tool, SDK::wh::entitymodule::C_Actor *actor);
        bool SmithingToolDetaching(SDK::wh::playermodule::C_BlacksmithingSlotToolAttachment *tool);
        void SmithingToolDetached(SDK::wh::playermodule::C_BlacksmithingSlotToolAttachment *tool);
        /** Reports to the server: a recipe to begin, and how a workpiece ended. */
        void SmithingBegin(const KCDC::Shared::RPC::SmithingBegin &request);
        void SmithingFinish(const KCDC::Shared::RPC::SmithingFinish &report);

        // --- Held items ----------------------------------------------------
        //
        // What is left here rather than on a service, and why: the local
        // body's two hands are one latch that three features write and read.
        //
        //   player      raises every hand and drop edge from its inventory
        //               hooks, and is where a settled hand is published --
        //               PlayerInventoryService resolves the item class and
        //               PlayerService puts it on the owning replica.
        //   ground_item owns the other half of a drop. The drop edges hand it
        //               the stack and the four pickup edges are its own, and
        //               its replica raises GroundDropBecamePhysical back here
        //               when the world item takes the hand's item over.
        //   inventory   is the vocabulary both speak: a hand holds a WUID the
        //               bags still know about until the drop completes.
        //
        // No one of the three owns it. Moving the latch to player would leave
        // ground_item calling into it for a drop it is completing, and moving
        // it to ground_item would put the body's hands inside a world-pile
        // feature. Section 5.2 keeps exactly this on the dispatcher: an event
        // that fans out to several features.

        // --- Markers -------------------------------------------------------

        /**
         * The local body crossed a marker's boundary. `entered` is true on the
         * way in.
         *
         * Reported rather than acted on: nothing on this client cares, and the
         * server is what turns a crossing into a gamemode event.
         */
        void MarkerCrossed(std::uint64_t markerNetworkId, bool entered);

        // --- Quests --------------------------------------------------------

        /**
         * The player started or stopped following a server quest. `tracked` is
         * true on the way in.
         *
         * Reported rather than acted on, like a marker crossing: the decision
         * is made here and the server cannot refuse it. QuestService has
         * already raised the local event; this gives the server its own.
         */
        void QuestTrackingChanged(const std::string &questKey, bool tracked);

        // --- Damage --------------------------------------------------------

        /**
         * The local body is about to lose `amount` health, through the
         * engine's own damage effect.
         *
         * Raised before the write lands, because the write that kills also
         * declares the death: reported afterwards, the blow would reach the
         * server behind the death it caused.
         *
         * Every such loss goes out as a report. Bleeding, poison and hunger
         * never arrive here tick by tick -- regeneration writes those with no
         * cause -- only as the one effect that takes the rest of the health.
         * A fatal write is also kept for the death report to name its killer.
         */
        void LocalBodyDamaged(std::uint64_t attackerSoulWuid, std::int32_t hitSubpart, std::uint32_t deathReason, float amount, bool fatal);

        /**
         * Marks the combat referee's ruling being taken off the local body.
         * The server raised that damage as it ruled, so none of it goes out
         * again as a report; a kill it causes is still kept for the death
         * report.
         */
        class RuledDamageScope final {
          public:
            explicit RuledDamageScope(LocalEvents &events): _events(events), _previous(events._applyingRuledDamage) {
                _events._applyingRuledDamage = true;
            }

            ~RuledDamageScope() {
                _events._applyingRuledDamage = _previous;
            }

            RuledDamageScope(const RuledDamageScope &)            = delete;
            RuledDamageScope &operator=(const RuledDamageScope &) = delete;

          private:
            LocalEvents &_events;
            bool _previous;
        };

        /** What the last fatal write said killed the local body. */
        struct FatalHit {
            std::uint64_t killerNetworkId = 0;
            std::uint8_t reason           = 0;
        };

        /** The fatal write behind the death being reported, cleared as it is read. */
        FatalHit TakeFatalHit() {
            return std::exchange(_fatalHit, FatalHit {});
        }

        /**
         * One of the local body's hands now holds this item, or nothing --
         * the moment it is really out, not the moment it was equipped.
         */
        void HeldItemChanged(std::uint32_t hand, std::uint64_t itemWuid);

        // Capture held-item identity before the native drop removes it.
        void InventoryDropStarting(std::uint64_t itemWuid);

        /** That drop succeeded and produced this world item. */
        void InventoryItemDropped(std::uint64_t sourceWuid, SDK::wh::entitymodule::C_Item *droppedItem);

        /** A native pickup is starting; answers whether it is one worth following. */
        bool PickupStarting(SDK::wh::entitymodule::S_ItemTransferDescriptor *descriptor, bool animated);

        /** Whether the followed pickup is the one `descriptor` names. */
        bool IsPickupTrackedFor(const SDK::wh::entitymodule::S_ItemTransferDescriptor *descriptor) const;

        /** The followed pickup's native call returned. */
        void PickupSettled(bool succeeded);

        /** The followed pickup was cancelled or refused. */
        void PickupCancelled();

        /**
         * Source physics started, so observers may replace the held torch
         * with its ground representation.
         */
        void GroundDropBecamePhysical(std::uint64_t droppedWuid);

        /** Resolves and publishes what the hand hooks latched, when it changed. */
        void Update() override;

      private:
        std::vector<std::pair<std::string, std::uint64_t>> _customItemUses;
        std::uint32_t _diceSceneRevision = 0;
        std::uint64_t _diceSceneAt       = 0;
        Shared::RPC::PlayerStation _stationReport;
        std::uint64_t _stationReportAt = 0;
        /** Publishes one hand's pending change, if it has one. */
        void PublishHand(std::size_t hand);

      public:
        // --- Carrying --------------------------------------------------------

        /**
         * The local player's carry action entered, holding `targetEntityId`.
         *
         * Reported only for a body the server knows -- another player's
         * copy. An NPC's corpse is this client's own business: there is no
         * one else's copy of it to put on anybody's shoulder.
         */
        void LocalCarryStarted(SDK::EntityId targetEntityId);

        /** The local player's carry action exited, however it ended. */
        void LocalCarryEnded();

      private:
        /** Tells the server one carry edge about `carriedNetworkId`. */
        static void ReportCarry(std::uint64_t carriedNetworkId, bool carrying);

        /** The player whose grab the server was last told of, until its put-down is. */
        std::uint64_t _reportedCarryNetworkId = 0;

      public:
        /**
         * The local player's carry-item action entered, for an item of
         * `itemClass` -- from the server's `CarryItem`, or from a pile the
         * player used. `groundItemNetworkId` is the prop it takes up, or zero.
         */
        void LocalItemCarryStarted(const KCDC::Shared::RPC::ItemClassId &itemClass, std::uint64_t groundItemNetworkId);

        /**
         * The local player's carry-item action exited, however it ended.
         * `dropped` says the item went to the ground, for the server to make
         * a prop of.
         */
        void LocalItemCarryEnded(bool dropped, const glm::vec3 *dropPosition, const glm::quat *dropRotation);

        /** The local player's put-down clip started; the item is still in their arms. */
        void LocalItemPutDownStarted();

      private:
        /** Tells the server one item carry edge. */
        static void ReportItemCarry(const KCDC::Shared::RPC::PlayerCarryItem &report);

        /** The item whose pick-up the server was last told of, until its put-down is. */
        KCDC::Shared::RPC::ItemClassId _reportedCarryItemClass;

      public:
        // --- Readiness -------------------------------------------------------

        //
        // Ready is the game taking its own loading screen down for this
        // session's level. That edge fires inside the engine's system-event
        // dispatch, under its engine lock, and may come before the session
        // is established; so the edge only latches, and the session tick
        // reports it -- the first one after both have happened.

        /**
         * The session's level is about to be asked for. A gameplay start from
         * before this is the previous session's, whose level is still up
         * behind the menu, and does not count.
         */
        void SessionLevelRequested();

        /** The game took its loading screen down. From the engine's own edge. */
        void GameplayStarted() {
            _gameplayStarted = true;
        }

      private:
        /** Raises and reports readiness once, the first tick it is latched. */
        void ReportReady();

      public:
        // --- Session --------------------------------------------------------

        /**
         * Clears per-session dispatcher state: what the hand hooks latched,
         * the drop still waiting on its ground replica, and the readiness
         * still to be reported.
         *
         * Only this dispatcher's own state. The features it reported for are
         * swept beside it, so each one clears what it owns.
         */
        void Reset() override;

      private:
        // The engine's own two hands, and the answer for neither of them.
        static constexpr std::size_t kHandCount = Shared::Entities::HumanEntity::kHandCount;
        static constexpr std::size_t kRightHand = Shared::Entities::HumanEntity::kRightHand;
        static constexpr std::size_t kLeftHand  = Shared::Entities::HumanEntity::kLeftHand;
        static constexpr std::size_t kNoHand    = Shared::Entities::HumanEntity::kNoHand;

        /** Whether a complete native state has been observed for that hand. */
        bool _handStateKnown[kHandCount] = {};

        /** Set by the hand edges, cleared once the replica has been told. */
        bool _handDirty[kHandCount] = {};

        /** The held item a running drop is about to take out of the bags. */
        std::uint64_t _droppingHeldWuid = 0;

        /** Which hand holds this exact WUID, or kNoHand. */
        std::size_t FindHandHolding(std::uint64_t itemWuid) const {
            if (itemWuid == 0) {
                return kNoHand;
            }
            if (_handStateKnown[kRightHand] && itemWuid == _desiredHandWuid[kRightHand]) {
                return kRightHand;
            }
            if (_handStateKnown[kLeftHand] && itemWuid == _desiredHandWuid[kLeftHand]) {
                return kLeftHand;
            }
            return kNoHand;
        }

        /** Engine-local desired identity per hand, never serialized. */
        std::uint64_t _desiredHandWuid[kHandCount] = {};

        /** A dropped held stack waiting on its ground replica, and its hand. */
        std::uint64_t _pendingHandDropWuid = 0;
        std::size_t _pendingHandDropHand   = kNoHand;

        /** Written by a fatal damage write, taken by the death report it causes. */
        FatalHit _fatalHit;

        /** Set while a combat ruling's damage is applied: the server already knows it. */
        bool _applyingRuledDamage = false;

        /** Set when the level is asked for, cleared once readiness is reported. */
        bool _awaitingReady = false;

        /** Latched by the gameplay-start edge since the level was asked for. */
        bool _gameplayStarted = false;
    };
} // namespace KCDC::Core
