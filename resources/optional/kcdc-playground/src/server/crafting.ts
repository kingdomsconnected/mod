import { integer, text } from "./validation.js";

const ALL_STEPS = 536870911;

export function recipes(player: Player, value: unknown): readonly CraftRecipeInfo[] {
    const kind = text(value);
    if (kind !== "alchemy" && kind !== "smithing") throw new Error("Choose alchemy or smithing.");
    return Crafting.recipes(player, kind).filter((row) => !row.ingredients.some((ingredient) => ingredient.quest));
}

export function craft(player: Player, request: Record<string, unknown>): string {
    const available = recipes(player, request.kind);
    const selected = request.recipe === "all" ? available : available.filter((row) => String(row.key.id) === request.recipe);
    if (!selected.length) throw new Error("No enabled recipes match this selection.");
    if (request.action === "learn") {
        let taught = 0;
        let refused = 0;
        const known = Crafting.knowledge(player);
        for (const recipe of selected) {
            if (recipe.key.kind === "alchemy") {
                if (known[recipe.key.id] === ALL_STEPS) continue;
                if (Crafting.setKnowledge(player, recipe.key.id, ALL_STEPS)) taught++; else refused++;
            } else if (recipe.perk && !player.hasPerk(recipe.perk)) {
                if (player.addPerk(recipe.perk)) taught++; else refused++;
            }
        }
        return `Learned ${taught} recipe unlocks. ${refused} refused. Skill requirements still apply.`;
    }
    const rounds = integer(request.rounds, 1, 10);
    const materials = new Map<string, { name: string; amount: number }>();
    for (const recipe of selected) {
        for (const ingredient of recipe.ingredients) {
            const previous = materials.get(ingredient.item)?.amount ?? 0;
            materials.set(ingredient.item, { name: ingredient.name, amount: previous + ingredient.amount * rounds });
        }
    }
    for (const material of materials.values()) integer(material.amount, 1, 10000);
    let granted = 0;
    const refused: string[] = [];
    for (const [item, material] of materials) {
        const result = Inventory.add(player, { item, amount: material.amount });
        if (result.ok) granted++; else refused.push(`${material.name} (${result.code})`);
    }
    return `Granted ${granted} material types for ${rounds} batch(es).${refused.length ? ` Refused: ${refused.join(", ")}.` : ""}`;
}
