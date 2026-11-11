# World Builder

Choose **World Builder** in the main menu to work without a server connection.
Choose Kuttenberg, Trosky, or Monastery and click **New project**. Monastery
requires its DLC. **Open project** uses the level saved in your project.
**Load last project** opens the project you most recently opened or saved,
using its saved level and camera position. Its filename appears below the
button. The button is disabled until you have a saved project in the history.
If that file has moved or been deleted, use **Open project** to choose it again.
The web menu disappears before loading starts. After the level loads, the
editor opens automatically. Press **F7** to return to player control; press
F7 again to keep editing. Your project and placed objects stay in the world.
**Return to menu** or the window's **X** ends the offline session and asks
you to save any unsaved changes.

Press **F7** to switch between World Builder and player control in
single-player or an offline view.
During multiplayer, access starts disabled for every connection. A server
script grants or revokes access for one player:

```ts
player.setWorldBuilderEnabled(true);  // Allow F7 and MapEditor.open().
player.setWorldBuilderEnabled(false); // Revoke access and close the editor.
```

The sample gamemode provides `/builder` (or `/builder on`) to grant yourself
access, then F7 opens the editor. `/builder off` revokes it. These are test
commands available to everyone in the sample; a custom gamemode decides
which players may use the builder. The permission resets on disconnect.
The old server-wide `mod.map_editor` setting is no longer used.

Client resources can also call `MapEditor.open()`. It returns `"opened"`,
`"alreadyOpen"`, or `"disabled"` when this multiplayer connection has no
access. `close()` closes the editor, `isOpen()` reports whether it is open,
and `isEnabled()` reports access under the same rule as F7. Offline access
is always enabled. F7 opens from gameplay, without interrupting loading,
chat input, or another screen holding the controls. Leaving a session
closes the editor.

The editor holds the one free camera: a `NoClip` flight in progress ends when
it opens (reason `mapEditor`), and `NoClip.enable` returns `cameraBusy` while
it is open.

For debug geometry during normal gameplay, use the
[client Gizmos API](gizmos.md). It works while World Builder is closed.

---

## Using it

While the editor is open the game's HUD is hidden (`wh_ui_ShowHud`, held
by the multiplayer frontend, which keeps the player's own setting), and it
comes back as it was when the editor closes.

### Camera

| input            | does                                                                                                                                                                                                                               |
| ---------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| hold right mouse | fly on the game's own photo-mode keys (Controls menu: forward, back, left, right, jump to rise, crouch to sink, fast movement), Alt slower; the wheel sets the speed. Rotating the camera keeps the current prop or effect in hand |
| wheel            | close in on whatever is under the cursor, by a share of the distance -- the same notch covers a room or a valley and never passes through the surface                                                                              |
| middle drag      | pan, scaled so the point you grabbed stays under the cursor                                                                                                                                                                        |
| Alt + left drag  | orbit the selection, or what the drag began on                                                                                                                                                                                     |
| F                | frame the selection (a group is framed whole)                                                                                                                                                                                      |
| Camera tab       | speed, FOV, saved views (stored in the map), go to / bring the player, the scale figure                                                                                                                                            |

The fly keys are read from the game's `photomode_camera` action map, so a
rebind in the Controls menu applies to the editor and to NoClip too. They go
through the key's scancode and the active keyboard layout, the way the game
itself reads them -- on AZERTY the defaults are Z Q S D. The help panel
prints them as the layout labels them.

### Travel using the game map

In an offline World Builder session, click **Map** in the toolbar. Click a
point on the native map to move the player and editor camera there. Drag to
pan, use the normal map zoom controls, or press **Escape** to cancel. Your
existing waypoint is left alone.

Travel stays in the loaded region and requires an unmounted player. The
editor waits for collision at the destination before moving the player. If
collision does not become available, it cancels and restores the camera.
You can also press Escape during this wait. A completed teleport cannot be
undone through editor history. Choose an open outdoor location; the map does
not choose an interior floor or guarantee that a spot is walkable.

The project, selections, and armed placement tool remain available when the
editor returns. Map travel is unavailable in multiplayer.

### Scale figure (Camera tab)

To judge a scene's proportions without leaving the editor, stand a person
in it. Rest the cursor on the ground or a floor and press **H**: a figure
stands there, facing the camera. **Stand it in the middle of the view**, in
the Camera tab's **Scale figure** section, does the same for the point the
view is centred on, and **Figure** chooses who stands: a guard, a townsman,
a townswoman and the other roles the patrol preview offers.

