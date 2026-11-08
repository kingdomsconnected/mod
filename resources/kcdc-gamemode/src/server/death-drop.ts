/**
 * What dying costs in the default gamemode: the pockets fall at the body and
 * the worn gear stays on it.
 *
 * `player.dropInventory` moves the items into a container the server spawns
 * at the body, so anyone can loot it through the game's own transfer screen.
 * The policy is all here, in script: drop everything by passing
 * `keepEquipped: false`, or keep it all by not calling it.
 */

/** The timers are engine globals the generated contract does not declare. */
interface Timers {
    setInterval(handler: () => void, milliseconds: number): unknown;
    setTimeout(handler: () => void, milliseconds: number): unknown;
}

const timers = globalThis as unknown as Timers;

/** How long an unlooted drop stays in the world before it and its contents go. */
const DROP_LIFETIME_MS = 30 * 60 * 1000;

/** How often expired drops are swept. */
const SWEEP_INTERVAL_MS = 60 * 1000;

interface Drop {
    stash: Stash;
    expiresAt: number;
}

export function installDeathDrops(): void {
    const drops = new Map<number, Drop>();

    Events.on("playerDied", (player) => {
        const drop = player.dropInventory({ keepEquipped: true });
        if (drop) {
            drops.set(drop.id, { stash: drop, expiresAt: Date.now() + DROP_LIFETIME_MS });
            console.log(`${player.nickname} dropped their pockets at their body`);
        }
    });

    // A drop that has been emptied has done its job. It goes on the next
    // tick, so every other handler of this change still finds it.
    Events.on("stashInventoryChanged", (stash) => {
        if (drops.has(stash.id) && (stash.getInventory()?.items.length ?? 0) === 0) {
            drops.delete(stash.id);
            timers.setTimeout(() => stash.destroy(), 0);
        }
    });

    Events.on("stashDestroy", (stash) => {
        drops.delete(stash.id);
    });

    timers.setInterval(() => {
        const now = Date.now();
        for (const [id, drop] of drops) {
            if (now < drop.expiresAt) {
                continue;
            }
            drops.delete(id);
            drop.stash.destroy();
        }
    }, SWEEP_INTERVAL_MS);
}
