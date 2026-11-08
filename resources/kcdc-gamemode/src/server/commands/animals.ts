import type { Command } from "../command.js";
import { animalPopulation } from "../animals.js";
import { hasPose, NO_POSE_YET, distanceBetween } from "../place.js";

export const animalsCommand: Command = {
    name: "animals",
    summary: "Runs animal populations at the level's spawnpoints.",
    usage: ["/animals list", "/animals start <spawnpointGuid>", "/animals stop"],
    run({ player, args, reply, usage }): void {
        if (args[0] === "stop") {
            animalPopulation.clear();
            reply.line("Removed the sample's animal populations.");
            return;
        }
        if (!hasPose(player)) {
            reply.line(NO_POSE_YET);
            return;
        }
        const points = Npc.animalSpawnpoints();
        if (!args[0] || args[0] === "list") {
            const nearby = points.sort((a, b) => distanceBetween(a.position, player.position) - distanceBetween(b.position, player.position)).slice(0, 10);
            for (const point of nearby) reply.line(`${point.guid}: ${point.actorClass}, ${point.count} authored animals, ${Math.round(distanceBetween(point.position, player.position))} m away.`);
            return;
        }
        if (args[0] === "start") {
            const point = points.find(candidate => candidate.guid === args[1]);
            if (!point) {
                reply.line("Choose a spawnpoint GUID from /animals list.");
                return;
            }
            if (point.count === 0) {
                reply.line("This spawnpoint has no authored animals. Choose a point with a nonzero count.");
                return;
            }
            if (!Navigation.ready) {
                reply.line("Animal roaming needs the server's navigation mesh.");
                return;
            }
            reply.line(animalPopulation.add(point, player.virtualWorld)
                ? "Population enabled. Animals spawn while players are 60–350 m away; deaths respawn after 5–6 minutes, once players are over 60 m away."
                : "This spawnpoint is already enabled.");
            return;
        }
        usage();
    },
};
