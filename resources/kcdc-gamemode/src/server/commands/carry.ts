import type { Command, CommandContext } from "../command.js";
import { NO_POSE_YET, hasPose, inFrontOf } from "../place.js";

/**
 * What `/carry list` offers: one of each kind the game has a carrying walk
 * for. Anything else `carryItem` accepts works too; these are the ones worth
 * remembering.
 */
const SUGGESTED = ["basket_full_wood", "basket_b_apples", "stonebasket", "firewoodChipsHand", "waterBucket", "eggbasket", "shoppingbasket", "sack", "barrel"];

/** How far in front of the caller `/carry prop` lays one. Within reach, to be taken up. */
const PROP_DISTANCE = 1.2;

export const carryCommand: Command = {
    name: "carry",
    summary: "Carries an item in your arms -- a basket, firewood, a sack -- or lays one down to be picked up.",
    usage: ["/carry <item>    -- carry it", "/carry prop <item>   -- lay one down in front of you; the game's pick-up prompt carries it", "/carry list", "/carry drop [now]   -- put it down; now lets go at once"],

    run({ player, args, reply, usage }: CommandContext): void {
        const subcommand = args[0];

        if (subcommand === undefined) {
            usage();
            return;
        }

        if (subcommand === "list") {
            reply.line(`Try: ${SUGGESTED.join(", ")}. Sacks, barrels and crates are carried by men only.`);
            return;
        }

        if (subcommand === "drop") {
            if (!player.putDownItem({ immediate: args[1] === "now" })) {
                reply.line("You are not carrying anything.");
            }
            return;
        }

        if (subcommand === "prop") {
            const item = args[1];
            if (item === undefined) {
                usage();
                return;
            }
            if (!hasPose(player)) {
                reply.line(NO_POSE_YET);
                return;
            }
            const { position, rotation } = inFrontOf(player, PROP_DISTANCE);
            try {
                const prop = GroundItem.spawn(item, position, rotation, 1, player.virtualWorld, { carryable: true });
                reply.line(`Laid down ${item} (ground item ${prop.id}). The pick-up prompt on it carries it.`);
            } catch (error) {
                reply.line(error instanceof Error ? error.message : String(error));
            }
            return;
        }

        if (player.carriedItem !== null) {
            reply.line(`You are already carrying ${player.carriedItem}. /carry drop puts it down.`);
            return;
        }

        // Caught here: a name with no carrying walk is the commonest way this
        // goes wrong, and the throw says which.
        try {
            if (!player.carryItem(subcommand)) {
                reply.line("You cannot pick anything up right now.");
            }
        } catch (error) {
            reply.line(error instanceof Error ? error.message : String(error));
        }
    },
};

/** Tells a carrier what the server made of their carry. */
export function installCarryHandlers(): void {
    Events.on("playerCarryItem", (player, item) => {
        Chat.sendToPlayer(player, `You are carrying ${item}. /carry drop, or the game's own key, puts it down.`);
    });
    Events.on("playerPutDownItem", (player, item, groundItem) => {
        if (groundItem !== null) {
            Chat.sendToPlayer(player, `${item} is on the ground (ground item ${groundItem.id}); the pick-up prompt carries it again.`);
        }
    });
}
