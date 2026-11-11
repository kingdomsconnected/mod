# World Builder MCP

The offline World Builder exposes its authoring tools to local MCP clients.
Open **World Builder**, choose a level, then open the **MCP** tab. Enter a
port and select **Start MCP server**. The default port is `7781`.

Select **Copy connection configuration** and add that configuration to your
MCP client. It contains only the endpoint: the server requires no
authorization, token or header. Clients must support HTTP MCP.
No account, internet connection, external service, or additional runtime is
needed to use the server.

The endpoint is `http://127.0.0.1:<port>/mcp`. It listens only on loopback.
Starting remembers the port and enables automatic start when you next enter
the offline World Builder. **Stop MCP server** disables automatic start.
Leaving World Builder or closing the client stops the listener without
changing that preference. A port conflict reports an error and keeps your
chosen port; it does not silently choose another one.

The server is unavailable in multiplayer. Closing the editor viewport with
F7 while remaining in the offline session retains the listener, allowing
`wb_editor_open` to reopen the viewport.

## Discover and edit

Use MCP `tools/list` to obtain the current tools and their schemas. Follow
`nextCursor` when present. Tool discovery is the authoritative argument
reference; clients should not infer argument names from tool names.

Start with `wb_get_state`. Editing tools require `expectedRevision` from
that response and a unique `idempotencyKey`. If the revision is stale, read
the current state and reconsider the edit. Repeating the same request with
the same key returns its existing operation while that operation is retained.
Reusing a key with different arguments is rejected. Receipts are bounded;
an expired operation cannot be used as a permanent transaction record.

Native work runs through the editor's command queue. A tool can return an
`operationId` and `status: "queued"`. Poll `wb_operation_get` or
`wb_operation_wait` until it completes or fails; `pollAfterMs` gives a delay.
The wait tool returns immediately, so it cannot block the editor's frame.
Successful HTTP delivery or a queued receipt does not mean an edit succeeded.
Check the completed result and any unresolved document entries.

`wb_operation_cancel` cancels native jobs that have not started. It cannot
reverse a running operation. Use editor history to reverse an applied edit.
`wb_batch_apply` runs an ordered native batch, reports each step, and stops
at the first failure. It is not atomic; completed steps remain applied.

Object IDs are opaque and belong to one running editor and level generation.
Do not store them in project files or reuse them after a level change. Native
level browse results also expire when their listing is replaced. Saved views
use indices, so a current revision is required before editing a view.

`wb_levels_list` reports supported levels and local DLC availability.
`wb_level_load` requires explicit discard confirmation and returns transition
acceptance. The listener pauses while the level loads, then resumes on the
same port. Reconnect and read `wb_get_state` to confirm the new level is ready.

## Map travel

`wb_map_open` opens the native map while the offline editor is open. The user
can click a destination or press Escape to cancel. `wb_map_travel` accepts
`position: [x, y]` in world metres and uses the same travel controller without
requiring a mouse click. It resolves the height in the loaded region, waits
for destination collision, and moves the unmounted player and editor camera.
It does not load another region or edit the player's waypoint.

Both tools use the normal `expectedRevision` and `idempotencyKey` fields.
Their completed operation acknowledges the request. Poll `wb_map_get` for
the travel outcome: `status` progresses through `opening`, `selecting`,
`closing`, `streaming`, and `placing`, then ends at `arrived`, `cancelled`, or
`failed`. Direct coordinate travel starts at `closing`. Wait for `active:
false` before treating that outcome as complete. `sequence` identifies the
travel request; `message` explains a failure and `destination` is either a
three-coordinate position or null. This state also appears as `mapTravel`
in `wb_get_state` and `worldbuilder://state`.

`wb_map_cancel` cancels selection or streaming and restores the editor
camera. Once placement has been queued it rejects cancellation. While travel
is active, other editing tools are rejected; state, operation receipts,
logs, and viewport captures remain available. Travel does not add an undo
entry. The collision check does not guarantee a walkable destination or
select an interior floor.

## Coverage

