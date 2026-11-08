import { join, readNumber, readQuoted } from "../args.js";
import type { Command, CommandContext } from "../command.js";

/** How many lines `/quest list` fills the caller's chat with. */
const LIST_LIMIT = 20;

/** What the journal sections are called, in the order the game files them. */
const TYPES = ["main", "side", "activity", "event", "micro", "racing"] as const;

/** The three states a quest or an objective can be moved to. `none` is not one of them: a quest nobody should see is removed. */
const STATES = ["active", "done", "failed"] as const;

type QuestType = (typeof TYPES)[number];
type QuestState = (typeof STATES)[number];

function isType(word: string | undefined): word is QuestType {
    return TYPES.includes(word as QuestType);
}

function isState(word: string | undefined): word is QuestState {
    return STATES.includes(word as QuestState);
}

/** `objectives()` can answer `none` for a slot, which is not a state a write accepts; an unset objective is an active one. */
function writable(step: { text: string; progress: string; optional: boolean }): { text: string; progress: QuestState; optional: boolean } {
    return { text: step.text, progress: isState(step.progress) ? step.progress : "active", optional: step.optional };
}

/** Newest first, which is what somebody who has just written a few wants. */
function inWorld(virtualWorld: number): Quest[] {
    return Quest.all(virtualWorld).sort((left, right) => right.id - left.id);
}

/** The quest a key argument names, or null once it has answered the caller itself. */
function resolve(key: string | undefined, world: number, reply: CommandContext["reply"]): Quest | null {
    if (key === undefined || key === "") {
        reply.line("Which quest? /quest list prints their keys.");
        return null;
    }
    const quest = Quest.find(key, world);
    if (!quest) {
        reply.line(`No quest in this world is filed under '${key}'.`);
        return null;
    }
    return quest;
}

export const questCommand: Command = {
    name: "quest",
    summary: "Writes a quest into the game's own journal, and moves it on.",
    usage: [
        '/quest give <key> "<title>" [main|side|activity|event|micro|racing]',
        '/quest desc <key> "<description>"',
        '/quest step <key> "<objective text>" [optional]',
        '/quest done <key> <index>',
        '/quest state <key> <active|done|failed>',
        "/quest list",
        "/quest show <key>",
        "/quest remove <key>",
        "/quest clear",
    ],

    run({ player, args, reply, usage }: CommandContext): void {
        const world = player.virtualWorld;
        const head = args[0];

        if (head === "list") {
            const quests = inWorld(world);
            if (quests.length === 0) {
                reply.line("No quest has been written in this world yet.");
                return;
            }
            const shown = quests.slice(0, LIST_LIMIT);
            reply.line(`${shown.length} of ${quests.length} quest(s), newest first:`);
            reply.lines(shown.map((quest) => `  ${quest.key}: ${quest.title} [${quest.type}, ${quest.progress}, ${quest.objectiveCount} step(s)]`));
            return;
        }

        if (head === "clear") {
            reply.line(`Removed ${Quest.removeAll(world)} quest(s).`);
            return;
        }

        if (head === "give") {
            const key = args[1];
            if (key === undefined) {
                usage();
                return;
            }

            const { value: title, next } = readQuoted(args, 2);
            if (title === "") {
                reply.line('A quest needs a title: /quest give errand "Deliver the letter".');
                return;
            }

            const type = isType(args[next]) ? args[next] : "side";
            const quest = Quest.give(key, title, { type }, world);
            reply.line(`Wrote quest '${quest.key}' into the journal as a ${quest.type} quest.`);
            return;
        }

        if (head === "desc") {
            const quest = resolve(args[1], world, reply);
            if (!quest) {
                return;
            }
            const { value: description } = readQuoted(args, 2);
            quest.description = description === "" ? join(args, 2) : description;
            reply.line(`Rewrote the diary page of '${quest.key}'.`);
            return;
        }

        if (head === "step") {
            const quest = resolve(args[1], world, reply);
            if (!quest) {
                return;
            }

            const { value: text, next } = readQuoted(args, 2);
            if (text === "") {
                reply.line('An objective needs a line: /quest step errand "Find Hans".');
                return;
            }

            const optional = args[next] === "optional";
            const steps = quest.objectives();
            if (steps.length >= 8) {
                reply.line("That quest already carries eight objectives, which is the journal's ceiling.");
                return;
            }

            quest.setObjectives([...steps.map(writable), { text, progress: "active", optional }]);
            reply.line(`Added objective ${steps.length} to '${quest.key}'.`);
            return;
        }

        if (head === "done") {
            const quest = resolve(args[1], world, reply);
            if (!quest) {
                return;
            }

            const index = readNumber(args[2]);
            if (index === null || !Number.isInteger(index) || index < 0 || index >= quest.objectiveCount) {
                reply.line(`That quest has objectives 0 to ${Math.max(quest.objectiveCount - 1, 0)}.`);
                return;
            }

            const step = quest.objectives()[index];
            if (step === undefined) {
                reply.line(`Objective ${index} of '${quest.key}' is not there any more.`);
                return;
            }

            quest.setObjective(index, { ...writable(step), progress: "done" });
            reply.line(`Marked objective ${index} of '${quest.key}' done.`);
            return;
        }

        if (head === "state") {
            const quest = resolve(args[1], world, reply);
            if (!quest) {
                return;
            }
            if (!isState(args[2])) {
                reply.line(`A quest is ${STATES.join(", ")}. Removing one is /quest remove.`);
                return;
            }
            quest.setProgress(args[2]);
            reply.line(`'${quest.key}' is now ${quest.progress}.`);
            return;
        }

        if (head === "show") {
            const quest = resolve(args[1], world, reply);
            if (!quest) {
                return;
            }
            reply.line(`${quest.key}: ${quest.title} [${quest.type}, ${quest.progress}]`);
            if (quest.description !== "") {
                reply.line(`  ${quest.description}`);
            }
            reply.lines(quest.objectives().map((step, index) => `  ${index}: ${step.text} [${step.progress}${step.optional ? ", optional" : ""}]`));
            return;
        }

        if (head === "remove") {
            const quest = resolve(args[1], world, reply);
            if (!quest) {
                return;
            }
            const key = quest.key;
            quest.remove();
            reply.line(`Removed quest '${key}'.`);
            return;
        }

        usage();
    },
};
