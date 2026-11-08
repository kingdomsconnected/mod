#pragma once

#include "npc_navigation.h"
#include "npc_order.h"

#include "shared/features/npc/npc_entity.h"
#include "shared/features/npc/npc_types.h"
#include "shared/features/player/player_appearance.h"

#include <mafianet/types.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace KCDC::Features::Npc {
    /** Everything a spawn can be told, in one bag, because most of it is optional. */
    struct SpawnParams {
        /**
         * A role from `Shared::Game::g_NpcRoles` (`guard`, `townswoman`), or a
         * soul GUID spelled out. A role fills in the entity class and the
         * outfit; a GUID is passed through untouched and refused by the client
         * if the game does not know it.
         */
        std::string soul;

        /** `NPC` or `NPC_Female`; empty takes the role's, or `NPC`. */
        std::string actorClass;

        /** Shown over the body and in dialogue; empty keeps the soul's own name. */
        std::string name;

        /** Clothing preset GUID from the game's own table, or empty for the soul's own clothes. */
        std::string outfit;

        /** Explicit item classes to wear, if the caller would rather list them than name a preset. */
        std::vector<std::string> wearing;

        Shared::Game::CharacterAppearance appearance;

        glm::vec3 position = glm::vec3(0.0F);
        glm::quat rotation = glm::identity<glm::quat>();

        std::uint8_t factionId               = 0;
        Shared::Game::NpcFlags flags         = Shared::Game::NpcFlags::Interactable | Shared::Game::NpcFlags::ShowNametag;
        Shared::Game::NpcLocomotion locomotion = Shared::Game::NpcLocomotion::Native;

        float health    = 100.0F;
        float maxHealth = 100.0F;

        std::uint32_t virtualWorld = 0xFFFFFFFFU; // sentinel: the global world

        /** Level `EntityGuid` to adopt instead of spawning a body. */
        std::uint64_t levelGuid = 0;
    };

    /**
     * Everything the server does to an NPC.
     *
     * One place per verb, so the chat command, the interaction RPC and the
     * scripting binding are three callers of the same code -- and so the two
     * rules every write has to obey cannot be obeyed by one caller and
     * forgotten by the next:
     *
     * 1. **Every write to a server-authored field is followed by
     *    `ForceState()`.** The field channel is withheld from an entity's
     *    owner, and for an NPC the owner is the client simulating it -- the one
     *    peer that has to know. `ForceState` also bumps the state epoch, which
     *    is what stops that client's in-flight echo of the previous value from
     *    landing after the write and reverting it.
     * 2. **Every intent change bumps `intentRevision`.** A simulator reports
     *    progress against the revision it was given, so an unbumped change
     *    would be answered by a report about the order before it.
     */
    class Npcs final {
      public:
        /** RPC handlers, the delegation budget and its handler. Call once at startup. */
        static void Install();

        /** Orders, routes, dormant dead reckoning and the queued route plans. Call every tick. */
        static void Update();

        // --- Lifecycle --------------------------------------------------------

        /** Spawns one and replicates it. Null when the parameters name nothing usable. */
        static Shared::Entities::NpcEntity *Spawn(const SpawnParams &params);

        /** Takes over a body the level already placed, addressed by its `EntityGuid`. */
        static Shared::Entities::NpcEntity *Adopt(std::uint64_t levelGuid, const SpawnParams &params);

        /** Despawns it everywhere, raising `npcDestroy` while it can still be read. */
        static void Destroy(Shared::Entities::NpcEntity *npc);

        // --- Identity ---------------------------------------------------------

        static void SetName(Shared::Entities::NpcEntity *npc, const std::string &name);
        static void SetAppearance(Shared::Entities::NpcEntity *npc, const Shared::Game::CharacterAppearance &appearance);

        /** Dresses it in one of the game's own clothing presets. False for an unknown preset. */
        static bool SetOutfit(Shared::Entities::NpcEntity *npc, const std::string &clothingPresetGuid);

        /**
         * Dresses it in exactly these item classes, each a dashed GUID; an
         * empty list leaves it what its soul owns, which for a role's soul is
         * only underwear. False, with each
         * refused GUID logged and nothing changed, when one is malformed or
         * names no item class this build has.
         */
        static bool SetWearing(Shared::Entities::NpcEntity *npc, const std::vector<std::string> &itemClassGuids);

        /** What it has been told to wear, as dashed item class GUIDs. */
        static std::vector<std::string> GetWearing(const Shared::Entities::NpcEntity *npc);

        static void SetFlags(Shared::Entities::NpcEntity *npc, Shared::Game::NpcFlags flags);

        /** What its tag draws when `ShowNametag` is on: which parts, the colour, and any text standing in for its name. */
        static void SetNametag(Shared::Entities::NpcEntity *npc, const Framework::Networking::Replication::NametagState &nametag);

        static void SetFaction(Shared::Entities::NpcEntity *npc, std::uint8_t factionId);
        static void SetLocomotion(Shared::Entities::NpcEntity *npc, Shared::Game::NpcLocomotion locomotion);

        // --- Ledger -----------------------------------------------------------

        static void SetHealth(Shared::Entities::NpcEntity *npc, float health);

        /**
         * Applies a hit an attacker's client resolved. Refused for an
         * invulnerable or already-dead body. Raises `npcDamage`, and
         * `npcDeath` when it takes the last of the health.
         */
        static void Damage(Shared::Entities::NpcEntity *npc, std::uint64_t attackerNetworkId, float amount);

        static void Kill(Shared::Entities::NpcEntity *npc, std::uint64_t attackerNetworkId);
        static void Revive(Shared::Entities::NpcEntity *npc);

        /** Moves the body outright, wherever it is and whoever is simulating it. */
        static void Teleport(Shared::Entities::NpcEntity *npc, const glm::vec3 &position, const glm::quat &rotation);

        // --- Intent -----------------------------------------------------------

        /**
         * The one place an order is given. Publishes it -- a new revision,
         * forced -- or queues the route it waits for, which publishes it
         * when planned. Throws for server pathfinding without a mesh, before
         * the previous order is touched.
         */
        static void SetIntent(Shared::Entities::NpcEntity *npc, const Shared::Game::NpcIntent &intent, Pathfinding pathfinding = Pathfinding::Auto);

        /** The script's order, including while a fallback walks mesh corners. */
        static Shared::Game::NpcIntentKind GetIntentKind(Shared::Entities::NpcEntity *npc);

        /** What has become of it, as scripts read it: see `NpcOrder::Status`. */
        static Shared::Game::NpcIntentStatus GetStatus(Shared::Entities::NpcEntity *npc);

        /** Throws when `pathfinding` asks for server routes and no mesh is loaded: the one place that is refused. */
        static void RequireMesh(Pathfinding pathfinding);

        /** The query options of every NPC route: open doors only, a bounded search, and `searchHeight` up or down. */
        static Navigation::QueryOptions RouteOptions(float searchHeight);

        static void Hold(Shared::Entities::NpcEntity *npc);
        static bool MoveTo(Shared::Entities::NpcEntity *npc, const glm::vec3 &point, Shared::Game::NpcSpeed speed, float arriveRadius, Pathfinding pathfinding = Pathfinding::Auto);
        /** Pursues a live player or NPC; another order stops the attack. */
        static bool Attack(Shared::Entities::NpcEntity *npc, std::uint64_t targetNetworkId, bool bow = false);

        static void Follow(Shared::Entities::NpcEntity *npc, std::uint64_t targetNetworkId, Shared::Game::NpcSpeed speed, float distance);
        /** Runs from `from`, or from the entity `threatNetworkId` names when it is not 0; false when that entity is not there or is `npc` itself. */
        static bool Flee(Shared::Entities::NpcEntity *npc, const glm::vec3 &from, std::uint64_t threatNetworkId, Shared::Game::NpcSpeed speed, float distance);
        static void LookAt(Shared::Entities::NpcEntity *npc, std::uint64_t targetNetworkId, const glm::vec3 &point);

        /**
         * Walks a route, one waypoint at a time. `waitSeconds` is how long it
         * stands at each. The route lives on the server: the intent only ever
         * names the waypoint being walked to, so a client that joins mid-patrol
         * sees one move rather than a plan it has to catch up with.
         *
         * False for an empty route, one longer than
         * `NpcOrder::kMaxPatrolPoints`, a waypoint outside the world or a
         * `waitSeconds` that is not a number; the wait is clamped to
         * `NpcOrder::kMaxPatrolWaitSeconds`. Throws as `SetIntent` does.
         */
        static bool Patrol(Shared::Entities::NpcEntity *npc, const std::vector<glm::vec3> &route, bool loop, Shared::Game::NpcSpeed speed, float waitSeconds, Pathfinding pathfinding = Pathfinding::Auto);
        static bool Patrol(Shared::Entities::NpcEntity *npc, const Shared::Game::PatrolRouteDefinition &route, Pathfinding pathfinding = Pathfinding::Auto);

        /** Stops a patrol without changing what the body is doing this moment. */
        static void ClearRoute(Shared::Entities::NpcEntity *npc);

        // --- One-shots --------------------------------------------------------

        static bool PlayAnimation(Shared::Entities::NpcEntity *npc, std::uint16_t animationId);
        static bool Speak(Shared::Entities::NpcEntity *npc, const std::string &text);

        // --- Simulation -------------------------------------------------------

        /** Pins the simulator to one player's client, or unpins with 0. */
        static void PinSimulator(Shared::Entities::NpcEntity *npc, std::uint64_t playerNetworkId);

        /** How many NPCs one client may simulate at once. See `kDefaultSimulationBudget`. */
        static void SetSimulationBudget(std::uint32_t perClient);

        /**
         * The default per-client budget.
         *
         * Not a bandwidth number: KCD2's AI LOD manager runs at most 70 bodies
         * in its Detail tier and the level's own population is already
         * competing for those slots, so a client asked to run more than a
         * couple of dozen mod NPCs would have them fake-moved by
         * `C_FakeMovementManager` instead of simulated.
         */
        static constexpr std::uint32_t kDefaultSimulationBudget = 24;

        // --- Lookup -----------------------------------------------------------

        static Shared::Entities::NpcEntity *Find(std::uint64_t networkId);
        static std::vector<Shared::Entities::NpcEntity *> Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld);
        static std::size_t DestroyAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld);
    };
} // namespace KCDC::Features::Npc
