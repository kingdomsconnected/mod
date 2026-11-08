/**
 * Test commands for the pieces a prop-hunt gamemode is built from: a disguise,
 * and the trace a hunter's swing would be answered with. Neither is a
 * gamemode -- each calls the API one verb at a time so it can be seen working
 * on its own. Blows need no command: players already hurt each other through
 * the game's own combat, and the game's own buffs cover the rest --
 * `player.addBuff("combatTutorial_preventDamage")` makes a body take no health
 * from hits.
 */

import type { Command, CommandContext } from "../command.js";
import { retarget } from "../players.js";
import { CAMERA_EVENT } from "./camera.js";

/** Answered by the client half in `src/client/pick.ts`. */
export const PICK_EVENT = "kcdc-gamemode:prophunt.pick";

export const disguiseCommand: Command = {
    name: "disguise",
    summary: "Draws you, or another player, as a prop from the catalog.",
    usage: [
        "/disguise <model> [@player]  -- a path or a file stem, e.g. barrel_a, crate_box_a",
        "/disguise off [@player]      -- back to a body",
    ],

    run({ player, args, reply, usage }: CommandContext): void {
        const aimed = retarget(args, player);
        if ("error" in aimed) {
            reply.line(aimed.error);
            return;
        }
        const { target, rest } = aimed;
        const model = rest[0];
        if (model === undefined) {
            usage();
            return;
        }

        const off = model === "off";
        try {
            if (!target.setDisguise(off ? null : model)) {
                reply.line(`${target.nickname} has no connection to ask.`);
                return;
            }
        } catch (error) {
            reply.line(error instanceof Error ? error.message : String(error));
            return;
        }

        // The body's own camera sits inside the mesh, so a disguise comes
        // with the view behind it, and taking it off puts the view back.
        target.emit(CAMERA_EVENT, JSON.stringify({ mode: off ? "first" : "third" }));
        reply.line(off ? `${target.nickname} looks like themselves again.` : `${target.nickname} is now drawn as ${model}.`);
    },
};

export const invisibleCommand: Command = {
    name: "invisible",
    summary: "Stops drawing you, or another player, anywhere -- body, clothes and whatever is in hand.",
    usage: [
        "/invisible [@player]      -- stop drawing them",
        "/invisible off [@player]  -- draw them again",
    ],

    run({ player, args, reply }: CommandContext): void {
        const aimed = retarget(args, player);
        if ("error" in aimed) {
            reply.line(aimed.error);
            return;
        }
        const { target, rest } = aimed;
        const visible = rest[0] === "off";

        if (!target.setVisible(visible)) {
            reply.line(`${target.nickname} has no connection to ask.`);
            return;
        }
        reply.line(visible ? `${target.nickname} is drawn again.` : `${target.nickname} is now invisible -- they still stand where they stand, and can still be hit.`);
    },
};

export const pickCommand: Command = {
    name: "pick",
    summary: "Traces from your crosshair and says what it met -- which player, disguised or not.",
    usage: ["/pick"],

    run({ player, reply }: CommandContext): void {
        // The trace is the client's: the server has no world to trace in.
        player.emit(PICK_EVENT, "");
        reply.line("Tracing from your crosshair; the answer shows on your screen.");
    },
};