| Area                           | Tools                                                                                                                                                                                                                                                                 |
| ------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Editor and levels              | State, open/close, level catalog and DLC availability, offline level load                                                                                                                                                                                             |
| Assets                         | Prop/effect search, metadata, preview, favorites and recents                                                                                                                                                                                                          |
| Placement                      | Physics, surface/grid alignment, random yaw/scale and effect tuning                                                                                                                                                                                                   |
| Props and effects              | Query, create, transform, duplicate, delete, groups, ground placement, physics and collision rebuild                                                                                                                                                                  |
| Doors, smart objects and items | Discover through `wb_assets_search`, create through `wb_props_create`, read with `wb_object_get`, edit settings with `wb_object_settings_update`; transform, duplicate, delete, groups and layers as props                                                            |
| Point lights                   | Create, inspect, move, recolor, change intensity/radius/shadows, duplicate, delete, groups, layers and history                                                                                                                                                        |
| Selection and history          | Native objects, authored areas and routes; region selection (`wb_select_in_rect`, `wb_select_in_polygon`, `wb_select_in_radius`, `wb_select_brush`, `wb_select_in_area`, `wb_select_similar`) with a shared filter; shared undo/redo, which reselects what it changed |
| Keys                           | Read and rebind the editor's keys, including the Default and W E R presets (`wb_keymap_get`, `wb_keymap_update`)                                                                                                                                                      |
| Native level objects           | Nearby browse, inspect, adopt, transform, hide, swap for an asset-library mesh (`wb_level_object_update` with `model`), restore, and copy or convert into props (`wb_objects_duplicate`, `wb_objects_convert`)                                                        |
| Plants under placed objects    | Take the level's low plants out from under placed objects (`wb_plants_flatten`), or as each is placed (`flattenPlants`, `plantMargin` and `plantMaxHeight` in `wb_placement_update`)                                                                                  |
| Native areas                   | Query, inspect, point containment and copy into authored areas                                                                                                                                                                                                        |
| Authored areas                 | Create, inspect, replace, duplicate, delete, groups and point containment                                                                                                                                                                                             |
| Patrols                        | Route and point authoring, NPC role discovery, preview start/progress/stop                                                                                                                                                                                            |
| Pose anchors                   | List where the game poses a body near a point: placed smart objects' seats, beds and stations, and the level's own (`wb_pose_anchors_query`)                                                                                                                         |
| Scale figure                   | Stand a temporary person of an NPC role at a position or under a screen point, read it and its eye point, take it away (`wb_figure_place`, `wb_figure_get`, `wb_figure_remove`)                                                                                      |
| Camera                         | Read/set, frame, saved views and restore                                                                                                                                                                                                                              |
| Environment                    | Preview conditions (hour, clock speed, sky preset, rain), level preset and profile discovery, and the game's own clock                                                                                                                                                |
| Spatial queries                | Raycast, what a click at a screen point would select (`wb_pick_at`), ground probe and point projection                                                                                                                                                                |
| Projects                       | New/open/save/recover, metadata, document inspection/import/validation and file listing                                                                                                                                                                               |
| Outputs                        | Blueprint save/place, world resource export and server API export                                                                                                                                                                                                     |
| Diagnostics                    | Operations, bounded audit log and viewport PNG capture                                                                                                                                                                                                                |

Resources provide readable state through `worldbuilder://state`,
`worldbuilder://project`, `worldbuilder://selection`,
`worldbuilder://capabilities`, and `worldbuilder://logs`. The project resource
contains metadata; use paginated `wb_document_get` for its collections.

File tools can access the `maps` directory beside the client and the export
directory selected in World Builder. Relative paths resolve under `maps`.
MCP cannot grant itself another file root. Writes use the editor's staged
file writer; replacing an existing file requires `overwrite: true` where
the tool exposes that option. File tools do not open modal dialogs.

## Doors, smart objects and items

The prop library's `doors/`, `interactive/` and `items/` folders list what
the server builds as working objects: door leaves, the placeable smart
object blueprints, and shipped item classes. `wb_assets_search` with
`kind: "prop"` returns them beside meshes. Their `path` is a library key,
and `placedKind` says which kind a row is:

