/**
 * The default answer to one player picking another's pocket.
 *
 * The native Rob entry, charge and loot screen run as they do against an NPC,
 * and the server already guarantees that only what really left the victim
 * reaches the thief. What it leaves open is what the theft means, which is
 * decided here: everyone may rob everyone, a clean theft stays secret, and a
 * caught thief is named to both players -- because no NPC reacts on a
 * player's behalf, a caught attempt would otherwise pass without a word.
 */


export function installPickpocketPolicy(): void {
    Events.on("playerPickpocketed", (thief, victim, items) => {
        const what = items.map((line) => `${line.amount}x ${line.name}`).join(", ") || "nothing";
        console.log(`playerPickpocketed: ${thief.nickname} took ${what} from ${victim.nickname}`);
    });

    Events.on("playerPickpocketCaught", (thief, victim) => {
        console.log(`playerPickpocketCaught: ${victim.nickname} caught ${thief.nickname}`);
        Chat.sendToPlayer(victim, `You caught ${thief.nickname} with a hand in your pocket.`);
        Chat.sendToPlayer(thief, `${victim.nickname} caught you.`);
    });
}
