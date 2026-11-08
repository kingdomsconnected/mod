/** Example recipes, chosen by this game mode. Other servers can supply their own. */
export const potRecipes: Readonly<Record<string, { label: string; recipe: CookPotRecipe }>> = {
    porridge: { label: "Porridge", recipe: { food: "lentil", portions: 4, ingredients: [{ item: "breadroll", amount: 4 }] } },
    vegetable: { label: "Vegetable stew", recipe: { food: "soup", portions: 4, ingredients: [{ item: "cabbageCooked", amount: 4 }, { item: "carrotCooked", amount: 4 }] } },
    meat: { label: "Meat stew", recipe: { food: "goulash", portions: 4, ingredients: [{ item: "beefCooked", amount: 4 }] } },
};

export function nearestPot(player: Player): CookPotSnapshot | undefined {
    const world = player.virtualWorld === 0xffffffff ? 0 : player.virtualWorld;
    const distance = (pot: CookPotSnapshot): number => Math.hypot(pot.position.x - player.position.x, pot.position.y - player.position.y, pot.position.z - player.position.z);
    return CookPot.all(world).filter(pot => distance(pot) <= 4).sort((a, b) => distance(a) - distance(b))[0];
}

export function potRefusal(code: string): string {
    const messages: Readonly<Record<string, string>> = {
        not_empty: "Finish the food in this pot before refilling it.",
        empty: "An empty pot cannot be poisoned.",
        disabled: "This pot is unavailable.",
        too_far: "Move within four metres of the pot.",
        cannot_act: "You cannot do that right now.",
        inventory_not_ready: "Wait for your inventory to finish updating, then try again.",
        missing_ingredients: "You do not have the required food. Use /pot recipes to see the ingredients.",
        already_poisoned: "The pot already contains poison.",
        not_poison: "Choose a poison from /pot poisons.",
        unknown_item: "That item is no longer in your inventory.",
    };
    return messages[code] ?? "The pot could not be changed.";
}
