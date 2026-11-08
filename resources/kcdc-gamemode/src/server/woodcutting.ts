/**
 * Woodcutting: a few blows of an axe fell a tree. The tree gives way to its
 * species' stump, and a copy of it topples away from the woodcutter -- a
 * `rigid` prop the nearest player's game simulates and everybody else sees --
 * then lies where it fell until the tree grows back.
 *
 * Built on `playerWeaponHit`, which names the tree an axe struck the way a
 * trace names it, and on `LevelEdit.apply`, which swaps one object the level
 * placed for another mesh on every player's game.
 */

import type { Command, CommandContext } from "./command.js";

/** Resource policy: change these to tune the game mode. */
export const WOODCUTTING = {
    /** Blows of an axe that fell a tree. */
    blows: 3,
    /** Seconds without a blow before a tree forgets the ones it took. */
    forgetAfterMs: 60_000,
    /** Milliseconds before a felled tree grows back; 0 keeps it felled until `/trees regrow`. */
    regrowMs: 10 * 60_000,
    /** The trunk's mass in kilograms. A tree's own collision carries none, and a rigid prop without a mass is refused. */
    trunkMass: 500,
    /** How fast the trunk starts to tip, in radians per second about its base; gravity does the rest. */
    tipSpin: 0.3,
    /** Roughly how high a trunk's centre of mass sits above its base, in metres, so the start of the tip turns about the base. */
    trunkCentre: 5,
    /** How far above a tree's pivot the trunk starts when the stump's top cannot be traced. */
    cutHeight: 0.45,
};

/** The stumps the game ships, by the species they belong to. Birches and oaks have none, so they stand. */
export const STUMPS: Readonly<Record<string, string>> = {
    tilia_cordata: "objects/natural/vegetation/trees/stumps/stump_tilia_cordata_a.cgf",
    fagus_sylvatica: "objects/natural/vegetation/trees/stumps/stump_fagus_sylvatica_a.cgf",
    pinus_sylvestris: "objects/natural/vegetation/trees/stumps/stump_pinus_sylvestris_a.cgf",
};

/** The axes of the game's item tables, by class GUID. A hand reports the class it holds, not its name. */
const AXES = [
    "1fc42528-2bef-4dde-bf8a-04febeef41c8", // axeWork01
    "81494400-b654-4aa7-8f31-c95c689db5f6", // axeWork02
    "0802c111-75aa-4b9c-9a3f-f30bac55fbc7", // axeWork02_kunesh
    "53612e76-76fd-4dca-84b6-7905b986dc3b", // axeBattle01
    "bd74ce18-2623-48ba-a1a1-c9b09bbb2827", // axeBattle02
    "0f0164d5-3746-4d07-a1ed-0f138225a6d9", // axeBattle03
    "214ffcdd-a7a7-4b7a-b484-f60c8d00b39b", // axeBattle04
    "1c3524bb-e4fa-48a1-b956-2dcbdbf69594", // axeCalvaria
    "eeeb5a48-9a97-41a6-aee0-3e1b64fc2405", // axeCuman
    "50a9ac1e-dbb5-480b-8b5e-2ba3a1ff8d82", // axeFancy
];

/**
 * A class GUID as `player.rightHandItem` spells it: the 16 bytes the game
 * stores, each half written back to front. The first three groups of the
 * text are little-endian integers and the last two are bytes in order.
 */
export function packedItemClass(guid: string): string {
    const digits = [6, 4, 2, 0, 11, 9, 16, 14, 19, 21, 24, 26, 28, 30, 32, 34];
    const bytes = digits.map((at) => guid.slice(at, at + 2).toLowerCase());
    return [...bytes.slice(0, 8).reverse(), ...bytes.slice(8).reverse()].join("");
}

const AXE_CLASSES = new Set(AXES.map(packedItemClass));

/** One tree, from its first blow until it grows back. */
interface Tree {
    object: WorldLevelObject;
    world: number;
    blows: number;
    lastBlow: number;
    felling?: boolean;
    stump?: LevelEdit;
    trunk?: Prop;
    regrow?: ReturnType<typeof setTimeout>;
}

const trees = new Map<string, Tree>();

const keyOf = (object: WorldLevelObject, world: number): string => `${world}:${object.name}@${object.at.x.toFixed(2)},${object.at.y.toFixed(2)},${object.at.z.toFixed(2)}`;
const stumpFor = (model: string): string | undefined => Object.entries(STUMPS).find(([species]) => model.includes(species))?.[1];
const holdsAxe = (player: Player): boolean => AXE_CLASSES.has(player.rightHandItem) || AXE_CLASSES.has(player.leftHandItem);

