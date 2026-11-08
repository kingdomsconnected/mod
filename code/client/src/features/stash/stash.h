#pragma once

#include "features/items/native_inventory_projection.h"
#include "shared/features/stash/stash_entity.h"
#include "shared/features/stash/stash_lid_motion.h"
#include "shared/item_stack.h"

#include <nlohmann/json.hpp>

#include "sdk/cryengine/animation/i_character_instance.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_inventory_manager.h"
#include "sdk/wh/entitymodule/c_stash.h"

#include <mafianet/replica_manager3.h>

#include <glm/vec3.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace KCDC::Features::Stash {
    /**
     * Client binding between a replicated container and a native stash entity.
     *
     * A spawned container is the game's own `Stash` class, spawned under the
     * EntityGuid the server's `stashId` dictates -- which is what makes the
     * engine's stash manager and the inventory it derives agree across
     * machines. A level container is the one the level already built under
     * `levelGuid`, bound rather than built, and found again whenever the
     * engine streams it back in.
     * Virtual stock creates only a native inventory and opens its screen
     * when the server authorizes it; no entity or lid is built.
     *
     * The server holds what is in it. The native inventory shows the rows the
     * server last sent, and a deposit or withdrawal in the transfer screen is
     * asked of the server rather than done; its answer moves the items. A
     * linked child opens its master's inventory, so the rows live on the
     * master's replica. Everyone else sees the lid follow `isOpen`.
     */
    class Stash final: public Shared::Entities::StashEntity {
      public:
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

        /** Binds the container, applies its lock, lid and the rows the server sent. */
        void Update();

        /** Network thread's snapshot, applied on the next update. */
        void OnServerContents(const nlohmann::json &snapshot);

        /** A server script authorized opening this virtual stock. */
        void OnVirtualOpen(const nlohmann::json &snapshot);

        /** Asks the server to move `items` between the local player and this container. False when it cannot be asked now. */
        bool RequestTransfer(const std::vector<std::pair<SDK::wh::entitymodule::C_Item *, std::uint32_t>> &items, bool deposit);

        /** The game changed some native inventory; the rows are applied again. */
        void NotifyNativeChange() {
            _projectionDirty = true;
        }

        /** The native inventory showing the server's rows, or null while it shows none. */
        SDK::wh::entitymodule::C_Inventory *ProjectedInventory() const {
            return _rowsApplied && OwnsContents() ? BoundInventory() : nullptr;
        }

        /** The inventory this replica owns, with or without a chest entity. */
        SDK::wh::entitymodule::C_Inventory *BoundInventory() const;

        SDK::wh::entitymodule::C_Stash *GetNativeStash() const;

        /** The native container this frame's update bound, without resolving it again; null when unbound. */
        SDK::wh::entitymodule::C_Stash *BoundNativeStash() const {
            return _nativeStash;
        }

        SDK::EntityId GetNativeEntityId() const {
            return _entityId;
        }

        /** Whether this replica carries its own contents rather than a linked master's. */
        bool OwnsContents() const {
            return contentsNetworkId == 0;
        }

        /** True between this client's own open and close of this container's contents. */
        bool IsOpenHere() const {
            return _openHere;
        }

        /** This client opened or closed the contents, through the physical chest `physicalNetworkId`. */
        void SetOpenHere(bool openHere, std::uint64_t physicalNetworkId);

        /** This client is presenting the physical chest's own open lid natively. */
        void SetPresentationOpenHere(bool openHere);

        /** The native context went away: whatever this client held open is closed. */
        void RetireView();

        /** The server took this client's view away; its screen can move nothing more. */
        void ServerClosed();

      private:
        /** Finds the native entity by GUID, building a spawned one that has none; false while there is none. */
        bool TryBind();
        bool TryMaterialize();

        void CleanupNative();

        /** Forgets everything applied to the native entity, so the next one gets it all again. */
        void ResetApplied();

        void ApplyState();
        /** Writes the server's lock difficulty and fanciness into `Properties.Lock`, or gives back the level's. */
        void ApplyLockTuning();
        void ApplyLidPresentation();
        /** Reads the lid clips off the bound character once; false when it has none. */
        bool ResolveLidClips(SDK::ICharacterInstance *character);
        void ReleaseLidControl();
        void ApplyContents();
        void FlushTransfer();
        void SendClosed();
        /** Releases a server grant this client holds without having opened the screen. */
        void SendClosedFor(std::uint64_t stashNetworkId);
        bool TryCreateVirtualInventory();
        void UpdateVirtualScreen();
        void CloseVirtualScreen();

        KCDC::Features::Items::NativeInventoryProjection _projection;
        std::map<std::string, nlohmann::json> _rows;
        std::uint64_t _revision = 0;
        bool _snapshotReceived  = false;
        bool _rowsApplied       = false;
        bool _projectionDirty   = false;
        bool _projectionFailed  = false;
        nlohmann::json _pendingSnapshot;

        // One transfer is asked at a time. Take All asks once per stack in
        // one frame; those are gathered and sent together.
        std::map<std::string, std::uint32_t> _queuedUnits;
        bool _queuedDeposit                 = false;
        bool _transferInFlight              = false;
        std::uint64_t _transferDeadline     = 0;
        std::uint64_t _openedThroughNetwork = 0;
        std::uint64_t _renewAt              = 0;

        SDK::wh::entitymodule::C_Stash *_nativeStash = nullptr;
        SDK::EntityId _entityId                      = 0;

        SDK::wh::entitymodule::C_Inventory *_virtualInventory = nullptr;
        SDK::wh::framework::WUID _virtualInventoryWuid {SDK::wh::framework::WUID::kInvalid};
        bool _virtualOpenRequested         = false;
        bool _virtualScreenOpened          = false;
        std::uint64_t _virtualOpenDeadline = 0;

        /**
         * The lock is applied on edges, not held: a key turned or a lock
         * picked on this client unlocks it natively a round trip before the
         * server's copy agrees, and forcing the replica's value every frame
         * would lock it again in between.
         */
        bool _stateApplied  = false;
        bool _appliedLocked = false;

        /**
         * `Properties.Lock` as the entity had it before the server tuned it,
         * read once per binding, so clearing the server's value gives the
         * level's back. AnimStash.lua regenerates a lock whose override flags
         * are clear, so those are written and restored with the values.
         */
        struct NativeLock {
            float difficulty        = 1.0F;
            std::string fanciness   = "Common";
            bool difficultyOverride = false;
            bool fancinessOverride  = false;
        };
        std::optional<NativeLock> _nativeLock;
        float _appliedLockDifficulty                                             = KCDC::Shared::Entities::StashEntity::kNativeLockDifficulty;
        KCDC::Shared::Entities::StashEntity::LockFanciness _appliedLockFanciness = KCDC::Shared::Entities::StashEntity::LockFanciness::Native;

        std::uint8_t _materializeAttempts = 0;
        bool _materializationFailed       = false;

        KCDC::Shared::StashLidMotion _lidMotion;
        SDK::ICharacterInstance *_lidCharacter = nullptr;
        int _lidAnimationId                    = -1;
        int _lidOpenId                         = -1;
        int _lidCloseId                        = -1;
        bool _lidSameClip                      = false;
        bool _nativeLidPending                 = false;
        bool _lidFailureLogged                 = false;
        bool _lidForcedUpdate                  = false;
        bool _lidWasActive                     = false;

        bool _presentationOpenHere = false;
        bool _openHere             = false;
    };
} // namespace KCDC::Features::Stash
