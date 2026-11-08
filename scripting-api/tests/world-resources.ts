function inspectWorldResources(): void {
    const resource = WorldResource.find("market");
    if (!resource) return;

    const props: Prop[] = resource.props;
    const effects: Vfx[] = resource.effects;
    const edits: LevelEdit[] = resource.levelEdits;
    const areas: Area[] = resource.areas;
    const loaded: boolean = resource.loaded;
    const names: string[] = WorldResource.all().map((entry) => entry.name);
    void [props, effects, edits, areas, loaded, names];

    // @ts-expect-error Resource identity is read-only.
    resource.name = "changed";
    // @ts-expect-error Runtime loading is not exposed.
    WorldResource.load("market.world.json");
    // @ts-expect-error Runtime reloading is not exposed.
    resource.reload();
    // @ts-expect-error Lookup requires a name.
    WorldResource.find(42);
}

if (WorldResource.ready) inspectWorldResources();
else Events.on("worldResourcesReady", inspectWorldResources);

// @ts-expect-error Readiness is a native getter.
WorldResource.ready = false;
// @ts-expect-error WorldResource handles come from all/find.
new WorldResource();
// @ts-expect-error State bags come from entity.state.
new StateBag();
