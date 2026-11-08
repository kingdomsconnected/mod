# Area collision

Select an authored area in World Builder and enable **Collision**. Boxes,
polygon areas and spheres become invisible boundaries that block crossing
from either side. The interior stays empty. Close the editor to walk against
the boundary.

Boxes and polygons get side walls only. **Floor** and **Ceiling** add a
plate across the whole inside, at the bottom or the top of the area. Turn
one on only when players must not get under or over the walls. A plate is
flat and as solid as a wall, so over sloping ground it can end up
just above the ground in one place and within reach overhead in another.
Spheres are always one closed shell, so the two options do nothing for them.

Collision is solid to the game's combat checks as well as to movement, like
any wall. Before a swing, the game looks for anything solid in a box about
2 metres wide and 2 metres tall around the player, and in a short path toward
the target. A wall only gets in the way of attacks when a player stands within
about a metre of it. A ceiling less than about 2 metres above where a player
stands blocks the overhead attack directions, so keep a ceiling well above head
height everywhere inside the area. Its height is the area's own top: a
polygon's **Height**, or a box's top face. Plates, like walls, are centred on
the outline, so a ceiling's underside is half the wall thickness below the
top, and a floor raises the ground inside by half the thickness.

**Show collision**, beside **Show drawn areas**, shades what the selected
areas' collision builds: the walls, any floor or ceiling, and their
thickness. A large sphere is too detailed to shade; its outline still shows.

Collision starts disabled on new and existing areas. Turning off **Enabled**
also removes collision. Moving, reshaping, duplicating or deleting an area
updates its boundary; undo and redo restore the setting. Projects, blueprints,
exported world resources and copied API calls keep it.

The toolbar's translation snap applies when creating or moving an area and
when dragging its handles. Hold **Shift** while moving a whole area to place
its bottom on the ground; polygon points and edges snap directly to the
surface. Hold **Shift** when clicking **+ Box**, **+ Shape** or **+ Sphere** to
ground the new area. Ground snapping takes precedence over the vertical grid
step. An area's own collision walls are ignored by its ground probe.

**Wall thickness** is 0.2 metres unless changed, from 0.05 to 2 metres, and
is centred on the drawn boundary. Box dimensions and sphere radii must
exceed the thickness. Polygons need a height above the thickness and a
simple outline without crossing or repeated edges. Concave
polygons work. A polygon's floor and ceiling stay horizontal, matching its
area containment rules. Spheres use a curved polygon mesh; very large spheres
have a coarser boundary. Collision does not move players already intersecting
a newly enabled or moved wall to a chosen side.

Server scripts can set `collision: true` in `Area.create(...)`, or assign
`area.collision = true` later. `collisionFloor` and `collisionCeiling`
(booleans, `false` when omitted) and `collisionThickness` (metres, `0.2`
when omitted) work the same way, in the definition or as properties.
Invalid collision shapes and options are rejected: `Area.create` throws,
and an assignment throws and leaves the area as it was. All four settings
appear in `area.toJSON()`. Level areas remain read-only; copy one
into an authored area before adding collision.

```ts
const boundary = Area.create({
  id: "courtyard-boundary",
  type: "box",
  position: new Vector3(2500, 1500, 100),
  min: new Vector3(-10, -10, 0),
  max: new Vector3(10, 10, 8),
  collision: true,
});

boundary.collision = false; // Remove the wall; keep the trigger area.
boundary.collision = true;  // Restore blocking from both sides.
boundary.collisionThickness = 0.5; // Thicker walls.
boundary.collisionCeiling = true;  // Also close the top.
```

To toggle an area loaded from a World Builder export, find it by its saved ID:

```ts
const boundary = Area.getById("courtyard-boundary");
if (boundary) {
  boundary.collision = false;
}
```

`area.enabled = false` disables both the trigger and the collision. Re-enabling
it restores collision when `area.collision` is still `true`.

Server boundaries follow the area's virtual world, arrive for players joining
later, and disappear when the area is destroyed or its world resource unloads.
Both the server and clients need a build supporting area collision.

Areas saved, exported or created by scripts before these options existed
read as walls only, with 0.2 metre walls: earlier builds also closed the
floor and ceiling. Set `collisionFloor` or `collisionCeiling` to get a plate
back. Players and server owners must update together. A client from before
this change still builds the floor and ceiling.

MCP uses the same optional `collision`, `collisionFloor` and
`collisionCeiling` booleans and `collisionThickness` number in the
definitions accepted by `wb_area_create` and `wb_area_update`.
`wb_area_get`, `wb_areas_query` and saved project resources report them.
The existing undo and validation rules apply.