| Key                 | Kind          | Model after the prefix                                   |
| ------------------- | ------------- | -------------------------------------------------------- |
| `door:<leaf>`       | `door`        | A leaf path or file stem, as `Door.models()` lists it    |
| `smart:<blueprint>` | `smartObject` | A blueprint name, as `SmartObject.blueprints()` lists it |
| `item:<class>`      | `item`        | An item class GUID or name                               |

Pass the key as `model` to `wb_props_create`, with an optional `settings`
object. A door takes `name`, `locked`, `lock` (`none`, `bolt`, `boltAndKey`
or `key`), `lockpickable`, `lockDifficulty` (0 to 1) and `keyItem`. A smart
object takes `name`. An item takes `name`, `amount`, `quality`, `health` and
`condition`. These objects reject `scale` and `physics`, in creation and in
`wb_objects_transform`, and `wb_prop_physics_set` refuses them: the server
builds them at their own size. A field a kind does not take is ignored, and
a value the server would refuse fails the call with the reason.

`wb_object_get` reports `kind` as `door`, `smartObject` or `item`, the
canonical `model` (a door leaf by path, a blueprint by name, a shipped item
by GUID), its `key`, `settings` and `built`. `wb_object_settings_update`
replaces the settings as one undo step; omitted fields take their defaults.
`wb_document_get` pages them through the `doors`, `smartObjects` and `items`
collections, which is how a project and an export store them.

## Point lights

Point lights are standalone objects you can place anywhere in the world.
Each has its own position, color, intensity, radius and shadow setting.

In the **Lights** tab, choose **Add light in front of camera**, select the
light, and use the **Object** tab's Position fields or move gizmo to place
it where you want the illumination. Color, intensity, radius and the
**Cast shadows** checkbox appear in both tabs.

Color uses RGB components from 0 to 1. Intensity is a diffuse multiplier
from 0 to 100; 0 turns off illumination. Radius is in metres, from 0.1 to
100. A new light defaults to warm orange `[1, 0.65, 0.3]`, intensity 1 and
a 5-metre radius, with shadows off. Enable **Cast shadows** to have nearby
objects cast shadows from that light. Point lights have no rotation or
object scale. Each client can display up to 192 lights from the editor,
server and client scripts combined.

Use `wb_lights_create` with a position and optional settings:

```json
{
  "position": [123, 456, 7],
  "color": [1, 0.65, 0.3],
  "intensity": 2,
  "radius": 6,
  "castShadows": true,
  "group": "courtyard",
  "expectedRevision": "42",
  "idempotencyKey": "courtyard-light-1"
}
```

Poll the operation to obtain the light's opaque ID. `wb_light_update`
takes that ID and all three settings: `color`, `intensity` and `radius`.
The optional boolean `castShadows` defaults to `false` when creating a light.
Omitting it from an update preserves the existing setting; pass `false` to
turn shadows off. Explicit nonboolean values are rejected.

Move it with `wb_objects_transform` and a `position`; rotation and scale
are rejected. Read it through `wb_object_get`, query `kind: "light"` with
`wb_objects_query`, or read `collection: "lights"` with `wb_document_get`.
Mutations use the same revision, idempotency and operation rules as props.

Lights support selection, duplication, deletion, groups, layers and
undo/redo. Hidden layers stop their lights; locked layers prevent editing.
Projects and blueprints retain lights and their settings, including shadows.
Older documents without `castShadows` load with shadows off. World exports
include a `lights` array, and the server creates those lights for players
who join or enter their streaming range. Use matching client and server
builds containing light support.

Server API exports use `Light.spawn(position, options)`:

```js
const light = Light.spawn(new Vector3(123, 456, 7), {
  color: new Vector3(1, 0.65, 0.3),
  intensity: 2,
  radius: 6,
  castShadows: true,
});
light.position = new Vector3(124, 456, 7);
light.intensity = 3;
light.radius = 8;
light.color = new Vector3(0.4, 0.6, 1);
light.castShadows = false;
light.destroy();
```

