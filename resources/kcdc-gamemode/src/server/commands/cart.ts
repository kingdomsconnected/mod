import type { Command, CommandContext } from "../command.js";
import { NO_POSE_YET, distanceBetween, groundForward, hasPose, yawTowards } from "../place.js";

/** How far in front of the caller a cart is put: a wagon is some five metres long and its horses stand ahead of it. */
const SPAWN_DISTANCE = 7;

/** How far `/cart sit` looks for the cart it is about. */
const REACH = 15;

/** The cart nearest the caller within reach, or null. */
function cartInReach(player: Player): Cart | null {
    let nearest: Cart | null = null;
    let nearestDistance = REACH;
    for (const cart of Cart.all(player.virtualWorld)) {
        const distance = distanceBetween(cart.position, player.position);
        if (distance <= nearestDistance) {
            nearestDistance = distance;
            nearest = cart;
        }
    }
    return nearest;
}

export const cartCommand: Command = {
    name: "cart",
    summary: "Puts one of the game's carts or wagons, horses and all, in front of you.",
    usage: ["/cart [blueprint]", "/cart list", "/cart sit [seat]", "/cart leave", "/cart clear"],

    run({ player, args, reply }: CommandContext): void {
        const subcommand = args[0];

        if (subcommand === "list") {
            reply.line(`${Cart.blueprints().length} blueprints: ${Cart.blueprints().join(", ")}.`);
            return;
        }

        if (subcommand === "clear") {
            reply.line(`Despawned ${Cart.destroyAll(player.virtualWorld)} cart(s).`);
            return;
        }

        if (subcommand === "sit") {
            const cart = cartInReach(player);
            if (!cart) {
                reply.line(`No cart within ${REACH} m.`);
                return;
            }
            const seat = args[1] ?? "driver";
            if (!cart.seats.includes(seat)) {
                reply.line(`Cart ${cart.id} has no seat '${seat}'. Its seats: ${cart.seats.join(", ")}.`);
                return;
            }
            reply.line(cart.putPlayer(player, seat) ? `Taking the ${seat} seat of cart ${cart.id}.` : `Could not take the ${seat} seat of cart ${cart.id}: it is taken, or a script refused it.`);
            return;
        }

        if (subcommand === "leave") {
            const cart = Cart.all().find((candidate) => candidate.seatOf(player) !== null);
            reply.line(cart && cart.removePlayer(player) ? `Out of cart ${cart.id}.` : "You are not in a cart.");
            return;
        }

        if (!hasPose(player)) {
            reply.line(NO_POSE_YET);
            return;
        }

        const blueprint = subcommand ?? "wagon_b_covered";
        if (!Cart.blueprints().includes(blueprint)) {
            reply.line(`No cart '${blueprint}'. /cart list names them.`);
            return;
        }

        // Ahead of the caller and broadside on, so the benches face them.
        const ahead = groundForward(player.rotation as Quaternion);
        const from = player.position;
        const position = new Vector3(from.x + ahead.x * SPAWN_DISTANCE, from.y + ahead.y * SPAWN_DISTANCE, from.z);
        const cart = Cart.spawn(blueprint, position, yawTowards(ahead.y, -ahead.x), player.virtualWorld);
        reply.line(`Spawned cart ${cart.id} (${blueprint}, ${cart.horseCount} horse(s)). Use its bench to take the reins, or /cart sit.`);
    },
};
