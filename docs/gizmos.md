# Client debug gizmos

Use `Gizmos` in a client resource to draw world-space debug geometry during normal
gameplay. Lines, areas, paths and filled meshes use the native game renderer.
Scene geometry hides drawings by default; set `depthTest: false` to show them
through walls. World Builder ([map editor](map-editor.md)) can stay closed,
including on servers where it is disabled. Drawings are local to that
client. They do not create entities, triggers or collision, and they do not
take keyboard or mouse input.

```ts
Gizmos.set('work-zone', {
    type: 'box',
    position: { x: 100, y: 200, z: 50 },
    min: { x: -5, y: -5, z: 0 },
    max: { x: 5, y: 5, z: 3 },
}, { color: 0xFFFFAA00, width: 2 });

Gizmos.set('interaction-range', {
    type: 'sphere',
    position: { x: 100, y: 200, z: 51 },
    radius: 3,
});

Gizmos.set('direction', {
    type: 'line',
    from: { x: 100, y: 200, z: 51 },
    to: { x: 100, y: 205, z: 51 },
}, { color: 0xFFFFFFFF });

// Reusing an ID replaces its geometry and style, even across shape types.
Gizmos.remove('direction');
// Gizmos.clear() removes every drawing owned by this resource.
```

IDs are private to the calling resource. Two resources can use `work-zone`
without replacing or removing each other's drawings. Drawings persist until
removed, their `durationMs` expires, their resource stops or fails, the
session ends, or the player arrives in another level. Reloading a save of
the same level keeps them. Redraw after loading into a new level. Nothing is
saved to disk.

Changes appear on the next game update. The service submits geometry to the
native frame collector; scripts never draw directly from their callbacks.

## Moving a drawing

`setTransform(id, position, rotation?)` moves a drawing without rebuilding
it: every authored point `p` is drawn at `position + rotation * p`. Author the
geometry around the origin, then move it as often as the object it follows.
This is far cheaper than calling `set` again each frame.

```ts
Gizmos.set('marker', {
    type: 'capsule',
    from: { x: 0, y: 0, z: 0 },
    to: { x: 0, y: 0, z: 1.8 },
    radius: 0.4,
});

// Each frame, for example, to follow the local player:
const player = LocalPlayer;
if (player !== null) {
    Gizmos.setTransform('marker', player.position);
}
```

`setTransform` returns false when the resource has no drawing with that ID.
The rotation is an optional `{ x, y, z, w }` quaternion, normalized on input
and defaulting to identity. `set` resets the transform to identity.

## Area geometry

`set(id, area, style?)` accepts the geometry fields from `area.toJSON()`
on the server or an area exported by World Builder. Extra fields such as
`id`, `name`, `labels`, `enabled` and `virtualWorld` are ignored. The client
draws exactly what its script requests; the script decides which world's
areas to show and whether disabled areas should be visible.

| Type     | Geometry                                                                           |
| -------- | ---------------------------------------------------------------------------------- |
| `box`    | Local `min` and `max` corners, defaulting to -1 and +1 on every axis.              |
| `sphere` | Required positive `radius`; drawn as three perpendicular circles.                  |
| `shape`  | Required `points` array of 3 to 256 local vectors; the last point joins the first. |

`position` is the world origin in metres, defaulting to zero. `rotation` is
an optional quaternion `{ x, y, z, w }`, normalized on input and defaulting
to identity. Box corners and polygon points are transformed by that pose.

A polygon's optional `height` draws a vertical prism from its lowest
transformed point. With height zero (the default), the outline follows the
points' own heights. For example:

```ts
Gizmos.set('courtyard', {
    type: 'shape',
    position: { x: 100, y: 200, z: 50 },
    points: [
        { x: 0, y: 0, z: 0 },
        { x: 8, y: 0, z: 0 },
        { x: 6, y: 5, z: 0 },
        { x: 0, y: 5, z: 0 },
    ],
    height: 3,
});
```

## Showing an area to selected players

Choose recipients in server code using your game's permission rules. Send
the definition with the existing per-player event API:

```ts
// Server: selectedPlayer and area are your chosen Player and Area handles.
const definition = area.toJSON();
if (definition !== null) {
    selectedPlayer.emit('debug:area', JSON.stringify(definition));
}
// To clear the debug resource's drawings on that player:
selectedPlayer.emit('debug:clear', '');
```

The client receives the payload already parsed, as an object, and the
receiving client resource draws the definition:

```ts
Events.on('debug:area', (payload) => {
    const area = payload as GizmoArea & { id: string };
    Gizmos.set(area.id, area, { color: 0xFF66CCFF, width: 2 });
});
Events.on('debug:clear', () => Gizmos.clear());
```

Server-created areas are not automatically sent to clients. Send updates
when their geometry changes and clear or remove drawings when debug access
ends. The client API itself grants no permission and sends no network data.

## Primitives and filled geometry

