import type { Command, CommandContext } from "../command.js";
import { findPlayer, retarget } from "../players.js";

/** The four parts a body is made of, in the order a chooser reads them. */
const PARTS = ["head", "hair", "beard", "body"] as const;
type Part = (typeof PARTS)[number];

/** How many names one `list` reply prints before asking for a filter. */
const PAGE = 20;

function isPart(value: string | undefined): value is Part {
    return PARTS.includes(value as Part);
}

function readGender(raw: string | undefined): "male" | "female" | null {
    if (raw === undefined) {
        return null;
    }
    return raw === "male" || raw === "female" ? raw : null;
}

/** Whether a face can grow a beard at all; an empty head means the default face. */
function wearable(gender: "male" | "female", head: string, beard: string): boolean {
    return Appearances.beards(head, gender).some((option) => option.name === beard);
}

/** One body as a line: the parts that are set, or a note that none are. */
function describe(appearance: Appearance): string {
    const chosen = PARTS.filter((part) => appearance[part] !== "").map((part) => `${part} ${appearance[part]}`);
    return chosen.length === 0 ? `the default ${appearance.gender} body` : `${appearance.gender}, ${chosen.join(", ")}`;
}

export const appearanceCommand: Command = {
    name: "appearance",
    summary: "Reads and changes what a body looks like.",
    usage: [
        "/appearance [info] -- what you are wearing",
        "/appearance who -- what everyone is wearing",
        "/appearance list <head|hair|beard|body> [filter] [male|female]",
        "/appearance set <head|hair|beard|body> <name|->",
        "/appearance gender <male|female>",
        "/appearance random [male|female]",
        "/appearance reset",
        "Add @<player> to any of the last four to aim it at somebody else.",
    ],

    run({ player, args, reply, usage }: CommandContext): void {
        const subcommand = args[0];

        if (subcommand === undefined || subcommand === "info") {
            reply.line(`You are wearing ${describe(player.appearance)}.`);
            return;
        }

        if (subcommand === "help") {
            usage();
            return;
        }

        // The replication check: change a body on one client, read it here
        // from another.
        if (subcommand === "who") {
            const players = Player.all();
            reply.line(`${players.length} player(s):`);
            reply.lines(players.map((other) => `  ${other.nickname}${other.id === player.id ? " (you)" : ""} -- ${describe(other.appearance)}`));
            return;
        }

        if (subcommand === "list") {
            const part = args[1];
            if (!isPart(part)) {
                reply.line(`'${args[1] ?? ""}' is not a part. They are ${PARTS.join(", ")}.`);
                return;
            }

            // Either trailing argument may be the gender; whatever is left
            // is the filter. Defaults to the gender being worn, so listing
            // after a gender change shows what can actually be put on.
            const named = [args[2], args[3]];
            const gender = named.map(readGender).find((value) => value !== null) ?? player.appearance.gender;
            const filter = named.find((value) => value !== undefined && readGender(value) === null)?.toLowerCase();

            // A beard is modelled per face, so the catalog's 51 are not
            // what any one body can grow.
            const head = gender === player.appearance.gender ? player.appearance.head : "";
            const all = part === "beard" ? Appearances.beards(head, gender) : Appearances.options(part, gender);
            const matches = filter ? all.filter((option) => option.name.toLowerCase().includes(filter)) : all;

            if (matches.length === 0) {
                reply.line(filter ? `No ${gender} ${part} matches '${filter}'; ${all.length} exist.` : `The ${gender} catalog has no ${part} at all.`);
                return;
            }

            const shipped = part === "beard" ? ` of the catalog's ${Appearances.options("beard", gender).length}, the rest being modelled for other faces` : "";
            reply.line(`${matches.length} of ${all.length} ${gender} ${part}${filter ? ` matching '${filter}'` : ""}${shipped}:`);
            reply.line(matches.slice(0, PAGE).map((option) => option.name).join(", "));
            if (matches.length > PAGE) {
                reply.line(`...and ${matches.length - PAGE} more. Add a filter to narrow it.`);
            }
            return;
        }

        const aimed = retarget(args, player);
        if ("error" in aimed) {
            reply.line(aimed.error);
            return;
        }
        const { target, rest } = aimed;
        const whose = target.id === player.id ? "your" : `${target.nickname}'s`;

        // The ask is a request to that body's own client, so what comes back
        // is only whether it went out.
        const refused = `Refused. ${whose[0]!.toUpperCase()}${whose.slice(1)} own client decides what it can wear.`;

        if (rest[0] === "set") {
            const part = rest[1];
            if (!isPart(part)) {
                reply.line(`'${rest[1] ?? ""}' is not a part. They are ${PARTS.join(", ")}.`);
                return;
            }

            if (rest[2] === undefined) {
                reply.line(`Which ${part}? A name from /appearance list ${part}, or '-' to hand it back to the game.`);
                return;
            }

            // '-' because a chat line cannot carry an empty argument.
            const name = rest[2] === "-" ? "" : rest[2];
            if (name !== "" && Appearances.find(part, name, target.appearance.gender) === null) {
                reply.line(`The ${target.appearance.gender} catalog has no ${part} called '${name}'. /appearance list ${part} ${name.slice(0, 6)} searches it.`);
                return;
            }

            const { gender, head, beard } = target.appearance;

            // The owning client refuses an impossible pair whole, so head
            // and beard cannot be set independently of each other.
            if (part === "beard" && name !== "" && !wearable(gender, head, name)) {
                const choices = Appearances.beards(head, gender);
                reply.line(`No '${name}' was modelled for ${whose} face. It can grow ${choices.length}: /appearance list beard.`);
                return;
            }

            // Shaved rather than refused: the face is what was asked for.
            const shaved = part === "head" && beard !== "" && !wearable(gender, name, beard);
            const change = shaved ? { head: name, beard: "" } : { [part]: name };

            if (!target.setAppearance(change)) {
                reply.line(refused);
                return;
            }
            reply.line(name === "" ? `Gave ${whose} ${part} back to the game.` : `Asked for ${whose} ${part} to be '${name}'.`);
            if (shaved) {
                reply.line(`Shaved '${beard}' off with it -- it was never modelled for that face. /appearance list beard once the face is on.`);
            }
            return;
        }

        if (rest[0] === "gender") {
            const gender = readGender(rest[1]);
            if (gender === null) {
                reply.line("A body is 'male' or 'female'.");
                return;
            }

            // Each part belongs to one half of the catalog, so the names
            // being worn cannot survive the change.
            if (!target.setAppearance({ gender, head: "", hair: "", beard: "", body: "" })) {
                reply.line(refused);
                return;
            }
            reply.line(`Asked for a ${gender} body. Its parts start over -- the two catalogs share no names -- and gender moves the archetype, so base armour and unarmed attack move with it.`);
            return;
        }

        if (rest[0] === "random") {
            const gender = readGender(rest[1]) ?? target.appearance.gender;
            const rolled = Appearances.random(gender);
            if (!target.setAppearance(rolled)) {
                reply.line(refused);
                return;
            }
            reply.line(`Rolled ${whose} new body: ${describe(rolled)}.`);
            return;
        }

        if (rest[0] === "reset") {
            if (!target.setAppearance({ head: "", hair: "", beard: "", body: "" })) {
                reply.line(refused);
                return;
            }
            reply.line(`Put ${whose} body back to the one the game hands out.`);
            return;
        }

        usage();
    },
};

export const lookalikeCommand: Command = {
    name: "lookalike",
    summary: "Wear what another player is wearing.",
    usage: ["/lookalike <player|slot>"],

    // The shortest round trip through the feature: what a remote client
    // published has to reach the server before this can copy it.
    run({ player, args, reply, usage }: CommandContext): void {
        if (args.length === 0) {
            usage();
            return;
        }

        const found = findPlayer(args[0]!);
        if ("error" in found) {
            reply.line(`${found.error} /appearance who names everyone here.`);
            return;
        }

        const model = found.player;
        if (model.id === player.id) {
            reply.line("You already look exactly like yourself.");
            return;
        }

        const wanted = model.appearance;
        if (PARTS.every((part) => wanted[part] === "")) {
            reply.line(`${model.nickname} has not chosen a body yet, so there is nothing to copy.`);
            return;
        }

        if (!player.setAppearance(wanted)) {
            reply.line("Refused. Your own client decides what it can wear.");
            return;
        }
        reply.line(`Asked to look like ${model.nickname}: ${describe(wanted)}.`);
    },
};
