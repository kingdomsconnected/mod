const STATE_EVENT = "kcdc-gamemode:combat.state";
const VOTE_EVENT = "kcdc-gamemode:combat.vote";

/** U by default, rebindable under Multiplayer controls. */
const COMBAT_HINT_KEY = "kcdc_end_combat";

interface Fight {
    opponent: number;
    name: string;
    token: number;
    voted: boolean;
    offered: boolean;
}

export function installCombat(): void {
    let fights: Fight[] = [];
    let selected: Fight | undefined;
    let hint = 0;
    const remove = (): void => {
        if (hint) ActionHint.remove(hint);
        hint = 0;
    };
    const update = (): void => {
        const local = LocalPlayer;
        const distance = (fight: Fight): number => {
            const other = new Player(fight.opponent);
            return local && other.ready ? Math.hypot(local.position.x - other.position.x, local.position.y - other.position.y, local.position.z - other.position.z) : Infinity;
        };
        const next = local && local.canAct ? fights.slice().sort((a, b) => distance(a) - distance(b))[0] : undefined;
        // A hold begun for one opponent must never vote for another.
        if (next?.token !== selected?.token) remove();
        selected = next;
        if (!selected) { remove(); return; }
        const name = selected.name.slice(0, 40);
        const text = selected.voted ? `End combat with ${name}: waiting (1/2)` : selected.offered ? `Accept peace with ${name} (1/2)` : `Offer peace to ${name} (0/2)`;
        if (!hint) {
            const fight = selected;
            hint = ActionHint.create({ key: COMBAT_HINT_KEY, text, mode: "hold", holdDuration: 1, enabled: !fight.voted }, () => {
                if (selected !== fight || fight.voted) return;
                Events.emitServer(VOTE_EVENT, { opponent: fight.opponent, token: fight.token });
            });
        } else {
            ActionHint.setText(hint, text);
            ActionHint.setEnabled(hint, !selected.voted);
        }
    };
    Events.on(STATE_EVENT, (payload) => {
        let state: unknown;
        try { state = typeof payload === "string" ? JSON.parse(payload) : payload; }
        catch { return; }
        if (!Array.isArray(state) || state.some((entry) => !entry || !Number.isSafeInteger(entry.opponent) || !Number.isSafeInteger(entry.token) || typeof entry.name !== "string" || typeof entry.voted !== "boolean" || typeof entry.offered !== "boolean")) return;
        // Replace the callback as well as its title when a vote changes.
        remove();
        fights = state;
        update();
    });
    const timer = setInterval(update, 100);
    Events.on("resourceStop", (name) => {
        if (name !== "kcdc-gamemode") return;
        clearInterval(timer);
        remove();
    });
    Events.emitServer("kcdc-gamemode:combat.sync");
}
