import { join } from "../args.js";
import type { Command, CommandContext } from "../command.js";
import { findPlayer } from "../players.js";

/**
 * Personal mutes: the caller stops hearing someone, and nobody else is
 * affected. Enforced by the server (`Voice.setLocalMute`), so the muted
 * player's voice is never relayed to the caller at all rather than being
 * dropped by a client that could be told otherwise.
 */

function resolveOther(context: CommandContext): Player | null {
    const { player, args, reply, usage } = context;
    if (args.length === 0) {
        usage();
        return null;
    }

    const found = findPlayer(join(args, 0));
    if ("error" in found) {
        reply.line(`${found.error} /tp list names everyone here.`);
        return null;
    }
    if (found.player.id === player.id) {
        reply.line("That is you.");
        return null;
    }
    return found.player;
}

export const muteCommand: Command = {
    name: "mute",
    summary: "Stops you hearing another player's voice.",
    usage: ["/mute <player|slot>"],

    run(context: CommandContext): void {
        const target = resolveOther(context);
        if (!target) {
            return;
        }

        if (Voice.isLocallyMuted(context.player, target)) {
            context.reply.line(`${target.nickname} is already muted for you.`);
            return;
        }

        Voice.setLocalMute(context.player, target, true);
        context.reply.line(`You no longer hear ${target.nickname}. /unmute ${target.nickname} undoes it.`);
    },
};

export const unmuteCommand: Command = {
    name: "unmute",
    summary: "Lets you hear a player you muted again.",
    usage: ["/unmute <player|slot>"],

    run(context: CommandContext): void {
        const target = resolveOther(context);
        if (!target) {
            return;
        }

        if (!Voice.isLocallyMuted(context.player, target)) {
            context.reply.line(`${target.nickname} is not muted for you.`);
            return;
        }

        Voice.setLocalMute(context.player, target, false);
        context.reply.line(`You hear ${target.nickname} again.`);
    },
};

export const mutesCommand: Command = {
    name: "mutes",
    summary: "Lists the players you have muted.",
    usage: ["/mutes"],

    run({ player, reply }: CommandContext): void {
        const muted = Player.all().filter((other) => other.id !== player.id && Voice.isLocallyMuted(player, other));
        reply.line(muted.length === 0 ? "You have not muted anyone." : `Muted for you: ${muted.map((other) => other.nickname).join(", ")}`);
    },
};
