# Native UI from a client resource

A client resource has two ways to put something on screen. `Web` is a browser:
it lays out anything, it is HTML, and it costs a CEF view. `NativeUI` is the
game's own Scaleform renderer: it composes inside the game's own movies, sits
inside the game's own layer stack, is drawn by the pass that already draws the
HUD, and can be navigated with a controller. Neither replaces the other, and a
resource may use both.

## Disabling vanilla menus

Use `NativeUI.setMenuEnabled(menu, enabled)` in a client resource to prevent a
vanilla menu from opening. All menus are enabled by default.

```js
NativeUI.setMenuEnabled('map', false);
NativeUI.setMenuEnabled('inventory', false);

// Bind your replacement UI normally. The native map cannot open on M.
Key.bind('m', () => openCustomMap());

NativeUI.isMenuEnabled('map'); // false
NativeUI.setMenuEnabled('map', true); // release this resource's restriction
```

| Menu        | Pages covered                          |
| ----------- | -------------------------------------- |
| `inventory` | Inventory and item information         |
| `player`    | Player, skills, and player information |
| `map`       | Map and map legend                     |
| `codex`     | Codex                                  |
| `journal`   | Quest log and quest diary              |
| `crafting`  | Crafting                               |

The restriction applies to native opening and navigation, including keyboard,
controller, and UI requests. It does not change key assignments. If the disabled
menu is already open, it closes on the next client tick. Native tab labels remain
visible; selecting a disabled tab does not open its page or move the camera.

Calling `false` repeatedly has the same effect as calling it once. Calling `true`
releases only the calling resource's restriction. A menu stays disabled while
another resource still disables it. Stopping, reloading, or failing a resource
releases its restrictions, and disconnecting clears them all. Requests can be
made before the game creates its menu screens.

`setMenuEnabled` returns nothing. `isMenuEnabled` returns whether no resource
disables that menu; it does not report whether the menu is open or whether native
gameplay rules currently allow it. Unknown menu names and non-boolean `enabled`
arguments throw, and so does calling `setMenuEnabled` outside a resource.

The custom character creator ([character-creator.md](character-creator.md))
may still use the player stage when the vanilla player menu is disabled.
Escape/settings, dialogue,
shops, and HUD elements are outside these six menu groups. Use
`Hud.setElementVisible` for HUD elements.

## Creating a custom screen

```js
const screen = NativeUI.createScreen();

screen.on('ready', () => {
    const panel = screen.root.createClip();       // the stage is 1920x1080
    panel.invoke('beginFill', 0x1a1109, 92);
    panel.invoke('moveTo', 0, 0);
    panel.invoke('lineTo', 840, 0);
    panel.invoke('lineTo', 840, 300);
    panel.invoke('lineTo', 0, 300);
    panel.invoke('endFill');
    panel.set({ x: 200, y: 180 });

    const label = panel.createText({ x: 32, y: 28, width: 776, height: 244, size: 30, color: 0xe7e0cf });
    label.setMember('wordWrap', true);
    label.setMember('multiline', true);
    label.setText('Kingdoms Connected');

    panel.on('press', () => Hud.showNotification('clicked'));
});

Key.bind('f9', 'down', () => {
    if (screen.hasFocus()) {
        screen.blur();
        screen.setVisible(false);
    }
    else {
        screen.setVisible(true);
        screen.focus();
    }
});
```

Four things in that example are the whole model, and each of them is a trap if
it is assumed away.

**A screen is not composed when `createScreen` returns.** The movie it lives in
is loaded by the engine and announces itself when it has run its first frame,
so `screen.root` is null and every call on it fails until then. `ready` is the
event that says otherwise, and it arrives on a later tick even when the movie
was already open for another screen. Build the screen in that handler.

**Positions are stage units, not pixels.** The stage is the movie's authored
size and the engine scales it to the backbuffer, so a point is in the same
place on a 1080p screen and on a 4K one. The bundled hosts are authored at
**1920x1080**, which is also the scale to lay out at: a 360-unit panel is a
fifth of the screen wide, not a third. `screen.stage()` reports the size and
`screen.screenToStage(x, y)` converts a viewport fraction into it.

