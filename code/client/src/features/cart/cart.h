#pragma once

#include "shared/features/cart/cart_entity.h"
#include "shared/features/cart/cart_motion.h"
#include "shared/features/cart/cart_seats.h"
#include "shared/game/cart_blueprints.h"

#include "common/replication/pose_snapshot.h"
#include "common/spawn/actor_spawner.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/physics/i_physical_entity.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_cart.h"

#include <mafianet/replica_manager3.h>
#include <mafianet/types.h>

#include <utils/snapshot_buffer.h>

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace KCDC::Features::Cart {
    /** One received drive, and how two of them combine, for a copy to be drawn between. */
    struct CartSnapshot {
        Shared::Game::CartDrive drive;
    };

    struct CartSnapshotPolicy {
        static glm::vec3 Position(const CartSnapshot &snapshot) {
            return snapshot.drive.lead;
        }

        static glm::vec3 Velocity(const CartSnapshot &snapshot) {
            return glm::vec3(Shared::Game::CartHeading(snapshot.drive.leadYaw) * snapshot.drive.speed, 0.0F);
        }

        static CartSnapshot Blend(const CartSnapshot &a, const CartSnapshot &b, float alpha, float) {
            CartSnapshot out;
            out.drive.lead    = glm::mix(a.drive.lead, b.drive.lead, alpha);
            out.drive.leadYaw = a.drive.leadYaw + Shared::Game::CartWrapAngle(b.drive.leadYaw - a.drive.leadYaw) * alpha;
            out.drive.bodyYaw = a.drive.bodyYaw + Shared::Game::CartWrapAngle(b.drive.bodyYaw - a.drive.bodyYaw) * alpha;
            out.drive.speed   = glm::mix(a.drive.speed, b.drive.speed, alpha);
            return out;
        }

        static CartSnapshot Extrapolate(const CartSnapshot &newest, float dtSeconds) {
            CartSnapshot out = newest;
            out.drive.lead += Velocity(newest) * dtSeconds;
            return out;
        }
    };

    /**
     * Client half of a replicated cart.
     *
     * Every client builds the same cart the level would: the chassis a
     * `GeomEntity` at the root, its wheels and body under it, the `WHCart`
     * whose extension is the cart and whose mount points raise the game's own
     * prompt -- and besides, a `WHCart` per seat under the body, which bodies
     * are seated in (see `SpawnSeatEntities`), and a horse at each place on
     * the pole.
     *
     * The game moves a cart only along a road, as an NPC caravan, and a player
     * drives one here as a wagon (`Shared::Game::StepCart`): the driver's
     * client steps the drive from the keys, every other client is handed it
     * and draws it a little behind. Each poses its own copy from the drive:
     * the chassis's world transform, the wheels rolled by how far they went,
     * each horse put at its place on the pole and playing the gait the pace
     * calls for. No road is ever bound, so the game's own cart movement never
     * runs (`TickCartOncePerFrame`, 0x181E8DAE0, does nothing without one),
     * and the game never walks the team: its harness walker only ever leads
     * a caravan forward along a road.
     *
     * `CartEntity` is shared with the server, which has no engine, so the
     * native parts this replica is bound to are held here.
     */
    class Cart final: public Shared::Entities::CartEntity {
      public:
        // --- Replica3 hooks ---

        void OnConstructed() override;
        void OnStateForced() override;
        void OnDeserialized(bool transformUpdated) override;
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

        // --- The native cart ---

        /** The prefab's own `WHCart`: the cart extension, and what the game's prompt names the cart by. */
        SDK::EntityId GetCartEntityId() const {
            return _cartEntityId;
        }

        /** The live cart extension, or nullptr while the cart is not built. */
        SDK::wh::entitymodule::C_Cart *GetCart() const {
            return _cart;
        }

        /**
         * Drops the native cart once its `WHCart` is gone -- a level change
         * takes the whole tree -- and looks it up again once there is one.
         */
        void RefreshNativeBinding();

        /** Whether the cart is rolling, as far as anyone can mount it. */
        bool IsRolling() const {
            return std::fabs(_drive.speed) > Shared::Game::kCartStandSpeed;
        }

        /** How fast it rolls this frame, along the lead's heading; negative backing up. */
        float GetSpeed() const {
            return _drive.speed;
        }

        /** The `WHCart` a body sits in `seat` by, or 0 for a seat the cart does not have or before it is built. */
        SDK::EntityId GetSeatEntityId(std::uint8_t seat) const {
            return seat < _seatEntityIds.size() ? _seatEntityIds[seat] : 0;
        }

        /** The seat a `WHCart` of this cart's stands for, or nothing for one that is not a seat of its. */
        std::optional<std::uint8_t> SeatOfEntity(SDK::EntityId entityId) const;

        /** The game's position a seat is climbed into by: the bench's for the front, the rail place it is named after for the back. */
        static SDK::wh::entitymodule::E_CartPosition::Type SeatPosition(const Shared::Game::CartSeat &seat);

        /** Where a body sits in `seat`, in the world, or nothing before the seat is built. */
        std::optional<glm::vec3> SeatWorldPoint(std::uint8_t seat) const;

        /** Every `WHCart` of this cart's: the prefab's own, whose mount points raise the prompt, and one per seat. */
        std::vector<SDK::EntityId> GetCartEntityIds() const;

        /**
         * The cart extension of every one of them, for the hooks' question of
         * whether a `C_Cart` is ours. Kept from the build rather than looked
         * up -- the lookup is a game-object query by extension name -- and
         * valid while `GetCart` is: the seats go with the tree.
         */
        void CollectNativeCarts(std::vector<const SDK::wh::entitymodule::C_Cart *> &out) const;

        // --- Per frame ---

        /**
         * This cart, this frame: built if it is not, then driven if the local
         * player holds its reins or drawn where its driver's was, and posed.
         */
        void Frame(const Shared::Game::CartControls &controls, bool localDriverSeated, float frameTime);

        // --- Clips ---

        /**
         * Starts `clip` on a layer of the entity's character, as the script
         * binding's `StartAnimation` starts one: false when the engine refused
         * it, which it does for a clip already playing there.
         */
        static bool StartClip(SDK::IEntity *entity, std::string_view clip, std::int32_t layer, float blend, float rate, bool loop);

        /** How long `clip` is on the entity's character, in seconds; 0 when it has none by that name. */
        static float ClipLength(SDK::IEntity *entity, std::string_view clip);

        /** Takes every part and horse out of the world. */
        void Despawn();

      private:
        // --- Building ---

        void TryBuild();

        /** Spawns the blueprint's tree with the chassis at this cart's pose. False on failure, having removed what it made. */
        bool BuildTree(const Shared::Game::CartBlueprint &blueprint);

        /** One part, at `slot` of this cart's GUIDs, hung off `parent` at `local`; 0 on failure. */
        SDK::EntityId SpawnPart(const Shared::Game::CartPart &part, std::size_t slot, SDK::IEntity *parent, const SDK::Vec3 &position, const SDK::Quat &rotation);

        /** Enables the seats the player gets; see the definition. */
        void OpenSeats();

        /** A `WHCart` per seat under the body; see the definition. */
        bool SpawnSeatEntities();

        /** Asks the spawner for the horses the chassis is harnessed for. */
        void RequestHorses();
        void OnHorseSpawned(std::size_t slot, KCDC::Common::Spawn::SpawnTicket &ticket, bool spawned);

        /** Each spawned horse, once it is up, without its collider and in its harness; see the definition. */
        void DressHorses();

        /** The way the team faces, and where a horse stands, for a drive. */
        float HorseYaw(const Shared::Game::CartDrive &drive) const;
        glm::vec3 HorsePoint(const Shared::Game::CartDrive &drive, std::size_t slot) const;

        // --- Driving ---

        /** The local player's reins: the drive stepped from the keys, and reported. */
        void Drive(const Shared::Game::CartControls &controls, float frameTime);

        /** Somebody else's reins, or none: the drive as the stream last had it. */
        void Follow();

        /** Whether the team cannot go on from `drive`, forward or back: something solid at knee height ahead of the horses, or behind the bed. */
        bool IsBlocked(const Shared::Game::CartDrive &drive, bool forward);

        // --- Posing ---

        /** Poses the whole cart from a drive, with the game's own pieces; see the definition. */
        void Present(const Shared::Game::CartDrive &drive, float frameTime);

        /** Each horse put at its place on the pole; whether all are up and settled onto their ground. */
        bool PlaceTeam(const Shared::Game::CartDrive &drive, float leadGround, const std::vector<SDK::IPhysicalEntity *> &skip, float frameTime);

        /** The team's gait clip and its time, on one clock for both horses; see the definition. */
        void StepTeamGait(float speed, float frameTime);

        /** The ground under a point, looked for from `reach` above its height down past it, the cart's own bodies passed over; see the definition. */
        float GroundAt(const glm::vec3 &point, const std::vector<SDK::IPhysicalEntity *> &skip, float reach) const;

        /** The physical bodies of the cart's own parts and horses, as they are now, for a probe to pass over. */
        std::vector<SDK::IPhysicalEntity *> OwnBodies() const;

        SDK::EntityId _chassisEntityId = 0;
        SDK::EntityId _bodyEntityId    = 0;
        SDK::EntityId _cartEntityId    = 0;

        // The `WHCart` each seat is sat in by, by seat.
        std::array<SDK::EntityId, Shared::Game::kCartMaxSeats> _seatEntityIds {};
        std::array<SDK::wh::entitymodule::C_Cart *, Shared::Game::kCartMaxSeats> _seatCarts {};
        SDK::wh::entitymodule::C_Cart *_cart = nullptr;

        // Where the blueprint's parts sit, and so how the cart moves and is posed.
        std::optional<Shared::Game::CartRig> _rig;

        // Every entity the tree is made of, the chassis first, so a teardown
        // removes exactly what was built.
        std::vector<SDK::EntityId> _parts;

        // The horses at the pole, left then right, and their spawns in
        // flight. Local to this client: every copy has its own. Whether each
        // is in its harness yet, when the harness goes on once more, and the
        // ground under each, eased.
        std::array<SDK::EntityId, 2> _horses {};
        std::array<KCDC::Common::Spawn::SpawnTicket *, 2> _horseTickets {};
        std::array<bool, 2> _horseDressed {};
        std::array<std::optional<float>, 2> _horseGround {};
        MafiaNet::Time _harnessAgainAt = 0;

        int _harnessSlots = 0;

        // The team's gait clock: the gait playing, one it is waiting on to
        // hold and since when, the stride's phase, the clip's rate and length,
        // and when the clip is next started again.
        Shared::Game::CartHorseGait _gait = Shared::Game::CartHorseGait::Idle;
        bool _gaitStarted                 = false;
        std::optional<Shared::Game::CartHorseGait> _gaitNext;
        MafiaNet::Time _gaitNextSince = 0;
        float _gaitPhase              = 0.0F;
        float _gaitRate               = 1.0F;
        float _gaitLength             = 0.0F;
        MafiaNet::Time _gaitCheckAt   = 0;

        bool _buildRefused               = false;
        bool _buildDeferred              = false;
        MafiaNet::Time _nextBuildAttempt = 0;

        // The drive this copy is posed from: the driver's own, stepped here,
        // or the one sampled out of the stream.
        Shared::Game::CartDrive _drive;
        bool _driving = false;
        Framework::Utils::SnapshotBuffer<CartSnapshot, CartSnapshotPolicy> _snapshots {&KCDC::Common::Replication::PoseBufferConfig()};

        // The drive the cart was last posed from, and whether it had settled
        // onto its ground then: a standing cart is posed again only once it
        // moves.
        Shared::Game::CartDrive _posedDrive;
        bool _posed       = false;
        bool _poseSettled = false;

        // The ground under the lead point and the trailing axle, eased, so a
        // cobble does not shake the bed.
        std::optional<float> _leadGround;
        std::optional<float> _axleGround;

        // How fast each of them is falling onto lower ground, metres a second.
        float _leadFall = 0.0F;
        float _axleFall = 0.0F;
    };
} // namespace KCDC::Features::Cart
