import { MAX_ITEM_AMOUNT } from "../args.js";
import type { Command, CommandContext } from "../command.js";

const ALL_ALCHEMY_STEPS = 536870911;

/** Preparation uses the public catalog; it does not simulate a craft result. */
class CraftTest {
    private readonly prepared = new Map<number, CraftStationInfo>();
    private readonly positioning = new Set<number>();

    forget(player: Player): void {
        this.prepared.delete(player.id);
        this.positioning.delete(player.id);
    }

    private async approach(player: Player, station: CraftStationInfo): Promise<Vector3 | null> {
        // AlchemyTable:IsUsable dots local X against the clockwise-rotated
        // table-minus-player vector, so the usable side is local -Y.
        const side = station.kind === "alchemy" ? -1 : 1;
        for (const distance of [1.8, 1.2, 2.4]) {
            const x = station.position.x + station.forward.x * side * distance;
            const y = station.position.y + station.forward.y * side * distance;
            const footprint = [[0, 0], [0.35, 0], [-0.35, 0], [0, 0.35], [0, -0.35]] as const;
            // Keep the probe at the station's storey. A broad downward ray
            // can accept the terrain below a missing upstairs floor.
            const floors = await Promise.all(footprint.map(([dx, dy]) => World.resolveGround(player, new Vector3(x + dx, y + dy, station.position.z), { up: 0.45, down: 0.45 })));
            if (Player.getById(player.id) === null) return null;
            if (floors.some((floor) => !floor.answered)) return null;
            const floor = floors[0]?.hit;
            if (!floor || floors.some((entry) => !entry.hit || entry.hit.normal.z < 0.9 || Math.abs(entry.hit.position.z - floor.position.z) > 0.15)) continue;

            // Cross the body space in both directions: starting a ray in a
            // wall may fail to report the solid that contains its origin.
            const checks: Promise<WorldTraceResult>[] = [];
            for (const height of [0.25, 0.9, 1.7]) {
                for (const [dx, dy] of [[0.4, 0], [0, 0.4]] as const) {
                    const a = new Vector3(x - dx, y - dy, floor.position.z + height);
                    const b = new Vector3(x + dx, y + dy, floor.position.z + height);
                    checks.push(World.raycast(player, a, b), World.raycast(player, b, a));
                }
            }
            for (const [dx, dy] of footprint) checks.push(World.raycast(player, new Vector3(x + dx, y + dy, floor.position.z + 0.08), new Vector3(x + dx, y + dy, floor.position.z + 1.95)));
            const clearance = await Promise.all(checks);
            if (Player.getById(player.id) === null) return null;
            if (clearance.some((trace) => !trace.answered)) return null;
            if (clearance.some((trace) => trace.hit !== null)) continue;
            return new Vector3(x, y, floor.position.z + 0.08);
        }
        return null;
    }