**Scale and alpha are multipliers here and percentages in the movie.** `set()`
converts: `scaleX: 1` is natural size and `alpha: 0.5` is half opaque. Read a
value back with `clip.get('_xscale')` and it is the movie's own 100.

**Events arrive a tick late.** ActionScript calls into the mod from inside
whatever native call the mod was making, so nothing is dispatched where it
arrives: it is queued and drained once per frame. A handler therefore never
runs inside a page the outer call is still working on, and in exchange a click
is one frame old. Nothing polls -- there is no "is it ready yet" to call in a
loop.

## Drawing with no assets at all

`clip.invoke` reaches the ActionScript method of that name on that object, so
the MovieClip drawing API -- `beginFill`, `moveTo`, `lineTo`, `curveTo`,
`lineStyle`, `endFill`, `clear` -- is available on any clip a screen creates.
That is a filled, stroked, arbitrary shape without shipping a single file, and
it is what the example above draws its panel with.

The other primitive is `createText`, which makes a real native text field:
`setText` writes it, `setText(html, true)` parses the engine's HTML subset, and
the ordinary field properties (`wordWrap`, `multiline`, `autoSize`, `html`) go
through `setMember`.

### Text needs a font, and the font has a name

A field that names no font draws every character as an empty box. It is not a
missing glyph -- it is the field falling back to a device font that has none,
and it is the first thing that goes wrong on a movie of your own, because our
host embeds no fonts.

The game's typefaces are available in every movie, including our hosts.
Select a logical name from `Libs/UI/fontconfig.xml`:

| Name                                                  | Face                            |
| ----------------------------------------------------- | ------------------------------- |
| `DefaultFont`, `DefaultFontBold`, `DefaultFontItalic` | Kingdom Come Regular            |
| `LightFont`, `LightFontBold`, `LightFontItalic`       | Kingdom Light Regular           |
| `DisplayFont`                                         | Kingdom Come Display Md Display |
| `Manuscript`                                          | Warhorse Manuscript             |

`createText` applies `DefaultFont` at size 20 unless told otherwise, and
`setTextStyle` restyles a field afterwards:

```js
const label = panel.createText({ x: 32, y: 28, width: 760, height: 240, font: 'DisplayFont', size: 34, color: 0xe7e0cf });
label.setTextStyle({ font: 'DefaultFont', size: 20, align: 'center' });
```

The CJK configs map the same names onto different faces for the language the
player is running, so a name is the portable thing to ask for and a face is
not.

## The game's own artwork

A screen opened with `NativeUI.createScreen({ library: 'Menu' })` may attach
sprites the `Menu` movie exports:

```js
const button = screen.root.attach('BasicButton');
```

`NativeUI.libraries()` lists what is in the bundle and what each one exports,
read from the inventory the host generator writes rather than from a list kept
in step by hand:

```js
for (const library of NativeUI.libraries()) {
    console.log(library.name || '(default)', library.movie, library.exports.length);
}
```

One screen sees one library, because a host is one movie and one ActionScript
global context: two libraries that export the same class name need two screens.
Opening screens from two libraries is fine -- they get separate hosts.

An attached class has its own constructor, timers and initialization
requirements.

## The game's own screens

`NativeUI.element` drives an element the game already has, rather than
composing a new one:

```js
const menu = NativeUI.element('Menu', 47001);   // our own private instance
menu.call('ClearAll', 0);
menu.call('PreparePage', 0, 400, 6, 'Kingdoms Connected', 1);
menu.call('AddBasicButton', 'first', 0, 'Do the thing', 'A tooltip', false);
menu.on('OnButton', ([id]) => Hud.showNotification(`pressed ${id}`));
menu.show();
menu.call('ShowPage', '');
```