Server script lights are removed when their resource stops. Lights loaded
from a world export are owned by that world resource and appear in its
`WorldResource.lights` array; unloading the world removes them. World Builder
continues to export server lights automatically.

In a client script, `Light.spawn(position, options)` creates a light visible
only on that client. It accepts the same position, color, intensity, radius
and `castShadows` controls shown above. Wait for `playerReady` before spawning;
check `LocalPlayer?.ready` too so a resource started after arrival can create
its light immediately:

```js
let localLight;

function createLocalLight() {
  if (!LocalPlayer?.ready || localLight?.valid) return;
  localLight = Light.spawn(new Vector3(123, 456, 7), {
    color: new Vector3(1, 0.65, 0.3),
    intensity: 2,
    radius: 6,
    castShadows: true,
  });
}

Events.on("playerReady", createLocalLight);
createLocalLight();
```

Client lights expose the same editable properties and `destroy()` method.
Their `valid` property becomes `false` after removal. They are cleaned up when
their creating resource stops or fails, or when the session or world ends.

## Viewport captures

Call `wb_capture_viewport`, then poll it with the returned `captureId`.
A completed result includes an MCP PNG image block and frame metadata.
Captures preserve aspect ratio within 1600 by 900 pixels. HDR frames are
converted to SDR PNG. `includeOverlays` chooses whether the Framework's
editor and browser overlays are included.

Capture metadata describes the copied backbuffer. It does not guarantee
that the latest document revision or camera change has reached that frame.
Only one capture can be in flight, and only a bounded number of completed
images are retained.

## Protocol and maintenance

The server implements the
[2026-07-28 HTTP transport](https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/streamable-http)
and a separate 2025-11-25 compatibility path. Modern clients use
`server/discover` and per-request protocol metadata and mirrored headers.
Legacy clients use `initialize` and `notifications/initialized`. Both use
the same POST endpoint. There are no legacy split SSE routes, subscriptions,
sampling requests, or arbitrary code-execution tools.

The implementation uses the Framework's existing HTTP, JSON, OpenSSL and
function2 dependencies, plus Windows graphics and image encoding APIs.
HTTP workers never access engine objects. Render-owned authoring operations
and game-thread operations retain the editor's existing ownership boundaries.

The repository's [agent instructions](../AGENTS.md) require MCP parity in
every World Builder change. Additions, changes and removals must update
discovery, schemas, resources, documentation and relevant tests together.
Changes to existing contracts require an explicit migration or versioning
decision. Contract tests live in the existing `KCDCTests` executable; the
loopback HTTP suite runs as `KCDCTests --suite world-builder-mcp`.

## Flattening plants under placed objects

`wb_plants_flatten` takes the level's low plants out of the world under
placed props, doors, smart objects and items. `ids` names them; a level
object, effect or light ID is refused and nothing changes. A plant goes when
the level placed it as an object of its own, it is rooted in an object's
world footprint grown by `margin` metres (0 to 5, 0.25 by default), from
2 m under the object's lowest point to its top, and its own box is no
taller than `maxHeight` (0.1 to 6, 3 by default). The result has
`flattened`, the count, and `ids`, the adopted level object IDs, which
`wb_level_edits_restore` gives back. The whole call is one undo step and
none when nothing went. It is refused while the Default layer is hidden or
locked, since newly adopted level objects belong to it.

The plants become hidden level edits: `wb_objects_query` lists them, a
project saves them, and an export carries them in `world`. The tall grass
is merged into the level's terrain detail, is not an object and cannot be
reached: expect litter, branches, tufts and bushes to go, and grass to
stay.

`wb_placement_get` and `wb_placement_update` carry the same behaviour for
interactive placement: `flattenPlants` (false by default) flattens under
each object as the brush places it, in the placement's own undo step, with
`plantMargin` and `plantMaxHeight`. `wb_props_create` does not flatten;
call `wb_plants_flatten` with the new ID.

## Viewport settings, project favorites, and layers

