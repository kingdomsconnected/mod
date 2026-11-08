/**
 * The server half of the debug panel: what its client cannot see for itself.
 * Answered on demand, so a panel nobody has open costs nothing.
 */

import type { CommandRegistry } from "./command.js";

const RESOURCE = "kcdc-gamemode";

function world(): Record<string, unknown> {
    return {
        day: World.day,
        hour: World.hour,
        timeScale: World.timeScale,
        weather: World.weather,
        previousWeather: World.previousWeather,
        blending: World.weatherBlending,
        blendRemaining: World.weatherRemaining,
        rainIntensity: World.rainIntensity,
        rainAmount: World.rainAmount,
        wetness: World.wetness,
        puddles: World.puddles,
    };
}

function roster(): ReadonlyArray<Record<string, unknown>> {
    return Player.all().map((player) => ({
        id: player.id,
        slot: player.playerIndex,
        nickname: player.nickname,
        ready: player.ready,
        alive: player.alive,
        healthPercent: player.healthPercent,
        mounted: player.mounted,
        position: player.ready ? { x: player.position.x, y: player.position.y, z: player.position.z } : null,
    }));
}

function spawned(): Record<string, number> {
    return {
        horses: Horse.all().length,
        props: Prop.all().length,
        groundItems: GroundItem.all().length,
        doors: Door.all().length,
        gates: Gate.all().length,
    };
}

/** `/ui` is the only caller. */
export function openPanelFor(player: Player): void {
    // The payload is declared optional, but the native binding still counts
    // two arguments; an empty one is what build.ts sends for the same reason.
    player.emit(`${RESOURCE}:panel.open`, "");
}

export function installDebugPanel(registry: CommandRegistry): void {
    Events.onClient(`${RESOURCE}:panel.poll`, (sender) => {
        (sender as Player).emit(
            `${RESOURCE}:panel.realm`,
            JSON.stringify({
                world: world(),
                players: roster(),
                spawned: spawned(),
                commands: registry.all().map((command) => ({ name: command.name, summary: command.summary, usage: command.usage })),
            }),
        );
    });

    console.log("debug panel answering; clients open it with F4 or /ui");
}
