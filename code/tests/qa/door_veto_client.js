"use strict";

// The client half of door_veto.js, loaded only into the isolated QA
// runtime. Refuses opens on this machine while the server's /doorqa says
// client, and prints every use it is asked about with [doorqa].
module.exports = function installDoorVetoClientQA() {
    let refusing = false;

    Events.on("kcdc-qa:door-veto", payload => {
        const mode = typeof payload === "string" ? JSON.parse(payload) : payload;
        refusing = mode !== null && mode.client === true;
        console.log(`[doorqa] client refusing=${refusing}`);
    });

    Events.on("doorInteract", (door, action, keySide) => {
        const refused = refusing && action === "open";
        console.log(`[doorqa] client: ${action} keySide=${keySide} on ${door.guid} '${door.name}' id=${door.id} open=${door.open} locked=${door.locked} -> ${refused ? "refused" : "allowed"}`);
        if (refused) return false;
    });
    console.log("[doorqa] Client half installed.");
};
