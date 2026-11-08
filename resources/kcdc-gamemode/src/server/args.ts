/**
 * Reading the arguments of a `/` line.
 *
 * The framework splits a chat command on whitespace and nothing else, so
 * anything richer than one word per argument is rebuilt here.
 */

/** The most of one item the wire carries in a single grant or stack. */
export const MAX_ITEM_AMOUNT = 10000;

/**
 * The highest tier the game makes any item in -- the IV of its I to IV
 * quality marks. Most classes go that far; the server refuses a quality past
 * the class's own maximum with `invalidQuality`.
 */
export const MAX_ITEM_QUALITY = 4;

/** The rest of the line from `from`, spacing collapsed to single spaces. */
export function join(args: readonly string[], from: number): string {
    return args.slice(from).join(" ");
}

/**
 * One value at `from`: a bare word, or a `"quoted run"` rebuilt from the
 * arguments the tokenizer split it into. An unterminated run takes the rest of
 * the line, which is what whoever forgot the closing quote meant.
 */
export function readQuoted(args: readonly string[], from: number): { value: string; next: number } {
    const first = args[from];
    if (first === undefined) {
        return { value: "", next: from };
    }
    if (!first.startsWith('"')) {
        return { value: first, next: from + 1 };
    }

    // One argument can open and close the run by itself: `"Sword"`.
    if (first.length > 1 && first.endsWith('"')) {
        return { value: first.slice(1, -1), next: from + 1 };
    }

    const words = [first.slice(1)];
    for (let index = from + 1; index < args.length; index += 1) {
        const word = args[index] as string;
        if (word.endsWith('"')) {
            words.push(word.slice(0, -1));
            return { value: words.join(" "), next: index + 1 };
        }
        words.push(word);
    }
    return { value: words.join(" "), next: args.length };
}

/** A finite number, or null. Refuses the trailing junk `parseFloat` would accept. */
export function readNumber(raw: string | undefined): number | null {
    if (raw === undefined || raw.trim() === "") {
        return null;
    }
    const value = Number(raw);
    return Number.isFinite(value) ? value : null;
}

/** A whole count from 1 to `max`, or null. */
export function readAmount(raw: string | undefined, max: number = MAX_ITEM_AMOUNT): number | null {
    const value = readNumber(raw);
    if (value === null || !Number.isInteger(value) || value < 1 || value > max) {
        return null;
    }
    return value;
}

/** A network entity ID, or null. */
export function readEntityId(raw: string | undefined): number | null {
    const value = readNumber(raw);
    if (value === null || !Number.isInteger(value) || value < 1) {
        return null;
    }
    return value;
}

/**
 * A level GUID as `door.guid` and `gate.guid` print it, with or without an `0x`
 * prefix, normalised to the form `Door.find` and `Gate.find` take. Null otherwise.
 */
export function readLevelGuid(raw: string | undefined): string | null {
    if (raw === undefined) {
        return null;
    }
    const digits = raw.toLowerCase().replace(/^0x/, "");
    return /^[0-9a-f]{1,16}$/.test(digits) ? digits.padStart(16, "0") : null;
}