Instance 0 is the copy the game itself drives; any other number is a private
copy of the same element, created from the same XML, which is how a mod puts
its own content on a game screen without fighting the game's. `call`,
`setVariable`, `getVariable`, `setArray`, `getArray` and `setClip` each take a
name the element's XML declares, and `on` takes an event it declares.

The element has to be loaded before its functions exist: `show()` loads it.
Reading from an element the game has not brought up returns null rather than
loading it, because a script reading a value must not be what puts a game
screen on the player's display.

### Hiding a piece of the HUD

To hide part of the HUD, use the resource-owned native HUD API:

```js
Hud.setElementVisible('Stats', false);   // the health and stamina bars
Hud.isElementVisible('Stats');           // false
```

Each name is one the game itself uses (`Stats`, `Compass`, `QAMWeapon`,
`Buffs`, `Subtitles` and so on, listed in the declarations). A `false` request
retains multiplayer suppression, even before the HUD exists. A `true` return
means the request was accepted, not that a Flash clip was already available.
Hidden clips are resolved in the current movie and suppressed after its
update, before each render submission, so movie and level reloads do not
discard the request. This integration does not read or write console variables.

`isElementVisible` reports this requested permission, not whether pixels are
currently drawn. It returns true when multiplayer does not suppress the
element. Turning an element on releases suppression; restoration queries the
native `I_UIHudMask` for the current visibility decision instead of forcing it
up during dialogue or a menu. Resource stop and session end also release
suppression. A later hide request takes ownership from the earlier resource;
stopping the earlier resource does not release the later one's request. If
the movie is unavailable during release, restoration waits for its next draw.
`Stats` contains the bars; `QAMWeapon` and `QAMFood` are separate sibling
elements with their own visibility requests.

## Shipping your own movie

A resource may ship a Scaleform movie and open a screen in it:

```json
{ "mafiahub": { "files": ["ui/**"] } }
```

```js
const screen = NativeUI.createScreen({ movie: 'ui/myscreen.swf', assets: ['ui/plate.dds'], layer: 46 });
```

The client caches the movie under `files/kcdc/native_ui/cache/` and loads
it without a restart or a file in the game directory. The cache uses a hash
of the movie's contents, so a changed movie loads separately from an earlier
version.

A movie shipped this way is on its own: it gets no composition bridge, so
`attach` has only what the movie itself exports, and the focus routing that
carries controller navigation into a page is not available. It is the
`LockPicking` pattern -- dumb art, named clips, everything driven from outside.

## Input

`screen.focus()` takes keyboard, controller and mouse for one screen. It
creates an exclusive action map at menu priority, turns on the element's mouse,
key and cursor flags, and raises the game's hardware cursor. One screen holds
focus at a time, the game takes it back whenever a higher-priority context
wants it, and `screen.on('input', (action, mode) => ...)` carries what the map
delivered: `kcdc_ui_accept`, `kcdc_ui_back` and `kcdc_ui_left/right/up/down`,
with the activation mode the engine raised it with. `screen.on('event', ...)`
carries an event the movie declares, with its arguments.

The cursor is gated on the game's own `wh_ui_InputDevice` reading 1, so on a
gamepad there is no pointer and a screen meant for one navigates through those
actions.

While a native screen holds focus, `Key` bindings stop firing, the same way
they do for a focused web view. Nothing else changes: the game is not paused,
the world keeps running, and other players keep moving.

`clip.on(event, ...)` -- `press`, `release`, `releaseOutside`, `rollOver`,
`rollOut`, `dragOver` or `dragOut` -- installs a real ActionScript mouse
handler and enables the clip as a mouse target. A clip with no geometry has
nothing to hit, so draw or attach something first. `clip.off` removes the
handler.

## Lifetime

A screen belongs to the resource that opened it. It goes when that resource
stops, when the session ends, and when the script closes it; its handlers go
with it. If the engine unloads the movie underneath -- a level load can --
every handle on it stops resolving, `unload` is raised, and the screen has to
be built again from `ready`.

