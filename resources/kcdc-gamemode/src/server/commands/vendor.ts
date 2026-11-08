import { spawnNpc } from "../npc-spawn.js";
import type { Command, CommandContext } from "../command.js";
import { inFrontOf } from "../place.js";

/**
 * A worked example of a vendor, and the shape every other one takes.
 *
 * A vendor is a price list and a purse, attached to nothing. What makes this
 * one a market stall is the handler below: talking to the NPC it spawned
 * opens the vendor with that NPC keeping the shop. The same vendor could just
 * as well be opened from a dialogue option or a command, and `/vendor here`
 * does exactly that, with nobody behind the counter.
 *
 * Prices are money units -- the amount of the game's `money` item, which is
 * also what `/give money 100` hands out.
 */

/** The stall's wares: what it sells and what it pays for. */
const STOCK: VendorStockRow[] = [
    { item: "bread", amount: 20, price: 30 },
    { item: "apple", amount: 30, price: 10 },
    { item: "beer", amount: 12, price: 45 },
    { item: "wineCheap", amount: 6, price: 90 },
    { item: "arrow_normal", amount: 60, price: 8 },
];

const BUYS: VendorBuyRow[] = [
    { item: "apple", price: 4 },
    { item: "carrot", price: 3 },
    { item: "bread", price: 12 },
];

/** Which vendor each spawned stall-keeper trades for, by NPC id. */
const stalls = new Map<number, number>();

/** The vendor `/vendor here` opens, made on first use. */
let pedlar = 0;

function makeVendor(purse: number | undefined): number {
    const vendor = Vendor.create({ name: "Sample stall", purse, buys: true });
    Vendor.setStock(vendor, STOCK);
    Vendor.setBuyPrices(vendor, BUYS);
    return vendor;
}

function stall({ player, reply }: CommandContext): void {
    const { position, rotation } = inFrontOf(player, 2.5);
    const npc = spawnNpc({
        soul: "townsman",
        position,
        rotation,
        name: "Stallholder Ondra",
        interactable: true,
        nametag: true,
    });
    // A purse, so the stall can run out of coin buying from players.
    const vendor = makeVendor(500);
    stalls.set(npc.id, vendor);
    reply.line(`Spawned a stall (vendor ${vendor}). Talk to Ondra to trade; he holds 500 money units.`);
}

function here({ player, reply }: CommandContext): void {
    if (pedlar === 0) {
        pedlar = makeVendor(undefined);
    }
    const session = Vendor.open(pedlar, player.id);
    reply.line(session !== 0 ? `Opened vendor ${pedlar} (session ${session}), with a purse that never runs out.` : "Could not open the vendor.");
}

function reset({ reply }: CommandContext): void {
    let removed = 0;
    for (const [npcId, vendor] of stalls) {
        Vendor.destroy(vendor);
        Npc.getById(npcId)?.remove();
        removed += 1;
    }
    stalls.clear();
    if (pedlar !== 0) {
        Vendor.destroy(pedlar);
        pedlar = 0;
    }
    reply.line(`Removed ${removed} stall(s).`);
}

export function installVendorSample(): void {
    Events.on("npcInteract", (npc, player) => {
        const vendor = stalls.get(npc.id);
        if (vendor === undefined) {
            return;
        }
        npc.lookAt(player);
        Vendor.open(vendor, player.id, npc.id);
    });

    Events.on("vendorTrade", (vendor, player, bought, sold, balance) => {
        const describe = (lines: VendorTradeLine[]): string => lines.map((line) => `${line.amount}x ${line.name}`).join(", ") || "nothing";
        console.log(`vendorTrade: ${player.nickname} at vendor ${vendor} bought ${describe(bought)}, sold ${describe(sold)}, balance ${balance}`);
    });

    Events.on("vendorClosed", (vendor, player, reason) => {
        const why = ["closed by the player", "closed by the server", "replaced by another vendor", "unavailable on the client", "the player left"][reason] ?? `reason ${reason}`;
        console.log(`vendorClosed: ${player?.nickname ?? "a player"} at vendor ${vendor} -- ${why}`);
    });
}

export const vendorCommand: Command = {
    name: "vendor",
    summary: "Trades on the game's own shop screen, at prices the server sets.",
    usage: ["/vendor stall", "/vendor here", "/vendor reset"],
    run(context: CommandContext): void {
        switch (context.args[0]) {
            case "stall":
                stall(context);
                return;
            case "here":
                here(context);
                return;
            case "reset":
                reset(context);
                return;
            default:
                context.usage();
        }
    },
};