**Look from its eyes** moves the editor camera to the figure's eye height,
about 1.65 m above its feet and a step in front of its face, looking where
it looks. Stand the figure in a doorway or at a table and look from its
eyes to see the room as a player standing there will.

The figure stands still, cannot be hurt, and is not part of the project: it
is not saved, exported or undone, and it cannot be selected or moved --
press H again to stand it somewhere else. It stays when F7 hands you the
player, so you can walk around it, and goes when you take it away, change
level or leave the session. One figure stands at a time, and it shares the
game's one preview body with the patrol preview: starting either takes the
other away. Without a level loaded, or where the game refuses the body, the
section says why.

### Conditions (Environment tab)

Build under the light the scene is meant for. The **Environment** tab sets
the hour, the sky and the rain the project is previewed in. These are
preview conditions: they are saved with the project, they are not authored
content, and nothing a server reads back carries them.

The tab is offline only. While you are connected the server owns the clock
and the weather, so a sky changed here would be yours alone; the tab says so
and the client refuses the write. Use the server's own `/world` commands in
multiplayer.

**Time of day**

- **Pin the sky to one hour** lights the level from that hour without moving
  the world clock, so nothing the level schedules by time of day moves with
  it. The row of shortcuts -- Dawn, Morning, Noon, Afternoon, Evening, Dusk,
  Night -- pins the sky as well as choosing the hour. 0 and 24 are the same
  midnight.
- **Clock speed** is the world clock's own multiplier, and 0 stops it. Stop
  it to keep the light still while a scene is being built; **Normal (x15)**
  is the game's own speed. When the game is holding the clock for its own
  reasons, the tab says so -- changing the speed does not lift that hold.
- **Move the world clock** moves gameplay time itself, which is what the
  level's schedules read. Offline the clock may be set back as well as
  forward; in a session it only ever moves forward, which is why this control
  exists only here. **Skip ahead** advances up to 72 hours.

**Weather**

KCD2's weather is a time-of-day preset: there is no separate weather dial.
The preset list is the level's own, read from the level as it loaded, so a
project carried to another level finds none of its presets and the tab says
which one is missing. Select a preset to blend it in; **Blend** is measured
in game seconds, and 0 applies it at once. **Apply again** blends the chosen
preset back in after the game has moved the sky itself.

- **Hold this sky** suspends the weather system so the chosen preset stands.
  Left running, the game replaces a blended preset after six game hours and
  then rolls its own next sky.
- **Rain by hand** chooses the rain instead of letting the weather system
  calculate it, with Dry, Drizzle, Rain and Downpour shortcuts. Turning it
  off restores the automatic calculation.
- Holding the sky and raining by hand cannot both run: the same switch that
  suspends the weather system stops rain being submitted to the renderer.
  The tab says so rather than appearing to do both.
- **Force an update** and **Rebuild the clouds** are repairs for a sky that
  has not caught up with the preset.
- **Weather profiles the level schedules** lists the records the game picks
  presets from, by temperature, rain and wind. Read-only: a profile name is
  not a preset name and cannot be blended.

**Back to the level's own conditions** clears the preview: the sky, the clock
speed and the rain go back to the game. A preset already blended in stands
until the weather system rolls its next one, because a blend cannot be taken
back. Leaving the offline session hands the same three back on its own, so a
pinned sky never follows you into a server.

F7 keeps the conditions, which is the point of them: hand the controls back
and walk the level in the light it was built for.

### The asset library (Library tab)

The shared prop catalog -- 8,456 static meshes -- as a tree built from the
whole directory path under `objects/`, not the catalog's two-segment `group`.
`manmade/structures` alone is 3,683 meshes; in the tree it is
`structures/houses/walls/...`.

- **Search**: every word must match the name or the path; `-word` excludes.
  `fence -broken` or `taverns/ table`.
- **Development folders** (`intermediates`, `test_souradnice`,
  `preproduction`, `helpers`, `ui`, `particles`, `default`,
  `animation_props`) are hidden until the checkbox asks for them.
- **Favorites** belong to the active project folder. **Recently placed**
  stays in `maps/library.prefs` beside KCDCClient.dll.
- **Preview**: rest the mouse on a row and the real mesh, untinted, turns in
  front of the camera. Its measured size fills the Size column from then on.
