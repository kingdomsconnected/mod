import type { Command, CommandContext } from "../command.js";
import { npcInReach } from "../place.js";

/** A style as `setTalking` takes it; the server checks the word and says which ones it knows. */
type TalkStyle = NonNullable<Parameters<Player["setTalking"]>[1]>;

export const faceCommand: Command = {
    name: "face",
    summary: "Sets a facial expression and makes the mouth talk, on you or the nearest NPC.",
    usage: [
        "/face <fragment> [tags|-]   -- e.g. /face FE_DialogueSpeaking happy, /face ADLG_FA_Smile",
        "/face clear                 -- back to the game's own face",
        "/face talk [neutral|happy|drunk|chew] | /face quiet",
        "/face npc <fragment> [tags|-] | /face npcclear | /face npctalk [style] | /face npcquiet",
        "/face list                  -- the FE_ moods the game plays",
    ],

    run({ player, args, reply, usage }: CommandContext): void {
        const subcommand = args[0];
        if (subcommand === undefined) {
            usage();
            return;
        }

        if (subcommand === "list") {
            const rows = Animations.list("FE_");
            reply.lines(rows.map((row) => `  ${row.fragment}${row.tags ? ` [${row.tags}]` : ""}`));
            reply.line("Gestures, played once: ADLG_FA_Smile, ADLG_FA_Wink, ADLG_FA_Laugh, ADLG_FA_Surprise, ADLG_FA_Hmm, ADLG_FA_Sigh...");
            return;
        }

        // `npc`, `npctalk`, `npcquiet` and `npcclear` act on the nearest NPC,
        // everything else on the caller.
        const onNpc = subcommand.startsWith("npc");
        const verb = onNpc ? subcommand.slice(3) : subcommand;
        const rest = args.slice(1);
        const target = onNpc ? npcInReach(player) : player;
        if (!target) {
            reply.line("No NPC within reach.");
            return;
        }
        const who = onNpc ? `NPC ${target.id}` : "You";

        try {
            if (verb === "talk") {
                const style = (rest[0]?.toLowerCase() ?? "neutral") as TalkStyle;
                reply.line(target.setTalking(true, style) ? `${who}: talking (${style}).` : "Refused.");
                return;
            }
            if (verb === "quiet") {
                reply.line(target.setTalking(false) ? `${who}: quiet.` : "Refused.");
                return;
            }
            if (verb === "clear") {
                reply.line(target.setFacialExpression("") ? `${who}: the game's own face.` : "Refused.");
                return;
            }
            if (onNpc && verb !== "") {
                usage();
                return;
            }

            // `/face <fragment> [tags]` or `/face npc <fragment> [tags]`.
            const [fragment, tagWord] = onNpc ? [rest[0], rest[1]] : [subcommand, rest[0]];
            if (fragment === undefined) {
                usage();
                return;
            }
            const tags = tagWord === undefined || tagWord === "-" ? "" : tagWord;
            reply.line(target.setFacialExpression(fragment, { tags }) ? `${who}: ${fragment}${tags ? ` [${tags}]` : ""}.` : "Refused.");
        } catch (error) {
            reply.line(String(error));
        }
    },
};
