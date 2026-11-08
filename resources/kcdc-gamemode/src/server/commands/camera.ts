import type { Command, CommandContext } from "../command.js";

/** Answered by the client half in `src/client/camera.ts`. */
export const CAMERA_EVENT = "kcdc-gamemode:camera.thirdPerson";

export const cameraCommand: Command = {
    name: "camera",
    summary: "Switches your own view between the game's first-person camera and a camera behind you.",
    usage: [
        "/camera                      -- flips between first and third person",
        "/camera third [distance]     -- behind you, 3.5 m back unless told otherwise",
        "/camera first                -- back to the game's own camera",
        "/camera shot                 -- a short cutscene around you: cut, two glides, glide back",
        "/camera shot stop            -- your own view back at once",
    ],

    run({ player, args, reply, usage }: CommandContext): void {
        if (args[0] === "shot") {
            runShotDemo(player, args[1], reply);
            return;
        }

        const mode = args[0] ?? "toggle";
        if (mode !== "toggle" && mode !== "third" && mode !== "first") {
            usage();
            return;
        }

        let distance: number | undefined;
        if (args[1] !== undefined) {
            distance = Number(args[1]);
            if (mode !== "third" || !Number.isFinite(distance) || distance <= 0) {
                usage();
                return;
            }
        }

        // The view is the client's own, so the server only carries the ask.
        player.emit(CAMERA_EVENT, JSON.stringify({ mode, distance }));
        reply.line(mode === "toggle" ? "Switching your camera." : `Camera: ${mode} person.`);
    },
};

/** Pending steps of each player's demo, so a second `/camera shot` or a stop cancels the first. */
const shotTimers = new Map<number, ReturnType<typeof setTimeout>[]>();

function stopShotDemo(player: Player): void {
    for (const timer of shotTimers.get(player.id) ?? []) clearTimeout(timer);
    shotTimers.delete(player.id);
}

// A beat that fired for a player who has left would be a shot sent to nobody,
// which throws -- and their slot number can be handed to whoever joins next.
Events.on("playerDisconnect", (player) => stopShotDemo(player));

/**
 * Four beats around where the player stands, on the world's axes so it does
 * not depend on which way they face: a cut to a close shot from the north, a
 * glide round to the east, a slow rise to a wide high shot with a narrower
 * lens, and a glide back into their eyes.
 */
function runShotDemo(player: Player, option: string | undefined, reply: CommandContext["reply"]): void {
    stopShotDemo(player);
    if (option === "stop") {
        Camera.clearShot(player);
        reply.line("Camera shot stopped.");
        return;
    }
    if (option !== undefined) {
        reply.line("Usage: /camera shot [stop]");
        return;
    }

    const at = player.position;
    const head = new Vector3(at.x, at.y, at.z + 1.6);
    const beats: [number, () => void][] = [
        [0, () => Camera.setShot(player, { position: new Vector3(at.x, at.y + 3, at.z + 1.8), lookAt: head })],
        [2500, () => Camera.setShot(player, { position: new Vector3(at.x + 3.5, at.y, at.z + 1.4), lookAt: head, duration: 2500 })],
        [6000, () => Camera.setShot(player, { position: new Vector3(at.x - 8, at.y - 8, at.z + 9), lookAt: at, duration: 4000, fov: 40 })],
        [12000, () => Camera.clearShot(player, 2000)],
        [14500, () => shotTimers.delete(player.id)],
    ];
    shotTimers.set(player.id, beats.map(([delay, beat]) => setTimeout(beat, delay)));
    reply.line("Camera shot demo: about 14 seconds. /camera shot stop ends it.");
}
