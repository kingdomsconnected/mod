/**
 * The client half of `/build`: the preview, and nothing else.
 *
 * `PropPlacer` runs the loop natively, so this file has no tick -- it answers
 * start, stop, and what the placer reports. A confirmed pose goes to the
 * server, which measures it again and is what actually builds.
 */


const RESOURCE = "kcdc-gamemode";

/** Translucent so you can see the ground through what you are about to build. */
const VALID_TINT = new Color(0.25, 0.95, 0.4, 0.35);
const INVALID_TINT = new Color(1, 0.2, 0.15, 0.35);

/**
 * The rules the preview is coloured by. Deliberately loose: a real gamemode
 * would put its own economy behind `PropPlacer.setVeto`.
 */
const RULES = {
    maxSlope: 35,
    minDistance: 2,
    maxDistance: 35,
    clearance: 1.5,
    requireSurface: true,
};

let handlersInstalled = false;

function readField(payload: unknown, key: string): unknown {
    return typeof payload === "object" && payload !== null ? (payload as Record<string, unknown>)[key] : undefined;
}

function parse(payload: unknown): unknown {
    if (typeof payload !== "string") {
        return payload;
    }
    try {
        return JSON.parse(payload) as unknown;
    } catch {
        return undefined;
    }
}

function installPlacerHandlers(): void {
    if (handlersInstalled) {
        return;
    }
    handlersInstalled = true;

    PropPlacer.on("confirm", (pose) => {
        if (!pose) {
            return;
        }
        Events.emitServer(`${RESOURCE}:build.place`, JSON.stringify({ position: pose.position, rotation: pose.rotation }));
    });

    PropPlacer.on("rejected", (pose) => {
        // The preview was already red; this is what says why it was.
        const why = pose && !pose.onSurface ? "there is nothing there to build on" : "that spot is refused";
        Hud.showInfoText(`Cannot build here: ${why}.`);
    });

    // Whatever ended it, the server is told, so it keeps no session open for
    // a client that has left the mode.
    PropPlacer.on("end", () => {
        Events.emitServer(`${RESOURCE}:build.ended`);
    });
}

export function installBuild(): void {
    Events.on(`${RESOURCE}:build.begin`, (payload) => {
        const request = parse(payload);
        const model = readField(request, "model");
        if (typeof model !== "string") {
            return;
        }

        const scale = readField(request, "scale");
        // A ladder's preview leans as it will stand, and places one at a time.
        const rotations = readField(request, "rotations");
        const keepOpen = readField(request, "keepOpen");
        const minDistance = readField(request, "minDistance");
        const clearance = readField(request, "clearance");

        installPlacerHandlers();

        // `begin` throws for a model the shared catalog does not carry.
        let started = false;
        try {
            started = PropPlacer.begin({
                model,
                rotations: Array.isArray(rotations) ? rotations.map((value) => {
                    const q = value as Record<string, number>;
                    return new Quaternion(q.w ?? 1, q.x ?? 0, q.y ?? 0, q.z ?? 0);
                }) : undefined,
                scale: typeof scale === "number" ? scale : 1,
                validTint: VALID_TINT,
                invalidTint: INVALID_TINT,
                range: 40,
                alignToSurface: false,
                faceAwayFromPlayer: true,
                keepOpen: typeof keepOpen === "boolean" ? keepOpen : true,
                rules: {
                    ...RULES,
                    minDistance: typeof minDistance === "number" ? minDistance : RULES.minDistance,
                    clearance: typeof clearance === "number" ? clearance : RULES.clearance,
                },
            });
        } catch (error) {
            Hud.showInfoText(`${model} is not a mesh this build knows.`);
            console.log(`build refused: ${String(error)}`);
            Events.emitServer(`${RESOURCE}:build.ended`);
            return;
        }

        if (!started) {
            // No world, no camera, or another part of the mod holding input.
            Hud.showInfoText("Cannot start building right now.");
            Events.emitServer(`${RESOURCE}:build.ended`);
            return;
        }

        console.log(`building ${model}`);
    });

    Events.on(`${RESOURCE}:build.stop`, () => {
        PropPlacer.end();
    });
}
