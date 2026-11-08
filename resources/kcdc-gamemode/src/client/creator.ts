/**
 * The client half of `/creator`: opens the character creator when the
 * server asks, and sends back the look the player accepted.
 *
 * `CharacterCreator` is client-only; how a look reaches the server is this
 * resource's business, so it goes up as one of its own events.
 */

const RESOURCE = "kcdc-gamemode";

/** What a creator ending is called in the chat line this sample leaves. */
const ENDINGS: Record<string, string> = {
    cancelled: "you backed out",
    closed: "it was closed",
    replaced: "another one replaced it",
    unavailable: "the stage could not be put up",
    interrupted: "it was interrupted",
};

function readGenders(payload: unknown): "male" | "female" | "both" {
    let parsed: unknown = payload;
    if (typeof payload === "string") {
        try {
            parsed = JSON.parse(payload) as unknown;
        } catch {
            parsed = undefined;
        }
    }
    const genders = typeof parsed === "object" && parsed !== null ? (parsed as { genders?: unknown }).genders : undefined;
    return genders === "male" || genders === "female" ? genders : "both";
}

export function installCreator(): void {
    let owned = false;
    Events.on(`${RESOURCE}:creator.open`, (payload) => {
        owned = CharacterCreator.open({ appearance: undefined, genders: readGenders(payload), title: "Your Character" });
    });

    Events.on(`${RESOURCE}:creator.close`, () => {
        if (owned) CharacterCreator.close();
    });

    Events.on("characterCreatorClosed", (appearance, reason) => {
        if (!owned) return;
        owned = false;
        if (appearance !== null) {
            Events.emitServer(`${RESOURCE}:creator.accepted`, JSON.stringify(appearance));
            return;
        }
        // `replaced` means a newer creator is already up; nothing to say.
        if (reason !== "replaced") {
            Hud.showInfoText(`Character creator closed: ${ENDINGS[reason] ?? reason}.`);
        }
    });
}
