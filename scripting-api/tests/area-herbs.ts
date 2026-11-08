const herbArea = Area.create({ id: "herb-clearing", type: "sphere", position: new Vector3(1000, 1000, 100), radius: 12, virtualWorld: 0 });
const areaHerbs: GatheringHerb[] = herbArea.getHerbs();
const hasGatheringHerbs: boolean = areaHerbs.length > 0;
for (const herb of areaHerbs) {
    const species: number = herb.kind;
    const itemClass: string = herb.item;
    const position: Vector3 = herb.position;
    void [species, itemClass, position];
}
const removedHerbs: number = herbArea.removeHerbs();
const restoredHerbs: number = herbArea.respawnHerbs(0);
const nearbyHerbs: GatheringHerb[] = Gathering.getHerbs(new Vector3(1000, 1000, 100), 12);
const removedNearbyHerbs: number = Gathering.removeHerbs(new Vector3(1000, 1000, 100), 12, 0);
const restoredNearbyHerbs: number = Gathering.respawn(new Vector3(1000, 1000, 100), 12, 0);
void [nearbyHerbs, removedNearbyHerbs, restoredNearbyHerbs];
void [hasGatheringHerbs, removedHerbs, restoredHerbs];
// @ts-expect-error World IDs are numeric.
herbArea.removeHerbs("all");