    async check({ player, reply }: CommandContext): Promise<void> {
        // A resource reload forgets preparation, but a player still standing
        // at a catalogued station can check it without teleporting again.
        const distanceToPlayer = (station: CraftStationInfo): number => Math.hypot(station.position.x - player.position.x, station.position.y - player.position.y, station.position.z - player.position.z);
        const station = this.prepared.get(player.id) ?? [...Crafting.stations()].filter((entry) => distanceToPlayer(entry) <= 4).sort((a, b) => distanceToPlayer(a) - distanceToPlayer(b))[0];
        if (!station) {
            reply.line("Prepare a station with /crafttest alchemy or /crafttest smithing first.");
            return;
        }
        const centre = new Vector3(station.position.x, station.position.y, station.position.z);
        const result = await World.entitiesInRadius(player, centre, 12, { class: station.kind === "alchemy" ? "AlchemyTable" : "Smithery", max: 32 });
        if (Player.getById(player.id) === null) return;
        const guid = station.id.slice(station.id.indexOf(":") + 1);
        const found = result.entities.find((entity) => entity.guid === guid);
        let physical: WorldRayHit | undefined;
        if (!found) {
            // This Suchdol table is interactable and physicalized but absent
            // from QueryProximity. Identify its collider by the same GUID;
            // an empty spatial query alone never proves an unloaded station.
            const side = station.kind === "alchemy" ? -1 : 1;
            const probes = await Promise.all([0.4, 0.8].map((height) => {
                const from = new Vector3(station.position.x + station.forward.x * side * 1.8, station.position.y + station.forward.y * side * 1.8, station.position.z + height);
                const to = new Vector3(station.position.x - station.forward.x * side * 0.8, station.position.y - station.forward.y * side * 0.8, station.position.z + height);
                return World.raycastAll(player, from, to, { mode: "anything", maxHits: 8 });
            }));
            if (Player.getById(player.id) === null) return;
            physical = probes.flatMap((probe) => probe.hits).find((hit) => hit.entityGuid === guid);
        }
        const confirmed = found !== undefined || physical !== undefined;
        console.log("crafttest.station", JSON.stringify({ player: player.id, station: station.id, confirmed, detection: found ? "proximity" : physical ? "physics" : "unconfirmed", proximityAnswered: result.answered, reason: result.reason, found: found ?? physical ?? null }));
        reply.line(confirmed ? `Your client sees station ${station.id}.` : "The lookup could not confirm this station. A visible station and its use prompt can still confirm it is loaded.");
        reply.line(station.kind === "alchemy" ? "Use the table normally. The server owns ingredients and brewing results; another player cannot use an occupied table." : "Use the smithery normally. The server takes the materials when you press Craft and grants the product; another player cannot work an occupied smithery.");
    }

    async run(context: CommandContext): Promise<void> {
        if (this.positioning.has(context.player.id)) {
            context.reply.line("A crafting location check is already running. Wait for it to finish.");
            return;
        }
        this.positioning.add(context.player.id);
        try {
            await this.execute(context);
        } finally {
            this.positioning.delete(context.player.id);
        }
    }

