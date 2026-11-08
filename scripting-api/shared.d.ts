/**
 * Structural TypeScript types that are not runtime globals.
 *
 * These describe what the MafiaHub Framework itself provides -- the base entity and player handles
 * KCDC's own `Entity`, `Player` and `Horse` extend, and the value types they exchange. The
 * generated declarations import from here; nothing in this file is produced by the exporter, so it
 * is edited by hand when the framework's own surface changes.
 *
 * KCDC's own callback types live here too. A binding may only declare a named type, never an
 * inline `(...) => ...`, so a handler parameter has nowhere else it can be spelled.
 */

/** Handler for an `ActionHint`, given the id `ActionHint.create` returned for it. */
export type ActionHintHandler = (hint: number) => void;

/** Handler for an event raised through the framework event bus. A returned promise is awaited, so a handler may be async. */
export type EventHandler = (...args: unknown[]) => unknown | Promise<unknown>;

/** Removes the subscription that returned it. Calling it more than once is harmless. */
export type Unsubscribe = () => void;

/** Replies to a request received by a MessageHandler. */
export type MessageReply = (value: unknown) => void;

/** Handler for a request addressed to a resource, given the payload and a reply callback. */
export type MessageHandler = (payload: unknown, reply: MessageReply) => void;

/** Handler for a bound key, given the key name and which edge fired. */
export type KeyHandler = (key: string, state: "down" | "up") => void;

/** Handler for an event emitted by a web view. */
export type WebEventHandler = (payload: unknown) => void;

/** Handler for a notification raised by a native screen, given whatever that notification carries. */
export type NativeScreenHandler = (...args: any[]) => void;

/** Handler for an ActionScript mouse event on a native clip, given the clip that raised it. Generic because `shared.d.ts` cannot reach the generated `NativeClip`. */
export type NativeClipHandler<TClip = unknown> = (clip: TClip, ...args: any[]) => void;

/** Handler for an event a native element's XML declares, given the arguments the movie raised it with. */
export type NativeElementHandler = (args: any[]) => void;

/** Handler for a placement-mode event, given where the placement is aimed and whether that spot is accepted. The pose is null for `cancel` and `end`. Generic because `shared.d.ts` cannot reach the generated `PlacementPose`. */
export type PlacementHandler<TPose = unknown> = (pose: TPose | null, valid: boolean) => void;

/** A two-dimensional vector. */
export interface Vector2 {
  x: number;
  y: number;
}

/** A normalized RGBA color. */
export interface Color {
  r: number;
  g: number;
  b: number;
  a: number;
}

/** A mutable three-dimensional vector used for world positions and Euler angles. */
export interface Vector3 {
  x: number;
  y: number;
  z: number;
  add(other: Vector3): this;
  sub(other: Vector3): this;
  mul(scalar: number): this;
  div(scalar: number): this;
  normalize(): this;
  dot(other: Vector3): number;
  cross(other: Vector3): this;
  distance(other: Vector3): number;
  lerp(target: Vector3, t: number): this;
  set(x: number, y: number, z: number): this;
  clone(): Vector3;
}

/** Quaternion used for entity rotations. */
export interface Quaternion {
  w: number;
  x: number;
  y: number;
  z: number;
}

