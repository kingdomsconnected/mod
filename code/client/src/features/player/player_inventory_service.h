#pragma once

#include "features/items/native_food_use.h"

#include "core/feature_registry.h"
#include "features/items/inventory_notifications.h"
#include "features/items/inventory_reconciliation.h"
#include "features/items/native_inventory_projection.h"
#include "features/items/native_repair_batch.h"

#include "shared/item_stack.h"

#include <nlohmann/json.hpp>

#include "sdk/wh/entitymodule/c_item.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Player {
    /**
     * The local player's inventory, which the server holds.
     *
     * The server sends its rows whole on join and as the rows that changed
     * after that; the native inventory is made to match them and each
     * projected revision is acknowledged. The game changing the inventory by
     * itself is not something the server accepts: an item it adds is refused
     * where it enters and one it takes away is put back by the next
     * projection. The losses the game legitimately causes are reported so
     * the server's rows follow them: wear, and the units a meal or a shot
     * spends once `ArmConsumption` armed it. What the body wears is reported
     * as the rows it holds, and the body is dressed once from the server's
     * record when the session's inventory first arrives.
     *
     * Hooks can fire on the game's worker threads; they only record, and
     * Update acts on the game thread.
     */
    class PlayerInventoryService final: public KCDC::Core::IFeatureService {
      public:
        /** Registers the sync RPC. Call once at startup. */
        static void Install();

        /** Game thread: apply what the server sent, report wear, project again when needed. */
        void Update() override;

        /** Forgets the session's inventory. */
        void Reset() override;

        /** The game rebound the local actor or its inventory. */
        void NativeContextChanged() override;

        /** Assigns/reuses the owner's stable identity and captures one native item. */
        bool CaptureItemState(SDK::wh::entitymodule::C_Item *nativeItem, Shared::RPC::InventoryItem &state);

        /** Resolves one local item's class, for the state a held item publishes. */
        static bool FindLocalItemClass(std::uint64_t itemWuid, Shared::RPC::ItemClassId &itemClass);

        /** The server sends this player's inventory; native changes are not accepted. */
        bool HasAuthority() const {
            return _authority.load();
        }
        /** The projection itself is changing the native inventory. */
        bool IsApplyingAuthority() const {
            return _applyingAuthority.load();
        }
        bool ManagesNativeItem(const SDK::wh::entitymodule::C_Item *item) const {
            return HasAuthority() && _projection.ManagesNativeItem(item);
        }
        bool ManagesHolder(const SDK::wh::entitymodule::C_ItemHolder *holder) const {
            return HasAuthority() && _projection.ManagesHolder(holder);
        }

        /**
         * Game thread: do not project until the synced revision reaches
         * `revision`. A feature that moves items between the bags and its
         * own native holders asks the server first; this keeps the
         * projection from undoing the move before the server's rows for it
         * have arrived. A revision already reached holds nothing.
         */
        void HoldProjectionUntil(std::uint64_t revision) {
            _projectionHold = revision;
        }
        /** The game lends the local player a native item it takes back; see `NativeInventoryProjection::Lend`. */
        void LendNativeItem(std::uint64_t wuid) {
            _projection.Lend(wuid);
        }
        void ReturnNativeItem(std::uint64_t wuid) {
            _projection.Return(wuid);
        }
        /**
         * Game thread: do not project while `paused`. For a feature whose
         * server request moves items before the native action that expects
         * them to be gone has run; independent of `HoldProjectionUntil`.
         */
        void PauseProjection(bool paused) {
            _projectionPaused = paused;
        }
        /**
         * Game thread: the virtual world the server last said this player is
         * in. Every sync carries it, and the server sends one when it changes;
         * it is read from each, rows taken or not (one waiting for a whole
         * inventory still moves it). Empty before the first. Replicas do not
         * carry their own world here.
         */
        std::optional<std::uint32_t> SyncedWorld() const {
            return _syncedWorld;
        }
        /** Game thread: the rows are the server's, not awaiting the whole inventory again. */
        bool IsSynced() const {
            return HasAuthority() && !_awaitingFullSync;
        }
        /** Game thread: the server revision the rows were last synced at; a request names it. */
        std::uint64_t SyncedRevision() const {
            return _revision;
        }
        /** Game thread: the server rows a native stack holds units of. */
        std::vector<KCDC::Features::Items::NativeUnit> UnitsOf(std::uint64_t wuid) const {
            return _projection.UnitsOf(wuid);
        }
        /**
         * Game thread: the server rows a stack of this kind can be handed over
         * from -- every row of its kind, less the units the body wears. For a
         * native screen that moves split-off stacks rather than the ones the
         * projection made.
         */
        std::vector<KCDC::Features::Items::NativeUnit> UnwornUnitsOfKind(SDK::wh::entitymodule::C_Item &item) const;

        /** Game thread: the rows are synced and projected, so a native action may spend them. */
        bool CanSpend() const {
            return IsSynced() && _projected;
        }

        /** Which native action an armed spend is; decides the classes it may shrink. */
        enum class ConsumptionKind : std::uint8_t {
            Use,  // eating, drinking, potions and ointments
            Shot, // one fired arrow, bolt or ball
        };

        /**
         * Any thread: a native use or shot is about to run. For a short window
         * the shrinking of that stack (a use) or of any projected ammunition
         * (a shot) is a spend to report, not drift to undo; the next Update
         * reports it.
         */
        void ArmConsumption(ConsumptionKind kind, std::uint64_t nativeId = 0);

        /** Holds native food use until this body's server reservation arrives. */
        bool InterceptFoodUse(SDK::wh::entitymodule::C_Item *item, const KCDC::Features::Items::NativeFoodUse &use);
        void FoodUseFinished(bool eaten) {
            if (_replayingFood) {
                _foodEaten = eaten;
            }
        }
        bool AllowsFoodEffects() const {
            return !_replayingFood || _foodEffects;
        }

        /** Any thread: the local body's equipment moved natively; the new set is reported next frame. */
        void NotifyEquipmentChanged() {
            _equipmentDirty = true;
        }

        /**
         * Game thread: a round this player fired came to rest where it can be
         * picked up -- the ground, a wall, a body. Reported next frame, after
         * the shot that spent it.
         */
        void QueueAmmunitionRecovery(const Shared::RPC::ItemClassId &fired, const Shared::RPC::ItemClassId &landed, float x, float y, float z, std::uint64_t nativeItemWuid = 0, bool laidOnGround = false);
        /** Gives an approved ground replica the exact native impact item, once. */
        SDK::wh::entitymodule::C_Item *TakeRecoveredAmmunition(std::uint64_t nativeItemWuid, const Shared::RPC::InventoryItem &item);

        /** Any thread: a native action was refused; the player is told once per few seconds. */
        bool RefuseUnsupportedAction(const char *message);
        /** Any thread: the game changed a managed item's health or quality. */
        void NotifyNativeWear(std::uint64_t itemWuid);
        /**
         * Game thread, inside `C_ItemManager::CombineItems`: one kit is about
         * to be spent on `equipment`. Records the units, never refuses -- the
         * combine repeats until the repair lands, so a skipped one would spin
         * it forever. A repair that cannot be reported is projected back.
         */
        void ObserveNativeRepair(SDK::wh::entitymodule::C_Item *kit, SDK::wh::entitymodule::C_Item *equipment);
        /** Game thread: reads both units before the combine merges or removes them. */
        void CaptureNativeRepairResult(SDK::wh::entitymodule::C_Item *kit, SDK::wh::entitymodule::C_Item *equipment);
        /**
         * Game thread: a drying rack, smokehouse or fire turned `count` units
         * (negative: every unit) of these projected stacks into their dried,
         * smoked or cooked kind. Asks the server for the same conversion of
         * the rows they held, `kind` "dry", "smoke" or "cook"; until it
         * answers the projection holds, and a refusal or a conversion that
         * cannot be described is projected back.
         */
        void ReportPreparation(const char *kind, const std::vector<std::uint64_t> &stacks, std::int32_t count);
        /** Any thread: the game changed the native inventory some other way. */
        void NotifyNativeChange();
        /** Game thread: a registry batch finished; retry a deferred projection. */
        void DefinitionsChanged() {
            _projected   = false;
            _nativeDirty = true;
            _projectionRetry.Reset();
        }

        /** Server reconciliation may call native inventory mutation paths. */
        class AuthorityProjection final {
          public:
            explicit AuthorityProjection(PlayerInventoryService &service): _service(service), _previous(service._applyingAuthority.exchange(true)) {}
            ~AuthorityProjection() {
                _service._applyingAuthority = _previous;
            }
            AuthorityProjection(const AuthorityProjection &)            = delete;
            AuthorityProjection &operator=(const AuthorityProjection &) = delete;

          private:
            PlayerInventoryService &_service;
            bool _previous;
        };

      private:
        struct PendingConsume {
            std::uint64_t deadline = 0;
            // The native stacks a repair changed; their wear is the repair's.
            std::vector<std::uint64_t> repaired;
        };
        struct PendingRecovery {
            Shared::RPC::ItemClassId fired;
            Shared::RPC::ItemClassId landed;
            std::array<float, 3> position {};
            std::uint64_t nativeItemWuid = 0;
            /** `position` is the ground the round is laid on, not where its pickable stopped. */
            bool laidOnGround = false;
        };
        struct RecoveringItem {
            Shared::RPC::ItemClassId itemClass;
            std::uint64_t expiresAt = 0;
        };

        void DrainSync();
        void DrainConsumeAnswers();
        bool AcceptFoodAnswer(const nlohmann::json &answer);
        void SettleConsumption();
        void SubmitConsume(nlohmann::json body, std::vector<std::uint64_t> repaired = {});
        void CloseNativeRepair();
        void DiscardRepairWear(const std::vector<std::uint64_t> &repaired);
        void SendRecoveries();
        void DiscardExpiredRecoveries(std::uint64_t now);
        void ReportWear();
        void DressFromServer();
        void ReconcileEquipment();
        void ReportEquipment();
        /** The recordable rows the body wears and how many units of each. */
        std::map<std::string, std::uint32_t> NativeEquipment() const;
        /** What the body wears, plus the recorded pieces the game could not make. */
        std::map<std::string, std::uint32_t> RecordedEquipment() const;
        std::optional<Shared::Inventory::Quickslots> RecordedQuickslots() const;
        void RestoreQuickslots(const std::vector<std::uint64_t> &wanted);
        void Project();
        void ShowInventoryNotifications();
        /** Logs and announces the rows the last apply could not show that it had not reported already. */
        void ReportUnshown(std::map<std::string, std::string> unshown);
        void Acknowledge(bool applied, bool resync);

        std::unordered_map<std::uint64_t, std::uint64_t> _localItemIds;
        std::uint64_t _nextLocalItemId = 1;

        // Filled on the network thread or by hooks, drained on the game thread.
        std::mutex _incomingMutex;
        std::vector<std::string> _pendingSync;
        std::set<std::uint64_t> _wornItems;
        std::string _unsupportedNotice;
        std::uint64_t _nextUnsupportedNotice = 0;

        std::atomic<bool> _authority            = false;
        std::atomic<bool> _applyingAuthority    = false;
        std::atomic<bool> _nativeDirty          = false;
        std::atomic<bool> _nativeContextChanged = false;
        bool _awaitingFullSync                  = false;
        std::uint64_t _revision                 = 0;
        // The server publishes this context; replica virtual worlds are local.
        std::optional<std::uint32_t> _world;       // the world of the rows applied
        std::optional<std::uint32_t> _syncedWorld; // the world of the latest sync, applied or not
        std::uint64_t _projectionHold           = 0;
        bool _projectionPaused                  = false;
        bool _projected                         = false;
        // Rows the last apply left out, with why; the rest was projected.
        std::map<std::string, std::string> _unshown;
        std::map<std::string, nlohmann::json> _rows;
        KCDC::Features::Items::InventoryNotifications _notifications;

        // Spends reported and not yet answered hold the projection, which
        // would otherwise put the eaten meal or the fired round back.
        std::atomic<std::uint64_t> _armedUseUntil  = 0;
        std::atomic<std::uint64_t> _armedShotUntil = 0;
        std::atomic<std::uint64_t> _armedUseItem   = 0;
        std::uint64_t _nextConsumeRequest          = 1;
        std::uint64_t _consumeHold                 = 0;
        std::uint64_t _consumeRelease              = 0;
        std::map<std::string, PendingConsume> _pendingConsumes;
        std::vector<std::string> _consumeAnswers; // guarded by _incomingMutex
        struct FoodUse {
            std::string request;
            std::uint64_t nativeId    = 0;
            std::uint64_t deadline    = 0;
            std::uint32_t incarnation = 0;
            std::uint32_t world       = 0;
            KCDC::Features::Items::NativeFoodUse native;
        } _foodUse;
        bool _foodEaten     = false;
        bool _replayingFood = false;
        bool _foodEffects   = true;
        KCDC::Features::Items::NativeRepairBatch _openRepair;
        // An answered repair split rows under projected stacks; wear waits
        // for the projection to rebind them.
        bool _repairSettling = false;
        // The grindstone was open since wear was last reported: what it did
        // is claimed as a sharpening, not refused as wear.
        bool _sharpening = false;
        std::vector<PendingRecovery> _pendingRecoveries; // guarded by _incomingMutex
        // Guarded by _incomingMutex. A replica takes ownership; refused or
        // unstreamed recoveries release native presentation after the timeout.
        std::unordered_map<std::uint64_t, RecoveringItem> _recoveringItems;

        // Only verified equipment is reported back after a server restore.
        std::atomic<bool> _equipmentDirty = false;
        std::uint64_t _nextEquipmentPoll  = 0;
        KCDC::Features::Items::InventoryEquipmentReconciliation _equipment;
        // What the last dressing pass left worn, and what the game took off again afterwards; a
        // contested piece is not put back, so the first pass decides. Cleared with each record.
        std::set<std::uint64_t> _wornAfterDress;
        std::set<std::uint64_t> _contested;
        KCDC::Features::Items::InventoryProjectionRetry _projectionRetry;
        KCDC::Features::Items::NativeInventoryProjection _projection;
    };
} // namespace KCDC::Features::Player
