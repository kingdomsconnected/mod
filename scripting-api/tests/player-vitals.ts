function restoreVitals(player: Player): void {
    const stat: (typeof PlayerStat)[keyof typeof PlayerStat] = PlayerStat.Stamina;
    const derived: (typeof DerivedPlayerStat)[keyof typeof DerivedPlayerStat] = DerivedPlayerStat.Dirtiness;
    const stamina: number = player.getStat(stat);
    const dirtiness: number = player.getDerivedStat(derived);
    const sent: boolean = player.setStat(stat, stamina);
    void [dirtiness, sent];
    // @ts-expect-error Derived stats are read-only and use their own enum.
    player.setStat(DerivedPlayerStat.Dirtiness, 0);
    // @ts-expect-error Resource stats use getStat.
    player.getDerivedStat(PlayerStat.Health);
    // @ts-expect-error Derived stats use getDerivedStat.
    player.getStat(DerivedPlayerStat.Charisma);
    // @ts-expect-error Enum members are immutable.
    PlayerStat.Health = "stamina";
    const health: number = player.health;
    const hunger: number = player.hunger;
    const healthSent: boolean = player.setHealth(health);
    const hungerSent: boolean = player.setHunger(hunger);
    void [healthSent, hungerSent];

    // @ts-expect-error Setters take native numeric units.
    player.setHealth("50");
    // @ts-expect-error A nourishment value is required.
    player.setHunger();
    // @ts-expect-error Reported health remains read-only.
    player.health = 50;
    // @ts-expect-error Reported nourishment remains read-only.
    player.hunger = 50;
}

Events.on("playerDisconnect", (player) => {
    const saved = { health: player.health, hunger: player.hunger, account: player.steamId };
    void saved;
});

Events.on("playerReady", (player) => {
    if (player.ready && player.alive) restoreVitals(player);
});
