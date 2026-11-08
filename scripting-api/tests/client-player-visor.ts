const localVisorDown: boolean = LocalPlayer !== null && LocalPlayer.visorClosed;
void localVisorDown;

Events.on("playerReady", (player) => {
    const visorDown: boolean = player.visorClosed;
    void visorDown;
    // @ts-expect-error The player's own game decides the visor; scripts only read it.
    player.visorClosed = false;
});