`wb_viewport_get` reads the project's `space` (`world` or `local`), snap
settings, and relative-grid preferences. `wb_viewport_update` patches its
`settings` object. `effectiveGridStep` reports the current spacing when
`gridMatchSnap` follows translation snap. Scale gizmos always use local
axes. `worldbuilder://state` includes viewport settings and layer definitions.
`gridLineWidth` accepts 1 to 8 pixels and defaults to 1.5; project saves
preserve it.

`pickPreferPhysics` defaults to `true`: resolved solid level objects take
priority over overlapping bounding boxes. Set it to `false` to pick non-solid
decoration in front of walls using bounds. This project preference controls
viewport clicks, hover, and `wb_raycast`; update it with
`wb_viewport_update({"settings":{"pickPreferPhysics":false}})`.
Anonymous level-brush colliders now resolve to their mesh and placement;
`wb_raycast` keeps its existing response schema.

`wb_pick_at` answers what a viewport click would select, without selecting
it. It takes `aim`, a screen point in normalized device coordinates (-1 to
1, +y up) through the editor camera, and returns `hit`, `kind` (`prop`,
`effect`, `light`, `level`, `terrain` or `none`), `by` (`collision` or
`bounds`), `id`, `name`, `detail`, `position` and `distance`. `id` is the
opaque object ID of a prop, effect, light or adopted level object, and
`null` for a level object not yet adopted and for terrain. It resolves a
click the way the viewport does:

- A solid object the editor placed is found where the ray strikes its
  collision (`by: collision`), never because the ray crossed its bounding
  box.
- An object without collision is found by its oriented box (`by: bounds`)
  when the ray enters it in front of the struck surface. A box containing
  the camera is not taken, and a smaller placed object struck behind a
  larger box keeps the pick.
- A solid placed object is offered by its oriented box only when the ray
  struck no placed object and the struck point lies outside that box.

`wb_raycast` is unchanged: it reports the level and collision under a ray
and applies none of these rules to placed objects' boxes. `wb_select_brush`
takes what `wb_pick_at` reports for the same `aim`.

`wb_level_object_update` takes `model`, a mesh path from the asset library,
to swap a level object for that mesh: the object goes out of the world with
its collision, and the mesh stands where it stood as static scenery. An empty
string takes the swap back. The change is one undo step. Reading an adopted
object with `wb_level_object_get` or `wb_object_get` reports the swap as
`model`, empty when there is none. A path outside the asset library is
refused.

`wb_favorites_get` takes `kind: "prop"` or `"effect"` and returns project
folders, their stable numeric IDs, paths, and the active folder.
`wb_favorites_update` takes the same kind plus `action`: `create`, `rename`,
`delete`, `activate`, `add`, `remove`, or `move`. Use `folderId` (defaults to
the active folder), `targetFolderId` for moves, `name` for creating or
renaming, and `path` for asset changes. Deletion requires `confirm: true`.
The root folder cannot be renamed or deleted. Changes participate in undo.
The existing `wb_asset_preferences_get` and `wb_asset_preferences_update`
retain their legacy global preference contract; they do not edit the new
project folders. Project saves include them. Read them with
`wb_favorites_get`; `worldbuilder://project` reports metadata and counts.

`wb_layers_get` returns layer definitions, the active layer, and custom
members using the existing opaque object IDs. Default membership is
implicit and never expands into a list of the whole level.
`wb_layers_update` takes `action`: `create`, `rename`, `activate`,
`visibility`, `lock`, `delete`, or `assign`. Supply `layerId` (defaults to
the active layer), `name`, `visible`, or `locked` for the matching action.
Assignment takes `ids`; omitting them uses the native object selection.
Deletion requires `confirm: true` and moves members back to Default.
Default cannot be renamed or deleted. Both tools complete through the
existing operation polling protocol.

Hidden or locked layers reject editing through MCP as well as the UI.
Assignment requires a visible, unlocked destination and editable source
objects. New props, effects and lights enter Default. World membership requires a
persistent native identity. Layer operations share undo history and are
stored only in project metadata; runtime exports retain all authored
objects. Project files store world identities and scene-array memberships,
never session object IDs. Existing projects without these fields remain
readable. Every mutation retains the usual expected revision and idempotency
requirements.

