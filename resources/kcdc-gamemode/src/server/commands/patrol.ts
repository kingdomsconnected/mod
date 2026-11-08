import type { Command, CommandContext } from "../command.js";
import { npcInReach } from "../place.js";

/** Exported routes become available after the server loads world resources. */
export function installPatrolSample(): void {
    const ready = (): void => {
        const routes = WorldResource.all().flatMap(resource => resource.patrolRoutes);
        console.log(`[patrol] ${routes.length} exported route(s): ${routes.map(route => route.id).join(", ") || "none"}`);
    };
    if (WorldResource.ready) ready();
    else Events.once("worldResourcesReady", ready);
}

export const patrolCommand: Command = {
    name: "patrol",
    summary: "Lists World Builder routes and assigns them to nearby NPCs.",
    usage: ["/patrol list", "/patrol start <route-id>", "/patrol validate <route-id>", "/patrol status", "/patrol stop"],
    run({ player, args, reply }: CommandContext): void {
        const action = args[0] ?? "list";
        if (action === "list") {
            const routes = PatrolRoute.all();
            if (!routes.length) reply.line("No patrol routes loaded. Export a World Builder project and add its .world.json to mod.world_resources.");
            for (const route of routes) {
                const definition = route.toJSON();
                if (definition) reply.line(`${route.id}: ${route.name || "Unnamed route"}, ${definition.points.length} waypoint(s), ${definition.mode}.`);
            }
            return;
        }
        if (!["start", "validate", "status", "stop"].includes(action)) {
            reply.line("Use /patrol list, start <route-id>, validate <route-id>, status or stop.");
            return;
        }
        const npc = npcInReach(player);
        if (!npc && action !== "validate") {
            reply.line("No NPC nearby. Spawn one with /npc or stand next to one.");
            return;
        }
        if (action === "status") {
            const state = npc!.patrolState;
            reply.line(state ? `NPC ${npc!.id}: ${state.routeId ?? "anonymous route"}, waypoint ${state.waypointIndex + 1}, lap ${state.lap + 1}, ${state.direction > 0 ? "forward" : "backward"}, ${state.phase}.` : `NPC ${npc!.id} is not patrolling.`);
            return;
        }
        if (action === "stop") {
            npc!.hold();
            reply.line(`NPC ${npc!.id} stopped patrolling.`);
            return;
        }
        const route = args[1] ? PatrolRoute.getById(args[1]) : null;
        if (!route) {
            reply.line("Unknown route. Use /patrol list for IDs.");
            return;
        }
        if (action === "validate") {
            const result = Navigation.validatePatrol(route, npc ? { actor: npc } : undefined);
            if (!result.available) reply.line("Navigation mesh unavailable. Supply the level's recast.pak to the server.");
            else if (result.complete) reply.line(`All ${result.legs.length} leg(s) are reachable.`);
            else for (const leg of result.legs) if (!leg.complete) reply.line(`Waypoint ${leg.fromIndex + 1} to ${leg.toIndex + 1}: ${leg.path ? "partial path" : "no path"}.`);
            return;
        }
        reply.line(npc!.patrol(route.id) ? `NPC ${npc!.id} started ${route.id}.` : `NPC ${npc!.id} refused the route.`);
    },
};
