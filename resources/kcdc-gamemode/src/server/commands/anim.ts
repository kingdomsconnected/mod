import { readNumber } from "../args.js";
import type { Command, CommandContext } from "../command.js";
import { npcInReach } from "../place.js";

/** How many rows `/anim list` and `/anim props` print. */
const LIST_LIMIT = 15;

/** One prop, as `playAnimation`'s options take it. */
type HeldProp = { item: string; hand: "right" | "left" };

/** Each caller's last `/anim list`, which `next`, `prev` and `<index>` step through. */
const browsing = new Map<number, { rows: AnimationInfo[]; index: number }>();

/** Who last used `/anim` on each body, by player or NPC id, so its end is told to them. */
const watchers = { players: new Set<number>(), npcs: new Map<number, number>() };

/** A prop that fits a hand the animation was made holding something in, or none. */
function propsFor(row: AnimationInfo | undefined): HeldProp[] {
    if (!row) {
        return [];
    }
    const props: HeldProp[] = [];
    const all = Animations.props();
    for (const [kind, hand] of [
        [row.rightHand, "right"],
        [row.leftHand, "left"],
    ] as const) {
        const prop = kind === "" ? undefined : all.find((candidate) => candidate.hand === kind);
        if (prop) {
            props.push({ item: prop.name, hand });
        }
    }
    return props;
}

function describe(row: AnimationInfo): string {
    const hands = [row.rightHand && `R:${row.rightHand}`, row.leftHand && `L:${row.leftHand}`].filter(Boolean).join(" ");
    const relations = [row.phase && `${row.phase} of ${row.sequence}`, row.partner && `pairs with ${row.partner}`, row.slaveBody && "moves a second body"].filter(Boolean).join(", ");
    return `${row.fragment}${row.tags ? ` [${row.tags}]` : ""} ${row.duration.toFixed(1)}s${row.oneShot ? "" : " loop"}${row.fullBody ? " full" : ""}${hands ? ` ${hands}` : ""}${relations ? ` (${relations})` : ""}`;
}

/** Plays one catalog row on whoever the caller named, with the props its hands call for. */
function playRow(target: Player | Npc, row: AnimationInfo, loop: boolean): boolean {
    return target.playAnimation(row.fragment, { tags: row.tags, loop, props: propsFor(row) });
}

