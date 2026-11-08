Events.on("playerReady", (player) => {
    const health: number = player.getStat(PlayerStat.Health);
    const dirtiness: number = player.getDerivedStat(DerivedPlayerStat.Dirtiness);
    void [health, dirtiness];
    // @ts-expect-error Client scripts cannot set authoritative player stats.
    player.setStat(PlayerStat.Health, 80);
    // @ts-expect-error Derived stats have a separate enum and getter.
    player.getStat(DerivedPlayerStat.Bloodiness);
    // @ts-expect-error Derived stat enum members are immutable.
    DerivedPlayerStat.Dirtiness = "bloodiness";
});
