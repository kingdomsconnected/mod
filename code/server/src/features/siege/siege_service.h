#pragma once

#include "features/siege/siege_common.h"

#include "shared/features/siege/siege_ballistics.h"
#include "shared/features/siege/siege_entity.h"
#include "shared/features/siege/siege_rpc.h"

#include <mafianet/virtual_world.h>
#include <mafianet/types.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace KCDC::Features::Siege {
    /**
     * Server authority over siege engines: the cycle, who works them, what
     * they can take, the shot, and where the shot came down.
     *
     * An engine's motion is replicated state every client plays out (see
     * `SiegeEngineEntity`). A shot is solved here when the engine is told to
     * fire and written onto the engine at once, with the server clock it is
     * let go at, so every client that has the engine launches it on time from
     * its own copy. At that clock it is also sent to the players near where
     * it lands who do not have the engine, and closed here either by the one
     * client asked to say where it hit -- checked against the arc solved
     * here -- or, when nobody can, at the target once the flight has run its
     * course. The server has no geometry of its own to trace a stone against,
     * which is the whole reason a client is asked.
     *
     * A `usable` engine is worked by a player who takes its place: the
     * operator lays it with their look (`SiegeAim`), loads it and lets fly.
     * A trebuchet can be made to need hands at its winches, and holds its
     * winding while it has fewer than that.
     *
     * What a hit does is the game's own for players: each client takes its
     * share of the blast on its own body, so it is reported like any blow. An
     * NPC's health is the server's ledger, so that share is taken here, and so
     * is an engine's.
     */
    class SiegeService final {
      public:
        static SiegeService &Get();

        /** Registers the reports and the operators' requests. Called once, at boot. */
        void Install();

        /** Moves every engine through its cycle, keeps its operator and crew honest, launches due shots and closes overdue ones. */
        void Update();

        /** Takes a player who has left the server out of every place they held, and off the winch pull. */
        void OnPlayerDisconnect(std::uint64_t playerNetworkId);

        Shared::Entities::SiegeEngineEntity *Spawn(std::size_t assetIndex, const glm::vec3 &position, const glm::quat &rotation, MafiaNet::VirtualWorldId virtualWorld);
        void Destroy(Shared::Entities::SiegeEngineEntity *engine);
        std::size_t DestroyAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld);
        static std::vector<Shared::Entities::SiegeEngineEntity *> Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld);
        static Shared::Entities::SiegeEngineEntity *Find(std::uint64_t networkId);

        /** Starts the reload cycle of an unloaded engine, taking one shot from its ammunition. */
        SiegeResult Load(Shared::Entities::SiegeEngineEntity &engine);

        /** Turns a loaded engine to face `target` and lets fly at it. `attackerNetworkId` is the player credited with whatever it hits, or 0. */
        SiegeResult Fire(Shared::Entities::SiegeEngineEntity &engine, const glm::vec3 &target, std::uint64_t attackerNetworkId);

        /** Turns an engine to face a point and lays it there, without firing. Refused mid-shot, when the turn would move the stone out from under its arc. */
        SiegeResult Aim(Shared::Entities::SiegeEngineEntity &engine, const glm::vec3 &target);

        /** Why `target` cannot be hit from where the engine stands, or nothing when it can. */
        static Shared::Game::Siege::AimResult CanHit(const Shared::Entities::SiegeEngineEntity &engine, const glm::vec3 &target);

        void SetSpeed(Shared::Entities::SiegeEngineEntity &engine, float speed);

        /** Health taken at the point of impact, and how far from it anything is hurt. Server-only: no client needs either until a shot lands. */
        float GetDamage(Shared::Entities::SiegeEngineEntity &engine) const;
        void SetDamage(Shared::Entities::SiegeEngineEntity &engine, float damage);
        float GetDamageRadius(Shared::Entities::SiegeEngineEntity &engine) const;
        void SetDamageRadius(Shared::Entities::SiegeEngineEntity &engine, float radius);

        /** How far a shot goes along the operator's facing when they fire before laying it. Server-only, like the damage. */
        float GetRange(Shared::Entities::SiegeEngineEntity &engine) const;
        void SetRange(Shared::Entities::SiegeEngineEntity &engine, float range);

        /**
         * What the engine can take: 0 makes it unharmable. Setting it heals
         * the engine to the new maximum, so a script arms an engine with one
         * assignment.
         */
        void SetMaxHealth(Shared::Entities::SiegeEngineEntity &engine, float maxHealth);

        /** Sets what is left of it; 0 wrecks it, raising `siegeEngineWreck` with nobody to blame. Ignored for an unharmable engine. */
        void SetHealth(Shared::Entities::SiegeEngineEntity &engine, float health);

        /** Takes `amount` off a harmable engine, wrecking it at 0. Raises `siegeEngineDamage`, then `siegeEngineWreck`. */
        void Damage(Shared::Entities::SiegeEngineEntity &engine, float amount, std::uint64_t attackerNetworkId);

        /** Makes a wrecked or damaged engine whole and idle again. */
        void Repair(Shared::Entities::SiegeEngineEntity &engine);

        /** Shots left, or `kUnlimitedAmmo`; anything below 0 is unlimited. */
        void SetAmmo(Shared::Entities::SiegeEngineEntity &engine, std::int32_t ammo);

        /** How many of a trebuchet's winches need a crewman for its arm to come down; clamped to what it has. */
        void SetCrewRequired(Shared::Entities::SiegeEngineEntity &engine, std::uint8_t required);

        /**
         * Puts a player in the engine's place, or takes whoever is there out
         * of it with 0. A player holds one place at a time, so they leave any
         * other engine or winch first. False for a player who is not
         * connected.
         */
        bool SetOperator(Shared::Entities::SiegeEngineEntity &engine, std::uint64_t playerNetworkId);

      private:
        struct Tuning {
            float damage       = 0.0F;
            float damageRadius = 0.0F;
            float range        = 0.0F;
        };

        struct Shot {
            std::uint32_t shotId                  = 0;
            std::uint64_t engineNetworkId         = 0;
            std::uint64_t attackerNetworkId       = 0;
            std::uint8_t assetIndex               = 0;
            MafiaNet::VirtualWorldId virtualWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL;
            glm::vec3 target {0.0F};
            Shared::Game::Siege::Launch launch;
            Tuning tuning;

            /** Server clock the projectile is let go at, then the one it was. */
            std::uint64_t releaseAtMs = 0;
            bool launched             = false;

            /** The player asked to say where it lands, or 0 for nobody. */
            std::uint64_t reporterNetworkId = 0;

            /** Past this an unreported shot lands on its target. */
            std::uint64_t deadlineMs = 0;
        };

        /** A reported impact may be this far off the solved arc, in metres -- the stone's own size and a quantized pose with room to spare. */
        static constexpr float kReportTolerance = 8.0F;

        /** How long after the arc reaches its target an unreported shot is held open, in milliseconds. */
        static constexpr std::uint64_t kReportGraceMs = 4000;

        /**
         * How far from an engine's base a player may stand and still work it,
         * in metres. The client offers the place within 12 m; this leaves
         * room for the player having moved since, so a press the client sent
         * is honoured, and an operator who walks further off has left it.
         */
        static constexpr float kUseReach = 16.0F;

        /** How far a crewman may stray from his winch before he has let go of it, in metres. */
        static constexpr float kCrewReach = 4.0F;

        /** How far around the engine and the target a shot is sent, in metres. */
        static constexpr float kShotRelayRadius = 400.0F;

        /** How far from the target a player may be and still be asked where the stone came down. */
        static constexpr float kReporterRadius = 400.0F;

        /** How often the places are checked against the players holding them, in milliseconds. */
        static constexpr std::uint64_t kPlaceCheckMs = 250;

        /** The least time between two of an operator's looks taken for one engine, in milliseconds; the client sends one at most every 100. */
        static constexpr std::uint64_t kAimIntervalMs = 50;

        /** How high a trebuchet's frame stands over its base, in metres: a blast is measured to the nearer of the two. */
        static constexpr float kFrameHeight = 3.0F;

        void OnImpactReport(const Shared::RPC::SiegeImpactReport &report, MafiaNet::Packet *packet);

        /** A player asked something of an engine: checked, offered to scripts, then done. */
        void OnUse(const Shared::RPC::SiegeUse &use, MafiaNet::Packet *packet);

        /** The operator's look: laid on when the engine can reach it, else as near as it can along the look. */
        void OnAim(const Shared::RPC::SiegeAim &aim, MafiaNet::Packet *packet);

        /** Settles a step that has run its course, starting the next one of a load. */
        void Advance(Shared::Entities::SiegeEngineEntity &engine, std::uint64_t now);

        /** Holds a winding short of hands at the winches, and lets it go on once they are there. */
        void HoldForCrew(Shared::Entities::SiegeEngineEntity &engine, std::uint64_t now);

        /** Lets go of an operator or crewman who has disconnected or walked away. */
        void CheckPlaces(Shared::Entities::SiegeEngineEntity &engine);

        /**
         * Plays each crewman's pull for as long as the arm is being winched
         * down, and takes it off him when it stops or he leaves; only ever
         * the pull this service put on him.
         */
        void SyncCrewAnimations(Shared::Entities::SiegeEngineEntity &engine);

        /** Ends the pull this service put on a player, if it is still what they play. */
        void StopPull(std::uint64_t playerNetworkId);

        /** Takes a player out of every place they hold on any engine. */
        void LeaveAllPlaces(std::uint64_t playerNetworkId);

        /** Takes a player out of every place they hold on this engine, holding its winding when that cost it a crewman. */
        void LeavePlaces(Shared::Entities::SiegeEngineEntity &engine, std::uint64_t playerNetworkId);

        void Wreck(Shared::Entities::SiegeEngineEntity &engine, std::uint64_t attackerNetworkId);

        void Launch(Shot &shot, std::uint64_t now);
        void Land(const Shot &shot, const glm::vec3 &point);

        /** Harms the engines within a blast. True when it reached any. */
        bool HarmEngines(const Shot &shot, const glm::vec3 &point);

        /** The connected player best placed to trace the shot: the attacker when near enough, else whoever is nearest the target. */
        static std::uint64_t ChooseReporter(const Shot &shot);

        Tuning &TuningOf(Shared::Entities::SiegeEngineEntity &engine);

        std::unordered_map<std::uint64_t, Tuning> _tuning;

        /** The players this service has made pull at a winch, and the fragment it put on them. */
        std::unordered_map<std::uint64_t, const char *> _pulling;

        /** By engine: when its operator's look was last taken. */
        std::unordered_map<std::uint64_t, std::uint64_t> _lastAimMs;

        /** The players at a winch, rebuilt each place check; kept so the check does not allocate. */
        std::unordered_set<std::uint64_t> _manned;
        std::vector<Shot> _shots;

        /** The engines this tick walks; kept so the tick does not allocate. */
        std::vector<std::uint64_t> _engines;
        std::uint32_t _nextShotId = 1;
        std::uint64_t _nextPlaceCheck = 0;
    };
} // namespace KCDC::Features::Siege
