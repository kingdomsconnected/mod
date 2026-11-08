import { nearestPot, potRecipes, potRefusal } from "../cook-pots.js";
import type { Command } from "../command.js";

export const potCommand: Command = {
    name: "pot",
    summary: "Inspect, refill or poison a nearby food pot.",
    usage: ["/pot", "/pot recipes", "/pot refill <porridge|vegetable|meat>", "/pot poisons", "/pot poison <inventory row>"],
    run({ player, args, reply, usage }): void {
        const action = args[0];
        if (action === "recipes" && args.length === 1) {
            for (const [name, entry] of Object.entries(potRecipes)) {
                reply.line(`${name}: ${entry.recipe.portions} portions of ${entry.label.toLowerCase()}, using ${entry.recipe.ingredients.map(i => `${i.amount} x ${i.item}`).join(" + ")}.`);
            }
            return;
        }
        if (action === "poisons" && args.length === 1) {
            const poisons = new Set(CookPot.poisons());
            const rows = Inventory.get(player)?.items.filter(row => poisons.has(row.item)) ?? [];
            if (rows.length === 0) reply.line("You are not carrying any poison.");
            for (const row of rows) reply.line(`${row.id}: ${row.name} (${row.amount}). Use /pot poison ${row.id}.`);
            return;
        }
        if ((action !== undefined && action !== "refill" && action !== "poison") || (action !== undefined && args.length !== 2)) {
            usage();
            return;
        }
        const pot = nearestPot(player);
        if (!pot) {
            reply.line("There is no food pot within four metres.");
            return;
        }
        if (action === undefined) {
            const label = Object.values(potRecipes).find(entry => entry.recipe.food === pot.food)?.label ?? pot.food;
            reply.line(`${label}: ${pot.portions} portions, ${pot.nutrition} nutrition each. ${pot.enabled ? "" : "Unavailable. "}Use /pot recipes for refill ingredients.`);
            return;
        }
        if (action === "refill") {
            const name = args[1]!.toLowerCase();
            const entry = Object.hasOwn(potRecipes, name) ? potRecipes[name] : undefined;
            if (!entry) {
                usage();
                return;
            }
            const result = CookPot.refillFromInventory(player, pot.guid, entry.recipe);
            reply.line(result.ok ? `Refilled the pot with ${entry.recipe.portions} portions of ${entry.label.toLowerCase()}.` : potRefusal(result.code));
            return;
        }
        const result = CookPot.poison(player, pot.guid, args[1]!);
        reply.line(result.ok ? "Added one dose of poison to the pot." : potRefusal(result.code));
    },
};
