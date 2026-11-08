/** Default gamemode policy. These percentages are deliberately editable here. */
export const animalDropChance = { meat: 100, specialPart: 50 } as const;

/** Creates stock once, before any client can see or butcher the new animal. */
export function spawnNpc(options: Parameters<typeof Npc.create>[0], random: () => number = Math.random): Npc {
    const npc = Npc.create(options);
    try {
        const candidates = npc.getAnimalLoot();
        if (candidates === null) return npc;
        const sample = (): number => {
            const value = random();
            if (!Number.isFinite(value) || value < 0 || value >= 1) throw new Error("Animal loot random sample must be in [0, 1).");
            return value;
        };
        const items: { item: string; amount: number }[] = [];
        for (const candidate of candidates) {
            const chance = candidate.food && !candidate.harvester ? animalDropChance.meat : animalDropChance.specialPart;
            if (sample() * 100 >= chance) continue;
            const fraction = Math.max(0, candidate.fraction + (candidate.variation === 0 ? 0 : (sample() * 2 - 1) * candidate.variation));
            const amount = Math.min(candidate.amount, Math.floor(candidate.amount * fraction));
            if (amount > 0) items.push({ item: candidate.item, amount });
        }
        const result = Inventory.set(npc, { items, harvested: false });
        if (!result.ok) throw new Error(`Animal inventory refused: ${result.code}`);
        return npc;
    } catch (error) {
        npc.remove();
        throw error;
    }
}
