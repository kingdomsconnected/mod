import { BOOK_EVENT } from "./book.js";
import type { Command, CommandContext } from "../command.js";

/** Types register on first command, exercising registration during play. */
class CustomItemDemo {
    private wood = "";
    private letter = "";
    private vendor = 0;

    install(): void {
        Events.on("playerCustomItemUse", (player, row, revision) => {
            if (row.item !== this.letter) return;
            const custom = row.metadata.custom as { data?: { sender?: string } } | undefined;
            const sender = custom?.data?.sender;
            if (sender !== "Anna" && sender !== "Hans") return;
            const text = sender === "Anna" ? "Henry, meet me by the mill. Anna." : "Your timber order is ready. Hans.";
            player.emit(BOOK_EVENT, JSON.stringify({ action: "customLetter", pages: [`<paragraph>${text}</paragraph>`] }));
            console.log("customitems.read", JSON.stringify({ player: player.id, row: row.id, revision, sender }));
        });
    }

    run({ player, args, reply, usage }: CommandContext): void {
        const action = args[0] ?? "grant";
        if (args.length > 1 || !["grant", "rename", "defaults", "vendor"].includes(action)) {
            usage();
            return;
        }
        if (!player.ready) {
            reply.line("Wait until your character has spawned.");
            return;
        }
        this.wood = Items.register("kcdc-demo:wood", { name: "Village timber", description: "Custom wood for the village", visual: "bsmt_woodExotic", weight: 2, price: 12 });
        this.letter = Items.register("kcdc-demo:letter", { name: "Letter", description: "A sealed letter", visual: "a54ab5ef-d4a2-4929-9045-1a1efde935c5", weight: 0.1, price: 1, stackable: false, usable: true, useLabel: "Read" });
        if (action === "vendor") {
            if (this.vendor === 0) this.vendor = Vendor.create({ name: "Custom item test stock", buys: true });
            if (!Vendor.setStock(this.vendor, [
                { item: this.wood, amount: 20, price: 12 },
                { item: this.letter, amount: 3, price: 1, metadata: { custom: { name: "Letter from Hans", description: "A vendor letter", data: { sender: "Hans" } } } },
            ])) {
                reply.line("Custom vendor stock could not be prepared.");
                return;
            }
            Vendor.setBuyPrices(this.vendor, [{ item: this.wood, price: 6 }, { item: this.letter, price: 1 }]);
            reply.line(Vendor.open(this.vendor, player.id) !== 0 ? "Custom stock opened. Close and repeat /customitems vendor to exercise stock cleanup." : "The vendor could not open; wait for inventory synchronization.");
            return;
        }
        if (action === "rename" || action === "defaults") {
            const inventory = Inventory.get(player);
            const row = inventory?.items.find((entry) => entry.item === this.letter);
            if (!row || !inventory) {
                reply.line("Grant a letter first with /customitems.");
                return;
            }
            const result = Inventory.updateCustom(player, { id: row.id, name: action === "rename" ? "Opened letter" : null, description: action === "rename" ? "Renamed while your inventory is open" : null, revision: inventory.revision });
            reply.line(result.ok ? "First letter updated; the other letter should keep its own text." : `Letter update refused: ${result.code}.`);
            return;
        }
        const wood = Inventory.add(player, { item: this.wood, amount: 10 });
        if (!wood.ok) {
            reply.line(`Wood grant refused: ${wood.code}.`);
            return;
        }
        for (const sender of ["Anna", "Hans"]) {
            const result = Inventory.add(player, { item: this.letter, amount: 1, metadata: { custom: { name: `Letter from ${sender}`, description: "Addressed to Henry", data: { sender } } } });
            if (!result.ok) reply.line(`Letter from ${sender} refused: ${result.code}.`);
        }
        reply.line("Granted 10 timber and requested two letters. Open inventory and try Read with primary action and double-click; letters remain after reading.");
    }
}

const demo = new CustomItemDemo();
export function installCustomItemDemo(): void {
    demo.install();
}

export const customItemsCommand: Command = {
    name: "customitems",
    summary: "Prepares custom wood, personalized letters and vendor stock for testing.",
    usage: ["/customitems [grant]", "/customitems rename", "/customitems defaults", "/customitems vendor"],
    run(context): void { demo.run(context); },
};
