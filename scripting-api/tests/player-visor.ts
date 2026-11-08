setInterval(() => {
    for (const player of Player.all()) {
        const visorDown: boolean = player.visorClosed;
        void visorDown;
        // @ts-expect-error The player's own game decides the visor; scripts only read it.
        player.visorClosed = true;
    }
}, 1000);
