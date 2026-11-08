"use strict";

// Loaded only into the isolated QA runtime. Refuses door opens in one of
// three places and prints what the server saw with [doorqa]; the client half
// (door_veto_client.js) prints what the player's machine saw.
//
//   server -- the server's doorInteract refuses: the door swings back once
//             the correction arrives, a round trip after the use.
//   client -- the client's doorInteract refuses: nothing moves and the
//             server is never told.
//   allow  -- nothing is refused.
//
// /doorqa bar and unbar make every door in the player's world unusable and
// usable again: no prompt at all while barred.
module.exports = function installDoorVetoQA(commands) {
    const MODE_EVENT = "kcdc-qa:door-veto";
    const modes = ["server", "client", "allow"];
    let mode = modes.includes(process.env.DOORQA_MODE) ? process.env.DOORQA_MODE : "server";

    function describe(door) {
        return `${door.guid} '${door.name}' open=${door.open} locked=${door.locked} usable=${door.usable} token=${door.stateToken}`;
    }

    function tell(player) {
        player.emit(MODE_EVENT, JSON.stringify({ client: mode === "client" }));
    }

    // Which of the nearest doors each player was last sent to.
    const visits = new Map();

    // The closest shut, unlocked level doors to where the player stands,
    // nearest first. The catalog has no side to stand on, so the tester lands
    // a metre off the hinge along the axis they pick and walks round if that
    // is inside.
    function gotoDoor(player, args, reply) {
        const here = player.position;
        const doors = Door.all()
            .filter(door => door.virtualWorld === player.virtualWorld && !door.spawned && !door.open && !door.locked)
            .map(door => ({ door, distance: Math.hypot(door.position.x - here.x, door.position.y - here.y, door.position.z - here.z) }))
            .filter(entry => entry.distance > 3)
            .sort((a, b) => a.distance - b.distance)
            .slice(0, 20);
        if (doors.length === 0) {
            reply.line("No shut, unlocked door nearby.");
            return;
        }
        const index = args[1] !== undefined && /^\d+$/.test(args[1]) ? Number(args[1]) % doors.length : ((visits.get(player.id) ?? -1) + 1) % doors.length;
        visits.set(player.id, index);
        const { door, distance } = doors[index];
        const axis = args.find(arg => ["x", "-x", "y", "-y"].includes(arg)) ?? "x";
        const sign = axis.startsWith("-") ? -1 : 1;
        const target = new Vector3(door.position.x + (axis.endsWith("x") ? sign : 0), door.position.y + (axis.endsWith("y") ? sign : 0), door.position.z + 0.2);
        player.teleport(target, `Door ${door.name}`);
        console.log(`[doorqa] ${player.nickname}: goto #${index} ${describe(door)} ${distance.toFixed(0)} m away, offset ${axis}`);
        reply.line(`Door #${index} of ${doors.length}: '${door.name}' ${door.guid}, ${distance.toFixed(0)} m away. /doorqa goto for the next, /doorqa goto ${index} -x|y|-y for another side.`);
    }

    function setUsable(player, usable, reply) {
        let changed = 0;
        for (const door of Door.all()) {
            if (door.virtualWorld === player.virtualWorld && door.usable !== usable) {
                door.usable = usable;
                changed++;
            }
        }
        console.log(`[doorqa] ${player.nickname}: usable=${usable} on ${changed} door(s)`);
        reply.line(`${changed} door(s) ${usable ? "usable again" : "unusable"}.`);
    }

    commands.add({
        name: "doorqa",
        summary: "Choose where door opens are refused, bar every door, or go to the next door to try.",
        usage: ["/doorqa server|client|allow|status", "/doorqa bar|unbar", "/doorqa goto [index] [x|-x|y|-y]"],
        run({ player, args, reply }) {
            const action = args[0] || "status";
            if (action === "goto") {
                gotoDoor(player, args, reply);
                return;
            }
            if (action === "bar" || action === "unbar") {
                setUsable(player, action === "unbar", reply);
                return;
            }
            if (modes.includes(action)) {
                mode = action;
                for (const other of Player.all()) {
                    if (other.ready) tell(other);
                }
            }
            console.log(`[doorqa] mode=${mode}`);
            reply.line(mode === "allow" ? "Door opens are allowed." : `Door opens are refused by the ${mode}.`);
        }
    });

    Events.on("playerReady", player => tell(player));

    Events.on("doorInteract", (player, door, action, keySide) => {
        const refused = mode === "server" && action === "open";
        console.log(`[doorqa] ${player.nickname}: ${action} keySide=${keySide} on ${describe(door)} -> ${refused ? "refused" : "allowed"}`);
        // What the door stands as once the correction has gone out, and once
        // the swing would have finished.
        for (const delay of [100, 2500]) {
            setTimeout(() => console.log(`[doorqa] +${delay} ms: ${describe(door)}`), delay);
        }
        if (refused) return false;
    });
    console.log(`[doorqa] Installed; mode=${mode}.`);
};
