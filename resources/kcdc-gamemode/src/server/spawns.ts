/**
 * Where arriving players are put. Add places here and joiners are drawn
 * between them; with none, everybody lands on the level's own start point.
 *
 * Coordinates are the level's own, the ones `/tp` reports. `yaw` is degrees
 * around Z. `weight` is a share of the draw, so 0 keeps a point in the list
 * but out of rotation. `tags` are for picking by faction, district or team.
 */
export interface SpawnPoint {
    name: string;
    position: Partial<Vector3>;
    yaw?: number;
    tags?: string[];
    weight?: number;
}

/**
 * Empty by design: a guessed coordinate puts somebody inside a hillside.
 * Fill it in for your own server, or answer `playerSpawning` yourself.
 */
export const spawnPoints: SpawnPoint[] = [];

/** One point drawn by weight, restricted to `tag` when given. Null when nothing matches. */
export function pickSpawnPoint(tag?: string): SpawnPoint | null {
    const candidates = tag ? spawnPoints.filter((point) => point.tags?.includes(tag)) : spawnPoints;
    const total = candidates.reduce((sum, point) => sum + Math.max(0, point.weight ?? 1), 0);
    if (total <= 0) {
        return null;
    }

    let remaining = Math.random() * total;
    for (const point of candidates) {
        remaining -= Math.max(0, point.weight ?? 1);
        if (remaining <= 0) {
            return point;
        }
    }
    return candidates[candidates.length - 1] ?? null;
}