/** Gives the tree back: the stump goes, the trunk goes, and the next blow starts afresh. */
function regrow(key: string): void {
    const tree = trees.get(key);
    if (!tree) return;
    if (tree.regrow !== undefined) clearTimeout(tree.regrow);
    tree.trunk?.destroy();
    if (tree.stump?.exists) tree.stump.restore();
    trees.delete(key);
}

/** Every tree felled, or being chopped, right now. */
export function regrowAll(): number {
    const felled = [...trees.values()].filter((tree) => tree.stump).length;
    for (const key of [...trees.keys()]) regrow(key);
    return felled;
}

async function fell(player: Player, key: string, tree: Tree, stump: string): Promise<void> {
    tree.felling = true;
    const { object } = tree;
    tree.stump = LevelEdit.apply({ ...object, model: stump }, tree.world);

    // The trunk starts on top of the stump, which the woodcutter's game can trace once the swap has reached it.
    const at = object.at;
    await new Promise((resolve) => setTimeout(resolve, 400));
    const probe = await World.raycast(player, new Vector3(at.x, at.y, at.z + 4), new Vector3(at.x, at.y, at.z - 0.5), { mode: "anything" });
    const top = probe.hit?.model === stump ? probe.hit.position.z : at.z + WOODCUTTING.cutHeight;
    if (trees.get(key) !== tree) return; // regrown while the trace was out

    const start = new Vector3(at.x, at.y, top + 0.02);
    const trunk = Prop.spawn(object.name, start, object.rotation, object.scale, "rigid", tree.world);
    tree.trunk = trunk;
    trunk.mass = WOODCUTTING.trunkMass;

    // A slow turn about the base, away from the woodcutter, and gravity takes it from there.
    const away = new Vector3(at.x - player.position.x, at.y - player.position.y, 0);
    const length = Math.hypot(away.x, away.y) || 1;
    const dx = away.x / length;
    const dy = away.y / length;
    const spin = WOODCUTTING.tipSpin;
    trunk.setVelocity(new Vector3(dx * spin * WOODCUTTING.trunkCentre, dy * spin * WOODCUTTING.trunkCentre, 0), new Vector3(-dy * spin, dx * spin, 0));
    tree.felling = false;

    if (WOODCUTTING.regrowMs > 0) tree.regrow = setTimeout(() => regrow(key), WOODCUTTING.regrowMs);
    console.log(`${player.nickname} felled ${object.name} at ${at.x.toFixed(1)}, ${at.y.toFixed(1)}`);
}

export function installWoodcutting(): void {
    Events.on("playerWeaponHit", (player, hit) => {
        const object = hit.levelObject;
        if (hit.category !== "tree" || !object || !holdsAxe(player)) return;
        const stump = stumpFor(object.name);
        if (!stump) return;

        const key = keyOf(object, player.virtualWorld);
        const now = Date.now();
        let tree = trees.get(key);
        if (tree && !tree.stump && now - tree.lastBlow > WOODCUTTING.forgetAfterMs) tree.blows = 0;
        if (!tree) {
            tree = { object, world: player.virtualWorld, blows: 0, lastBlow: now };
            trees.set(key, tree);
        }
        if (tree.stump || tree.felling) return;

        tree.blows += 1;
        tree.lastBlow = now;
        if (tree.blows < WOODCUTTING.blows) return;
        fell(player, key, tree, stump).catch((error) => console.log(`Felling ${object.name} failed: ${error}`));
    });

    // A trunk that has come down stays there: a log, no longer something to push about.
    Events.on("propRest", (prop) => {
        for (const tree of trees.values()) {
            if (tree.trunk?.id === prop.id && prop.physics === "rigid") prop.physics = "static";
        }
    });
}

export const treesCommand: Command = {
    name: "trees",
    summary: "Lists the trees felled with an axe, or grows them back.",
    usage: ["/trees", "/trees regrow"],

    run({ args, reply, usage }: CommandContext): void {
        if (args[0] === "regrow") {
            reply.line(`Grew back ${regrowAll()} felled tree(s).`);
            return;
        }
        if (args.length > 0) {
            usage();
            return;
        }
        const felled = [...trees.values()].filter((tree) => tree.stump);
        reply.line(felled.length === 0 ? "No tree has been felled." : `${felled.length} tree(s) felled; they grow back ${WOODCUTTING.regrowMs > 0 ? `${Math.round(WOODCUTTING.regrowMs / 60_000)} minutes after they fall` : "with /trees regrow"}.`);
    },
};
