/** The command vocabulary, and the one place a `/` line becomes a call. */


/** One player's chat channel. */
export class Reply {
    constructor(private readonly player: Player) {}

    /** One line into this player's chat log, and nobody else's. */
    line(text: string): void {
        Chat.sendToPlayer(this.player, text);
    }

    lines(texts: readonly string[]): void {
        for (const text of texts) {
            this.line(text);
        }
    }
}

/** What a command is given about the line that invoked it. */
export interface CommandContext {
    readonly player: Player;
    readonly name: string;

    /** The rest of the line, split on whitespace. Quotes are `readQuoted`'s job. */
    readonly args: readonly string[];

    readonly reply: Reply;

    /** Replies with every form of this command, which is how a bad line is answered. */
    usage(): void;
}

export interface Command {
    /** The word that invokes it, without the slash, lowercase. */
    readonly name: string;

    /** One line, as `/help` lists it. */
    readonly summary: string;

    /** Every form of the command, one per line. */
    readonly usage: readonly string[];

    /** May be async: `/door` waits on an answer from the caller's own client. */
    run(context: CommandContext): void | Promise<void>;
}

export class CommandRegistry {
    private readonly commands = new Map<string, Command>();

    add(...commands: readonly Command[]): this {
        for (const command of commands) {
            this.commands.set(command.name, command);
        }
        return this;
    }

    all(): readonly Command[] {
        return [...this.commands.values()];
    }

    get(name: string): Command | undefined {
        return this.commands.get(name);
    }

    /** Runs the command a line names. False for a name this registry does not have. */
    dispatch(player: Player, name: string, args: readonly string[]): boolean {
        const command = this.commands.get(name.toLowerCase());
        if (!command) {
            return false;
        }

        const reply = new Reply(player);
        const context: CommandContext = {
            player,
            name: command.name,
            args,
            reply,
            usage: () => reply.lines(command.usage.map((form) => `Usage: ${form}`)),
        };

        // A throw is ordinary here: a command is player input reaching game
        // state. Answer the caller, log it, carry on.
        try {
            const running = command.run(context);
            if (running instanceof Promise) {
                running.catch((error: unknown) => this.report(command, reply, error));
            }
        } catch (error: unknown) {
            this.report(command, reply, error);
        }
        return true;
    }

    private report(command: Command, reply: Reply, error: unknown): void {
        const message = error instanceof Error ? error.message : String(error);
        console.error(`/${command.name} failed:`, message);
        reply.line(`/${command.name} could not run: ${message}`);
    }
}
