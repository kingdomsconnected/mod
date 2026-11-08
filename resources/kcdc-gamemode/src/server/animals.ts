/** Bounded animal populations at selected level spawnpoints. */

import { distanceBetween } from "./place.js";
import { spawnNpc } from "./npc-spawn.js";

export type AnimalSpawnpoint = ReturnType<typeof Npc.animalSpawnpoints>[number];

/** null enables all populated sites; use GUIDs for selected sites or [] to disable. */
export const animalSpawnpoints: string[] | null = null;

export const animalPopulationSettings = {
    maxAnimals: 256,
    maxPerPoint: 4,
    respawnMs: 5 * 60_000,
    respawnJitterMs: 60_000,
    corpseMs: 90_000,
    retryMs: 15_000,
    hiddenDistance: 60,
    activationDistance: 350,
    roamRadius: 20,
    spawnRadius: 8,
    aggroRadius: 18,
    leashRadius: 45,
    spawnsPerTick: 2,
    queriesPerTick: 4,
};

interface AnimalSlot {
    id: number | null;
    state: "vacant" | "alive" | "corpse";
    respawnAt: number;
    corpseUntil: number;
    moveAt: number;
    target: number | null;
}

interface AnimalSite {
    point: AnimalSpawnpoint;
    world: number;
    home: Vector3 | null;
    slots: AnimalSlot[];
}

/** Match MafiaNet visibility: the global world sees and is seen by every world. */
function worldsOverlap(a: number, b: number): boolean {
    return a === b || a === 0xFFFFFFFF || b === 0xFFFFFFFF;
}

export class AnimalPopulation {
    private readonly sites = new Map<string, AnimalSite>();
    private running = true;
    private cursor = 0;

    constructor(private readonly settings = animalPopulationSettings, private readonly now = () => Date.now(), private readonly random = () => Math.random()) {}

    add(point: AnimalSpawnpoint, world: number): boolean {
        const key = `${point.guid}:${world}`;
        const count = Math.min(this.settings.maxPerPoint, Math.max(0, Math.floor(point.count)));
        if (!this.running || this.sites.has(key) || count === 0) return false;
        this.sites.set(key, {
            point, world, home: null,
            slots: Array.from({ length: count }, () => ({ id: null, state: "vacant", respawnAt: 0, corpseUntil: 0, moveAt: 0, target: null })),
        });
        return true;
    }

    private delay(): number {
        return this.settings.respawnMs + this.random() * this.settings.respawnJitterMs;
    }

    /** Only the first transition starts a cooldown; repeated events do not extend it. */
    lost(id: number, dead: boolean): void {
        if (!this.running) return;
        for (const site of this.sites.values()) {
            for (const slot of site.slots) {
                if (slot.id !== id) continue;
                if (slot.state === "alive") {
                    slot.respawnAt = this.now() + this.delay();
                    slot.corpseUntil = this.now() + this.settings.corpseMs;
                    slot.state = dead ? "corpse" : "vacant";
                    slot.target = null;
                }
                if (!dead) {
                    slot.id = null;
                    slot.state = "vacant";
                }
            }
        }
    }

