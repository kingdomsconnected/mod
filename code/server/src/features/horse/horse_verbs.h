#pragma once

#include "shared/features/horse/horse_entity.h"
#include "shared/features/player/player_buff.h"
#include "shared/features/player/player_damage.h"
#include "shared/game/buff_catalog.h"
#include "shared/game/horse_gear_catalog.h"

#include <mafianet/types.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace KCDC::Features::Horse {
    // Everything the server does to a horse. One place per verb, so the chat command, the mount RPC handler and the scripting binding are three callers of the same code -- and so the event a verb raises cannot be raised by one caller and forgotten by the next.
    //
    // No V8 here. What happened is reported through this feature's own KCDC::Scripting::Events, declared in horse_scripting.h, which is a notification seam rather than a binding: a server with no resources loaded runs all of this unchanged.
    class Horses final {
      public:
        // Spawns and replicates one. `soul` is a breed from `Shared::Game::g_HorseBreeds` (`pebbles`), a soul GUID, or empty for the generic riding horse. The horse is born wearing `gear` on every client, or without it the game's own tack for it (`DefaultGear`); an empty set is a bare horse. Raises `horseSpawn`.
        static Shared::Entities::HorseEntity *Spawn(const std::string &soul, const glm::vec3 &position, const glm::quat &rotation, const std::string &name = std::string(), const Shared::Game::HorseGear *gear = nullptr);

        // Despawns it everywhere, raising `horseDestroy` while the body can still be read. Anyone in the saddle is taken off it first, with a `horseDismount`; their own client drops them as the body goes.
        static void Destroy(Shared::Entities::HorseEntity *horse);

        // The one place the name is written. A rider keeps the name it mounted with until it dismounts: the field channel is withheld from an entity's owner, and the only way past that costs a pose warp.
        static void Rename(Shared::Entities::HorseEntity *horse, const std::string &name);

        // Makes the horse `playerNetworkId`'s, or nobody's when that is 0. Every client re-possesses the body onto the new owner's soul from the replicated field. Moves no authority: who simulates a horse is its rider or the nearest player, whoever owns it. Raises `horseOwnerChanged` on an actual change. False, and nothing written, when the id names no player.
        static bool SetOwner(Shared::Entities::HorseEntity *horse, std::uint64_t playerNetworkId);

        // What a disconnect does to the player's horses: each is destroyed unless a script cleared its `destroyWithOwner`, in which case it stays in the world with no owner.
        static void OwnerLeft(std::uint64_t playerNetworkId);

        // Seats `playerNetworkId` on the server's record, or empties the saddle when that is 0. A rider's client is pinned as the horse's simulator for the length of the ride; an empty saddle unpins it and the delegation election picks the nearest player again -- usually the one who just got off. Raises nothing, and moves nobody's body: the caller decides which events and which orders a change deserves.
        static void SetRider(Shared::Entities::HorseEntity *horse, std::uint64_t playerNetworkId, MafiaNet::PeerGuid riderGuid, bool seatedInstantly, std::uint8_t dismountSide);

        // The horse this player is riding, or nullptr.
        static Shared::Entities::HorseEntity *RiddenBy(std::uint64_t playerNetworkId);

        // Empties whatever saddle a player is sitting in, returning the horse it took them out of. Raises no event: the caller knows whether this was a dismount worth reporting or the teardown of a connection that is already gone.
        static Shared::Entities::HorseEntity *UnseatPlayer(std::uint64_t playerNetworkId);

        // Takes whoever is in the saddle out of it and raises `horseDismount`. With `orderClient` their own client is told to get off too -- needed whenever the server is the one deciding, since only that client can move its player's body. Returns who was taken off, 0 for an empty saddle.
        static std::uint64_t TakeRiderOff(Shared::Entities::HorseEntity *horse, bool orderClient);

        // Tells one player's client to get into this horse's saddle: the get-on a player plays at a horse, or with `instant` straight into the seat as the game's own ForceMount does. Every other client that watches it plays the same; one that streams the ride in later seats the
        // rider either way. The seat itself is reported back like any other mount, so `horseMounting` and `horseMount` follow. False when the horse is dead or somebody else is riding it, or the player has no connection.
        static bool Mount(Shared::Entities::HorseEntity *horse, std::uint64_t playerNetworkId, bool instant);

        // Tells one player's client to get off `horse` without reporting a dismount, because the server's record already has the saddle empty.
        static void OrderOff(Shared::Entities::HorseEntity *horse, std::uint64_t playerNetworkId);

        // The game's own RearAndThrowDown: a request the horse plays out rather than a pose, so what everyone watches is the real animation. False when nobody is riding it.
        static bool RearAndThrowDown(Shared::Entities::HorseEntity *horse);

        // The game's own RequestHorsePullDown, on every client. False when the horse has no rider.
        static bool PullDownRider(Shared::Entities::HorseEntity *horse, std::uint64_t attackerNetworkId);

        // --- Life ---

        // A blow some attacker's own game resolved against its copy of the horse. Handed to the simulator, whose soul is the one that counts; with nobody simulating it (nobody near), the server takes it off its own record and raises the events itself.
        static void Hit(Shared::Entities::HorseEntity *horse, std::uint64_t attackerNetworkId, float amount, Shared::Game::DamageReason reason);

        // Sets health to `value`, clamped to the reported maximum. 0 kills. False for a dead horse, which only `Revive` brings back, or one no simulator has reported a maximum for yet.
        static bool SetHealth(Shared::Entities::HorseEntity *horse, float value);

        // Kills it on its simulator's soul, the way any other death happens there. Anyone riding it comes off. False when it is already dead.
        static bool Kill(Shared::Entities::HorseEntity *horse);

        // Brings a dead horse back to its full health, in place. False when it is alive.
        static bool Revive(Shared::Entities::HorseEntity *horse);

        // Ends a knockout on its simulator's soul, the way the game ends one early; the copies follow that soul. False when the horse is not knocked out, is dead, or nobody is running it.
        static bool Wake(Shared::Entities::HorseEntity *horse);

        // The one place a death is announced: `alive` drops, the rider comes off, `horseDeath` is raised. Once per death, whichever edge reaches it first -- the simulator's `HorseDied`, a script's kill, a blow on a horse nobody runs.
        static void AnnounceDeath(Shared::Entities::HorseEntity *horse, std::uint64_t killerNetworkId, Shared::Game::DamageReason reason);

        // --- Gear ---

        // The tack the game would give a horse with this soul: its own preset for a named horse, one of the generic ones otherwise.
        static Shared::Game::HorseGear DefaultGear(std::string_view soulName, std::uint64_t networkId);

        // What the server's record says the horse wears.
        static Shared::Game::HorseGear GearOf(const Shared::Entities::HorseEntity *horse);

        // The one place gear is written: every copy is dressed in exactly `gear`, the simulator included, and `horseGearChanged` is raised with `playerNetworkId` as the one who changed it (0 for a script). False, and nothing written, when the set is not valid.
        static bool SetGear(Shared::Entities::HorseEntity *horse, const Shared::Game::HorseGear &gear, std::uint64_t playerNetworkId = 0);

        // The rider's report of what their copy wears, `basedOnRevision` being the write it was dressed from. Accepted, or refused by the catalog, the lock or a `horseGearChanging` handler; a refusal is published anyway, so the copy that changed is put back.
        static void ReportGear(Shared::Entities::HorseEntity *horse, std::uint64_t riderNetworkId, const std::string &packed, std::uint32_t basedOnRevision);

        // --- Buffs ---
        //
        // A horse's stats -- speed, stamina, courage, carrying capacity -- are its soul's derived stats, and the game moves them with buffs (horseshoes, race boosts, horse plate). So do these verbs. Unlike a player's buffs they are state: the list is written on the entity and every copy of the horse wears it, so the simulator's speed and an observer's copy agree, and a late joiner or a new owner starts from the same list.

        // The definitions the server holds on the horse, in the order they were added.
        static std::vector<Shared::RPC::BuffId> BuffsOf(const Shared::Entities::HorseEntity *horse);

        // Adds one; true when it is on the list afterwards, including when it already was. False for a horse that is gone or already carrying `HorseEntity::kMaxBuffs`.
        static bool AddBuff(Shared::Entities::HorseEntity *horse, const Shared::Game::BuffEntry &buff);

        // Takes one off; false when it was not on the list.
        static bool RemoveBuff(Shared::Entities::HorseEntity *horse, const Shared::Game::BuffEntry &buff);

        static bool HasBuff(const Shared::Entities::HorseEntity *horse, const Shared::Game::BuffEntry &buff);

        // Takes every server buff off; false when there was none.
        static bool ClearBuffs(Shared::Entities::HorseEntity *horse);

      private:
        // Pushes a buff write to every copy the way a gear write goes: revision bump, then the forced state for the simulator.
        static void PublishBuffs(Shared::Entities::HorseEntity *horse, const std::vector<Shared::RPC::BuffId> &buffs);

        // Pushes a gear write to every copy: the revision bump is what makes each of them re-dress, and the forced state is the only way the simulator hears it.
        static void PublishGear(Shared::Entities::HorseEntity *horse);

        // Pushes a server write to the simulator, the one peer the field channel never tells.
        static void PublishVitals(Shared::Entities::HorseEntity *horse);
    };
} // namespace KCDC::Features::Horse
