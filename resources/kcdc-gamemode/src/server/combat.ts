const STATE_EVENT = "kcdc-gamemode:combat.state";
const VOTE_EVENT = "kcdc-gamemode:combat.vote";

/** Resource policy, in milliseconds. Zero disables inactivity cancellation. */
export const COMBAT_TIMEOUT_MS = 45_000;

interface Fight {
    first: Player;
    second: Player;
    world: number;
    token: number;
    lastAttack: number;
    votes: Set<number>;
}

/** The server negotiates; clients can only vote on a fight it already knows. */
export function installCombat(timeoutMs = COMBAT_TIMEOUT_MS): void {
    if (!Number.isFinite(timeoutMs) || timeoutMs < 0) throw new Error("Combat timeout must be non-negative; 0 disables it.");
    const fights = new Map<string, Fight>();
    let nextToken = 1;
    const key = (first: Player, second: Player): string => `${Math.min(first.id, second.id)}:${Math.max(first.id, second.id)}`;
    const eligible = (fight: Fight): boolean => [fight.first, fight.second].every((player) => player.ready && player.canAct && player.virtualWorld === fight.world);
    const publish = (player: Player): void => {
        if (!player.ready) return;
        const state = [...fights.values()].filter((fight) => fight.first.id === player.id || fight.second.id === player.id).map((fight) => {
            const other = fight.first.id === player.id ? fight.second : fight.first;
            return { opponent: other.id, name: other.nickname, token: fight.token, voted: fight.votes.has(player.id), offered: fight.votes.has(other.id) };
        });
        player.emit(STATE_EVENT, JSON.stringify(state));
    };
    const remove = (fight: Fight): void => {
        if (!fights.delete(key(fight.first, fight.second))) return;
        publish(fight.first);
        publish(fight.second);
    };
    const end = (fight: Fight): void => {
        // cancelCombat also raises playerCombatCancelled when another resource calls it.
        fight.first.cancelCombat(fight.second);
        remove(fight);
    };
    const forget = (player: Player): void => {
        for (const fight of [...fights.values()]) {
            if (fight.first.id === player.id || fight.second.id === player.id) end(fight);
        }
    };
    const attack = (first: Player, second: Player): void => {
        if (first.id === second.id) return;
        let fight = fights.get(key(first, second));
        if (!fight) fight = { first, second, world: first.virtualWorld, token: 0, lastAttack: 0, votes: new Set() };
        if (!eligible(fight)) { remove(fight); return; }
        fight.lastAttack = Date.now();
        fight.token = nextToken++;
        fight.votes.clear();
        fights.set(key(first, second), fight);
        publish(first);
        publish(second);
    };
    Events.on("playerAttack", attack);
    // A contact can name a different victim from the swing's original target.
    Events.on("playerHit", (victim, attacker, hit) => { if (hit.source === "melee" && "nickname" in attacker) attack(attacker, victim); });
    Events.on("playerCombatCancelled", (first, second) => {
        const fight = fights.get(key(first, second));
        if (fight) remove(fight);
    });
    Events.on("playerDisconnect", forget);
    Events.on("playerDied", forget);
    Events.on("playerSpawning", forget);
    Events.onClient("kcdc-gamemode:combat.sync", (sender) => publish(sender as Player));
    Events.onClient(VOTE_EVENT, (sender, payload) => {
        const player = sender as Player;
        let request: unknown;
        try { request = typeof payload === "string" ? payload.length <= 128 ? JSON.parse(payload) : null : payload; }
        catch { return; }
        if (!request || typeof request !== "object") return;
        const { opponent, token } = request as { opponent?: unknown; token?: unknown };
        if (!Number.isSafeInteger(opponent) || !Number.isSafeInteger(token)) return;
        const fight = [...fights.values()].find((fight) => fight.token === token &&
            ((fight.first.id === player.id && fight.second.id === opponent) || (fight.second.id === player.id && fight.first.id === opponent)));
        if (!fight) { publish(player); return; }
        if (!eligible(fight)) { end(fight); return; }
        fight.votes.add(player.id);
        if (fight.votes.size === 2) end(fight);
        else { publish(fight.first); publish(fight.second); }
    });
    const timer = setInterval(() => {
        const now = Date.now();
        for (const fight of [...fights.values()]) {
            if (!eligible(fight) || (timeoutMs > 0 && now - fight.lastAttack >= timeoutMs)) end(fight);
        }
    }, 250);
    Events.on("resourceStop", (name) => {
        if (name !== "kcdc-gamemode") return;
        clearInterval(timer);
        for (const fight of [...fights.values()]) end(fight);
    });
}