The older-map import section has been removed from the UI.
`wb_document_import` is deprecated and retained for existing MCP clients.
Use `wb_project_open` or `wb_blueprint_place` for new integrations. Merging
a document or placing a blueprint requires visible, unlocked layers.

Authored area definitions accept `collision: true` for invisible boundaries
that block both directions, with `collisionFloor` and `collisionCeiling`
(booleans, `false` when omitted) and `collisionThickness` (metres, 0.05 to
2, `0.2` when omitted). Create/update, query/get, project saves and undo
use the same settings. See [Area collision](area-collision.md) for shape
limits.

## Scale figure

`wb_figure_place` stands a temporary person in the scene to judge its
proportions by. `role` is a name from `wb_npc_roles_query`. Give either
`position`, the figure's feet as `[x, y, z]` in world metres, with an optional `yaw` in
degrees as `wb_camera_get` reads it, counter-clockwise from +Y seen from
above, or `aim`, a screen point in normalized device coordinates through
the editor camera: the figure then stands on the surface there and faces
the camera. Giving both, neither, or `yaw` with
`aim` is refused, as is an unknown role. The call is queued; read the
outcome with `wb_figure_get`.

`wb_figure_get` returns `active` (a figure was asked for), `standing` (its
body is up), `role`, `position`, `yaw`, `eyePoint` and `status`. `eyePoint`
is where the editor's **Look from its eyes** stands: 1.65 m above the feet
and 0.3 m in front of the face. Pass it and `yaw` to `wb_camera_set` to
look at the scene as that person does. `position`, `yaw` and `eyePoint` are
`null` while no figure is active; when set, `position` and `eyePoint` are
`[x, y, z]`, as `wb_camera_set` spells a position. `wb_figure_remove` takes
the figure away.

The figure belongs to the editing session, not to the document: it is not
saved, exported or undone, and has no object ID. One figure stands at a
time. It shares the game's one preview body with the
patrol preview, so `wb_figure_place` stops a patrol preview and
`wb_patrol_preview_start` takes the figure away. The key that stands it
under the cursor is `figureAtCursor` in `wb_keymap_get` and
`wb_keymap_update`.

## Pose anchors

`wb_pose_anchors_query` lists where the game poses a body: every part of a
placed smart object that carries a pose helper class, read off its
blueprint, and the level's own entities of the classes the shipped
blueprints pose bodies with. `center` defaults to the editor camera and
`radius` to 40 metres (1 to 150). Each entry of `anchors` has `position`,
`yaw` in degrees as `wb_camera_get` reads it, `label` (the helper class, such as
`Sit_1Place_Bench_Low`, or a level entity's name), `class`, `part` (the
blueprint's name for the part; empty for the level's own) and `objectId`,
the placed smart object's opaque ID or `null` for the level's own. `count`
is their number. The query reads the scene as it stands and does not
depend on the editor's overlay being shown.

An anchor is the part's origin and facing. The helper points themselves
are the game's, so an anchor says a pose is bound there and which way it
faces, not each seat's exact spot. Pass an anchor's `position` and `yaw` to
`wb_figure_place` to stand the scale figure on it; the figure stands and
does not play the pose.

## Selecting many objects

The region tools take an optional `filter` object, the same one the editor's
Selection settings build: `kinds` (any of `prop`, `effect`, `light`, `brush`,
`vegetation`, `entity`), `text` (search words; a leading `-` excludes), `mesh`
(exact, ignoring case and slash direction), `className`, `layer`, `group`,
`physics` and `includeHidden`. `mode` is `replace` (the default), `add` or
`remove`, and `visibleOnly` keeps only objects the camera can see.

