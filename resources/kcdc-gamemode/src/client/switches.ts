/** Switches local to this machine: what this player sees and hears, never the world. */

export interface Switch {
    readonly name: string;
    readonly label: string;
    readonly hint: string;
    read(): boolean;
    write(value: boolean): void;
}

const switches: readonly Switch[] = [
    {
        name: "nametags",
        label: "Names above heads",
        hint: "This viewer's own switch; the server has a second one per player.",
        read: () => Nametags.isVisible(),
        write: (value) => Nametags.setVisible(value),
    },
    {
        name: "nametagHealth",
        label: "Health bars under names",
        hint: "Drawn only where the name is drawn too.",
        read: () => Nametags.isHealthVisible(),
        write: (value) => Nametags.setHealthVisible(value),
    },
    {
        name: "compass",
        label: "Compass strip",
        hint: "Pushes the game's own suppression, the one a cutscene uses.",
        read: () => Compass.visible,
        write: (value) => {
            Compass.visible = value;
        },
    },
    {
        name: "chat",
        label: "Chat overlay",
        hint: "Hides the log without closing the input field.",
        read: () => Chat.isUIVisible(),
        write: (value) => Chat.setUIVisible(value),
    },
    {
        name: "voice",
        label: "Voice chat",
        hint: "Stops this client sending and receiving; everyone else carries on.",
        read: () => Voice.isEnabled(),
        write: (value) => Voice.setEnabled(value),
    },
];

export function describeAll(): ReadonlyArray<{ name: string; label: string; hint: string; on: boolean }> {
    return switches.map((entry) => ({ name: entry.name, label: entry.label, hint: entry.hint, on: entry.read() }));
}

/** Flips one switch and answers where it landed, or null for a name nothing owns. */
export function flip(name: string): boolean | null {
    const entry = switches.find((candidate) => candidate.name === name);
    if (!entry) {
        return null;
    }

    const next = !entry.read();
    entry.write(next);
    return next;
}
