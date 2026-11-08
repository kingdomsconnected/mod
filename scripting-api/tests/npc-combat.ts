function npcBowExample(npc: Npc, player: Player, otherNpc: Npc): void {
    const accepted: boolean = npc.attack(player, { weapon: "bow" });
    npc.attack(otherNpc, { weapon: "melee" });
    npc.attack(player);
    // @ts-expect-error Only implemented combat modes are accepted.
    npc.attack(player, { weapon: "crossbow" });
    void accepted;
}
