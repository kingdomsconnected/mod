/**
 * Where a command puts something.
 *
 * KCD2's world is CryEngine's: Z is up, and identity rotation faces +Y.
 */

/** A body with no pose would have everything put down at the world origin. */
export const NO_POSE_YET = "Your position has not reached the server yet; try again in a moment.";

/** Whether the body has reported where it is. */
export function hasPose(player: Player): boolean {
    return player.ready;
}

/** The entity's facing flattened onto the ground, so looking up or down does not shorten it. */
export function groundForward(rotation: Quaternion): Vector3 {
    const facing = rotation.rotateVector(new Vector3(0, 1, 0));
    const length = Math.sqrt(facing.x * facing.x + facing.y * facing.y);
    return length < 1e-4 ? new Vector3(0, 1, 0) : new Vector3(facing.x / length, facing.y / length, 0);
}

/** The yaw-only rotation whose entity forward is this direction on the ground. */
export function yawTowards(directionX: number, directionY: number): Quaternion {
    return Quaternion.fromAxisAngle(new Vector3(0, 0, 1), Math.atan2(-directionX, directionY));
}

export interface Placement {
    position: Vector3;
    rotation: Quaternion;
}

/** `distance` metres in front of the player, turned back towards them. `lift` raises it off the ground. */
export function inFrontOf(player: Player, distance: number, lift: number = 0): Placement {
    const ahead = groundForward(player.rotation as Quaternion);
    const from = player.position;
    return {
        position: new Vector3(from.x + ahead.x * distance, from.y + ahead.y * distance, from.z + lift),
        rotation: yawTowards(-ahead.x, -ahead.y),
    };
}

/** Distance between two world points. */
export function distanceBetween(a: Vector3, b: Vector3): number {
    const dx = a.x - b.x;
    const dy = a.y - b.y;
    const dz = a.z - b.z;
    return Math.sqrt(dx * dx + dy * dy + dz * dz);
}

/** How far a command acting on "the NPC you are at" looks for one. */
const NPC_REACH = 8;

/** The NPC a caller means: the nearest one within reach, or null. */
export function npcInReach(player: Player): Npc | null {
    let nearest: Npc | null = null;
    let nearestDistance = NPC_REACH;
    for (const npc of Npc.all()) {
        const distance = distanceBetween(npc.position, player.position);
        if (distance <= nearestDistance) {
            nearestDistance = distance;
            nearest = npc;
        }
    }
    return nearest;
}

/** A position as a command prints it back. */
export function formatPosition(position: Vector3): string {
    return `${position.x.toFixed(1)}, ${position.y.toFixed(1)}, ${position.z.toFixed(1)}`;
}
