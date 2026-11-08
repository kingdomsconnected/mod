const inventoryUse = Inventory.onUse("apple", (use) => {
    const item: string = use.item;
    const uid: string = use.uid;
    const id: string = use.id;
    const revision: number = use.revision;
    const amount: number = use.amount;
    const action: "primary" | "secondary" | "activate" = use.action;
    const instance: number = Inventory.onUse({ uid }, () => {});
    const removed: boolean = Inventory.offUse(instance);
    void [item, id, revision, amount, action, removed];
});
Inventory.offUse(inventoryUse);
// @ts-expect-error Native UIDs must be strings, not imprecise JS numbers.
Inventory.onUse({ uid: 9007199254740993 }, () => {});
// @ts-expect-error Client handlers cannot mutate authoritative inventory directly.
Inventory.remove(null, {});
// @ts-expect-error A callback is required.
Inventory.onUse("apple");