export const animCommand: Command = {
    name: "anim",
    summary: "Browses and plays the game's own animations, with the props they hold.",
    usage: [
        "/anim list <search>        -- find animations; then /anim next, /anim prev or /anim <n>",
        "/anim play <fragment> [tags|-] [loop] [prop] [left]",
        "/anim hold <prop>          -- carry a prop while walking",
        "/anim props [search]",
        "/anim npc <fragment> [tags|-] [loop] [prop]  -- on the nearest NPC",
        "/anim stop [standup] | /anim npcstop [standup]  -- standup gets up out of a sitting or lying stance",
    ],

    run({ player, args, reply, usage }: CommandContext): void {
        const subcommand = args[0];

        if (subcommand === "list") {
            const rows = Animations.list(args.slice(1).join(" "));
            if (rows.length === 0) {
                reply.line("No animation matches that.");
                return;
            }
            browsing.set(player.id, { rows, index: -1 });
            reply.line(`${rows.length} animation(s); /anim next plays them in turn:`);
            reply.lines(rows.slice(0, LIST_LIMIT).map((row, index) => `  ${index}: ${describe(row)}`));
            return;
        }

        if (subcommand === "next" || subcommand === "prev" || (subcommand !== undefined && readNumber(subcommand) !== null)) {
            const state = browsing.get(player.id);
            if (!state) {
                reply.line("Search first: /anim list <search>.");
                return;
            }
            const step = subcommand === "next" ? state.index + 1 : subcommand === "prev" ? state.index - 1 : (readNumber(subcommand) ?? 0);
            state.index = Math.max(0, Math.min(state.rows.length - 1, step));
            const row = state.rows[state.index]!;
            watchers.players.add(player.id);
            reply.line(`${state.index}/${state.rows.length - 1}: ${describe(row)} -> ${playRow(player, row, args[1] === "loop") ? "playing" : "refused"}`);
            return;
        }

        if (subcommand === "props") {
            const search = (args[1] ?? "").toLowerCase();
            const props = Animations.props().filter((prop) => search === "" || prop.name.toLowerCase().includes(search) || prop.hand.toLowerCase().includes(search));
            reply.line(`${props.length} prop(s):`);
            reply.lines(props.slice(0, LIST_LIMIT).map((prop) => `  ${prop.name} (${prop.hand})`));
            return;
        }

        if (subcommand === "hold") {
            const prop = Animations.props().find((candidate) => candidate.name === args[1]);
            if (!prop) {
                reply.line("Usage: /anim hold <prop>. /anim props lists them.");
                return;
            }
            // No fragment: the hand tag switches the body's own idle and walk
            // to the carrying variants -- when the game animates that kind of
            // load at all -- and the prop is what it carries.
            const tag = `r_${prop.hand}`;
            const tags = Animations.list(tag).some((row) => row.tags.split("+").includes(tag)) ? tag : "";
            const ok = player.playAnimation("", { tags, props: { item: prop.name } });
            reply.line(ok ? `Holding ${prop.name}.` : "Refused.");
            return;
        }

        const standUp = args.includes("standup");

        if (subcommand === "stop") {
            watchers.players.add(player.id);
            reply.line(player.stopAnimation({ standUp }) ? (standUp ? "Stopped; standing up." : "Stopped.") : "Refused.");
            return;
        }

        if (subcommand === "npcstop") {
            const npc = npcInReach(player);
            if (npc) {
                watchers.npcs.set(npc.id, player.id);
            }
            reply.line(npc ? (npc.stopAnimation({ standUp }) ? `Stopped NPC ${npc.id}.` : "Refused.") : "No NPC within reach.");
            return;
        }

        if (subcommand === "play" || subcommand === "npc") {
            const fragment = args[1];
            if (fragment === undefined) {
                usage();
                return;
            }
            // Everything after the fragment is optional: `loop` and `left` are
            // flags wherever they stand, the first other word is the tags and
            // the second the prop.
            const words = args.slice(2).filter((arg) => arg !== "loop" && arg !== "left");
            const tags = words[0] === undefined || words[0] === "-" ? "" : words[0];
            const loop = args.includes("loop");
            const propName = words[1];
            const row = Animations.list(fragment).find((candidate) => candidate.fragment.toLowerCase() === fragment.toLowerCase() && candidate.tags.toLowerCase() === tags.toLowerCase());
            const props: HeldProp[] = propName ? [{ item: propName, hand: args.includes("left") ? "left" : "right" }] : propsFor(row);

            const target = subcommand === "npc" ? npcInReach(player) : player;
            if (!target) {
                reply.line("No NPC within reach.");
                return;
            }
            if (subcommand === "npc") {
                watchers.npcs.set(target.id, player.id);
            } else {
                watchers.players.add(player.id);
            }
            try {
                const ok = target.playAnimation(fragment, { tags, loop, props });
                reply.line(`${ok ? "Playing" : "Refused"} ${fragment}${tags ? ` [${tags}]` : ""}${loop ? " on a loop" : ""}${props.length > 0 ? ` holding ${props.map((prop) => prop.item).join(", ")}` : ""}${row ? "" : " (not a catalog row)"}.`);
            } catch (error) {
                reply.line(String(error));
            }
            return;
        }

        usage();
    },
};

/** Tells whoever used `/anim` on a body when its animation ended, and how. */
export function installAnimHandlers(): void {
    Events.on("playerAnimationEnd", (player, fragment, reason) => {
        if (watchers.players.has(player.id)) {
            Chat.sendToPlayer(player, `Animation ${fragment} ended: ${reason}.`);
        }
    });

    Events.on("npcAnimationEnd", (npc, fragment, reason) => {
        const watcher = watchers.npcs.get(npc.id);
        const player = watcher === undefined ? null : Player.getById(watcher);
        if (player) {
            Chat.sendToPlayer(player, `NPC ${npc.id}: animation ${fragment} ended: ${reason}.`);
        }
    });

    Events.on("playerDisconnect", (player) => {
        watchers.players.delete(player.id);
        browsing.delete(player.id);
    });
}