- **Pick up**: click a row. A translucent copy follows the cursor over the
  world -- blue on a surface, red in the air -- and a left click puts one
  down. It stays in hand for the next one; Escape or releasing right mouse
  without rotating the camera puts it away, even while a search field has
  focus. A right-mouse drag lets you look around and continue placing.

While a mesh is in hand: Shift+wheel or R turns it (by the rotate snap step
when snapping is on), Ctrl+wheel resizes it. The Placement section sets the
physics it is built with, standing it on the slope, a ground grid, random
turn and size per placement (for scattering rocks and bushes), a height
offset, and where it goes when the cursor points at the sky.

If a model cannot create collision, placement with static or rigid physics
fails with a message. Choose **None** to place it as decoration.

### Doors, interactive objects and items

The Library also lists three kinds of things the server builds as working
objects rather than as meshes. They sit beside the mesh folders and work
like any other row: search, favorites, favorite folders and recently placed
treat them the same.

- **`doors`**: every door leaf the shipped levels hang. The detail column
  says how many doors in the levels use it. A door opens, locks and takes a
  key or a lockpick on the server, as the level's own doors do.
- **`interactive`**: benches, chairs and stools under `seats`, beds under
  `beds`, the dice table under `games`, and the alchemy tables, the smithy,
  the drying rack and the smokehouse under `stations`. Players sit, sleep,
  play dice and craft at them on the server.
- **`items`**: every shipped item class, by the game's category. Each one is
  a stack lying on the ground that players can pick up.

Pick one up and place it as you would a mesh. These keep their own size, so
Ctrl+wheel and random size do nothing, and the Placement physics setting does
not apply to them. While one is in hand, a door's leaf and a worn item show as
a box, because the preview cannot draw them; once placed you see the real
thing.

In the editor, a placed door shows its leaf and an item shows its mesh. A
worn item, which the editor cannot stand up as a mesh, shows as a small box.
An interactive object stands assembled as players will see it.

Select one and the **Object** tab shows what the export carries for it, in
place of Scale and Physics. Each change is one undo step.