- `wb_select_in_radius` takes a `center` and `radius` in metres.
- `wb_select_brush` is one dab of the editor's selection brush. It takes an
  `aim` point in normalized device coordinates through the editor camera and
  an optional `radius` (0.25 to 50 metres, 3 by default). It selects what a
  click at `aim` would pick and the first plant whose box the ray enters
  before the surface -- a tree by its crown, which has no collider --
  wherever their positions are, and every object whose position is within
  `radius` of the surface there. All must pass the filter. Aiming at the sky selects nothing. A brush stroke in the
  editor is a run of these dabs in `add` mode, or `remove` with Shift.
- `wb_select_in_rect` takes `min` and `max` corners, and
  `wb_select_in_polygon` an outline of `points`, in normalized device
  coordinates through the editor camera, with an optional `reach` in metres.
- `wb_select_in_area` takes an area definition as `wb_area_get` returns it.
- `wb_select_similar` takes an object `id` and a `radius`. The object decides
  the kinds, mesh and class; the filter's other fields still apply.

Each reports `matched`, `added`, `removed`, `total`, `truncated` and the
resulting `selection`. Level objects are adopted as they are selected. The
next region selection lets go of those that left the selection unchanged, so
a `world/...` ID a region tool returned stays valid until then. An object
that was moved, hidden, put on a layer or named by undo history keeps its ID
for the session. Region tools add up to 5000 objects, and
`wb_selection_set` and the explicit-ID object tools take up to 5000 IDs.
`wb_selection_get` also reports `count`.

`wb_history_undo` and `wb_history_redo` select the objects the step changed
that still exist, as Ctrl+Z and Ctrl+Y do in the editor.

## Keys

`wb_keymap_get` lists every remappable action with its `id`, `label`,
current `chord`, and its chords in the `default` and `wer` presets, plus
`conflicts` as pairs of action IDs. `wb_keymap_update` takes an optional
`preset` (`default` or `wer`), applied first, and `keys`, an object of action
IDs to chords such as `Ctrl+D`; an empty string unbinds. An unknown action or
key changes nothing. The bindings are saved with the editor settings, for the
user rather than the project.

## Preview conditions

`wb_environment_get` reads the project's preview conditions and the live
clock, sky and weather beside them. The `settings` object is what
`wb_environment_update` patches: `pinSky`, `skyHour` (0-24), `clockSpeed`
(0-200, where 0 stops the world clock), `preset`, `blendSeconds` (0-3600
game seconds), `holdWeather`, `overrideRain` and `rainIntensity` (0-1).
A patch is merged over the current settings and rejected whole if any field
is out of range. `worldbuilder://state` includes the same settings object,
and project saves preserve it.

These are preview conditions. They are saved with the project, they are not
authored content, they take no part in undo history, and no export carries
them. The engine is reconciled with them on the editor's next frame rather
than inside the tool call, so a successful update is an accepted intent:
read `wb_environment_get` to see what landed, and `applied`, `pendingCommands`
and `lastAction` to see how far it got.

Every environment tool is offline only and is refused while a session is
connected, because the server owns the clock and the weather and a sky
changed on one client would be that client's alone.

`wb_environment_presets_query` lists the sky presets this level loaded, and
the weather profiles it schedules them from. Preset names belong to the
level, so query them before setting one; a profile name is not a preset name
and cannot be blended. Reading `warnings` from `wb_environment_get` reports
a chosen preset the current level does not load.

`warnings` also reports what the engine will not do as asked, which the
contract does not hide:

- Holding the sky suspends the weather system, and the same switch stops
  rain being submitted, so `holdWeather` and `overrideRain` cannot both take
  effect.
- A held sky applies its preset at once, whatever `blendSeconds` says.
- Without a hold, the game replaces a blended preset after six game hours.
- Blend length is game time, which a stopped clock never spends.

Setting `preset` to the preset already chosen blends it in again, which is
how a sky the game has since moved is restored.

`wb_environment_action` runs one one-shot: `forceUpdate`, `rebuildClouds`,
`refreshProfiles`, `skipHours` (`value` 0-72) and `setWorldHour` (`value`
0-24). The last two move the game's own clock rather than the preview, and
`setWorldHour` may move it back, which only the offline builder may do.
