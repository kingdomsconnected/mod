export interface InteractionViewOptions {
    /** Maximum distance from the player's feet to the target position, in metres. */
    maxDistance: number;
    /** Radius around the aim point, also allowing the target's own surface to intercept the trace. */
    aimRadius: number;
    /** Height of the aim point above the target position, in metres. */
    height?: number;
}

/** Call on nearby candidates at the interaction's polling rate, not on every object each frame. */
export function isInteractionTargetVisible(position: Pick<Vector3, "x" | "y" | "z">, options: InteractionViewOptions): boolean {
    const player = LocalPlayer;
    if (!player?.ready || !player.alive) return false;
    const p = player.position;
    if (Math.hypot(p.x - position.x, p.y - position.y, p.z - position.z) > options.maxDistance) return false;
    const centre = new Vector3(position.x, position.y, position.z + (options.height ?? 0));
    const ray = Camera.screenRay();
    if (!ray) return false;
    const dx = centre.x - ray.origin.x, dy = centre.y - ray.origin.y, dz = centre.z - ray.origin.z;
    const along = dx * ray.direction.x + dy * ray.direction.y + dz * ray.direction.z;
    if (along <= 0 || Math.hypot(dx - along * ray.direction.x, dy - along * ray.direction.y, dz - along * ray.direction.z) > options.aimRadius) return false;
    // Only nearby targets under the player's aim reach the physics query.
    const hit = World.raycast(ray.origin, centre, { mode: "cover" });
    return !hit || hit.distance >= Math.hypot(dx, dy, dz) - options.aimRadius;
}
