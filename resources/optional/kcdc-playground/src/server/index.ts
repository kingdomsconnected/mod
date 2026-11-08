import { Actors } from "./actors.js";
import { config } from "./config.js";
import { craft, recipes } from "./crafting.js";
import { distance, integer, object, text, vector } from "./validation.js";

const RESOURCE = "kcdc-playground";
const actors = new Actors();
const items = Inventory.classes();
const itemById = new Map(items.map((item) => [item.id, item]));
const pois = World.pointsOfInterest();
interface Session { busy: boolean; last: number; generation: number; creator: boolean; drops: GroundItem[] }
const sessions = new Map<number, Session>();
let stopped = false;

function session(player: Player): Session {
    let state = sessions.get(player.id);
    if (!state) { state = { busy: false, last: 0, generation: 0, creator: false, drops: [] }; sessions.set(player.id, state); }
    return state;
}

function clear(player: Player): void {
    const state = sessions.get(player.id);
    if (state) {
        state.generation++;
        for (const drop of state.drops) GroundItem.getById(drop.id)?.destroy();
    }
    sessions.delete(player.id);
    actors.clear(player.id);
}

function reply(player: Player, id: number, data: unknown): void {
    if (!stopped && Player.getById(player.id)) player.emit(`${RESOURCE}:response`, JSON.stringify({ id, ...data as object }));
}

async function perform(player: Player, state: Session, request: Record<string, unknown>, active: () => boolean): Promise<Record<string, unknown>> {
    switch (request.action) {
        case "init":
            return { level: World.level, locations: [...pois.map((row, id) => ({ id: `poi:${id}`, name: row.name, group: row.group })), ...actors.locations().map((row, id) => ({ id: `trader:${id}`, name: row.name, group: "Playground traders" }))], roles: Npc.roles(), categories: [...new Set(items.map((row) => row.category))].sort() };
        case "items": {
            const query = text(request.query ?? "").toLowerCase();
            const category = text(request.category ?? "");
            const page = integer(request.page ?? 0, 0, 10000);
            const rows = items.filter((row) => (!category || row.category === category) && `${row.name} ${row.id}`.toLowerCase().includes(query));
            return { items: rows.slice(page * 48, (page + 1) * 48), total: rows.length, page };
        }
        case "item": {
            const item = itemById.get(text(request.item));
            if (!item) throw new Error("Choose a listed equippable item.");
            const quality = integer(request.quality, 1, item.maximumQuality);
            const amount = integer(request.amount, 1, 20);
            if (request.destination === "inventory") {
                const result = Inventory.add(player, { item: item.id, amount, metadata: { quality } });
                if (!result.ok) throw new Error(`Inventory refused the grant: ${result.code}.`);
                return { message: `Added ${amount} × ${item.name}, quality ${quality}, to your inventory.` };
            }
            if (request.destination !== "cursor") throw new Error("Choose inventory or cursor.");
            state.drops = state.drops.filter((drop) => GroundItem.getById(drop.id) !== null);
            if (state.drops.length + amount > config.maxDropsPerPlayer) throw new Error("Remove your playground drops before spawning more.");
            const aimed = vector(request.position);
            if (distance(aimed, player.position) > config.cursorRange) throw new Error("Aim at ground within 15 metres before opening the panel.");
            const floor = await World.resolveGround(player, aimed, { up: 0.5, down: 1 });
            if (!active()) throw new Error("Your character changed while placing the item.");
            if (!floor.answered || !floor.hit || floor.hit.normal.z < 0.65 || distance(floor.hit.position, player.position) > config.cursorRange) throw new Error("No nearby ground was confirmed at the cursor.");
            // One unit per replica also handles native non-stackable equipment.
            const created: GroundItem[] = [];
            try {
                for (let i = 0; i < amount; i++) created.push(GroundItem.spawn(item.id, new Vector3(floor.hit.position.x, floor.hit.position.y, floor.hit.position.z + 0.15 + i * 0.03), undefined, 1, player.virtualWorld, { quality }));
            } catch (error) {
                for (const drop of created) drop.destroy();
                throw error;
            }
            state.drops.push(...created);
            return { message: `Spawned ${amount} × ${item.name}, quality ${quality}, at the cursor.` };
        }
        case "teleport": {
            if (player.mounted || Dice.matchOf(player.id) || Crafting.session(player)) throw new Error("Dismount and leave the current activity before teleporting.");
            const id = text(request.location);
            const location = /^poi:\d+$/.test(id) ? pois[Number(id.slice(4))] : /^trader:\d+$/.test(id) ? actors.locations()[Number(id.slice(7))] : undefined;
            if (!location) throw new Error("That location is not in the current level.");
            const p = location.position;
            const destination = new Vector3(p.x, p.y, p.z + 0.1);
            if (id.startsWith("trader:")) destination.x += 1.5;
            if (!player.teleport(destination, location.name)) throw new Error("Teleport was refused.");
            return { message: `Teleport requested: ${location.name}.`, close: true };
        }
        case "recipes":
            return { recipes: recipes(player, request.kind).map((row) => ({ id: String(row.key.id), name: row.name, minSkill: row.minSkill, ingredients: row.ingredients })) };
        case "learn":
        case "materials":
            return { message: craft(player, request) };
        case "dice": {
            if (player.mounted || Dice.matchOf(player.id) || Crafting.session(player)) throw new Error("Dismount and leave the current activity before teleporting.");
            const response = object(await Messages.request("kcdc-gamemode", "playground:freeDice", { player: player.id }));
            if (!active()) throw new Error("Your character changed while checking dice tables.");
            if (response.error) throw new Error(String(response.error));
            // The game mode selects and rechecks its own table immediately before moving.
            return { message: text(response.message, 500), close: true };
        }
        case "spawn":
            return { message: await actors.spawn(player, request, active) };
        case "clear":
            actors.clear(player.id);
            for (const drop of state.drops) GroundItem.getById(drop.id)?.destroy();
            state.drops = [];
            return { message: "Removed your playground companions and ground drops. Inventory items are kept." };
        case "creator":
            if (player.mounted || Dice.matchOf(player.id) || Crafting.session(player)) throw new Error("Dismount and leave the current activity first.");
            state.creator = true;
            return { creator: true };
        case "appearance": {
            if (!state.creator) throw new Error("Open the playground character creator first.");
            state.creator = false;
            if (request.appearance === null) return { message: "Character creation cancelled." };
            const appearance = object(request.appearance);
            if (!player.setAppearance(appearance as Partial<Appearance>)) throw new Error("That appearance was refused.");
            return { message: "Your new character appearance is applied." };
        }
        default:
            throw new Error("Unknown playground action.");
    }
}