    tick(): void {
        if (!this.running || this.sites.size === 0) return;
        const now = this.now();
        // Read native-backed properties once per player, not once per animal.
        const players = Player.all().filter(player => player.ready).map(player => ({ player, position: player.position, world: player.virtualWorld, canAct: player.canAct }));
        const sites = [...this.sites.entries()];
        let bodies = sites.reduce((sum, [, site]) => sum + site.slots.reduce((count, slot) => count + Number(slot.id !== null), 0), 0);
        const despawnDistance = Math.max(this.settings.activationDistance, this.settings.hiddenDistance);
        let spawns = this.settings.spawnsPerTick;
        let queries = this.settings.queriesPerTick;
        for (let i = 0; i < sites.length; ++i) {
            const [key, site] = sites[(this.cursor + i) % sites.length]!;
            const visiblePlayers = players.filter(player => worldsOverlap(player.world, site.world));
            const unseen = (point: Vector3) => visiblePlayers.every(player => distanceBetween(player.position, point) >= this.settings.hiddenDistance);
            const near = visiblePlayers.some(player => distanceBetween(player.position, site.point.position) < this.settings.activationDistance);
            const canSpawn = near && unseen(site.point.position);
            for (const slot of site.slots) {
                if (!this.running || this.sites.get(key) !== site) break;
                let npc = slot.id === null ? null : Npc.getById(slot.id);
                if (slot.id !== null && !npc) {
                    this.lost(slot.id, false);
                    bodies -= 1;
                }
                if (npc && !npc.alive && slot.state === "alive") this.lost(npc.id, true);
                const position = npc?.position;
                if (npc && position && slot.state === "alive" && !near && visiblePlayers.every(player => distanceBetween(player.position, position) >= despawnDistance)) {
                    const distant = npc;
                    slot.id = null;
                    slot.state = "vacant";
                    slot.target = null;
                    slot.respawnAt = now;
                    bodies -= 1;
                    distant.remove();
                    if (!this.running || this.sites.get(key) !== site) break;
                    continue;
                }
                if (npc && slot.state === "corpse") {
                    if (now < slot.corpseUntil || !unseen(npc.position)) continue;
                    const corpse = npc;
                    // Detach first: npcDestroy fires synchronously from remove.
                    slot.id = null;
                    slot.state = "vacant";
                    npc = null;
                    bodies -= 1;
                    corpse.remove();
                    if (!this.running || this.sites.get(key) !== site) break;
                }
                if (!npc) {
                    if (now < slot.respawnAt || bodies >= this.settings.maxAnimals || spawns <= 0 || queries <= 0 || !Navigation.ready) continue;
                    // Keep a cooldown even when there is no mesh point or a spawn fails.
                    slot.respawnAt = now + this.settings.retryMs;
                    if (!canSpawn) continue;
                    if (!site.home) {
                        queries -= 1;
                        site.home = Navigation.closestPoint(site.point.position, { searchRadius: 12, searchHeight: 20, doors: "none" });
                    }
                    if (!site.home || queries <= 0) continue;
                    queries -= 1;
                    const position = Navigation.randomPoint(site.home, this.settings.spawnRadius, { doors: "none" });
                    if (!position || !unseen(position)) continue;
                    // Do not stack animals on the same point or a retained corpse.
                    const occupied = [...this.sites.values()].some(other => worldsOverlap(other.world, site.world) && other.slots.some(entry => {
                        const body = entry.id === null ? null : Npc.getById(entry.id);
                        return body !== null && distanceBetween(body.position, position) < 2;
                    }));
                    if (occupied) continue;
                    spawns -= 1;
                    try {
                        npc = spawnNpc({ soul: site.point.soul, class: site.point.actorClass, position, virtualWorld: site.world, nametag: false, interactable: false, lootable: true }, this.random);
                        // npcSpawn can stop/clear the sample before create returns.
                        if (!this.running || this.sites.get(key) !== site) {
                            npc.remove();
                            break;
                        }
                        slot.id = npc.id;
                        bodies += 1;
                        slot.state = "alive";
                        slot.moveAt = now + 3_000 + this.random() * 7_000;
                        slot.target = null;
                    } catch (error) {
                        console.log(`Animal spawn at ${site.point.guid} failed: ${String(error)}`);
                    }
                    continue;
                }
                if (!npc.alive || !site.home) continue;
                const predator = site.point.actorClass === "Wolf" || site.point.actorClass === "WildDog";
                const away = distanceBetween(npc.position, site.home) > this.settings.leashRadius;
                if (predator && !away) {
                    let target: Player | null = null;
                    let nearest = this.settings.aggroRadius;
                    const position = npc.position;
                    for (const candidate of visiblePlayers) {
                        if (!candidate.canAct || distanceBetween(candidate.position, site.home) > this.settings.leashRadius) continue;
                        const range = distanceBetween(candidate.position, position);
                        if (candidate.player.id === slot.target && range <= this.settings.leashRadius) {
                            target = candidate.player;
                            break;
                        }
                        if (range <= nearest) {
                            nearest = range;
                            target = candidate.player;
                        }
                    }
                    if (target) {
                        const retry = (npc.status === "blocked" || npc.status === "failed") && now >= slot.moveAt;
                        if (slot.target !== target.id || retry) {
                            if (npc.attack(target)) slot.target = target.id;
                            slot.moveAt = now + this.settings.retryMs;
                        }
                        if (slot.target === target.id) continue;
                    }
                }
                if (slot.target !== null) {
                    slot.target = null;
                    npc.hold();
                    slot.moveAt = now;
                }
                if (now < slot.moveAt || npc.status === "running" || (!away && queries <= 0) || !Navigation.ready) continue;
                slot.moveAt = now + 5_000 + this.random() * 10_000;
                if (!away) queries -= 1;
                const point = away ? site.home : Navigation.randomPoint(site.home, this.settings.roamRadius, { doors: "none" });
                if (point) npc.moveTo(point, { speed: "walk", pathfinding: "server", radius: 1.5 });
            }
        }
        this.cursor = sites.length > 0 ? (this.cursor + 1) % sites.length : 0;
    }

    clear(): void {
        const ids = [...this.sites.values()].flatMap(site => site.slots.flatMap(slot => slot.id === null ? [] : [slot.id]));
        this.sites.clear();
        for (const id of ids) Npc.getById(id)?.remove();
    }

    stop(): void {
        this.running = false;
        this.clear();
    }
}

export const animalPopulation = new AnimalPopulation();

export function installAnimalPopulation(): void {
    const catalog = new Map(Npc.animalSpawnpoints().map(point => [point.guid, point]));
    for (const guid of animalSpawnpoints ?? catalog.keys()) {
        const point = catalog.get(guid);
        if (point) animalPopulation.add(point, 0);
        else console.log(`Animal spawnpoint ${guid} is not in this level.`);
    }
    const timer = setInterval(() => animalPopulation.tick(), 1000);
    Events.on("npcDeath", npc => animalPopulation.lost(npc.id, true));
    Events.on("npcDestroy", npc => animalPopulation.lost(npc.id, false));
    Events.on("resourceStop", name => {
        if (name !== "kcdc-gamemode") return;
        clearInterval(timer);
        animalPopulation.stop();
    });
}