A handle is a number, not a pointer: a closed screen and a removed clip keep
reading, report `valid: false`, and every call on them fails cleanly.

## Limits

| Limit                              | Value                                               |
| ---------------------------------- | --------------------------------------------------- |
| Screens per resource               | 16                                                  |
| Mouse handlers per screen          | 512                                                 |
| Arguments per ActionScript call    | 16                                                  |
| String crossing into ActionScript  | 64 KiB                                              |
| Extra files beside a shipped movie | 64                                                  |
| Shipped movie size                 | 32 MiB                                              |
| Layer of a shipped movie           | 1 to 100, 46 by default                             |
| Queued events per frame            | 4096; any further events are dropped with a warning |

## Coverage against the Lua surface

The community's `UIAction` write-up is the reference list of what the engine's
own Lua bridge can do. Everything on it has an equivalent here except two
entries, and both are absent for a reason rather than for want of time.

| `UIAction`                                                        | Here                                                     |
| ----------------------------------------------------------------- | -------------------------------------------------------- |
| `ShowElement` / `HideElement` / `RequestHide`                     | `element.show()` / `hide()` / `requestHide()`            |
| `ReloadElement` / `UnloadElement`                                 | `element.reload()` / `unload()`                          |
| `CallFunction`                                                    | `element.call(fn, ...)`                                  |
| `SetVariable` / `GetVariable`                                     | `element.setVariable` / `getVariable`                    |
| `SetArray` / `GetArray`                                           | `element.setArray` / `getArray`                          |
| `SetPos` / `SetScale` / `SetRotation` / `SetAlpha` / `SetVisible` | `element.setClip(name, {...})`, one call for all of them |
| `GetPos` / `GetScale` / `GetRotation` / `GetAlpha` / `IsVisible`  | `element.getClip(name)`, likewise                        |
| `GotoAndPlay` / `GotoAndStop` / `…FrameName`                      | `element.gotoClip(name, frameOrLabel, play?)`            |
| `RegisterElementListener`                                         | `element.on(event, handler)`                             |
| no equivalent                                                     | `clip.*` — composing a screen of your own                |

Two things Lua has that this does not:

**Instance `-1` and `-2`.** The Lua bridge takes them as "every instance" and
"every initialized instance". Address instances by their own id
instead: 0 is the game's, and any other number is yours.

**`StartAction` / `EndAction` / `EnableAction` / `DisableAction`, and the
action and event-system listeners.** Those drive UI *flowgraphs* --
`Libs/UI/UIActions/*.xml` -- which is a separate subsystem from elements, and
the lifecycle events they carry are ones a resource already gets from the
framework's own event bus. Nothing here needs them, so nothing here binds
`IUIActionManager`.

Beyond the Lua list, three things that bridge does not have at all:

- **Composition.** Lua can move the 77 clips two elements happen to declare.
  A screen here creates its own clips, fields and geometry, so it is not
  limited to what somebody else's XML named.
- **Masks and hit areas.** `clip.setMask(other)` and `clip.setHitArea(other)`,
  through the host's own helpers, because the native setters have no
  self-binding rejection and a mask belongs to one target at a time.
- **Images.** `clip.load('img://…')` (or `imgps://`) loads through the
  engine's image loader; a movie path inside the mounted archive loads too.
  A missing asset installs the engine's placeholder rather than failing, so a
  true result is not an existence check.

## What it does not do

- **No layout.** There is no flexbox, no text metrics beyond what a field
  reports, and no reflow. Position everything yourself, in stage units.
- **No object values across the boundary.** Numbers, strings, booleans and
  null cross; an object or an array argument does not. `setArray` on an
  element is the exception, and it builds the array natively.
- **No synchronous return from a handler.** A movie callback is answered
  before the script sees it, so a handler cannot veto what raised it.
- **No layer per screen inside a shared host.** Screens in one library share
  its element and therefore its layer. A resource's own movie has its own
  element, and `layer` sets it.