Events.onClient(`${RESOURCE}:request`, async (sender, payload) => {
    const player = sender as Player;
    let id = 0;
    let state: Session | undefined;
    let ownsBusy = false;
    try {
        const request = object(payload);
        id = integer(request.id, 1, 2147483647);
        if (stopped || !config.allowed(player)) throw new Error("Playground access is disabled for your character.");
        if (!player.ready || !player.alive) throw new Error("Wait until your character is alive and ready.");
        state = session(player);
        if (state.busy || (request.action !== "appearance" && Date.now() - state.last < 100)) throw new Error("Please wait for the previous request.");
        state.last = Date.now();
        state.busy = true;
        ownsBusy = true;
        const current = state;
        const world = player.virtualWorld;
        const generation = state.generation;
        const active = (): boolean => !stopped && sessions.get(player.id) === current && current.generation === generation && Player.getById(player.id) !== null && player.ready && player.alive && player.virtualWorld === world && config.allowed(player);
        const result = await perform(player, state, request, active);
        if (active()) reply(player, id, { ok: true, ...result });
    } catch (error) {
        reply(player, id, { ok: false, message: error instanceof Error ? error.message : String(error) });
    } finally {
        if (ownsBusy && state) state.busy = false;
    }
});

Events.on("playerDisconnect", clear);
Events.on("playerDied", clear);
Events.on("playerSpawning", clear);
Events.on("resourceStop", (name) => {
    if (name !== RESOURCE) return;
    stopped = true;
    for (const player of Player.all()) clear(player);
    actors.stop();
});

try { actors.install(); }
catch (error) { actors.stop(); throw error; }
console.log("Playground enabled: development cheats are available to allowed players.");
