#pragma once

#include "core/feature_registry.h"

#include "features/player/player.h"

#include "sdk/wh/entitymodule/c_inventory.h"
#include "sdk/wh/entitymodule/c_shooting_system.h"
#include "sdk/wh/rpgmodule/c_rpg_module.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/rpgmodule/s_missile_hit_data.h"
#include "shared/features/player/player_missile.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace KCDC::Features::Player {
    /**
     * Client owner for every shot in flight, local and relayed.
     *
     * The local shots this peer has to report, the remote ones it replays on
     * copies, the shadow weapons and ammunition those replays need, and the
     * impacts waiting on a target -- none of it belongs to one body, so all
     * of it lives here. The RPCs land on the network thread and are queued
     * under the inbox mutex rather than played where they arrive.
     */
    class PlayerMissileService final: public KCDC::Core::IFeatureService {
      public:
        /** Registers the shot RPCs. Call once at startup. */
        static void Install();

        void Update() override;
        void Reset() override;
        void ForgetActor(std::uint64_t networkId);

        static bool IsLocalActor(SDK::EntityId entityId);
        static bool IsSimulatedNpc(SDK::EntityId entityId);
        static bool IsRemoteActor(const SDK::wh::entitymodule::C_Actor *actor);
        static bool IsRemoteSoul(const SDK::wh::rpgmodule::I_Soul *soul);
        bool IsReproducingDraw(const SDK::wh::rpgmodule::I_Soul *soul);
        static bool TryGetRemoteReloadSpeed(const SDK::wh::rpgmodule::I_Soul *soul, float &speed);
        static bool IsReplayingLaunch();
        void RemoteWeaponFired(SDK::wh::entitymodule::C_Actor *actor);

        /**
         * Queues a remote copy's next reload stage, or its draw, while its
         * current reload stage finishes.
         *
         * Raised from the copy's own `C_MissileWeaponPlayerController`
         * callback, which only its shooting expansion's signals reach, so
         * that expansion exists. A sub-action's finish (0x18289D050) opens
         * the only window in which `CanReload` and `CanCharge` pass on a
         * running shooting action, and broadcasts the signal that callback
         * is bound to; the owner's controller queues its next step there
         * while the prepare input is held. A copy has no input, so its
         * owner's phase stands in for it: anything but idle continues the
         * chain.
         */
        void ContinueRemoteReload(SDK::wh::entitymodule::C_Actor *actor);
        bool IsObserverProjectile(const SDK::wh::entitymodule::CProjectile *projectile) const;
        bool IsTrackedLocalProjectile(SDK::wh::entitymodule::CProjectile *projectile) const;
        bool IsNpcProjectile(SDK::wh::entitymodule::CProjectile *projectile) const;
        static bool ProcessSourceEffects(SDK::wh::rpgmodule::I_EventManager *manager, SDK::wh::rpgmodule::I_Event *event, const SDK::wh::rpgmodule::S_MissileHitData &data);
        void BindReplayedProjectile(SDK::wh::entitymodule::CProjectile *projectile);

        bool TrackLocalLaunch(const Shared::RPC::MissileLaunched &message, SDK::wh::entitymodule::CProjectile *projectile);

        // Returns the local shot sequence, or zero for an untracked/remote shot.
        std::uint64_t ForgetProjectile(SDK::wh::entitymodule::CProjectile *projectile, bool destructed = false);
        void QueueRecoveredItem(SDK::wh::entitymodule::C_Item *item);
        void QueueRetainedAmmunitionRemoval(std::uint64_t targetNetworkId, SDK::wh::entitymodule::C_Item *item);
        bool CaptureImpact(SDK::wh::entitymodule::CProjectile *projectile, const SDK::wh::rpgmodule::S_MissileHitData &data, Shared::RPC::MissileImpact &report);
        bool CaptureRetained(SDK::wh::entitymodule::CProjectile *projectile, SDK::wh::entitymodule::C_ItemHolder *destination, SDK::wh::entitymodule::C_Item *item, Shared::RPC::MissileRetained &report);

        // --- Local engine edges, raised by this feature's own hooks --------

        // Reports accepted native launches.
        void NotifyLaunched(SDK::EntityId actorEntityId, const SDK::wh::entitymodule::MissileLaunch &launch);
        void NotifyEnded(std::uint64_t sequence);
        /**
         * The class this local projectile was fired as, once: the first time
         * it comes to rest. The projectile's own item may by then be the
         * broken round an impact swapped in.
         */
        std::optional<Shared::RPC::ItemClassId> ClaimRecovery(SDK::wh::entitymodule::CProjectile *projectile) {
            const auto shot = _local.find(projectile);
            if (shot == _local.end() || shot->second.recovered || shot->second.message.npcRevision != 0) {
                return std::nullopt;
            }
            shot->second.recovered = true;
            return shot->second.message.ammunition;
        }
        void NotifyAmmunitionRecovered(SDK::wh::entitymodule::C_Item *item);
        /** The ground under `position` when this local round came to rest there off a body; nothing when it did not, or no ground was found. */
        std::optional<SDK::Vec3> GroundUnderStruckBody(SDK::wh::entitymodule::CProjectile *projectile, const SDK::Vec3 &position) const;

        // Captures attacker-soul queries around one native hit calculation.
        // The scope must close before the result is evaluated.
        struct MissileImpactCapture {
            Shared::RPC::MissileImpact report;
            std::optional<Human::SoulActionScope> queries;

            /** Set once this hit is ours to report and recording has begun. */
            bool routed = false;
        };

        /** Opens the record, before native effects can retire the shot. */
        void NotifyImpactBegan(SDK::wh::entitymodule::CProjectile *projectile, const SDK::wh::rpgmodule::S_MissileHitData &data, MissileImpactCapture &capture);

        /** Closes it and reports the hit. False when it was never ours. */
        bool NotifyImpactCompleted(MissileImpactCapture &capture);
        void NotifyAmmunitionRetained(SDK::wh::entitymodule::CProjectile *projectile, SDK::wh::entitymodule::C_ItemHolder *destination, SDK::wh::entitymodule::C_Item *item);

        // --- The local missile state -------------------------------------
        //
        // Edge-driven, never sampled: see `sdk/wh/entitymodule/
        // c_actor_action_shooting_main.h` for which engine writers the
        // hooks sit on and why they are exhaustive. Nothing here
        // broadcasts -- `missile` is a replicated field, not an RPC.

        /**
         * Re-reads the local state and writes it to the owner replica when
         * it differs. Raised by this feature's own hooks, which do not ask
         * whose actor moved: the read is of the local player either way, so
         * a bystander's shot costs one comparison that finds nothing
         * changed.
         */
        void NotifyLocalStateChanged();

        /**
         * Seeds a newly constructed owner replica with one full read.
         *
         * The only read not driven by an edge, and it has to exist: a
         * session can begin with a bow already drawn, and nothing in the
         * action tree will move until the player next touches it.
         */
        void SeedLocalState(Human &owner);

      private:
        struct Body;
        static Shared::RPC::MissileState CaptureLocalState();
        static Shared::RPC::MissileState CaptureState(SDK::wh::entitymodule::C_Actor *actor);

        struct LocalShot {
            std::uint64_t sequence;
            std::uint64_t expiresAt;
            Shared::RPC::MissileLaunched message;
            std::uint64_t impactTarget = 0;
            // The engine can settle one arrow more than once; only the first
            // settlement is reported, or one arrow would spend two rounds'
            // worth of recoverable stock on the server.
            bool recovered = false;
        };

        struct ShotItems {
            SDK::EntityId actorId    = 0;
            std::uint64_t weapon     = 0;
            std::uint64_t ammunition = 0;
            bool failed              = false;
        };

        struct RemoteShot {
            Shared::RPC::MissileLaunched message;
            SDK::wh::entitymodule::CProjectile *projectile = nullptr;
            std::uint64_t expiresAt                        = 0;
            std::uint8_t attempts                          = 0;
            bool launched                                  = false;
            bool abandoned                                 = false;
        };

        struct RemoteWeapon {
            SDK::EntityId actorId = 0;
            Shared::RPC::MissileState desired;
            Shared::RPC::ItemClassId fireAmmunition;
            Shared::RPC::ItemClassId fireWeapon;
            std::uint64_t fireExpiresAt = 0;
            std::map<std::pair<std::uint64_t, std::uint64_t>, std::uint8_t> createAttempts;
            std::uint64_t lastSequence = 0;
            std::uint64_t fireSequence = 0;
            bool awaitingDrawReset     = false;
            // The copy released this draw on its owner's Firing phase, ahead
            // of the launch: the next launch is that shot, not a new draw.
            bool releasedBeforeLaunch = false;
            bool aimApplied           = false;
        };

        struct PendingImpact {
            Shared::RPC::MissileImpact message;
            std::uint64_t expiresAt;
            bool finished = false;
            ShotItems items;
        };

        struct RemoteAmmoRemoval {
            std::uint64_t targetNetworkId;
            SDK::EntityId actorId;
            std::uint64_t itemWuid;
        };

        SDK::wh::entitymodule::C_Item *EnsureItem(SDK::wh::entitymodule::C_Inventory *inventory, const Shared::RPC::ItemClassId &itemClass, RemoteWeapon &state, int amount);
        void ReleaseShotItems(ShotItems &items);
        bool PrepareShotItems(ShotItems &items, const Shared::RPC::MissileLaunched &launch, const Body &human);
        void ApplyWeapon(const Body &human);
        void DestroyRemote(RemoteShot &shot);
        void LaunchRemote(RemoteShot &shot, const Body &human);
        bool ApplyImpact(PendingImpact &pending, const Body &shooter);
        void RemoveRemoteAmmunition();

        std::mutex _incomingMutex;
        std::vector<std::variant<Shared::RPC::MissileLaunched, Shared::RPC::MissileEnded, Shared::RPC::MissileImpact>> _incoming;
        bool _acceptIncoming = false;
        std::unordered_map<SDK::wh::entitymodule::CProjectile *, LocalShot> _local;
        std::map<std::pair<std::uint64_t, std::uint64_t>, RemoteShot> _remote;
        std::map<std::pair<std::uint64_t, std::uint64_t>, PendingImpact> _impacts;
        std::unordered_map<std::uint64_t, RemoteWeapon> _weapons;

        // Keep deferred-destruction observers until destructor or pool reuse.
        std::unordered_set<const SDK::wh::entitymodule::CProjectile *> _observers;
        std::vector<std::uint64_t> _recoveredItems;

        std::vector<RemoteAmmoRemoval> _remoteAmmoRemovals;

        // What this peer has already told the server about its own shots: the
        // generation counter that orders the launches, and the sequences whose
        // impact and retained-ammunition reports have gone out once.
        // The local missile state as last written to the owner replica, so an
        // edge that changed nothing costs no field write.
        Shared::RPC::MissileState _localState;

        std::uint64_t _nextMissileSequence = 1;
        std::unordered_map<std::uint64_t, std::uint64_t> _shotOwners;
        std::unordered_set<std::uint64_t> _reportedMissileImpacts;
        std::unordered_set<std::uint64_t> _reportedRetainedMissiles;

        // The replay the engine is inside, on the thread that entered it.
        // Per-thread rather than per-service: it marks a call, not a session.
        static thread_local RemoteShot *_replaying;
    };
} // namespace KCDC::Features::Player
