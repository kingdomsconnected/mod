import type { Command, CommandContext } from "../command.js";
import { NO_POSE_YET, hasPose } from "../place.js";

/**
 * `/blipdemo` lays out one of everything blips can do around the caller, so a
 * single look at the compass and the map screen checks the lot.
 */

/** How far the orbiting blip circles the spot the demo was placed from. */
const ORBIT_RADIUS = 60;

/** One step a second, so a full lap takes half a minute. */
const ORBIT_STEP_DEGREES = 12;
const ORBIT_INTERVAL_MS = 1000;

interface DemoBlip {
    /** East and north of the caller, in metres. */
    east: number;
    north: number;
    type: string;
    label: string;
    /** What to look for, told back to the caller. */
    expect: string;
    compass?: boolean;
    map?: boolean;
    markState?: number;
    mine?: boolean;
}

const LAYOUT: readonly DemoBlip[] = [
    { east: 0, north: 40, type: "Shop", label: "Demo: shop", expect: "compass + map, hover shows 'Demo: shop'" },
    { east: 80, north: 0, type: "QuestGiver", label: "Demo: quest giver", expect: "compass + map" },
    { east: 0, north: -60, type: "Arena", label: "Demo: compass only", expect: "compass only, NOT on the map", map: false },
    { east: -120, north: 0, type: "Camp", label: "Demo: map only", expect: "map only, NOT on the compass", compass: false },
    { east: 30, north: 30, type: "Home", label: "Demo: only you", expect: "private -- other players must not see it", mine: true },
    { east: 0, north: 2000, type: "GeneralPoi", label: "Demo: 2 km north", expect: "far away, still there (never culled by distance)" },
    { east: -40, north: 40, type: "Blacksmith", label: "", expect: "no label -- hover shows the game's own name for the icon" },
    { east: 40, north: -40, type: "Shrine", label: "Demo: state 0", expect: "state 0", markState: 0 },
    { east: 55, north: -40, type: "Shrine", label: "Demo: state 1", expect: "state 1 (the map's 'undiscovered' art), next to state 0", markState: 1 },
    { east: -60, north: -60, type: "Main", label: "Demo: quest icon", expect: "compass only -- the map has no art for quest icons" },
];

/** What the running demo owns, so a second run or a clear takes exactly that down. */
const demo = {
    ids: [] as number[],
    orbitId: 0,
    orbitTimer: undefined as Timeout | undefined,
};

function clearDemo(): number {
    if (demo.orbitTimer !== undefined) {
        clearInterval(demo.orbitTimer);
        demo.orbitTimer = undefined;
    }

    let removed = 0;
    for (const id of [...demo.ids, demo.orbitId]) {
        const blip = id !== 0 ? Blip.getById(id) : null;
        if (blip) {
            blip.remove();
            removed += 1;
        }
    }
    demo.ids = [];
    demo.orbitId = 0;
    return removed;
}

export const blipDemoCommand: Command = {
    name: "blipdemo",
    summary: "Places one of every kind of blip around you, plus one that circles, to check the compass and the map screen.",
    usage: ["/blipdemo", "/blipdemo clear"],

    run({ player, args, reply }: CommandContext): void {
        if (args[0] === "clear") {
            reply.line(`Removed ${clearDemo()} demo blip(s).`);
            return;
        }

        if (!hasPose(player)) {
            reply.line(NO_POSE_YET);
            return;
        }

        // A second run replaces the first rather than stacking on it.
        clearDemo();

        const origin = player.position;
        const world = player.virtualWorld;

        reply.line("Blip demo -- look at the compass, then open the map (M):");
        for (const entry of LAYOUT) {
            const blip = Blip.create({
                position: new Vector3(origin.x + entry.east, origin.y + entry.north, origin.z),
                type: entry.type,
                label: entry.label,
                compass: entry.compass ?? true,
                map: entry.map ?? true,
                markState: entry.markState ?? 0,
                virtualWorld: world,
                player: entry.mine ? player : undefined,
            });
            demo.ids.push(blip.id);
            reply.line(`  ${blip.id} ${entry.type}: ${entry.expect}`);
        }

        // Moves every second, which is the case the map screen has to redraw
        // while it is open: a row can only be replaced, never moved.
        let angle = 0;
        const orbit = Blip.create({ position: new Vector3(origin.x + ORBIT_RADIUS, origin.y, origin.z), type: "Pub", label: "Demo: orbiting", virtualWorld: world });
        demo.orbitId = orbit.id;
        demo.orbitTimer = setInterval(() => {
            const blip = Blip.getById(demo.orbitId);
            if (!blip) {
                return;
            }
            angle = (angle + ORBIT_STEP_DEGREES) % 360;
            const radians = (angle * Math.PI) / 180;
            blip.position = new Vector3(origin.x + Math.cos(radians) * ORBIT_RADIUS, origin.y + Math.sin(radians) * ORBIT_RADIUS, origin.z);
        }, ORBIT_INTERVAL_MS);
        reply.line(`  ${orbit.id} Pub: circles you at ${ORBIT_RADIUS} m -- it must keep moving with the map open`);

        reply.line("Also try: the map legend toggle for Shop hides the demo shop too. /blipdemo clear removes it all.");
    },
};
