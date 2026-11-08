import type { Command, CommandContext } from "../command.js";

/** Answered by the client half in `src/client/hud.ts`. */
const HUD_EVENT = "kcdc-gamemode:hud";
const HUD_RESULT_EVENT = "kcdc-gamemode:hud.result";

export const hudCommand: Command = {
    name: "hud",
    summary: "Tests native HUD element suppression on your own client; Stats is the default element.",
    usage: [
        "/hud hide [element]   -- suppress Stats, or the named element",
        "/hud show [element]   -- release suppression; the game decides when it appears",
        "/hud status [element] -- report the multiplayer permission, not visible pixels",
        "/hud list             -- list accepted element names",
        "/hud reset            -- release hides made by this demo's HUD commands",
    ],

    run({ player, args, usage }: CommandContext): void {
        const action = args[0] ?? "status";
        if (["hide", "show", "status"].includes(action) && args.length <= 2) {
            player.emit(HUD_EVENT, JSON.stringify({ action, element: args[1] ?? "Stats" }));
        } else if (["list", "reset"].includes(action) && args.length === 1) {
            player.emit(HUD_EVENT, JSON.stringify({ action }));
        } else {
            usage();
        }
    },
};

/** Opt-in reproduction of the server -> playerSpawned -> client hide path. */
class HudSpawnTest {
    private enabled = false;

    install(): void {
        Events.on("consoleCommand", (command, args) => {
            if (command !== "hudspawn") {
                return;
            }
            if (args.length === 1 && (args[0] === "on" || args[0] === "off")) {
                this.enabled = args[0] === "on";
            } else if (args.length > 0 && !(args.length === 1 && args[0] === "status")) {
                console.log("usage: hudspawn on|off|status");
                return;
            }
            console.log(`HUD spawn test ${this.enabled ? "on: future playerSpawned events hide Stats" : "off"}. Existing clients are unchanged; /hud reset releases their test hides.`);
        });

        Events.on("playerSpawned", (player) => {
            if (this.enabled) {
                // One request, with no delay or script-side retry. The native
                // service must hold it across HUD initialization and redraws.
                player.emit(HUD_EVENT, JSON.stringify({ action: "hide", element: "Stats" }));
            }
        });

        Events.onClient(HUD_RESULT_EVENT, (sender, payload) => {
            if (typeof payload === "string" && payload.length <= 512) {
                Chat.sendToPlayer(sender as Player, `[HUD client] ${payload}`);
            }
        });
    }
}

export function installHudHandlers(): void {
    new HudSpawnTest().install();
}