- **Name**: what a server script finds it by, up to 64 bytes.
- A door: **Locked**, **Can be picked**, **Lock** (no lock, a bolt on one
  side, a bolt and a keyhole, or a keyhole on both sides), **Own difficulty**
  for the lockpicking minigame (off uses the leaf's own), and **Key**, the
  item class GUID of the key that opens it.
- An item: **Amount**, **Quality** (0 for the class's own), and **Own
  health** and **Own condition** (off uses the class's own).

The level's own doors and stations are level objects: select one and delete
it as any other, and the export takes it out on the server too. The level's
own pickable items never appear on a server, so there are none to remove.

### Particle effects (Effects tab)

The 588 effects the game ships, browsed the same way: a tree from
`library.group.effect`, search, favourites and recents. Resting on a row
plays the real effect in front of the camera; clicking it picks it up, and
the effect itself follows the cursor until a click places it. Grid, surface
alignment and scatter are shared with the mesh brush.

- An effect is drawn as a diamond marker, since an idle smoke or a dim
  flame is hard to click. Selecting, moving, turning, grouping, duplicating,
  deleting and undo work as they do for props.
- The Object tab tunes a selected effect: size, particles, speed, time,
  strength, a repeat interval and "start already running". Tuning is read
  when an emitter starts, so each change restarts it.
- A one-shot effect plays once and is gone, so new ones repeat every few
  seconds by default ("One-shot repeat").
- Effects share the client's budget of 192 emitters with everything else
  playing; the Effects tab shows how much is left.

### Selecting and editing

Left click selects anything: a prop you placed, or the level's own brushes,
vegetation and entities. Ctrl+click adds to or takes out of the selection;
the gizmo sits on the first and the rest follow it as one body -- moved,
turned about it and scaled from it.

A click selects what the cursor is on, not what it is near:

- A solid object you placed -- static or rigid physics -- is selected where
  the cursor is on its collision. The box around it does not count: a click
  that passes a fence panel's box and lands on the table behind it selects
  the table.
- An object without collision -- physics **None**, a door's leaf, an item --
  is found by its box, which now turns and scales with the object instead
  of standing square to the world. It is selected when the cursor crosses
  that box in front of the surface behind it. When that surface is a
  smaller object you placed, the smaller one is selected: a cup under a
  canopy, not the canopy.
- A box the camera is inside is never taken, so standing inside a large
  decoration no longer sends every click to it. Step outside it to click
  it, or select it with a box, a lasso or the Layers tab.
- A solid object whose collision does not cover all of its mesh, such as a
  tree that is solid at the trunk only, is still selected by its box when
  the cursor is on nothing else you placed. A click on the ground under its
  crown stays a click on the ground.

The label beside the cursor names what a click there would select.

Undo and redo select what they changed, when it is still there: Ctrl+Z after
moving a prop gives you that prop back, even one that is hard to click
because it stands beside something larger. Undoing a deletion selects what
came back. When nothing the step changed is left, for example after undoing
a placement, the selection stays as it was.

A click always picks one object. What a drag with the left button does is
set under **Drag** on the toolbar:

- **Box** (the default): drag a rectangle. Everything whose position is
  inside it on screen, within **Box and lasso reach** of the camera, is
  selected.
- **Lasso**: drag an outline around the objects. **L** switches the drag
  between box and lasso.
- **Brush**: **B** turns it on or off. A drag then paints: the brush takes
  whatever it passes over, as a click there would pick it, the first plant
  it passes over, and everything standing within **Brush radius** of the
  ground under it. The brush is drawn as a circle on that ground. A tree is
  taken by its crown, however far above its trunk's foot the crown is; a
  click on a crown does not take the tree, so that it cannot steal clicks
  on what stands under it.

A box or lasso replaces the selection; a brush stroke adds to it, so a
second stroke carries on where the first stopped. Hold **Ctrl** to add with
a box or lasso, or **Shift** to take objects out with any of them. Escape,
or a click on empty ground, starts again. **Visible only** leaves out
objects the camera cannot see, behind a wall or under a roof.

**Selection settings**, on the toolbar, holds the brush radius, the reach,
**Visible only** and the filter every tool uses. The filter can limit the
kinds (props, effects, lights, level brushes, vegetation, level entities),
match search words (a word with a leading `-` must not appear), an exact
mesh or entity class (**From selection** fills either in from the selected
object), a placed group, a layer and a prop's physics. Objects taken out of
the world are left out unless **Include objects taken out of the world** is
on. **Select all matching within reach** selects everything around the
camera that the filter lets through.

To select trees with the brush, open **Selection settings**, leave only
**Vegetation** ticked, press **B** and paint over the trees: the ground,
rocks and buildings you pass over stay out. To take only one kind of tree,
select one and press **From selection** beside the mesh as well.

**Select similar nearby**, in the Object tab and in **Selection settings**,
selects every object with the same mesh as the selected one within the
settings' **Radius**; a level entity without a mesh matches by class. The
filter's layer, group, search words, physics and hidden setting still apply;
its kinds, mesh and class do not, because the selected object decides those.
To remove one kind of tree from a stretch of land, select one, click
**Select similar nearby**, then press **Delete**. In the Areas tab,
**Select objects inside this area** uses a drawn area as the boundary
instead, with the same filter.

Selection tools add up to 5000 objects. The level's own objects are found
within 250 metres of the camera along each axis. A crowded stretch is
searched in smaller pieces; the status line says when a search was still cut
short. Level objects a tool selected that nothing changed are let go the next
time you use a selection tool, so they never reach the saved project. Grass
and small plants are merged into the level's terrain detail and cannot be
selected.

These are the default keys. The **Keys** tab changes any of them: click an
action, then press the new key with any Ctrl, Shift or Alt it needs; Escape,
a click anywhere else, flying or leaving the tab cancels. **W E R** puts move, rotate and scale on W, E and R, turning the mesh
being placed on T. **Default: 1 2 3** puts everything back. Select similar
has no key until you give it one. A key without Ctrl or Alt does nothing
while you fly, so the fly keys stay free; two actions on the same key both
happen, and the tab warns about it. Escape, the mouse, F7 and Ctrl+Shift+Z
for redo cannot be changed.

The keys are saved for you rather than for a project, in
`editor.settings.json` beside your projects, together with the gizmo tool in
hand, Keep proportions, whether the key help shows, whether a drag draws a
box or a lasso or paints with the brush, and the brush radius, reach,
similar radius and Visible only.

| key             | does                                                                                                               |
| --------------- | ------------------------------------------------------------------------------------------------------------------ |
| 1 2 3           | move, rotate, scale                                                                                                |
| B               | brush selection on or off                                                                                          |
| H               | stand the scale figure under the cursor                                                                            |
| L               | a drag selects by box or by lasso                                                                                  |
| G               | local or world axes                                                                                                |
| X               | snapping for the tool in hand (each tool keeps its own step)                                                       |
| Ctrl+D          | duplicate the selection; a level object becomes an editable prop of the same mesh; the copies become the selection |
| Del             | delete props, take the level's own out (reversible); on one already out, puts it back                              |
| End             | drop to the ground                                                                                                 |
| Ctrl+Z / Ctrl+Y | undo / redo -- every placement, move, delete, physics change and removal, a whole drag as one step                 |

The Object tab has the numbers: position, angles, scale with a "keep
proportions" lock (on by default), and physics. Collision updates automatically
200 ms after scaling pauses, including group edits and undo/redo. Stretched
props still use their largest axis for collision. **Rebuild its collision**
remains available to retry a failed rebuild.

Hold **Shift** while dragging a translate gizmo to snap to the ground. This
works with world and local axes, plane handles, props, effects, whole areas,
area points and edges, and patrol waypoints. Props keep their bottom on the
ground; effects and waypoints use their position. Groups retain their relative
positions. If the ray finds no ground, the translation stays where you moved it.
Shift-clicking area outlines away from the gizmo still adds or removes areas
from the selection.

### The level's own

The **Level → Nearby** list shows what the level itself put around the camera --
entities by class, brushes and vegetation by mesh -- within a radius,
filterable, nearest first. Click selects, double-click frames. Objects
already hidden are listed dimmed.

Viewport picking prefers solid level objects by default. A collision hit on
a bucket or building takes priority over overlapping decoration bounds.
Precomputed level colliders are matched back to their mesh and placement,
so large buildings can be picked without the fallback's size restrictions.
Under **Level → Nearby**, turn off **Prefer solid objects** to pick non-solid
decoration in front of walls using bounding boxes. The nearby list also
selects these objects directly. This choice is saved with the project.

To change a level object like a placed prop, select it and use **Copy as a
prop** (Ctrl+D) or **Convert to a prop** in the Object tab. **Copy as a prop**
places an editable static prop of the same mesh beside it. **Convert to a
prop** takes every selected level object out of the world and puts such a
prop where each one stood, as one undo step; undo brings the originals back
and removes the copies. Both work only for meshes in the asset library. A
level brush can use a different material than its mesh's own, and the copy
does not carry that change. Characters and other objects without a library
mesh are skipped, and the status line says how many were converted.

To put a different mesh where a level object stands, select it and type in
the **Swap for a mesh** box in the Object tab. It searches the asset library;
click a result to swap. The object goes out of the world with its collision,
and the chosen mesh stands in its place as solid static scenery: a stump
where a tree was, or a ruin where a house stood. The swap keeps the object's
position, rotation and scale, and follows it if you move it. **Take the swap
back** removes the mesh again. Ctrl+Z undoes a swap like any other edit. The
swap is saved with the project and exported in the map's `world` entry as
`model`, so a server running the export swaps it for every player.

**Level → Deleted (count)** lists the level objects you have removed, with
search by name, class or mesh. The list is independent of the nearby radius;
objects stay listed when the game unloads them, and saved deletions return
when you reopen the project. Swapped objects are listed too, as **Swapped**.
Click a row to select it, or double-click to frame its position. **Restore**
brings a deleted object back while keeping any move you made, and **Unswap**
gives a swapped one back. Turn on **Include moved objects** to also see objects you
moved without deleting. **Restore all level edits** undoes every level-object
move and deletion, including objects hidden by the search filter.

Taking a level object out hides it and its collision, and is reversible.
The Level tab's **Deleted** list lets you bring it back. Moving a hidden
object keeps it without collision.

A map loaded while some of its objects are streamed out keeps their edits
waiting instead of dropping them, and the status line counts them. While
an object is streamed out the row says **far**. From far off, the level's
baked distant mesh still shows the original; up close it is gone.

Placed props cast shadows.

### Areas (Areas tab)

The tab shows the level's own gameplay areas, read from the compiled area
catalog: trigger areas, unions and smart areas, each class toggled on its
own, filtered by name, label or location and listed nearest first within a
radius. Selecting one, in the list or by its marker in the world, inspects
it, including the doors it links.

Use **Create editable copy** to turn a game area into a custom polygon at
the same location, with its name, labels and vertical bounds. You can then
change its ID, name, labels and shape. **Copy game area definitions** copies
the same definitions for **Paste definitions**. A union becomes separate
polygons for its members. Polygons outside the custom-area limits, including
more than 256 corners, are skipped with a notice. The original game area
stays as it was.

Use **+ Box**, **+ Shape**, or **+ Sphere** to draw a new area. Select it in
the list or by its marker, then edit it in the inspector or in the world:

- Click a polygon corner, then drag the arrows to move that point. You can
  also select **Point 1**, **Point 2**, and so on in the inspector.
- Click an edge or its midpoint handle to move both endpoints together.
  Double-click an edge to add a point where you clicked, including the edge
  joining the last point back to the first. **Split selected edge** adds a
  new corner halfway along it.
- **Delete** removes the selected point, or both endpoints of a selected
  edge. If fewer than three points would remain, it deletes the whole area.
  The point's **x** button in the inspector follows the same rule.
- Click a sphere's **Radius** handle and drag its arrow to resize it.
- Click a **Box face** handle to move that face while the opposite face
  stays in place. A polygon's **Height** handle raises or lowers its ceiling.
- **Escape** or **Move whole area** returns to the whole-area gizmo.
  Point and edge handles use the local/world setting; size handles follow
  the dimension they change. Handles use translation snapping.

Click an area's marker or outline to select it. **Shift-click** in the list
or viewport adds or removes an area from the selection. Drag the gizmo to
move or rotate the selection together; scaling several areas keeps their
proportions. Select one area to edit its points or size handles.

With areas selected, enter a **Group name** and click **Group selected**.
**Ungroup selected** removes only the selected areas from their groups.
Choose a group in **Area group** to show its members; **Select group**
selects them together. **Select all areas** is available when showing all
groups. **Duplicate selected** and **Ctrl+D** copy the selected drawn areas.
**Delete selected areas** and **Delete** remove them when no point or edge
is selected. The delete button changes to **Delete point** or **Delete edge**
while editing that part.

Use **Copy selected definitions** or **Copy selected areas as API calls**
for a selection, or **Copy group definitions** and **Copy group as API
calls** for the current group. **Copy all definitions** includes every drawn
area. **Paste definitions** accepts a single definition or an array, keeps
group names, and gives duplicate IDs a free suffix. Groups are saved in the
project and omitted from server API calls and runtime exports.

**Collision** turns a drawn area into an invisible boundary. **Floor**,
**Ceiling** and **Wall thickness** set how it is built, and **Show
collision** shades the result for the selected areas. See
[Area collision](area-collision.md).

Drawn areas are `Area.create` definitions, optionally bound to one virtual
world. The editor shapes them; the server tests them. They are saved in the
map's `areas`. Outlines use the client [Gizmos](gizmos.md) renderer.

### Projects and recovery

The **Project** tab has **New**, **Open**, **Recent projects**, **Save**, and
**Save as**. Open and Save use the Windows file picker, so projects can live
in any folder. **Ctrl+S** saves; **Ctrl+Shift+S** saves under another name.
An asterisk marks unsaved changes. Opening another project, starting a new
one, or returning to the main menu offers to save your changes first.
F7 keeps the project in memory. Offline, the window's X uses the same save
prompt as **Return to menu**. In multiplayer, X returns to player control.

Drag the editor panel's edges or corner to resize it, or its title bar to move it.
Tabs wrap onto additional rows to fit the panel width.

A `.project.json` file keeps the level, placed props and particle effects,
level object edits, areas, groups, saved camera views, the current camera
position and rotation, the preview conditions from the **Environment** tab,
resource name, and export directory. Opening the
project restores the camera to where it was when saved. Recovery saves keep
the camera too. Older projects without a camera open at the current view.
Keep this file to continue editing. It is not a server resource.

While a project has unsaved changes, a recovery copy is written every
30 seconds to `maps/recovery.project.json` beside KCDCClient.dll. **Recover
autosaved project** opens this latest copy as an unsaved project. Save it
under a name of your own. There is one recovery slot, shared by projects;
it is not a version history or a substitute for saving.

Entries that cannot be placed are retained in the project instead of being
lost on the next save. Older projects open with default viewport settings,
an empty project favorites folder, every object in Default, and the level's
own conditions.

### Grid and gizmo coordinates

Use **World axes** or **Local axes** in the top toolbar, or press **G**, to
change the move and rotate gizmo axes. Scaling always follows local axes.
The selected coordinate space and snap increments are saved in the project.

**Grid** beside **Snap** displays a horizontal world-space grid. It starts
disabled. The grid follows the camera in whole cells, so its lines remain
aligned to world coordinates. In **Grid settings**, **Match move snap** uses
the translation snap increment even while rotating or scaling. Disable it
to set **Spacing (m)** independently. **Line width (px)** sets thickness from
1 to 8 pixels; it starts at 1.5 pixels. The height slider offsets the grid
relative to the camera; it starts two metres below it. Drawing the grid
does not enable snapping or change placement.

### Favorite folders

Favorites in the **Library** and **Effects** tabs belong to the project.
Choose an active favorite folder, then click the star beside an asset to
add or remove it in that folder. Select the favorites list to place assets
from it. Add or rename custom folders using the folder controls. Right-click
a favorite to move it to another folder. Deleting a custom folder asks for
confirmation and removes its saved favorites, without changing placed
objects. The root **Favorites** folder remains available.

Favorites are included in project saves, recovery copies, and undo history.
Opening another project switches to that project's folders. Recently placed
assets remain a local library preference.

### Layers

Use the **Layers** tab to organize placed props, particle effects, and level
objects. **Default** contains every unassigned object. It cannot be renamed
or deleted; its members are not listed. It has the same visibility and lock
controls as custom layers.

Create a layer, select objects, and click **Assign selection to active
layer** or press **Ctrl+L**. Selecting a custom layer lists its members.
Click an editable member to select it; Ctrl+click adds it to the selection.
Custom layers are independent of Default. Hidden objects cannot be selected
or changed; locking leaves objects visible and prevents editing. Areas and
patrol authoring follow Default's visibility and lock controls.

Newly placed objects start in Default. Show and unlock Default to place
objects, then assign them to a custom layer. Copies inherit their source
layer. Deleting a custom layer requires confirmation and returns its objects
to Default. The objects themselves stay in the project.

Layer settings and memberships are saved with the project. Visibility is
an editor preference: exporting includes objects in hidden layers, and
layer visibility does not write authored hide edits or remove collision.

### Exporting to a server

1. In **Project**, set a **Resource name** and **Output directory**. Use
   **Choose export directory...** to browse folders inside the editor.
   Open a child folder, use **Parent folder**, or enter a path and press
   **Go** to switch drives. **Use this folder** confirms your choice;
   **Cancel** keeps the previous output directory.
2. Click **Export world resource**. This writes `<name>.world.json` in that
   directory. Exporting does not save your editable project.
3. Copy the export to your server, or export directly into a local server's
   folder. Add its path to `mod.world_resources` in `server.json`:

```json
{
  "mod": {
    "level": "kutnohorsko",
    "world_resources": ["worlds/market.world.json"]
  }
}
```

Paths are relative to the server's working directory. Restart the server
after updating an export or changing this list. The server checks the file
format and level before running. A missing, malformed, or wrong-level file
stops startup with an error identifying the file.

The server creates the props, effects, level edits, areas, doors,
interactive objects and items automatically, in the global world.
Props and effects use the existing replication and distance streaming;
level edits reach every player, including players who join later.

Every prop within 250 m of a player reaches that player, however densely the
map is furnished. A prop with `static` or `none` physics stands as level
geometry does: the game draws it as far as it draws the same mesh placed in
the level, so a table shows from much farther away than a cup, and a
`static` one collides wherever it stands. A `rigid` prop is a physics object
that can fall and be pushed. Areas are
available to server scripts through the Area API. No loader script is needed.
Exports are the one way to bring a whole world into a server: `Prop.spawn`,
`Vfx.spawn`, `LevelEdit.apply` and `Area.create` each create a single object
at runtime.
The server reads the export locally and sends the resulting world state to
clients; it does not send your project or the export file itself.

Exports contain no groups, saved camera views, or editor preferences. Both
client and server must use v2.0.0 or later compatible builds because prop
replication now carries separate X, Y, and Z scales. Physics still uses the
engine's existing prop collision behavior.

### Reading exports from server scripts

`WorldResource.all()` lists the loaded exports in configuration order.
`WorldResource.find("market")` returns the export named `market`, or `null`.
The default name is the filename without `.world.json`. Names are
case-sensitive and must be unique. If two files have the same name, give
them explicit names in the config; ordinary string entries still work:

```json
"world_resources": [
  "worlds/market.world.json",
  { "name": "village-market", "path": "village/market.world.json" }
]
```

Each resource has read-only `name`, `path` (the resolved absolute file
path), `level`, and `loaded` properties. Its `props`, `effects`,
`levelEdits`, and `areas` properties return fresh arrays of the existing
`Prop`, `Vfx`, `LevelEdit`, and `Area` handles. Use those handles as usual.
Destroyed objects and restored level edits disappear from subsequent
reads. Changing an array does not change the export's membership. A
resource remains loaded even if scripts remove all of its contents.
An export can also hang doors, place seats, beds and crafting stations, and
lay items, listed through `doors`, `smartObjects` and `items`; see
[World objects](world-objects.md#world-resources).

Exports load after the server scripts start. Before the whole configured
list has loaded successfully, `WorldResource.ready` is `false`, `all()`
returns an empty array, and `find()` returns `null`. The server then sets
`ready` to `true` and emits `worldResourcesReady` once, with no arguments.
An empty configuration also becomes ready. A failed load publishes no
resources, emits no readiness event, and prevents server startup.
Spawn handlers must leave newly exported props and effects alive until
loading finishes. To remove selected contents on startup, do so from
`worldResourcesReady`.

Use this pattern at the top level of a server script. It also works for a
script started or restarted after the exports have loaded:

```ts
function useWorldExports(): void {
    const market = WorldResource.find("market");
    if (!market) return;

    console.log(`${market.name}: ${market.props.length} props`);
    for (const area of market.areas) {
        console.log(`Exported area: ${area.name}`);
    }
}

if (WorldResource.ready) {
    useWorldExports();
} else {
    Events.on("worldResourcesReady", useWorldExports);
}
```

Do not await this event inside `resourceStart`: exports load after script
startup finishes. Readiness handlers are notifications; their promises
are not awaited. The event is not replayed for scripts started later.

These exports belong to the server. Stopping a script does not remove
them. Changes through their object handles last only for this server run;
they are not written back to the export. This API exposes inspection and
existing object operations; loading, reloading, and unloading a whole
export still require updating the config/files and restarting the server.

### Copying API calls

Select a placed prop, particle effect, or edited level object and click
**Copy selection as API calls**, or press **Ctrl+C** while the viewport has
focus. Ctrl+click selects several entries. The clipboard contains only that
selection as server `Prop.spawn`, `Vfx.spawn`, or `LevelEdit.apply` calls,
including transforms and effect tuning. Paste it into your server resource.
The **Areas** tab has **Copy selected areas as API calls** for `Area.create`.
**Copy whole project as API calls** is in the Project tab.

### Groups and blueprints

Select props and effects with Ctrl+click, then use **Blueprints > Group
selection**. Clicking a member selects the group; the gizmo moves, rotates,
and scales its members together. **Ungroup** keeps the members in place.
Grouping supports undo, and group membership is saved with the project.
Duplicating several entries copies them as one block, beside the original
by the selection's own width, and keeps their arrangement. Each copied group
becomes a new group: a copy of `market` is `market-2`.

**Save selection as blueprint** writes a `.blueprint.json` file. **Open
blueprint** chooses a saved file; **Place blueprint in front of camera**
places a copy eight metres ahead, ready to move with the gizmo. Positions
are relative to the first selected prop or effect, so the arrangement can
be reused in another project or level. Level object removals and moves
cannot go into a blueprint because they refer to the original level.


---

## Patrol routes

The **Patrols** tab creates named NPC routes, edits their waypoints and previews
them with a temporary NPC. Routes save in projects, blueprints and world
exports. See [NPC patrol routes](patrol-routes.md) for controls, scripting,
preview requirements and sample commands.

## Not done yet

Offline entry, F7 and player control, saving, area outlines, panel controls,
and gizmo recovery have been checked in game. Project and blueprint round
trips, API export, file overwrite, server resource loading, and interrupted
gizmo drags have automated checks. Replication of exported worlds to another
client still needs an in-game pass. Other things to watch:

- the turntable preview floats in the world in front of the camera, so it
  can clip into a wall the camera is close to;
- an entity hidden with `IEntity::Hide` that a quest script shows again is
  re-hidden only while the editor holds it;
- the drag-to-orbit and pan scales are first guesses.

Missing, in the order they would pay off:

1. Snapping to other objects -- surface, edge and pivot snaps, which is what
   fitting walls and fences together wants.
2. A grid drawn on the ground while grid snapping is on.
3. Placing entity classes, not only meshes and effects -- lights, doors,
   NPC spawners.
4. Selecting the level's own particle effects (to hide a campfire's smoke):
   their entity class has not been checked in KCD2, and the picker skips
   entities with no geometry.
5. Publishing an updated export to a running remote server without a
   restart. The current workflow exports locally, then uses a file copy and
   a server restart.
