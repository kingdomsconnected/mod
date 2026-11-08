/**
 * `/hud` uses only the public native HUD API. A hide is issued once; keeping
 * it applied while the movie loads or redraws is the native service's job.
 */
const HUD_EVENT = "kcdc-gamemode:hud";
const HUD_RESULT_EVENT = "kcdc-gamemode:hud.result";

const ELEMENTS = [
    "Compass", "Stats", "QAMWeapon", "QAMFood", "Subtitles", "InfoText", "GameLog", "Hints", "DialogLeft", "DialogRight", "Cursor", "Crime", "Wanted", "PopUpBackground",
    "TutorialMessage", "FancyEvent", "SkillCheck", "ItemTransfer", "Buffs", "CommonEvent", "DiceCursor", "Trespassing", "RatioStrips", "ShootingContest", "Bubbles", "TutorialInDialog", "DiceContainer", "Vignette",
];

class HudTest {
    private readonly hidden = new Set<string>();

    private report(message: string): void {
        console.log(`[HUD] ${message}`);
        Events.emitServer(HUD_RESULT_EVENT, message);
    }

    private describe(element: string): string {
        return `${element}: ${Hud.isElementVisible(element) ? "allowed (the game may still hide it)" : "suppressed by multiplayer"}`;
    }

    private handle(payload: unknown): void {
        let parsed: unknown = payload;
        if (typeof payload === "string") {
            try {
                parsed = JSON.parse(payload) as unknown;
            } catch {
                return;
            }
        }
        if (typeof parsed !== "object" || parsed === null) {
            return;
        }
        const { action, element } = parsed as Record<string, unknown>;
        if (action === "list") {
            this.report(`Elements: ${ELEMENTS.join(", ")}`);
            return;
        }
        if (action === "reset") {
            for (const name of this.hidden) {
                if (Hud.setElementVisible(name, true)) {
                    this.hidden.delete(name);
                }
            }
            this.report(this.hidden.size === 0 ? "Demo HUD hides released; native visibility applies." : `Hides still pending: ${[...this.hidden].join(", ")}`);
            return;
        }
        if (action !== "hide" && action !== "show" && action !== "status") {
            return;
        }
        if (typeof element !== "string" || !ELEMENTS.includes(element)) {
            this.report("Unknown element. Names are case-sensitive; /hud list lists them.");
            return;
        }
        if (action !== "status") {
            if (!Hud.setElementVisible(element, action === "show")) {
                this.report(`${element}: request was not accepted.`);
                return;
            }
            if (action === "hide") {
                this.hidden.add(element);
            } else {
                this.hidden.delete(element);
            }
        }
        this.report(this.describe(element));
    }

    install(): void {
        Events.on(HUD_EVENT, (payload) => this.handle(payload));
        // Resource stop and disconnect release the native resource-owned
        // suppression automatically. Do not mask that behavior with a reset.
    }
}

export function installHud(): void {
    new HudTest().install();
}
