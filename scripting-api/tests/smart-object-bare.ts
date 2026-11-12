const seat: SmartObject = SmartObject.spawn({ blueprint: "Bench/bench_2places_low", position: [1, 2, 3], bare: true });
const furnished: SmartObject = SmartObject.spawn({ blueprint: "Bench/bench_2places_low", position: [1, 2, 3] });
const bare: boolean = seat.bare;
const saved: SmartObjectDefinition | null = seat.toJSON();
const again: boolean | undefined = saved?.bare;
const standsBare: string[] = SmartObject.blueprints()
    .filter((blueprint) => blueprint.bare)
    .map((blueprint) => blueprint.name);
void [furnished, bare, again, standsBare];

// @ts-expect-error A placed smart object is built once: bare is read-only.
seat.bare = false;
// @ts-expect-error bare is a boolean, not a word for one.
SmartObject.spawn({ blueprint: "Bench/bench_2places_low", position: [1, 2, 3], bare: "yes" });
