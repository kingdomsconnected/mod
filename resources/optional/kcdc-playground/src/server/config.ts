/** Loading this resource grants these cheats to everyone allowed here. */
export const config = {
    allowed: (_player: Player): boolean => true,
    maxNpcsPerPlayer: 4,
    maxDropsPerPlayer: 40,
    cursorRange: 15,
    traders: [
        { placement: "ttac_blacksmith", name: "Playground smith" },
        { placement: "ttkc_tradersMom", name: "Playground trader" },
        { placement: "kkut_enderlin", name: "Playground swordsmith" },
        { placement: "kkut_lacek", name: "Playground tailor" },
        { placement: "ssed_monasteryWorker_1", name: "Playground test trader" },
        { placement: "ssed_blazena", name: "Playground test trader" },
    ],
};