    private async execute(context: CommandContext): Promise<void> {
        const { player, args, reply, usage } = context;
        if (args[0] === "check") {
            await this.check(context);
            return;
        }
        const moveOnly = args[0] === "move";
        const kind = args[moveOnly ? 1 : 0];
        if (kind !== "alchemy" && kind !== "smithing") {
            usage();
            return;
        }
        const stations = [...Crafting.stations(kind)].sort((a, b) => {
            const aCommon = a.layer.endsWith("/_common") ? 0 : 1;
            const bCommon = b.layer.endsWith("/_common") ? 0 : 1;
            return aCommon - bCommon || a.id.localeCompare(b.id);
        });
        if (!moveOnly && args[1] === "list") {
            for (const [index, station] of stations.entries()) reply.line(`${index + 1}: ${station.layer} (${station.id})`);
            if (stations.length === 0) reply.line(`This level has no ${kind} stations.`);
            return;
        }
        const rounds = moveOnly || args[1] === undefined ? 1 : Number(args[1]);
        const index = args[2] === undefined ? 1 : Number(args[2]);
        if (!Number.isInteger(rounds) || rounds < 1 || rounds > 10 || !Number.isInteger(index) || index < 1 || index > stations.length || args.length > 3) {
            reply.line(`Rounds must be 1–10; station must be 1–${stations.length}. /crafttest ${kind} list shows the locations.`);
            return;
        }
        if (!player.ready) {
            reply.line("Wait until your character has spawned.");
            return;
        }
        const station = stations[index - 1];
        if (!station) return;
        // A quest material comes only from its quest, and the inventory
        // refuses to grant one, so those recipes cannot be prepared here.
        const available = Crafting.recipes(player, kind);
        const recipes = available.filter((recipe) => !recipe.ingredients.some((ingredient) => ingredient.quest));
        const skipped = available.length - recipes.length;
        const materials = new Map<string, { name: string; amount: number }>();
        for (const recipe of recipes) {
            for (const ingredient of recipe.ingredients) {
                const current = materials.get(ingredient.item);
                materials.set(ingredient.item, { name: ingredient.name, amount: (current?.amount ?? 0) + ingredient.amount * rounds });
            }
        }
        if (materials.size === 0 || [...materials.values()].some((row) => !Number.isSafeInteger(row.amount) || row.amount < 1 || row.amount > MAX_ITEM_AMOUNT)) {
            reply.line("The selected material batch is empty or exceeds the item grant limit.");
            return;
        }

        reply.line("Checking the station approach for a floor and standing room...");
        const destination = await this.approach(player, station);
        if (Player.getById(player.id) === null) return;
        if (!destination) {
            reply.line("No clear approach with a loaded floor was found. You have not been moved or given items. Move closer to load the area, or choose another station.");
            return;
        }
        if (!player.teleport(destination, `${kind}: ${station.layer}`)) {
            reply.line("Could not send the teleport to your client.");
            return;
        }
        this.prepared.set(player.id, station);
        console.log("crafttest.position", JSON.stringify({ player: player.id, station: station.id, destination: { x: destination.x, y: destination.y, z: destination.z }, moveOnly }));
        if (moveOnly) {
            reply.line("Moved to the checked station approach without granting ingredients. Use /crafttest check to check the station itself.");
            reply.line("Use the station normally once inventory synchronization is ready.");
            return;
        }
        const granted: { item: string; name: string; amount: number }[] = [];
        const refused: string[] = [];
        for (const [item, material] of materials) {
            if (player.giveItem(item, material.amount)) granted.push({ item, ...material });
            else refused.push(material.name);
        }
        // Alchemy uses a mask of 29 recipe steps; smithing uses unlock perks.
        const knowledge = kind === "alchemy" ? Crafting.knowledge(player) : {};
        const taught = kind === "alchemy"
            ? recipes.filter((recipe) => knowledge[recipe.key.id] !== ALL_ALCHEMY_STEPS && Crafting.setKnowledge(player, recipe.key.id, ALL_ALCHEMY_STEPS)).length
            : recipes.filter((recipe) => recipe.perk !== "" && !player.hasPerk(recipe.perk) && player.addPerk(recipe.perk)).length;
        console.log("crafttest.prepared", JSON.stringify({ player: player.id, kind, station: station.id, rounds, recipes: recipes.map((recipe) => recipe.key), skipped, granted, refused, taught }));
        reply.line(`Teleport requested. Committed ${granted.length} ingredient classes to your server inventory for ${rounds} batch(es) of ${recipes.length} enabled ${kind} recipes.`);
        if (taught > 0) reply.line(kind === "alchemy" ? `Taught ${taught} alchemy recipes.` : `Taught ${taught} smithing recipe perks.`);
        if (skipped > 0) reply.line(`Skipped ${skipped} recipes that need a quest item; their quest provides it.`);
        if (refused.length > 0) reply.line(`Grant requests refused: ${refused.join(", ")}. See the server log for the batch.`);
        reply.line(kind === "alchemy" ? "Wait for the inventory update, then use the table normally. Brewing results are committed by the server." : "Wait for the inventory update, then use the smithery normally. The server takes the materials on Craft and grants the product.");
        reply.line("Once the area loads, use /crafttest check. Repeating preparation gives another batch.");
    }
}

const test = new CraftTest();
Events.on("playerDisconnect", (player) => test.forget(player));

export const craftTestCommand: Command = {
    name: "crafttest",
    summary: "Checks crafting station placement, grants test ingredients and teaches recipes. The server owns ingredients and results for both.",
    usage: ["/crafttest <alchemy|smithing> [rounds=1] [station=1]", "/crafttest move <alchemy|smithing> [station=1]", "/crafttest <alchemy|smithing> list", "/crafttest check"],
    run: (context) => test.run(context),
};
