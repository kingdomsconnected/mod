/**
 * The client half of `/screenfx`, and two worked examples of the `Hud` screen effects:
 * intoxication and injury.
 *
 * Both are presentation only. Whether a player is drunk or hurt is decided
 * elsewhere -- by the server, by the game's own health -- and is never read
 * back from the screen. A gamemode keeps that state where it decides it, sends
 * the client a level, and the client turns the level into a picture. That is
 * all `setIntoxication` and `setInjury` do.
 */

const SCREEN_EVENT = "kcdc-gamemode:screenfx";

/**
 * Drunkenness as a picture: colour drains a little, the screen softens and
 * starts to smear. Every part scales with one 0..1 level, and a new level is
 * eased in over two seconds rather than snapped to, so drink after drink
 * deepens it smoothly.
 */
class Intoxication {
    private grade = 0;
    private blur = 0;
    private ghosting = 0;

    set(level: number): void {
        const l = Math.min(Math.max(level, 0), 1);
        if (l === 0) {
            this.clear(3000);
            return;
        }

        const grade = { saturation: 1 - 0.45 * l, contrast: 1 - 0.15 * l, yellow: 0.08 * l };
        const blur = { amount: 0.6 * l };
        const ghosting = { amount: 0.4 + 0.6 * l };

        if (this.grade === 0) {
            // A first drink fades in rather than appearing.
            this.grade = Hud.addScreenEffect("colorGrade", grade, { fadeInMs: 2000, fadeOutMs: 3000 });
            this.blur = Hud.addScreenEffect("blur", blur, { fadeInMs: 2000, fadeOutMs: 3000 });
            this.ghosting = Hud.addScreenEffect("ghosting", ghosting, { fadeInMs: 4000, fadeOutMs: 3000 });
            return;
        }

        Hud.updateScreenEffect(this.grade, grade, { fadeMs: 2000 });
        Hud.updateScreenEffect(this.blur, blur, { fadeMs: 2000 });
        Hud.updateScreenEffect(this.ghosting, ghosting, { fadeMs: 2000 });
    }

    clear(fadeOutMs: number): void {
        for (const handle of [this.grade, this.blur, this.ghosting]) {
            if (handle !== 0) {
                Hud.removeScreenEffect(handle, { fadeOutMs });
            }
        }
        this.grade = this.blur = this.ghosting = 0;
    }
}

/**
 * A wound as a picture: blood creeps in from the edges and the colour goes,
 * on top of the game's own low-health desaturation rather than instead of it.
 * A hit flashes the blood in and lets it settle back to the level the wound
 * stays at.
 */
class Injury {
    private blood = 0;
    private grade = 0;

    set(level: number): void {
        const l = Math.min(Math.max(level, 0), 1);
        if (l === 0) {
            this.clear(1500);
            return;
        }

        const blood = { amount: 0.3 + 0.7 * l };
        const grade = { saturation: 1 - 0.5 * l, darkness: 0.1 * l };
        if (this.blood === 0) {
            this.blood = Hud.addScreenEffect("screenBlood", blood, { fadeInMs: 300, fadeOutMs: 1500 });
            this.grade = Hud.addScreenEffect("colorGrade", grade, { fadeInMs: 300, fadeOutMs: 1500 });
            return;
        }
        Hud.updateScreenEffect(this.blood, blood, { fadeMs: 800 });
        Hud.updateScreenEffect(this.grade, grade, { fadeMs: 800 });
    }

    /** One short flash of blood for a hit: up fast, gone on its own after a moment. */
    hit(): void {
        Hud.addScreenEffect("screenBlood", { amount: 1 }, { fadeInMs: 80, durationMs: 250, fadeOutMs: 900 });
    }

    clear(fadeOutMs: number): void {
        for (const handle of [this.blood, this.grade]) {
            if (handle !== 0) {
                Hud.removeScreenEffect(handle, { fadeOutMs });
            }
        }
        this.blood = this.grade = 0;
    }
}

const intoxication = new Intoxication();
const injury = new Injury();

/** For example the server's own drink handler: `player.emit(SCREEN_EVENT, JSON.stringify({ kind: "drunk", level }))`. */
export function setIntoxication(level: number): void {
    intoxication.set(level);
}

export function setInjury(level: number): void {
    injury.set(level);
}

interface ScreenRequest {
    kind: string;
    level?: number;
    effect?: string;
    amount?: number;
}

function readRequest(payload: unknown): ScreenRequest | undefined {
    let parsed: unknown = payload;
    if (typeof payload === "string") {
        try {
            parsed = JSON.parse(payload) as unknown;
        } catch {
            return undefined;
        }
    }
    if (typeof parsed !== "object" || parsed === null) {
        return undefined;
    }
    const { kind, level, effect, amount } = parsed as Record<string, unknown>;
    if (typeof kind !== "string") {
        return undefined;
    }
    return {
        kind,
        level: typeof level === "number" ? level : undefined,
        effect: typeof effect === "string" ? effect : undefined,
        amount: typeof amount === "number" ? amount : undefined,
    };
}

/** Raw effects `/screenfx raw` put up, so `/screenfx clear` takes them down too. */
const raw: number[] = [];

export function installScreen(): void {
    Events.on(SCREEN_EVENT, (payload) => {
        const request = readRequest(payload);
        if (!request) {
            return;
        }

        switch (request.kind) {
            case "drunk":
                setIntoxication(request.level ?? 0.6);
                Hud.showNotification(`Intoxication ${(request.level ?? 0.6).toFixed(2)}`);
                break;
            case "injured":
                setInjury(request.level ?? 0.6);
                Hud.showNotification(`Injury ${(request.level ?? 0.6).toFixed(2)}`);
                break;
            case "hit":
                injury.hit();
                break;
            case "raw": {
                const effect = Hud.screenEffects().find((candidate) => candidate.name === request.effect);
                if (!effect || effect.name === "colorGrade") {
                    Hud.showNotification("raw takes blur, ghosting or screenBlood");
                    return;
                }
                raw.push(Hud.addScreenEffect(effect.name as "blur" | "ghosting" | "screenBlood", { amount: request.amount ?? effect.params[0]?.default ?? 0.5 }, { fadeInMs: 500, fadeOutMs: 500 }));
                break;
            }
            case "grey":
                raw.push(Hud.addScreenEffect("colorGrade", { saturation: request.amount ?? 0 }, { fadeInMs: 500, fadeOutMs: 500 }));
                break;
            case "clear":
                intoxication.clear(1000);
                injury.clear(1000);
                for (const handle of raw.splice(0)) {
                    Hud.removeScreenEffect(handle);
                }
                Hud.showNotification("Screen effects cleared");
                break;
            case "list":
                for (const effect of Hud.screenEffects()) {
                    console.log(`${effect.name}: ${effect.params.map((param) => `${param.name} ${param.min}..${param.max} (default ${param.default})`).join(", ")}`);
                }
                Hud.showNotification("Screen effects listed in the client console");
                break;
            default:
                break;
        }
    });
}
