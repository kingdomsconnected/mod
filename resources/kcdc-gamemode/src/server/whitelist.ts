/**
 * Who may join, decided before they are sent anything: `playerConnecting`
 * answers while the player is still at the gates, so someone turned away
 * never downloads a resource or sees the level load.
 *
 * Off by default, so a bare server lets everyone in. Fill `entries` in for
 * your own server, or drive it from the server console:
 *
 *   whitelist on | off
 *   whitelist add <entry>        whitelist remove <entry>
 *   whitelist list
 *
 * An entry of digits is a Steam id, matched against `connection.steamId`.
 * `name:<nickname>` matches the name a player asks to join under, which
 * anybody can type -- fine for a test session, not for keeping anyone out.
 * Neither id is verified by the server; a whitelist that has to hold should
 * check `connection.ticket` against whatever issued it.
 *
 * Console changes last until the server stops. Persisting them, or asking a
 * database instead, is a matter of making the handler async: the connection
 * waits for it.
 */

export const whitelist = {
    enabled: false,
    /** What a player who is not on it reads, as written. */
    message: "You are not on this server's whitelist.",
    entries: [] as string[],
};

function matches(connection: PendingConnection, entry: string): boolean {
    if (entry.startsWith("name:")) {
        return entry.slice("name:".length).toLowerCase() === connection.nickname.toLowerCase();
    }
    return connection.steamId !== "" && entry === connection.steamId;
}

function isAllowed(connection: PendingConnection): boolean {
    return whitelist.entries.some((entry) => matches(connection, entry));
}

function describe(connection: PendingConnection): string {
    return `${connection.nickname} (steam ${connection.steamId || "none"}, ${connection.ip})`;
}

function runConsoleCommand(args: readonly string[]): void {
    const [verb, entry] = args;
    switch (verb) {
    case "on":
    case "off":
        whitelist.enabled = verb === "on";
        console.log(`whitelist ${verb}, ${whitelist.entries.length} entr${whitelist.entries.length === 1 ? "y" : "ies"}`);
        return;
    case "add":
        if (!entry) {
            break;
        }
        if (!whitelist.entries.includes(entry)) {
            whitelist.entries.push(entry);
        }
        console.log(`whitelist: added ${entry}`);
        return;
    case "remove":
        if (!entry) {
            break;
        }
        whitelist.entries = whitelist.entries.filter((existing) => existing !== entry);
        console.log(`whitelist: removed ${entry}`);
        return;
    case "list":
    case undefined:
        console.log(`whitelist is ${whitelist.enabled ? "on" : "off"}: ${whitelist.entries.length ? whitelist.entries.join(", ") : "no entries"}`);
        return;
    }
    console.log("usage: whitelist on|off | add <steamid|name:nickname> | remove <entry> | list");
}

export function installWhitelist(): void {
    Events.on("playerConnecting", (connection) => {
        if (!whitelist.enabled) {
            return;
        }
        if (!isAllowed(connection)) {
            console.log(`whitelist: turned away ${describe(connection)}`);
            connection.reject(whitelist.message);
            return;
        }
        console.log(`whitelist: let in ${describe(connection)}`);
    });

    Events.on("consoleCommand", (command, args) => {
        if (command === "whitelist") {
            runConsoleCommand(args);
        }
    });
}
