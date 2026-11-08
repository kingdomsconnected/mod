# Character creator

`CharacterCreator.open(options)` is a client-only API: it lets the local
player choose how they look, on the game's own character stage, and reports
the look they accept through the client event `characterCreatorClosed`.
Nothing is sent to the server and nothing is put on the body; a resource
that wants the look kept sends it up with its own event and calls
`player.setAppearance` on the server.

```ts
// client
CharacterCreator.open({ appearance: undefined, genders: "both", title: "Your Character" });

Events.on("characterCreatorClosed", (appearance, reason) => {
    if (appearance !== null) {
        Events.emitServer("my-resource:look", JSON.stringify(appearance));
    }
});

// server
Events.onClient("my-resource:look", (sender, payload) => {
    (sender as Player).setAppearance(JSON.parse(payload as string));
});
```

`/creator [male|female|both]` in `kcdc-gamemode` is the worked example: the
server command asks the client to open one, and the client sends back what
was accepted.

## Controls

The panel has one tab per part: Body, Face, Hair and, for a man, Beard.
Q and E (or Tab) change tab, Up and Down choose a row, Left and Right step
through it (held, they repeat), dragging the figure turns it, Enter
accepts and Escape backs out. G switches between Man and Woman when both
are on offer, which starts the parts again from that gender's defaults. R
puts a random look on, and Backspace goes back to the look the creator
opened on. The keys are named by position, as CryInput names them, so
on AZERTY the tab keys are A and E. The strip of key caps at the foot of
the panel reads each action's key off the creator's action map and names it
the way the game's own hints do, so it shows the layout's letter and the key
names in the game's language.

The camera frames the whole figure on the Body tab and eases in on the head
for the other three.

Each tab has up to two rows, following the catalog's own grouping as
`Appearances.options` lists it: a variant names its family in `group`. The
first row picks the family -- a skin tone, a face, a hair style -- and the
second, shown only when the family has more than one member, picks within
it: a variant, a version, or a hair colour drawn as swatches. Changing hair
style keeps the colour when the new style comes in it. Each row shows its
position in its list. The first family is always the figure's own part,
what an empty name means in an `Appearance`. The beard tab only offers the
beards modelled for the current face, and a face change clears a beard the
new face does not carry.

## Close reasons

`characterCreatorClosed(appearance, reason)` is dispatched once per creator.

| `reason`      | Meaning                                                                                                                               |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| `accepted`    | The player accepted; `appearance` is the look                                                                                         |
| `cancelled`   | The player backed out                                                                                                                 |
| `closed`      | `CharacterCreator.close`                                                                                                              |
| `replaced`    | Another `CharacterCreator.open` took its place                                                                                        |
| `unavailable` | The stage could not be put up: no session body, the female figure could not be spawned, or the screen did not come free in 600 frames |
| `interrupted` | The session or the body went away under it                                                                                            |

The event is raised on the creator's next tick rather than where the
ending happens: the player backing out ends it inside the engine's own
`I_UIApse::Hide`, and no script runs there.
