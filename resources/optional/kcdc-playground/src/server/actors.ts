import { config } from "./config.js";
import { distance, text } from "./validation.js";

interface Owned {
    npcs: Npc[];
    horse?: Horse;
    dog?: Dog;
}

export class Actors {
    private readonly owned = new Map<number, Owned>();
    private readonly traders = new Map<number, { npc: Npc; vendor: number }>();

    install(): void {
        for (const placement of Npc.placements()) {
            const entry = config.traders.find((row) => row.placement === placement.name);
            if (!entry) continue;
            const q = placement.rotation;
            const npc = Npc.create({ soul: placement.soul, class: placement.actorClass, name: entry.name, position: placement.position, rotation: new Quaternion(q.x, q.y, q.z, q.w), invulnerable: true, interactable: true, nametag: true });
            npc.hold();
            let vendor: number;
            try { vendor = Vendor.create({ name: entry.name, purse: 10000, buys: true }); }
            catch (error) { npc.remove(); throw error; }
            this.traders.set(npc.id, { npc, vendor });
            const pool = Inventory.classes().filter((item) => !item.quest);
            const stock: VendorStockRow[] = [{ item: "bread", amount: 20, price: 5 }, { item: "apple", amount: 20, price: 2 }];
            for (let i = 0; i < 10 && pool.length; i++) {
                const [item] = pool.splice(Math.floor(Math.random() * pool.length), 1);
                if (item) stock.push({ item: item.id, amount: 3, price: Math.max(1, Math.round(Inventory.getItemPrice(item.id)?.current ?? 10)) });
            }
            if (!Vendor.setStock(vendor, stock)) throw new Error(`Could not stock ${entry.name}.`);
        }
        Events.on("npcInteract", (npc, player) => {
            const trader = this.traders.get(npc.id);
            if (!trader || !config.allowed(player) || !player.ready || !player.alive || distance(player.position, npc.position) > 4) return;
            if (npc.virtualWorld !== -1 && npc.virtualWorld !== player.virtualWorld) return;
            npc.lookAt(player);
            Vendor.open(trader.vendor, player.id, npc.id);
        });
    }

    locations(): { name: string; position: Vector3 }[] {
        return [...this.traders.values()].map(({ npc }) => ({ name: npc.name, position: npc.position }));
    }

    async spawn(player: Player, request: Record<string, unknown>, active: () => boolean): Promise<string> {
        const kind = text(request.kind);
        if (!["npc", "horse", "dog"].includes(kind)) throw new Error("Unknown companion type.");
        let owned = this.owned.get(player.id);
        if (!owned) { owned = { npcs: [] }; this.owned.set(player.id, owned); }
        owned.npcs = owned.npcs.filter((npc) => Npc.getById(npc.id) !== null);
        const role = kind === "npc" ? text(request.role) : "";
        if (kind === "npc" && (!Npc.roles().includes(role) || owned.npcs.length >= config.maxNpcsPerPlayer)) throw new Error("Choose a listed NPC role, or remove your test NPCs first.");
        if (kind === "horse" && player.horse) throw new Error("You already own a horse.");
        if (kind === "dog" && player.dog) throw new Error("You already own a dog.");
        const facing = (player.rotation as Quaternion).rotateVector(new Vector3(0, 1, 0));
        const length = Math.hypot(facing.x, facing.y) || 1;
        const position = new Vector3(player.position.x + facing.x / length * 3, player.position.y + facing.y / length * 3, player.position.z);
        const floor = await World.resolveGround(player, position, { up: 1, down: 3 });
        if (!active()) throw new Error("Your character changed while placing the companion.");
        if (!floor.answered || !floor.hit || floor.hit.normal.z < 0.7) throw new Error("Look toward nearby clear ground and try again.");
        if (kind === "horse" && player.horse) throw new Error("You already own a horse.");
        if (kind === "dog" && player.dog) throw new Error("You already own a dog.");
        position.z = floor.hit.position.z + 0.1;
        if (kind === "npc") {
            const npc = Npc.create({ soul: role, name: `Test ${role}`, position, virtualWorld: player.virtualWorld, nametag: true });
            owned.npcs.push(npc);
            if (request.follow !== true) npc.hold();
            if (request.follow === true && !npc.follow(player, { radius: 2.5 })) return "NPC spawned, but its follow order was refused.";
            return request.follow === true ? "NPC spawned and ordered to follow you." : "NPC spawned and standing by.";
        }
        if (kind === "horse") {
            const horse = Horse.spawn(position, player.rotation, undefined, "Playground horse");
            owned.horse = horse;
            horse.setVirtualWorld(player.virtualWorld);
            horse.giveTo(player);
            horse.destroyWithOwner = true;
            return "Your playground horse is ready.";
        }
        const dog = Dog.spawn(player, position, player.rotation, undefined, "Playground dog");
        owned.dog = dog;
        dog.setVirtualWorld(player.virtualWorld);
        dog.destroyWithOwner = true;
        dog.mode = 1;
        return "Your playground dog is following you.";
    }

    clear(playerId: number): void {
        const owned = this.owned.get(playerId);
        this.owned.delete(playerId);
        if (!owned) return;
        for (const npc of owned.npcs) Npc.getById(npc.id)?.remove();
        if (owned.horse && Horse.getById(owned.horse.id)) owned.horse.destroy();
        if (owned.dog && Dog.getById(owned.dog.id)) owned.dog.destroy();
    }

    stop(): void {
        for (const id of this.owned.keys()) this.clear(id);
        for (const { npc, vendor } of this.traders.values()) { Vendor.destroy(vendor); Npc.getById(npc.id)?.remove(); }
        this.traders.clear();
    }
}