`Gizmos.set(id, primitive, style?)` accepts all area definitions above and
these additional primitives. Positions and endpoints are in world metres.

| Type       | Fields                                                                                                                                                                             |
| ---------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `line`     | `from`, `to`.                                                                                                                                                                      |
| `arrow`    | `from`, `to`, optional `headLength`. The head defaults to the smaller of 0.5 metres and a quarter of the arrow length, and must be greater than zero and no longer than the arrow. |
| `circle`   | `position`, `radius`, optional `normal` (default world Z) and `segments`. A fill makes a disk.                                                                                     |
| `capsule`  | `from`, `to` hemisphere centers, `radius`, optional `segments`. Coincident centers make a sphere.                                                                                  |
| `cone`     | `from` base center, `to` tip, `radius`, optional `segments`.                                                                                                                       |
| `cylinder` | `from` base center, `to` top center, `radius`, optional `segments`.                                                                                                                |
| `axes`     | `position`, optional quaternion `rotation` and `size` (default 1 metre). X is red, Y green and Z blue.                                                                             |
| `grid`     | `position`, optional `normal`, `size` (default 10 metres) and `divisions` (default 10).                                                                                            |
| `polyline` | `points` (2 to 256), optional `closed` (default false).                                                                                                                            |
| `mesh`     | `points` (3 to 1024), `indices` in triangle triples, optional `wireframe` (default true).                                                                                          |

Radial `segments` must be an integer from 8 to 64 (default 24). Grid
`divisions` must be an integer from 1 to 64. Mesh indices are integers from
zero to `points.length - 1`, with at most 3072 indices. Mesh triangles render
on both sides. Arrows, cones and cylinders need distinct endpoints; plane
normals and rotation quaternions must be non-zero.

```ts
Gizmos.set('aim', {
    type: 'arrow',
    from: { x: 100, y: 200, z: 51 },
    to: { x: 100, y: 205, z: 51 },
}, { color: 0xFFFFCC33, fillColor: 0x80FFCC33, durationMs: 3000 });

Gizmos.set('volume', {
    type: 'capsule',
    from: { x: 102, y: 204, z: 51 },
    to: { x: 102, y: 204, z: 53 },
    radius: 0.5,
}, { fillColor: 0x4066CCFF });

// This drawing remains visible behind scene geometry.
Gizmos.set('path', {
    type: 'polyline',
    points: [
        { x: 100, y: 200, z: 50 },
        { x: 103, y: 202, z: 50 },
        { x: 106, y: 201, z: 51 },
    ],
}, { depthTest: false, color: 0xFFFFFFFF });

Gizmos.set('surface', {
    type: 'mesh',
    points: [
        { x: 100, y: 200, z: 50 },
        { x: 104, y: 200, z: 50 },
        { x: 104, y: 204, z: 51 },
        { x: 100, y: 204, z: 51 },
    ],
    indices: [0, 1, 2, 0, 2, 3],
    wireframe: false,
}, { fillColor: 0x6066CCFF });
```

`fillColor` fills boxes, spheres, circles, capsules, cones, cylinders, arrow
heads and meshes. It defaults to transparent. Polygon areas (`type: 'shape'`)
remain outlines; supplying a visible fill throws. To fill a polygon, supply
its triangles as a mesh. Lines, polylines, grids and axes have no surface, and
a visible fill on them throws too.

## Appearance and limits

Depth testing defaults to **on** for every drawing. Use `{ depthTest: false }` to show a drawing through terrain and
walls. Gizmos never write the scene depth buffer. Translucent surfaces use
native alpha blending; intersecting translucent surfaces may show ordering
artifacts.

Drawings follow the active game camera and are hidden while loading or in
the multiplayer frontend. A drawing wholly outside the view costs nothing to
draw. They have no transform handles or picking controls.

`style.color` is an unsigned `0xAARRGGBB` number, defaulting to opaque green
(`0xFF6EE678`). `style.width` is 0.5 to 8 pixels, defaulting to 1.5. Polygon
vertical edges use 60% of that width. Axes use their fixed RGB colors.
Omitted style fields return to their defaults when replacing a drawing.

`style.durationMs` is an integer from 0 to 3,600,000. Zero (default) keeps
the drawing until cleared. Replacing an ID restarts its lifetime.

- IDs must contain 1 to 128 UTF-8 bytes.
- A resource can hold 128 named drawings. All resources together can hold
  16,384 geometry units: each line costs one and each triangle costs three.
  An outline box uses 12 units, a filled box 48 and an outline sphere 96.
- Input coordinates must be finite and within +/-16,384 metres. Area
  positions and local coordinates must have absolute values below 16,384.
  Area radius must be positive and less than 16,384; area height can be zero.
  Other radii and sizes range from 0.001 to 16,384 metres. Every box minimum
  must be less than its maximum.
- Invalid input and exceeded drawing limits throw. A failed replacement
  leaves the previous drawing intact. `remove` returns false when the
  resource has no drawing with that ID; `clear` affects only its caller.
