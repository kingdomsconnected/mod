import type { ActionHintHandler, Color, EventHandler, KeyHandler, MessageHandler, MessageReply, NativeClipHandler, NativeElementHandler, NativeScreenHandler, PlacementHandler, Quaternion, Unsubscribe, Vector2, Vector3, WebEventHandler } from "../shared.js";

declare global {
  /**
   * Native events dispatched through `Events.on`. Each property is the exact callback argument tuple for that event.
   */
  interface EventMap {
    /**
     * Dispatched once per session, when the local player can play: the session is established and the game has taken its own loading screen down. The game raises that moment itself -- it is its own gameplay-start signal, not a guess from timing.
     * 
     * Resources are running long before this, so it is not the moment a resource may start listening; it is the moment the player is looking at the world. Put what the player should see on arrival here -- a welcome, a first page -- rather than in `resourceStart`, which runs behind the loading screen. The server raises its own `playerReady` for this player right after.
     */
    playerReady: [player: Player];

    /**
     * Dispatched when the local player's XP on a track went up: after the server granted a gain and the game applied it, with its own multipliers. `xp` is what landed, in the game's own units.
     */
    progressionXpGained: [track: string, xp: number];

    /**
     * Dispatched when a track of the local player's reaches a new level. `perkPoints` is what that track's tree now has unspent.
     */
    progressionLevelUp: [track: string, level: number, perkPoints: number];

    /**
     * Dispatched when the local player gained a perk: learnt on the perk screen and granted by the server, given by a server resource, or granted by the game on its own.
     */
    progressionPerkAdded: [perk: string];

    /**
     * Dispatched when the local player lost a perk.
     */
    progressionPerkRemoved: [perk: string];

    /**
     * Dispatched when the server turned down a perk the local player confirmed on the perk screen. The game's own screen says nothing when that happens, so this is where a resource tells the player why.
     */
    progressionPerkRefused: [perk: string, reason: "points" | "level" | "parent" | "exclusive" | "blocked" | "script" | "owned" | "hidden"];

    /**
     * Dispatched when a monologue line of the local character -- a photo-mode comment, the sharpening remark -- was kept silent and unsubtitled. `text` is localized; `textKey` is its localization key. Not raised while `Hud.setMonologueEnabled(true)` has released it. Handler promises are not awaited.
     */
    monologueSuppressed: [text: string, textKey: string];

    /**
     * Dispatched when a quest asked to play a story cutscene and multiplayer kept it from playing, so the player stays in control and the world clock runs on. `cutscene` is its name in the game's cutscene table. Not raised while `Hud.setStoryCutscenesEnabled(true)` has released them. Handler promises are not awaited.
     */
    storyCutsceneSuppressed: [cutscene: string];

    /**
     * Dispatched when this player starts or stops following a server quest in the game's journal, whichever way it happened: the track button, the game auto-tracking a quest that turns active, or the untrack that follows finishing or failing one. Local to this machine and raised before the server is told; the server raises its own `questTrackingChanged` when the report arrives. Repeats are filtered, so it only fires on an actual change. Handler promises are not awaited.
     */
    questTrackingChanged: [questKey: string, tracked: boolean];

    /**
     * Dispatched on this machine when the local player uses a door, before anything happens: the door has not moved, the body has not started, and the server has not been told. `action` is `open`, `close`, `unlock` or `lockpick`, as server-side `doorInteract` has it; a key turned in the same use as the push arrives as `unlock` and then `open`, and refusing the `unlock` refuses the use without asking about the `open`. `keySide` says whether the player stands on the side with the keyhole.
     * 
     * Return false to refuse it: the use does nothing, with no round trip and nothing to put back. Every handler runs whatever an earlier one returned, and an async handler cannot refuse. A door the server made unusable (`door.usable` there) offers no prompt and never gets here.
     * 
     * It is this machine's own rule. The server's `doorInteract` still decides what it is told, so a refusal that has to hold against a modified client belongs there as well.
     */
    doorInteract: [door: DoorUse, action: "open" | "close" | "unlock" | "lockpick", keySide: boolean];

    /**
     * Dispatched when the game's trade screen actually comes up on this machine for a server vendor -- not when the server asked for it, which can take a few frames or fail. `npc` is the network ID of the NPC keeping the shop, or null when nobody does. Every `vendorOpened` is followed by exactly one `vendorClosed`.
     */
    vendorOpened: [session: number, npc: number | null];

    /**
     * Dispatched when a trade screen that `vendorOpened` announced goes away: the player closed it, the server closed it, the server opened another vendor over it, the level changed under it, or the connection ended. A screen that never came up raises neither event. The server raises its own `vendorClosed` for the session.
     */
    vendorClosed: [session: number, reason: "player" | "server" | "replaced" | "levelChange" | "sessionOver"];

    /**
     * Dispatched when a point of interest becomes discovered at this machine: silently while the server's `reveal_map` is on, as the game's own discovery when it is off, and for each one `WorldMap.discover` marks. Handler promises are not awaited. A position merely made known emits nothing.
     */
    poiDiscovered: [poiId: LocationId];

    /**
     * Dispatched when a location -- a town, a district -- becomes discovered at this machine through the game's own discovery, which only happens while the server's `reveal_map` is off. `WorldMap.discover` does not raise it. Handler promises are not awaited.
     */
    locationDiscovered: [locationId: LocationId];

    /**
     * Dispatched on this machine when the local player's swing meets something solid that is not a body: a tree, a wall, a rock, a prop, the ground. Once per thing per attack, at the first contact, as the game itself counts it; a blade that rests against a wall is not reported again. `hit` is named as a trace names it, with `distance` measured from the player's feet. Fists and kicks are not reported, and neither is a blow on a player, an NPC or an animal, which `playerHit` and the damage events cover. The server hears of it as `playerWeaponHit`.
     */
    weaponHit: [hit: WorldRayHit];

    /**
     * Dispatched when the map screen comes up at this machine.
     */
    mapOpened: [];

    /**
     * Dispatched when the map screen goes away.
     */
    mapClosed: [];

    /**
     * Dispatched when the player drops their own marker on the map, or moves the one already down -- `moved` separates the two, because the game uses one action for both. The position is world-space metres. Maps reuse one coordinate range, which is why `mapId` travels with it.
     */
    mapWaypointSet: [position: Vector3, mapId: number, moved: boolean];

    /**
     * Dispatched when the player takes their marker off the map, including through `WorldMap.clearWaypoint`. It carries where the marker was, since afterwards nothing says.
     */
    mapWaypointCleared: [position: Vector3, mapId: number];

    /**
     * Dispatched when the flight starts and when it ends, on this machine only. Handler promises are not awaited.
     * 
     * `reason` is `script` on the way in. On the way out it is `script` for a resource ending it, `mapEditor` for the map editor taking the one camera, `viewLost` for the level going away underneath the camera's anchor, and `sessionOver` for the session ending.
     */
    noclipChanged: [active: boolean, reason: "script" | "mapEditor" | "viewLost" | "sessionOver"];

    /**
     * Dispatched when a book `Book.open` asked for is in the player's hands and showing its first page, on this machine only.
     */
    bookOpened: [bookId: number];

    /**
     * Dispatched when a book leaves the player's hands, on this machine only. `reason` is `player` for the player's own exit (or the game ending the reading itself), `script` for `Book.close`, `failed` for a book that never reached the hands, and `sessionOver` for the session ending. Handler promises are not awaited, and a handler may open the next book straight away.
     */
    bookClosed: [bookId: number, reason: "player" | "script" | "failed" | "sessionOver"];

    /**
     * The local native follow action has entered. A successful start request may still fail before this event.
     */
    followStarted: [target: FollowTarget];

    /**
     * A queued or active scripted follow ended on this client.
     */
    followStopped: [target: FollowTarget, reason: "script" | "input" | "targetLost" | "mountChanged" | "teleported" | "unavailable" | "interrupted" | "failed" | "resourceStopped" | "sessionEnded" | "worldChanged"];

    /**
     * Dispatched once for every creator, on this machine only, whatever ended it. `appearance` is the look the player accepted, and null for every other ending. `reason` is `accepted`, `cancelled` for the player backing out, `closed` for `CharacterCreator.close`, `replaced` for another `CharacterCreator.open`, `unavailable` when the stage could not be put up, and `interrupted` when the session or the body went away under it. Nothing is put on the body: send the look wherever it should go -- `Events.emitServer`, then `player.setAppearance` on the server -- or open the next creator straight from the handler.
     */
    characterCreatorClosed: [appearance: Appearance | null, reason: "accepted" | "cancelled" | "closed" | "replaced" | "unavailable" | "interrupted"];

    /**
     * Dispatched when what the HUD's hints say changes: one appears or goes, changes its text, is greyed out, or a hold ring moves. `ActionHint.shown()` reads the new set. Handler promises are not awaited.
     */
    actionHintsChanged: [];

    /**
     * Dispatched after a resource entry point has run and immediately before the resource becomes running.
     */
    resourceStart: [resourceName: string];

    /**
     * Dispatched while a resource is stopping, before its stop callback, timers, exports and event handlers are cleaned up.
     */
    resourceStop: [resourceName: string];

    /**
     * Dispatched when one key of an entity's state changes: on the server when a script writes it, on a client when the write arrives. `value` is undefined when the key was removed and `previous` is undefined when it held nothing before, so a stored null stays distinguishable from an absent key. The entity is whatever the game's WrapScriptEntity answers, and the base Entity handle by default.
     */
    entityStateChange: [entity: Entity, key: string, value: any, previous: any];

    /**
     * Dispatched once per view, before any navigation, to the resource that created it.
     */
    browserCreated: [event: BrowserCreatedEvent];

    /**
     * Dispatched when any frame of an owned view begins loading.
     */
    browserLoadingStart: [event: BrowserLoadingStartEvent];

    /**
     * Dispatched when an owned view's main document is ready; the earliest point at which the page can receive Web.emit.
     */
    browserDocumentReady: [event: BrowserDocumentReadyEvent];

    /**
     * Dispatched when a frame of an owned view fails to load.
     */
    browserLoadingFailed: [event: BrowserLoadingFailedEvent];

    /**
     * Dispatched for every navigation an owned view is asked to perform, refused or not.
     */
    browserNavigate: [event: BrowserNavigateEvent];

    /**
     * Dispatched when an owned view blocks a popup; windowless views cannot host one, so handle the URL yourself.
     */
    browserPopup: [event: BrowserPopupEvent];

    /**
     * Dispatched when an owned view's requested cursor shape changes.
     */
    browserCursorChange: [event: BrowserCursorChangeEvent];

    /**
     * Dispatched when an owned view requests a tooltip; windowless rendering draws none, so the script must.
     */
    browserTooltip: [event: BrowserTooltipEvent];

    /**
     * Dispatched when focus enters or leaves an editable element of an owned view; use it to stop routing keys to the game.
     */
    browserInputFocusChange: [event: BrowserInputFocusChangeEvent];

    /**
     * Dispatched when an owned view rejects a navigation or a page event from outside its locked origin.
     */
    browserResourceBlocked: [event: BrowserResourceBlockedEvent];

    /**
     * Dispatched for console output of an owned view; the framework logs these regardless.
     */
    browserConsoleMessage: [event: BrowserConsoleMessageEvent];

    /**
     * Dispatched on view creation and whenever Web.loadURL re-locks an owned view to a different origin.
     */
    browserOriginChange: [event: BrowserOriginChangeEvent];
  }

  /** Names of native events available in this scripting environment. */
  type EventName = keyof EventMap;

  /**
   * Writable player stats, in native units. Use getStat and the server's setStat.
   */
  const PlayerStat: {
    /**
     * Current health, in native units. Zero can kill.
     */
    readonly Health: "health";

    /**
     * Current stamina; the game continues spending and regenerating it.
     */
    readonly Stamina: "stamina";

    /**
     * Current energy reserve; higher means better rested.
     */
    readonly Exhaust: "exhaust";

    /**
     * Current nourishment; higher means better fed.
     */
    readonly Hunger: "hunger";
  };

  /**
   * Read-only computed player stats. Use getDerivedStat; these values cannot be passed to setStat.
   */
  const DerivedPlayerStat: {
    /**
     * Total bleeding strength.
     */
    readonly Bleeding: "bleeding";

    /**
     * Sleepiness; above zero means asleep.
     */
    readonly Sleeping: "sleeping";

    /**
     * Consciousness; zero means knocked out.
     */
    readonly Consciousness: "consciousness";

    /**
     * Native drunkenness value.
     */
    readonly Drunkenness: "drunkenness";

    /**
     * Native poisoning value; not every named poison changes this value.
     */
    readonly Poisoning: "poisoning";

    /**
     * Effective charisma.
     */
    readonly Charisma: "charisma";

    /**
     * Effective visibility.
     */
    readonly Visibility: "visibility";

    /**
     * Effective conspicuousness.
     */
    readonly Conspicuousness: "conspicuousness";

    /**
     * Effective noise.
     */
    readonly Noise: "noise";

    /**
     * Native dirtiness reading; reading it does not clean the body or equipment.
     */
    readonly Dirtiness: "dirtiness";

    /**
     * Native bloodiness reading.
     */
    readonly Bloodiness: "bloodiness";

    /**
     * Native smell reading.
     */
    readonly Smell: "smell";

    /**
     * Native smell intensity.
     */
    readonly SmellIntensity: "smellIntensity";

    /**
     * Native fragrance reading.
     */
    readonly Fragrance: "fragrance";

    /**
     * Carried weight in the same units as InventoryCapacity.
     */
    readonly CarriedWeight: "carriedWeight";

    /**
     * Native carrying capacity, including applicable equipment effects.
     */
    readonly InventoryCapacity: "inventoryCapacity";

    /**
     * Native encumbrance reading.
     */
    readonly Encumbrance: "encumbrance";

    /**
     * Native hangover reading.
     */
    readonly Hangover: "hangover";

    /**
     * Computed alcoholism reading, not the persistent soul resource.
     */
    readonly Alcoholism: "alcoholism";

    /**
     * Effective armor rating.
     */
    readonly ArmorRating: "armorRating";

    /**
     * Overall armor defense.
     */
    readonly OverallArmorDefense: "overallArmorDefense";

    /**
     * Overall weapon attack.
     */
    readonly OverallWeaponAttack: "overallWeaponAttack";

    /**
     * Normalized run speed; not world-space velocity.
     */
    readonly NormalizedRunSpeed: "normalizedRunSpeed";

    /**
     * Native base run speed.
     */
    readonly RunSpeedBase: "runSpeedBase";

    /**
     * Native morale reading.
     */
    readonly Morale: "morale";
  };

  /**
   * How a player's body looks: four of the game's own character-component names, and the gender whose catalog they come from.
   * 
   * A part left out, or set to an empty string, means "leave that one alone" -- the body keeps whatever it would have had. An appearance with every part empty is the default body, which is what everyone wears until something chooses otherwise.
   */
  interface Appearance {
    /**
     * Which half of the game's character-component tree the four names come from. Not one of the four: a body's gender comes from its soul, and a name from one tree means nothing under the other, so this travels with them and is applied first.
     */
    gender: "male" | "female";

    /**
     * The skin: complexion and build. One of `Appearances.options("body", gender)`, or empty.
     */
    body: string;

    /**
     * The face. One of `Appearances.options("head", gender)`, or empty.
     */
    head: string;

    /**
     * The hairstyle, including its colour -- the game ships each style recoloured rather than colouring one. One of `Appearances.options("hair", gender)`, or empty.
     */
    hair: string;

    /**
     * The beard. Male only -- the female tree has none -- and one of `Appearances.beards(head)`, or empty.
     * 
     * Not every beard goes with every face: they are modelled per head, and a body asked for a pair that was never modelled refuses the whole appearance rather than quietly losing the beard.
     */
    beard: string;
  }

  /**
   * One entry of the game's character-component tree: something a body can be given for one part.
   */
  interface AppearanceOption {
    /**
     * What to put in the part. This is the game's own node name.
     */
    name: string;

    /**
     * Which part it fills.
     */
    part: "body" | "head" | "hair" | "beard";

    /**
     * Which tree it came out of. An option is only valid on a body of the same gender.
     */
    gender: "male" | "female";

    /**
     * The option this one is a variation of, or null when it stands on its own. The game ships a hairstyle once per colour, all derived from the same style node, so grouping by this is what turns 262 hairstyles into a list somebody can read.
     */
    group: string | null;
  }

  /**
   * The three character attributes a body's soul carries, in the game's own units.
   */
  interface SoulStats {
    /**
     * Attribute value, or 0 while the body has published no soul.
     */
    strength: number;

    /**
     * Attribute value, or 0 while the body has published no soul.
     */
    agility: number;

    /**
     * Attribute value, or 0 while the body has published no soul.
     */
    vitality: number;
  }

  /**
   * The nine skills a hit or a draw is evaluated against, in the game's own units.
   */
  interface SoulSkills {
    /**
     * Skill level, or 0 while the body has published no soul.
     */
    fencing: number;

    /**
     * Skill level, or 0 while the body has published no soul.
     */
    survival: number;

    /**
     * Skill level, or 0 while the body has published no soul.
     */
    defense: number;

    /**
     * Skill level, or 0 while the body has published no soul.
     */
    sword: number;

    /**
     * Skill level, or 0 while the body has published no soul.
     */
    heavyWeapons: number;

    /**
     * Skill level, or 0 while the body has published no soul.
     */
    marksmanship: number;

    /**
     * Skill level, or 0 while the body has published no soul.
     */
    dagger: number;

    /**
     * Skill level, or 0 while the body has published no soul.
     */
    largeWeapons: number;

    /**
     * Skill level, or 0 while the body has published no soul.
     */
    unarmed: number;
  }

  /**
   * What the game's own tables say about one status effect.
   */
  interface BuffInfo {
    /**
     * The name the game's own buff tables give the effect, and the spelling every buff verb takes.
     */
    name: string;

    /**
     * The kind of effect it is -- `poison`, `alcohol`, `injury`, `potion`. `Buffs.classes` lists the ones a server can take over.
     */
    class: string;

    /**
     * The family `player.clearBuffs` would take it off with, or null for the majority that belong to none.
     */
    aiTag: string | null;

    /**
     * How long the effect runs from start to finish, in real seconds, and negative for one with no end.
     */
    duration: number;

    /**
     * Whether this definition was registered with Buffs.register.
     */
    custom: boolean;

    /**
     * Registered display text for a custom effect; null for a native effect.
     */
    displayName: string | null;

    /**
     * Registered description for a custom effect; null for a native effect.
     */
    description: string | null;
  }

  /**
   * One status effect currently on a body: everything `BuffInfo` says about it, plus how long it has been there and who put it there.
   */
  interface BuffState {
    /**
     * The name the game's own buff tables give the effect, and the spelling every buff verb takes.
     */
    name: string;

    /**
     * The kind of effect it is -- `poison`, `alcohol`, `injury`, `potion`. `Buffs.classes` lists the ones a server can take over.
     */
    class: string;

    /**
     * The family `player.clearBuffs` would take it off with, or null for the majority that belong to none.
     */
    aiTag: string | null;

    /**
     * How long the effect runs from start to finish, in real seconds, and negative for one with no end.
     */
    duration: number;

    /**
     * Whether this definition was registered with Buffs.register.
     */
    custom: boolean;

    /**
     * Registered display text for a custom effect; null for a native effect.
     */
    displayName: string | null;

    /**
     * Registered description for a custom effect; null for a native effect.
     */
    description: string | null;

    /**
     * How long it has been on the body, in real seconds. `duration - since` is roughly what is left, and roughly is the best anyone can do: the game advances buff time from each client's own frame delta. 0 for an effect that was already there when the player connected.
     */
    since: number;

    /**
     * Whether this server put it there, or the game did -- a potion they drank, an injury they took.
     */
    source: "server" | "native";
  }

  /**
   * One skill or stat a player levels, as the game's own tables describe it.
   */
  interface TrackInfo {
    /**
     * The name every progression verb takes, as the game's tables spell it: `weapon_sword`, `thievery`, `strength`.
     */
    name: string;

    /**
     * Whether it is a skill or one of the stats the skills sit on.
     */
    kind: "skill" | "stat";

    /**
     * The highest level the game lets it reach.
     */
    cap: number;

    /**
     * True for fencing alone: it is never earned, the game derives it from the five weapon skills, so XP handed to it is refused.
     */
    induced: boolean;
  }

  /**
   * One perk, as the game's own perk tables describe it.
   */
  interface PerkInfo {
    /**
     * The perk's `perk_name`, unique across the tables, and the spelling every perk verb takes alongside its GUID.
     */
    name: string;

    /**
     * The perk's GUID.
     */
    id: string;

    /**
     * The localisation key the perk screen shows, or empty for a perk it never shows.
     */
    uiName: string;

    /**
     * The track whose points pay for it: a track name, `main` for the main-level bank, or null for a perk no player's tree carries.
     */
    track: string | null;

    /**
     * The level that track has to reach before the perk can be learnt. 0 for none.
     */
    level: number;

    /**
     * The perk that has to be owned first, or null.
     */
    parent: string | null;

    /**
     * `visible` perks are the ones on the perk screen and the only ones a player can learn; `hidden` ones appear once something grants them; `system` ones implement game features and are never shown.
     */
    visibility: "system" | "hidden" | "visible" | "obsolete";

    /**
     * What the perk does when owned, from the game's side tables: `buff` (a real status effect), `script`, `technique` (a combat move), `ability`, `param` (rewrites a game rule), `companion` (the dog), `recipe`, `codex`. Empty for a perk that only gates others.
     */
    kinds: string[];

    /**
     * Whether the game grants it on its own once its requirements are met, rather than on the perk screen.
     */
    autolearnable: boolean;
  }

  /**
   * Where a player stands on one track.
   */
  interface TrackProgress {
    /**
     * The level reached.
     */
    level: number;

    /**
     * XP earned towards the next level, in the game's own units.
     */
    xp: number;

    /**
     * XP the next level costs in total, from the start of this one. `xp / xpToNext` is the bar the character sheet draws.
     */
    xpToNext: number;

    /**
     * Unspent perk points in this track's tree.
     */
    perkPoints: number;
  }

  /**
   * A player's level on every track, keyed by track name.
   */
  interface TrackLevels {
    /**
     * The level reached.
     */
    stealth: number;

    /**
     * The level reached.
     */
    horse_riding: number;

    /**
     * The level reached.
     */
    fencing: number;

    /**
     * The level reached.
     */
    thievery: number;

    /**
     * The level reached.
     */
    alchemy: number;

    /**
     * The level reached.
     */
    craftsmanship: number;

    /**
     * The level reached.
     */
    drinking: number;

    /**
     * The level reached.
     */
    survival: number;

    /**
     * The level reached.
     */
    weapon_sword: number;

    /**
     * The level reached.
     */
    heavy_weapons: number;

    /**
     * The level reached.
     */
    marksmanship: number;

    /**
     * The level reached.
     */
    weapon_large: number;

    /**
     * The level reached.
     */
    weapon_unarmed: number;

    /**
     * The level reached.
     */
    scholarship: number;

    /**
     * The level reached.
     */
    houndmaster: number;

    /**
     * The level reached.
     */
    strength: number;

    /**
     * The level reached.
     */
    agility: number;

    /**
     * The level reached.
     */
    vitality: number;

    /**
     * The level reached.
     */
    speech: number;

    /**
     * The level reached.
     */
    prestige: number;
  }

  /**
   * Everything a server needs to put a player's progression back: plain JSON a resource can store however it likes. A player starts every session with a fresh character, so this is how progression outlives a disconnect.
   */
  interface ProgressionSnapshot {
    /**
     * Level and XP into the next level, per track name.
     */
    tracks: Record<string, { level: number; xp: number }>;

    /**
     * Every perk owned, by name.
     */
    perks: string[];

    /**
     * Unspent points per tree: track names, and `main`.
     */
    perkPoints: Record<string, number>;
  }

  /**
   * A KCDC player this client can see, and the body they occupy.
   */
  class Player {
    /**
     * Creates a script wrapper for a player this client already knows about; it neither connects nor spawns anyone.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * The name this player connected under, or an empty string once their body is gone.
     */
    readonly nickname: string;

    /**
     * Connection slot this player holds, or 65535 when unassigned. Stable for the length of the session and reused afterwards.
     */
    readonly playerIndex: number;

    /**
     * What this player's body looks like, as their own client last published it. Every part is empty until something chooses one, which is the default body -- and why everyone looks the same until a resource says otherwise.
     * 
     * Read back rather than assumed after `setAppearance`: the owning client is authoritative for its own body, so a new look appears here once they have actually put it on.
     */
    readonly appearance: Appearance;

    /**
     * The prop-catalog mesh this player's body is drawn as, by its `objects/...cgf` path, or null while they look like themselves. Published by their own client, so it follows `setDisguise` once they have actually put it on.
     */
    readonly disguise: string | null;

    /**
     * Whether this player's body is drawn at all -- itself, what it wears and what it carries. True for everybody until something hides them. Published by their own client, so it follows `setVisible` once they have actually stopped drawing. A hidden player is only invisible: they still stand where they stand, still collide, can still be hit, and NPCs still see them.
     */
    readonly visible: boolean;

    /**
     * Whether this player's body has both a pose and a soul, which is what everyone else waits for before spawning a puppet for them. False for the first moments of a connection.
     */
    readonly ready: boolean;

    /**
     * Whether this player has health left. False also while no soul has been published.
     */
    readonly alive: boolean;

    /**
     * Whether the body can be driven at all: alive, conscious and not asleep.
     */
    readonly canAct: boolean;

    /**
     * Health as a percentage from 0 to 100 -- what the nametag bar draws. Derived from the pair behind it, so it survives a maximum that changes.
     */
    readonly healthPercent: number;

    /**
     * Current health, in the game's own units. 0 while no soul has been published.
     */
    readonly health: number;

    /**
     * Health capacity, in the game's own units.
     */
    readonly maxHealth: number;

    /**
     * Current stamina, in the game's own units.
     */
    readonly stamina: number;

    /**
     * Stamina capacity, in the game's own units. Collapses to 0 for a tick around death, which is real rather than a bad read.
     */
    readonly maxStamina: number;

    /**
     * The stamina ceiling the body's injuries currently allow, which is at or below maxStamina.
     */
    readonly healthyStamina: number;

    /**
     * Current energy reserve, in the game's own units. Higher means better rested.
     */
    readonly exhaust: number;

    /**
     * Maximum energy reserve, in the game's own units.
     */
    readonly maxExhaust: number;

    /**
     * Current nourishment, in the game's own units.
     */
    readonly hunger: number;

    /**
     * Nourishment capacity, in the game's own units.
     */
    readonly maxHunger: number;

    /**
     * How heavily the body is bleeding; 0 when it is not.
     */
    readonly bleeding: number;

    /**
     * Sleepiness the game has accumulated for this body; above 0 means asleep.
     */
    readonly sleeping: number;

    /**
     * How conscious the body is; 0 is knocked out.
     */
    readonly consciousness: number;

    /**
     * How drunk the body is; 0 is sober.
     */
    readonly drunkenness: number;

    /**
     * How poisoned the body is; 0 is clean.
     */
    readonly poisoning: number;

    /**
     * Strength, in the game's own units.
     */
    readonly strength: number;

    /**
     * Agility, in the game's own units.
     */
    readonly agility: number;

    /**
     * Vitality, in the game's own units.
     */
    readonly vitality: number;

    /**
     * The same three attributes as the engine's relative values, which is what its own modifiers are expressed in.
     */
    readonly relativeStats: SoulStats;

    /**
     * The nine combat and survival skills a hit or a draw is evaluated against. Read as a whole rather than one at a time: the snapshot carries them together.
     */
    readonly skills: SoulSkills;

    /**
     * The same nine skills as the engine's relative values.
     */
    readonly relativeSkills: SoulSkills;

    /**
     * On the owning machine, the velocity the body's own animation was driven by this frame, not the one its physics settled on. Everywhere else, measured from successive replicated positions.
     */
    readonly velocity: Vector3;

    /**
     * World-space direction the head and eyes are turned towards. Zero while the body has reported none.
     */
    readonly lookDirection: Vector3;

    /**
     * Whether the body is off the ground -- fallen or mid-jump -- as its own physics reports it, debounced past the flicker a stair step causes.
     */
    readonly inAir: boolean;

    /**
     * Whether this player is crouched, as their own game's crouch action reports it. False once their body is gone.
     */
    readonly crouched: boolean;

    /**
     * The ragdoll physics profile the body is in, or 255 when it has none to report.
     */
    readonly physicsProfile: number;

    /**
     * Whether this player has their fists -- or their weapon -- up. Stays true after the arm comes down, which is how the engine holds it.
     */
    readonly fistsUp: boolean;

    /**
     * The arm guard in the engine's own levels: 0 arms down, 1 the guard a player holds.
     */
    readonly guard: number;

    /**
     * Which direction of the combat star this player is aiming at, as a row of the game's zone table, or -1 when they are aiming at none.
     */
    readonly combatZone: number;

    /**
     * Item class drawn in the right hand as 32 hex digits, or an empty string when the hand is empty. A class rather than an item: no engine-local identifier crosses the wire.
     */
    readonly rightHandItem: string;

    /**
     * Item class drawn in the left hand as 32 hex digits, or an empty string when the hand is empty.
     */
    readonly leftHandItem: string;

    /**
     * Item classes this player is wearing, each as 32 hex digits. What they actually have on rather than a preset, so a bare body reads as an empty array.
     */
    readonly equipment: string[];

    /**
     * Whether this player has a helmet visor down, as their own game reports it. False when they wear no helmet with a visor, and once their body is gone. There is no setter: the player raises and lowers it in their own game.
     */
    readonly visorClosed: boolean;

    /**
     * The player's main level, the one the game derives from the stats. 0 until known.
     */
    readonly level: number;

    /**
     * The level of every skill and stat, keyed by track name. The local player reads its own character; anyone else reads what the server last accepted from theirs, and all zero until it has.
     */
    readonly levels: TrackLevels;

    /**
     * Every perk the player owns, by name -- the perk screen's and the hidden ones the game grants on its own.
     */
    readonly perks: string[];

    /**
     * The locomotion pace tag -- walk, jog, sprint -- this machine's animation system picked for this body. Raw Mannequin tag ids; 255 means nothing is set.
     */
    readonly moveSpeedTag: number;

    /**
     * The movement direction tag this machine's animation system picked for this body. Raw Mannequin tag ids; 255 means nothing is set.
     */
    readonly moveDirTag: number;

    /**
     * The stance tag -- upright, sneaking, sitting, lying -- this machine's animation system picked for this body. Raw Mannequin tag ids; 255 means nothing is set.
     */
    readonly stanceTag: number;

    /**
     * Whether this handle is the player sitting at this machine, rather than someone else's copy.
     */
    readonly local: boolean;

    /**
     * The engine entity behind this player: the game's own player entity for the local player, otherwise the entity the copy was spawned as, or 0 across a level load and before it exists. Not stable across a session: the engine reuses entity ids.
     */
    readonly entityId: number;

    /**
     * Network ID of the horse this player is riding, or 0 when they are on foot.
     */
    readonly horseId: number;

    /**
     * Whether this player is in a saddle.
     */
    readonly mounted: boolean;

    /**
     * The status effects on this body: potions, poison, drunkenness, injuries, illness, unconsciousness, and anything the server added. Perks and equipment effects are not in here -- they follow state that already replicates.
     * 
     * Only the local player has this list. Nobody else's client publishes their effects, because what everyone else's body is wearing reaches this machine as the consequences already -- so it reads empty for any other handle.
     */
    readonly buffs: BuffState[];

    /**
     * Formats this player handle for logging and debugging.
     * @returns The player's network entity ID, nickname, and whether they are the local player.
     */
    toString(): string;

    /**
     * Reads the latest reported stat in native units. Returns zero when no valid body snapshot exists; check player.ready to distinguish that from a real zero. Throws for an unknown stat or invalid arguments.
     * @param stat A PlayerStat value.
     */
    getStat(stat: "health" | "stamina" | "exhaust" | "hunger"): number;

    /**
     * Reads a computed stat from the latest body snapshot. Bleeding, Sleeping, Consciousness, Drunkenness and Poisoning follow the body as it changes; the others are sampled about every 250 ms to a hundredth of a native unit, then replicated. Values use native units and can be negative; they are not universally percentages. Returns zero without a valid snapshot. Throws for an unknown stat or invalid arguments. These readings do not override another player's native calculations.
     * @param stat A DerivedPlayerStat value.
     */
    getDerivedStat(stat: "bleeding" | "sleeping" | "consciousness" | "drunkenness" | "poisoning" | "charisma" | "visibility" | "conspicuousness" | "noise" | "dirtiness" | "bloodiness" | "smell" | "smellIntensity" | "fragrance" | "carriedWeight" | "inventoryCapacity" | "encumbrance" | "hangover" | "alcoholism" | "armorRating" | "overallArmorDefense" | "overallWeaponAttack" | "normalizedRunSpeed" | "runSpeedBase" | "morale"): number;

    /**
     * Where the local player stands on one track: level, XP towards the next one, and unspent perk points. Only the local player carries XP and points; for anyone else this is null.
     * @param track A track name, from `Progression.tracks()`.
     * @returns The progress, or null for any other player. Throws for a track name the tables do not carry.
     */
    getTrack(track: string): TrackProgress | null;

    /**
     * Whether the player owns a perk.
     * @param perk A perk's name or GUID.
     * @returns True when they do; false for a perk they do not own, or a name the tables do not carry.
     */
    hasPerk(perk: string): boolean;

    /**
     * Keeps another player's body out of this machine's combat lock-on and opponent picker, or lets it back in -- for someone lying wounded, say. Only this client's picker is affected, so a resource that wants it everywhere tells every client. It does not stop a swing that lands anyway; damage is the server's to refuse. Lasts until changed or the session ends. Throws unless `targetable` is a boolean.
     * @param targetable False to keep this player's body out of the local lock-on, true to let it back.
     */
    setTargetable(targetable: boolean): void;
  }

  interface Player extends BasePlayer {}

  /**
   * The game's skills, stats and perks. Read-only on the client: every change to a player's progression is the server's.
   */
  const Progression: {
    /**
     * Every skill and stat a player levels: the fifteen skills the character sheet shows, then strength, agility, vitality, speech and prestige.
     * @returns The tracks, in that order.
     */
    tracks(): TrackInfo[];

    /**
     * Looks a perk up.
     * @param query A perk's name, as the game's perk tables spell it, or its GUID.
     * @returns What the tables say about it, or null for a name they do not carry.
     */
    findPerk(query: string): PerkInfo | null;

    /**
     * Lists the game's perks: 945 of them, most of them hidden system perks. `Progression.perks({ visibleOnly: true })` is the perk screen.
     * @param filter `track` keeps the perks one tree pays for -- a track name, or `main`; `visibleOnly` keeps the perks the perk screen shows.
     * @returns The perks, sorted by name.
     */
    perks(filter?: { track?: string; visibleOnly?: boolean }): PerkInfo[];

    /**
     * The total XP a track costs from level 0 to `level`, on the game's own curve: each level costs `base + diff * level`, with base and diff from the game's RPG tables.
     * @param track A track name.
     * @param level The level to reach.
     * @returns The XP. Throws for a track name the tables do not carry.
     */
    xpForLevel(track: string, level: number): number;
  };

  /**
   * The game's own HUD messages, shown to the player at this machine. Nothing here is replicated: a message everyone should see is sent to everyone, and each client shows it. Every message call returns false while there is no HUD, which is the case in the main menu and across a level load.
   * 
   * The screen-effect verbs -- `addScreenEffect` and its siblings -- put full-screen post effects on this player's own picture. They are drawn by the game's renderer rather than the HUD, so they need no HUD and hiding HUD elements leaves them alone.
   */
  const Hud: {
    /**
     * Whether this player is in a conversation right now -- one a server resource opened through `Dialogue`, or one the game started by itself. Both count, because a resource that acts during the game's own dialogue is as wrong as one that acts during ours. The game's half is read from its own dialogue manager, so it is true for real NPC conversations as well.
     * @returns True while a conversation owns the screen.
     */
    isInDialogue(): boolean;

    /**
     * Shows the plain notification: one line, no icon, no payload.
     * @param message Text to show, as written.
     * @returns True when the HUD took it.
     */
    showNotification(message: string): boolean;

    /**
     * Shows the single line of info text above the HUD.
     * @param message Text to show, as written.
     * @param durationMs How long to hold it, in real milliseconds; defaults to 3000.
     * @param priority What an already-showing message does to this one; defaults to 0.
     * @returns True when the HUD took it.
     */
    showInfoText(message: string, durationMs?: number, priority?: number): boolean;

    /**
     * Takes the info text down before its time is up.
     * @returns True when the HUD took it.
     */
    hideInfoText(): boolean;

    /**
     * Shows the perk-used toast: an icon and a line.
     * @param iconName Icon from the game's own atlas, by name -- `perk_alchemist`, say. It is not text and is not translated.
     * @param name Line beside the icon, as written.
     * @returns True when the HUD took it.
     */
    showPerkUsed(iconName: string, name: string): boolean;

    /**
     * Shows the perk-gained toast, which is the one the game plays when a perk is earned.
     * @param iconName Icon from the game's own atlas, by name.
     * @param name Line beside the icon, as written.
     * @returns True when the HUD took it.
     */
    showPerkGained(iconName: string, name: string): boolean;

    /**
     * Shows the combo-learned toast.
     * @param iconName Icon from the game's own atlas, by name.
     * @param name Line beside the icon, as written.
     * @returns True when the HUD took it.
     */
    showComboLearned(iconName: string, name: string): boolean;

    /**
     * Shows the reputation-change message.
     * @param eventId Which reputation event the HUD draws it as.
     * @param message Line to show, as written.
     * @returns True when the HUD took it.
     */
    showReputationChanged(eventId: number, message: string): boolean;

    /**
     * Shows the random-event result panel.
     * @param type Which result the HUD draws it as.
     * @param result Result line, as written.
     * @param name Name of what it was, as written.
     * @returns True when the HUD took it.
     */
    showRandomEventResult(type: number, result: string, name: string): boolean;

    /**
     * Shows the experience-gain toast.
     * @param stat The stat's own unique name -- `str`, say. It is an identifier, not text, and is not translated.
     * @param statName What to call it on screen, as written.
     * @param type Which XP bar the HUD draws it against; defaults to 0.
     * @returns True when the HUD took it.
     */
    showXpGain(stat: string, statName: string, type?: number): boolean;

    /**
     * Shows one of the game's tutorials. This drives the HUD slot directly, which is past whatever the mod suppresses at the source.
     * @param name The tutorial's own name in the game's tables, such as `OB_O20_Inventory`. Not text.
     * @returns True when the HUD took it.
     */
    showTutorial(name: string): boolean;

    /**
     * Takes one named tutorial down.
     * @param name The tutorial's own name in the game's tables.
     * @returns True when the HUD took it.
     */
    hideTutorial(name: string): boolean;

    /**
     * Takes down whichever tutorial is showing, whatever it is.
     * @returns True when the HUD took it.
     */
    hideCurrentTutorial(): boolean;

    /**
     * Takes down the codex action hint.
     * @returns True when the HUD took it.
     */
    hideCodexActionHint(): boolean;

    /**
     * Empties the queue of notifications waiting to be shown. It is the only piece of the HUD's own state reachable from here; everything else is one-shot.
     * @returns True when the HUD took it.
     */
    clearNotifications(): boolean;

    /**
     * Retains a visibility request for one HUD element, including before the HUD is ready. Hidden clips are suppressed before each HUD render, across movie and level reloads. Turning an element on releases multiplayer suppression; the current native HUD rules still apply. Suppression is released when its owning resource stops or the session ends. No console variables are changed.
     * @param element The element, by the game's own name for it: `Stats` is the health, stamina and nourishment bars along the bottom. One of `Compass`, `Stats`, `QAMWeapon`, `QAMFood`, `Subtitles`, `InfoText`, `GameLog`, `Hints`, `DialogLeft`, `DialogRight`, `Cursor`, `Crime`, `Wanted`, `PopUpBackground`, `TutorialMessage`, `FancyEvent`, `SkillCheck`, `ItemTransfer`, `Buffs`, `CommonEvent`, `DiceCursor`, `Trespassing`, `RatioStrips`, `ShootingContest`, `Bubbles`, `TutorialInDialog`, `DiceContainer`, `Vignette`, compared exactly.
     * @param visible False hides the element, true lets the game show it again.
     * @returns True when the request is retained; application waits for the native HUD when it is not ready yet.
     */
    setElementVisible(element: string, visible: boolean): boolean;

    /**
     * Whether multiplayer permits the element to show. True when no resource suppresses it; native gameplay rules can still hide it. This is not a pixel-visibility query.
     * @param element The element, by the game's own name for it, as `setElementVisible` takes it.
     * @returns True while multiplayer allows the element to show.
     */
    isElementVisible(element: string): boolean;

    /**
     * The local character's own monologue -- photo-mode comments, the sharpening remark -- is spoken in Henry's voice and captioned with his name whoever is playing, so multiplayer suppresses it and raises each line as `monologueSuppressed`. Releasing it lasts until the session ends.
     * @param enabled True lets the game voice and subtitle it again; false suppresses it.
     */
    setMonologueEnabled(enabled: boolean): void;

    /**
     * Whether the game voices and subtitles the local character's monologue.
     * @returns False while multiplayer suppresses it, which is the default.
     */
    isMonologueEnabled(): boolean;

    /**
     * A story cutscene -- the one a quest plays when the player walks into its trigger -- puts the player into the game's cutscene state, can hold the world clock and loads the quest's own scenery, so multiplayer suppresses it and raises each one as `storyCutsceneSuppressed`. Everything a quest queues is one, its fades and time skips included; fast travel, sleeping and waiting that the player starts are not, and always play. A suppressed cutscene is not played later when this is released. Releasing it lasts until the session ends.
     * @param enabled True lets the quests play their cutscenes again; false suppresses them.
     */
    setStoryCutscenesEnabled(enabled: boolean): void;

    /**
     * Whether the quests may play their cutscenes on this machine.
     * @returns False while multiplayer suppresses them, which is the default.
     */
    isStoryCutscenesEnabled(): boolean;

    /**
     * Every effect `addScreenEffect` accepts, with each parameter's range, default and neutral value.
     * @returns The effects, in a fixed order.
     */
    screenEffects(): ScreenEffect[];

    /**
     * Puts a full-screen effect on this player's own picture -- a colour grade, a blur, ghosting or the game's blood overlay, for intoxication, injury, poison, dreams or a place that should feel wrong -- owned by the calling resource. These are the game's own post-processing passes, driven with the parameters this build actually reads; they are not part of the HUD and showing or hiding HUD elements does not touch them.
     * 
     * This is presentation, not state. Nothing is replicated, nobody else sees it, and none of it makes a player drunk or hurt: whatever decides that -- usually the server -- keeps it, and a client script asks for the picture to match. Weather and time of day are shared world state and are not touched.
     * 
     * Effects from every resource combine rather than overwrite each other, on top of the game's own grade -- which already desaturates the screen as health runs low -- rather than in its place: brightness, contrast and saturation multiply, the colour cast and the hue shift add, and blur, ghosting and blood each draw the strongest one asked for. The result does not depend on which resource asked first, and removing one effect leaves the game's grade and every other effect as they were.
     * 
     * An effect belongs to the resource that created it: only that resource can change or remove it, and it goes when the resource stops, when the session ends or when the level changes. A cinematic that resets the game's post-processing clears the screen until the effect next changes. Up to 64 effects are kept across every resource.
     * @param effect Which effect, as `screenEffects` names it.
     * @param params Values for the effect's own parameters, each clamped into its range. A parameter left out takes its default; one the effect does not take is an error.
     * @param options `fadeInMs` brings it up from nothing; `fadeOutMs` is how long it takes to go, when its duration runs out or when `removeScreenEffect` is called without a fade of its own; `durationMs` ends it on its own after that long, before its fade-out -- 0, the default, keeps it until it is removed. Fades are up to a minute.
     * @returns A handle for `updateScreenEffect`, `removeScreenEffect` and `isScreenEffectActive`, or 0 when 64 effects are already on screen.
     */
    addScreenEffect(effect: "colorGrade" | "blur" | "ghosting" | "screenBlood", params?: { saturation?: number; brightness?: number; contrast?: number; cyan?: number; magenta?: number; yellow?: number; darkness?: number; hue?: number; amount?: number }, options?: { fadeInMs?: number; fadeOutMs?: number; durationMs?: number }): number;

    /**
     * Changes an effect while it is on screen -- a drunkenness that deepens with every drink, a wound that worsens.
     * @param handle What `addScreenEffect` returned.
     * @param params New values for some of the effect's parameters. The others keep theirs.
     * @param options `fadeMs` moves the parameters to their new values over that long, from wherever they are now; 0, the default, moves them at once.
     * @returns False when the handle is gone or belongs to another resource.
     */
    updateScreenEffect(handle: number, params: { saturation?: number; brightness?: number; contrast?: number; cyan?: number; magenta?: number; yellow?: number; darkness?: number; hue?: number; amount?: number }, options?: { fadeMs?: number }): boolean;

    /**
     * Takes an effect off the screen. An effect already fading out carries on from where it is.
     * @param handle What `addScreenEffect` returned.
     * @param options How long it takes to fade out. Left out, it fades over the `fadeOutMs` it was created with; 0 takes it off at once.
     * @returns False when the handle is gone or belongs to another resource.
     */
    removeScreenEffect(handle: number, options?: { fadeOutMs?: number }): boolean;

    /**
     * Takes every effect the calling resource owns off the screen at once. Other resources' effects stay.
     * @returns How many were removed.
     */
    removeAllScreenEffects(): number;

    /**
     * Whether an effect is still on screen, fading out included.
     * @param handle What `addScreenEffect` returned.
     * @returns False once it has been removed and faded, has run its duration out, or was dropped by its resource stopping, the session ending or the level changing.
     */
    isScreenEffectActive(handle: number): boolean;

    /**
     * Projects a world point through the camera this frame is drawn with, so a resource can pin its own overlay to the world.
     * @param point World position to project.
     * @returns Its screen position, or null when the point is behind the camera or there is no view to place it in.
     */
    worldToScreen(point: Vector3): ScreenPoint | null;
  };

  /**
   * One parameter a screen effect takes.
   */
  interface ScreenEffectParam {
    /**
     * The key to pass it under in a params bag.
     */
    name: string;

    /**
     * What it does to the picture.
     */
    description: string;

    /**
     * The lowest value it takes. Anything lower is clamped.
     */
    min: number;

    /**
     * The highest value it takes. Anything higher is clamped.
     */
    max: number;

    /**
     * What `addScreenEffect` uses when the parameter is not given.
     */
    default: number;

    /**
     * The value at which it leaves the picture alone, which is also where a fade starts and ends.
     */
    neutral: number;
  }

  /**
   * One screen effect `Hud.addScreenEffect` accepts.
   */
  interface ScreenEffect {
    /**
     * What to pass to `addScreenEffect`.
     */
    name: string;

    /**
     * What it does, and how several of it combine.
     */
    description: string;

    /**
     * Every parameter it takes, with its range.
     */
    params: ScreenEffectParam[];
  }

  /**
   * Where a world point lands on screen, in the pixels Web views are placed in.
   */
  interface ScreenPoint {
    /**
     * Pixels from the left edge of the window's client area.
     */
    x: number;

    /**
     * Pixels from the top edge of the window's client area.
     */
    y: number;

    /**
     * Whether the point is inside the view. One past an edge keeps its coordinates with this false.
     */
    onScreen: boolean;
  }

  /**
   * The game's own sound triggers, played at this machine. A trigger is a name the game's audio data declares -- `a_o_bell_kkut_kostelni`, `c_torch_whoosh1`, `f_ge_cough_woman` -- standing for an FMOD event. The full vocabulary is `Libs/GameAudio/*.xml` in `IPL_GameData.pak`, with the gameplay-facing subset listed in `Libs/Tables/GameAudio/SkaldAtlTrigger.xml`. Nothing here is replicated: a sound everyone should hear is one the server tells every client to play. Every call returns false while the audio system is not up, which is the case before a world is loaded, and for a trigger name the audio data does not declare.
   */
  const Audio: {
    /**
     * Plays a trigger with no position: heard at full volume wherever the player is, like a UI cue. It goes on the one global audio object every 2D sound in the game shares, which is why it cannot be stopped again.
     * @param trigger Name of the trigger, spelled as the game's audio data spells it. Not text and not translated.
     * @returns True when the audio system took it.
     */
    play(trigger: string): boolean;

    /**
     * Plays a trigger once at a point in the world, positioned and attenuated against the player's ears. The sound is handed to the engine and forgotten: it stays where it was put, plays to its end, and cannot be stopped or moved. Occlusion is ignored rather than ray-traced, so a wall between the point and the player does not silence it.
     * @param trigger Name of the trigger, spelled as the game's audio data spells it.
     * @param position Where to play it, in world-space metres.
     * @returns True when the audio system took it.
     */
    playAt(trigger: string, position: Vector3): boolean;

    /**
     * Plays a trigger on an entity, giving it an audio component if it has none. The sound follows the entity for as long as it lasts and can be stopped again by name, which is what anything attached to a body, a horse or a prop wants.
     * @param trigger Name of the trigger, spelled as the game's audio data spells it.
     * @param entityId Engine entity to hang it on -- what `Player.entityId` reports. Entity ids are reused, so one is only meaningful while the entity is alive.
     * @returns True when the entity was there and took it.
     */
    playOnEntity(trigger: string, entityId: number): boolean;

    /**
     * Plays a sound file on an entity, positioned and attenuated like the game's own spoken lines and under the same volume settings. A file streamed by the server plays exactly like one the game ships. An entity takes one file per 0.1 seconds.
     * @param file Game-root path of an `.ogg`, with or without the extension -- `sounds/kcdc/<resource>/line` for one a server streams.
     * @param entityId Engine entity to play it on -- what `Player.entityId` reports.
     * @param purpose Which of the game's voice host events carries it: `inner`, `10`, `20`, `35`, `50`, `100`, `200` (a bark heard up to that many metres), `sfp_dialog`, `sfp_cutscene` or `sfp_music`. Defaults to `50`.
     * @returns True when the entity was there and took it.
     */
    playFile(file: string, entityId: number, purpose?: string): boolean;

    /**
     * Stops every instance of one trigger on the entity. Triggers that have already finished are not an error.
     * @param trigger Name of the trigger to stop.
     * @param entityId Engine entity it was played on.
     * @returns True when the entity was there to take it.
     */
    stopOnEntity(trigger: string, entityId: number): boolean;

    /**
     * Stops everything the entity is playing, the game's own sounds included. Use `stopOnEntity` when the trigger is known.
     * @param entityId Engine entity to silence.
     * @returns True when the entity was there to take it.
     */
    stopAllOnEntity(entityId: number): boolean;

    /**
     * Sets one of the continuous parameters the game's FMOD events read, on this entity's sounds alone. It affects what is already playing as well as what starts later.
     * @param entityId Engine entity whose sounds the parameter applies to.
     * @param parameter Name of the continuous parameter, as `Libs/Tables/GameAudio/SkaldAtlRtpc.xml` lists it -- `horse_speed`, `player_stamina`, `wind_velocity`.
     * @param value What to set it to. The range is whatever the FMOD event expects.
     * @returns True when both the parameter and the entity were there.
     */
    setEntityParameter(entityId: number, parameter: string, value: number): boolean;

    /**
     * Selects one of the discrete switch states the game's sounds branch on, for this entity alone.
     * @param entityId Engine entity whose sounds the switch applies to.
     * @param switchName Name of the switch, as `Libs/GameAudio/g_switches.xml` declares it -- `sword_mat`, `bow_type`.
     * @param state Which of that switch's states to select. States belong to their switch, so the same name under a different switch is a different state.
     * @returns True when the switch, the state and the entity were all there.
     */
    setEntitySwitch(entityId: number, switchName: string, state: string): boolean;

    /**
     * Asks the audio system whether it knows a trigger by this name, without playing it. A patch or another mod can change what is declared, so a resource that ships names can fail early rather than silently.
     * @param trigger Name to look for.
     * @returns True when the name resolves. False also while the audio system is not up.
     */
    hasTrigger(trigger: string): boolean;
  };

  /**
   * What the server streams, seen from this machine. Models, materials, textures, sounds and particle libraries a server streams load by path like the game's own -- `objects/kcdc/<resource>/chair.cgf` works wherever a model path does -- so nothing here is needed to use them. Clips play through the server's `playAnimation("", { clip })`.
   */
  const Assets: {
    /**
     * Whether the session's streamed assets carry a file at this path. Only new assets count; a replaced stock file is the game's own path.
     * @param path Path inside the game's data, as a server streams it -- `objects/kcdc/<resource>/chair.cgf`.
     * @returns True when a mounted stream-lane pak carries it.
     */
    has(path: string): boolean;
  };

  /**
   * The game's own particle effects, played on this machine. An effect is a name out of the game's particle libraries -- `WH_Particels.fires.candle`, `collisions.combat.sword_sword`, `cinematics.dust.dust_army_a` -- and `list` is the whole vocabulary. Nothing here is replicated: an effect everyone should see is one the server tells every client to play with `Vfx.burst`, or one the server places with `Vfx.spawn`, which the interest grid then streams to whoever is near it. Almost every effect the game ships is continuous, which means it never ends on its own -- so everything started here is given a duration, and one that is not stopped first is stopped when the session ends.
   */
  const Vfx: {
    /**
     * Plays an effect standing at a point in the world, on an entity this client owns and removes again. Every spawn parameter is read once, when the emitter is created, so none of them can be changed afterwards -- stop it and start another.
     * @param effect Name of the effect, spelled as the game's particle libraries spell it.
     * @param position Where to play it, in world-space metres.
     * @param options `rotation` aims the emitter; `scale`, `countScale`, `speedScale` and `timeScale` multiply what the effect authored; `strength` feeds its strength curves; `pulsePeriod` restarts it on that many seconds; `prime` starts it already running, which is what a fire wants. `durationMs` is how long it lives -- two seconds by default, and an hour at most.
     * @returns A handle to pass to `stop`, or 0 when the world was not up, the budget was full or the game would not load the effect.
     */
    spawn(effect: string, position: Vector3, options?: { rotation?: Quaternion | Vector3; scale?: number; countScale?: number; speedScale?: number; timeScale?: number; strength?: number; pulsePeriod?: number; prime?: boolean; durationMs?: number }): number;

    /**
     * Plays an effect in a free render slot of an entity, where it follows that entity: smoke off a cart, a glow around a body, dust under a horse. The slot is freed when the duration runs out, when `stop` is called, or when the entity goes. The offset is measured from the entity's own origin, not from a bone, so an effect cannot be pinned to a hand or a weapon this way.
     * @param effect Name of the effect, spelled as the game's particle libraries spell it.
     * @param entityId Engine entity to hang it on -- what `Player.entityId` reports. Entity ids are reused, so one is only meaningful while the entity is alive.
     * @param options As `spawn`, plus `offset`: where the emitter sits in the entity's own space, in metres. Up to 8 m from its origin.
     * @returns A handle to pass to `stop`, or 0 when the entity was not there or the game would not load the effect.
     */
    attach(effect: string, entityId: number, options?: { offset?: Vector3; rotation?: Quaternion | Vector3; scale?: number; countScale?: number; speedScale?: number; timeScale?: number; strength?: number; pulsePeriod?: number; prime?: boolean; durationMs?: number }): number;

    /**
     * Stops one effect and releases what it was standing on.
     * @param handle What `spawn` or `attach` returned.
     * @returns True when the handle named an effect that was still playing.
     */
    stop(handle: number): boolean;

    /**
     * Stops every effect this client started, including ones other resources started. The session ending does this too.
     * @returns Nothing.
     */
    stopAll(): void;

    /**
     * Whether the effect behind a handle is still running. False once its duration has run out, its entity has gone, or the level has changed under it.
     * @param handle What `spawn` or `attach` returned.
     * @returns True while it is playing.
     */
    isPlaying(handle: number): boolean;

    /**
     * Asks the game whether it can resolve an effect by this name, without playing it. False while no world is loaded, and for a name the catalog knows but this install's particle libraries no longer declare, so a resource that ships names can fail early rather than silently.
     * @param effect Name to look for.
     * @returns True when the name resolves here and now.
     */
    has(effect: string): boolean;

    /**
     * Every effect name the shipped game declares, in order. Mined from the particle libraries at build time, so it is the same list on the server.
     * @param prefix Keep only the names starting with this, e.g. `WH_Particels.fires` or `collisions.combat`.
     * @returns The matching names.
     */
    list(prefix?: string): string[];

    /**
     * How many effects this client is playing right now, across every resource. The budget is 192.
     * @returns The number of live effects.
     */
    count(): number;
  };

  /**
   * A point light created on this client. Not replicated; server scripts can use events to coordinate local lights, or the server Light API for persistent replicated lights.
   */
  class Light {
    private constructor();

    /**
     * Client-local service handle, distinct from server network entity identifiers.
     */
    readonly id: number;

    /**
     * Whether the native light still exists. False after destruction, resource cleanup or a world change.
     */
    readonly valid: boolean;

    /**
     * World-space position in metres. Invalid positions throw before mutation.
     */
    position: Vector3;

    /**
     * RGB color using x, y and z, each from 0 to 1.
     */
    color: Vector3;

    /**
     * Diffuse intensity multiplier, from 0 to 100.
     */
    intensity: number;

    /**
     * Light radius in metres, from 0.1 to 100.
     */
    radius: number;

    /**
     * Whether to cast native shadows. False by default; rendering also depends on the game's shadow settings.
     */
    castShadows: boolean;

    /**
     * Removes this client-local light. Safe to repeat.
     */
    destroy(): void;

    /**
     * Creates a client-local point light until destroyed, its creating resource stops or errors, or the session/world ends. Throws when the world is unavailable, native creation fails or the shared 192-light budget is full.
     * @param position World-space position in metres.
     * @param options Defaults to warm orange, intensity 1, radius 5 and shadows off.
     * @returns The new local light handle.
     */
    static spawn(position: Vector3 | { x: number; y: number; z: number }, options?: { color?: Vector3 | { x: number; y: number; z: number }; intensity?: number; radius?: number; castShadows?: boolean }): Light;
  }

  /**
   * A mark this client owns on the game's compass and map screen.
   */
  class Blip {
    /**
     * Creates a handle for a blip that already exists; use Blip.create() to make one.
     * @param id Identifier of an existing blip.
     */
    constructor(id: number);

    /**
     * Identifier of this blip, unique for the lifetime of the session. Ids are not reused.
     */
    readonly id: number;

    /**
     * Whether the blip is still in the registry. A handle to a removed blip keeps reading, and every other property reads as its default.
     */
    readonly valid: boolean;

    /**
     * Whether the compass currently carries a mark for this blip. It goes false on its own whenever the game empties the compass -- a level load does -- and back to true on the next tick.
     */
    readonly live: boolean;

    /**
     * World-space position in metres. The compass recomputes bearing and distance from it every frame, so assigning moves the mark with no rebuild.
     */
    position: Vector3;

    /**
     * Which of the game's own compass icons the mark draws as, by the game's own name for it. Assignment rebuilds the native mark, because the icon is read once when a mark is registered and never re-read.
     */
    type: string;

    /**
     * The mark's state, forwarded to the compass movie as-is. What a given value draws is undocumented; 0 is what a fresh mark carries.
     */
    state: number;

    /**
     * The blip's name on the map screen, shown verbatim in place of the name the game gives its icon type; empty keeps that name. A compass mark has no text, so the compass never shows it.
     */
    label: string;

    /**
     * Whether the blip is drawn on the HUD compass. True by default.
     */
    compass: boolean;

    /**
     * Whether the blip is drawn on the map screen. True by default, but only for an icon the map has art for: `Main`, `Side`, `Micro`, `Checkpoint`, `Dlcs`, `Bailiff`, `FistFight` and `Racing` have none and stay on the compass. The map legend's switch for the blip's icon type hides it like one of the game's own unless `legend` is false, and a change to a blip on the open map is redrawn within a frame.
     */
    map: boolean;

    /**
     * Whether the map legend's switch for the blip's icon type hides it, as it does the game's own marks of that type. True by default. False keeps it on the map screen whatever that switch says, so a script can hide the game's own marks of a type with `WorldMap.setCategoryVisible` and still draw its own with the same icon.
     */
    legend: boolean;

    /**
     * Formats this blip handle for logging and debugging.
     * @returns The blip's id, its type and its position, or a note that it is gone.
     */
    toString(): string;

    /**
     * Takes this blip off the compass and the map screen and out of the registry.
     * @returns True when it was still there to remove.
     */
    remove(): boolean;

    /**
     * Puts a mark on this client's compass and map screen and keeps it there, rebuilding it whenever the game drops it.
     * @param options Where the blip goes, and optionally which icon to draw it with, what state to give it, what the map screen calls it, whether it shows on the compass and the map screen -- both, by default -- and whether the map legend's switch for its icon hides it, as by default. `type` is one of the game's own compass mark names -- `Checkpoint`, `QuestGiver`, `Shop`, `GeneralPoi` and the rest -- and defaults to `GeneralPoi`; an unknown one is rejected rather than guessed at.
     * @returns The new blip, or null at the blip limit.
     */
    static create(options: { position: Vector3; type?: string; state?: number; label?: string; compass?: boolean; map?: boolean; legend?: boolean }): Blip | null;

    /**
     * Looks a blip up by its id.
     * @param id Blip identifier.
     * @returns The blip's handle, or null when it no longer exists.
     */
    static getById(id: number): Blip | null;

    /**
     * Lists every blip this client's scripts made. Blips the server placed are not included.
     * @returns One handle per live blip, in creation order.
     */
    static all(): Blip[];

    /**
     * Takes every blip this client's scripts made off the compass and the map screen, leaving the server's blips and the game's own marks alone.
     */
    static removeAll(): void;
  }

  /**
   * The door a player's use is about, as this machine sees it when they use it.
   */
  interface DoorUse {
    /**
     * The server's door entity, as server-side `door.id`; 0 when the server has not sent this door yet.
     */
    id: number;

    /**
     * The door's EntityGuid, as sixteen lowercase hex digits: the same value as server-side `door.guid`.
     */
    guid: string;

    /**
     * The entity name the level gives the door.
     */
    name: string;

    /**
     * Whether the door stands open on this machine.
     */
    open: boolean;

    /**
     * Whether the door is locked on this machine.
     */
    locked: boolean;
  }

  /**
   * Native location or POI GUID fields.
   */
  interface LocationId {
    /**
     * First GUID field, an unsigned 32-bit integer.
     */
    data1: number;

    /**
     * Second GUID field, an unsigned 16-bit integer.
     */
    data2: number;

    /**
     * Third GUID field, an unsigned 16-bit integer.
     */
    data3: number;

    /**
     * The final eight GUID bytes, in native order.
     */
    data4: Uint8Array;
  }

  /**
   * One thing a trace met, as the machine that traced it saw it.
   */
  interface WorldRayHit {
    /**
     * Where the ray met it, in world-space metres.
     */
    position: Vector3;

    /**
     * The surface normal at that point, as a unit vector.
     */
    normal: Vector3;

    /**
     * How far along the ray it sits, in metres.
     */
    distance: number;

    /**
     * The surface type's own name, spelled the way the game's tables spell it in `Libs/MaterialEffects/SurfaceTypes.xml` -- `mat_wood`, `mat_rock`, `mat_soil`, `mat_water`. It says what the thing is made of, which is how footsteps and blows sound, not what it is: a plank wall is `mat_wood` as much as a trunk is, so tell a tree by `category`. Among the 74 the game defines: `mat_wood`, `mat_wood_soft`, `mat_vegetation`, `mat_bushes`, `mat_rock`, `mat_rock_unwalk`, `mat_rock_horse_ignore`, `mat_stairs_stone`, `mat_gravel`, `mat_soil`, `mat_mud`, `mat_grass`, `mat_road`, `mat_metal`, `mat_glass`, `mat_plaster`, `mat_thatch`, `mat_fabric`, `mat_flesh`. Empty only before the material tables are up.
     */
    surface: string;

    /**
     * Whether the ground itself was hit rather than anything placed on it. Nothing lies behind the terrain, so a trace stops there. The same as `kind` being `terrain`.
     */
    terrain: boolean;

    /**
     * What sort of thing the ray stopped on. `entity` is anything with an `entityClass` -- doors, props, NPCs, players. `vegetation` is an instance the level paints on: trees, bushes, plants, and boulders painted the same way. `brush` is a static mesh placed one by one: walls, houses, rocks and cliffs. `static` is static geometry that is neither, or whose source the engine does not record. Trees and rocks are not entities, so for them this, `model` and `category` are what say what was hit.
     */
    kind: "terrain" | "entity" | "vegetation" | "brush" | "static";

    /**
     * The mesh of a vegetation instance or a brush, as the path the game loads it from -- `objects/natural/vegetation/trees/normal_trees/quercus_robur/quercus_robur_big_a.cgf`. Null for terrain, entities and `static`. A tree or a rock has no GUID, but its `model` and `position` together are the same on every client on the same level, which is what a server keeps a felled tree or a mined rock by.
     */
    model: string | null;

    /**
     * Which of the game's own folders of natural objects `model` comes from: `tree` for `objects/natural/vegetation/trees` -- living, dead, fallen and stumps alike, which `model` tells apart -- `bush` for its bushes, `plant` for the rest of its vegetation (grass, ferns, mushrooms, branches), and `rock` for `objects/natural/rocks` and `objects/natural/stones`. Null for anything else, man-made included.
     */
    category: "tree" | "bush" | "plant" | "rock" | null;

    /**
     * The material drawn where the ray met it, as the path the material manager keys it by, the sub-material when the mesh has several -- a trunk's bark rather than the tree's whole material. Null for terrain and when nothing names one.
     */
    material: string | null;

    /**
     * The level's own identity for what was hit, as sixteen lowercase hex digits -- the same on every machine, and what `Door.find` and the other GUID lookups take. Null for terrain, for static geometry and for anything the session spawned, none of which the level names.
     */
    entityGuid: string | null;

    /**
     * The name the level gives it, often empty. Null when nothing that carries a name was hit.
     */
    entityName: string | null;

    /**
     * The engine class it belongs to -- `AnimDoor`, `NPC_NAI`, `GeomEntity`. Null when nothing that carries a class was hit.
     */
    entityClass: string | null;

    /**
     * This machine's own handle for it. Local to this client and to this session -- it is not the network ID and it is not portable, so store `entityGuid` instead. Null for terrain and static geometry.
     */
    entityId: number | null;

    /**
     * The object the level placed that was hit, named the way `LevelEdit.apply` finds it again on every machine, with where the level put it and how it turned and sized it. A tree, a rock or a wall is one; so is a door or any other entity the level gives a GUID. Null for terrain, for static geometry the engine records no source for, and for anything the session spawned -- props, players, NPCs. Spread it into a definition to take the object out or swap it: `LevelEdit.apply({ ...hit.levelObject, model: stump })`.
     */
    levelObject: WorldLevelObject | null;

    /**
     * The player whose body it was -- the local player's own included, when `ignoreSelf` is off -- or null for anything else. Found through the body's own entity, so it holds for a disguised player too: their collider is the mesh's size, and a trace that meets the mesh names them.
     */
    player: Player | null;
  }

  /**
   * An object the level placed, as a level edit keys it, and the pose the level gave it. Every field a `LevelEditDefinition` needs to find it again.
   */
  interface WorldLevelObject {
    /**
     * A `brush` is a static mesh or a vegetation instance -- a wall, a rock, a tree -- found by its mesh and where the level put it. An `entity` is found by its GUID.
     */
    kind: "brush" | "entity";

    /**
     * A brush's mesh path, the same as the hit's `model`; an entity's name.
     */
    name: string;

    /**
     * An entity's level GUID as sixteen lowercase hex digits; absent for a brush.
     */
    guid: string | undefined;

    /**
     * An entity's engine class; absent for a brush.
     */
    class: string | undefined;

    /**
     * Where the level put it: its pivot, not the point the ray met. Unchanged while an edit has moved it, since it is the key.
     */
    at: Vector3;

    /**
     * How the level turned it, for a prop or effect standing in its place. `LevelEdit.apply` reads a rotation only with a `position`, so spreading this into a definition moves nothing.
     */
    rotation: Quaternion;

    /**
     * How the level sized it, per axis. Read only with a `position`, as `rotation` is.
     */
    scale: Vector3;
  }

  /**
   * One entity a sphere reported, and where it stood when it did.
   */
  interface WorldNearbyEntity {
    /**
     * Where it is, in world-space metres.
     */
    position: Vector3;

    /**
     * How far it is from the centre of the sphere, in metres.
     */
    distance: number;

    /**
     * The level's own identity for it, as sixteen lowercase hex digits. Null for anything the session spawned, which the level does not name.
     */
    guid: string | null;

    /**
     * The name the level gives it, often empty.
     */
    name: string;

    /**
     * The engine class it belongs to -- `AnimDoor`, `NPC_NAI`, `GeomEntity`.
     */
    class: string;

    /**
     * Whether the engine has built physics for it, which is what makes it something a trace could also find.
     */
    physicalized: boolean;

    /**
     * This machine's own handle for it. Local to this client and to this session -- it is not the network ID and it is not portable, so store `guid` instead.
     */
    entityId: number;
  }

  /**
   * What this machine's engine can be asked about the level around it: what a trace meets, what the ground is under a point, and which entities are inside a sphere.
   * 
   * Each is the engine's own query rather than a search over everything loaded, so the cost is what the question covers. They answer inside the call, because this is the machine that has the world -- the server's `World` asks the same three questions as promises, because it has to borrow a client's world to answer them.
   * 
   * None of it is replicated and none of it is authority: an answer describes what one client has streamed in at one moment. A zone that matters is decided on the server, from an answer the server asked for itself.
   */
  const World: {
    /**
     * Traces a segment through the world and reports the first thing it meets.
     * @param from Where the ray starts, in world-space metres.
     * @param to Where it ends. Up to 4096 metres away; a ray of no length is refused.
     * @param options `mode` picks which of the game's own three traces to run: `cover` asks whether the world is in the way -- static geometry, props and doors block, and bodies cannot, which is what a line of sight wants; `anything` asks what is under the ray, bodies included, which is what a pick wants; `ground` sees only surfaces a player could stand on. It defaults to `cover`, and a name that is none of the three is rejected rather than guessed at.
     * 
     * `ignoreSelf` passes the local player's own body through, and is on by default: the player stands where the camera does, so a trace from the camera would otherwise mostly hit them. Both of a body's colliders are skipped, not just the entity's.
     * @returns What it hit, or null when the ray met nothing and when this machine has no world loaded.
     */
    raycast(from: Vector3, to: Vector3, options?: { mode?: "cover" | "anything" | "ground"; ignoreSelf?: boolean }): WorldRayHit | null;

    /**
     * Traces a segment and reports everything solid along it, nearest first. Each hit is found by re-tracing past the one before, so a window does not hide the wall it is set in.
     * @param from Where the ray starts, in world-space metres.
     * @param to Where it ends. Up to 4096 metres away; a ray of no length is refused.
     * @param options `mode` and `ignoreSelf` are as `World.raycast` documents them. `maxHits` is how many things along the ray to report, up to 8 and 8 by default.
     * @returns What the ray met, nearest first; empty when it met nothing and when this machine has no world loaded.
     */
    raycastAll(from: Vector3, to: Vector3, options?: { mode?: "cover" | "anything" | "ground"; maxHits?: number; ignoreSelf?: boolean }): WorldRayHit[];

    /**
     * The height of the ground under a point.
     * 
     * A trace rather than a terrain height, so it stands on whatever is actually there -- a bridge, a floor, a castle roof -- and not on the heightmap underneath it.
     * @param position The point to look under, in world-space metres. Its own z is where the probe is centred.
     * @param options How far above the point the probe starts and how far below it reaches, in metres. 5 and 200 by default -- the 5 above is what lets a point already slightly underground still resolve -- and up to 512 each.
     * @returns The world-space z of what is underfoot, or null when the probe reached nothing within its range.
     */
    getGroundZ(position: Vector3, options?: { up?: number; down?: number }): number | null;

    /**
     * The same probe as `getGroundZ`, with everything it found rather than only the height: the slope it landed on, and what the surface is made of.
     * @param position The point to look under, in world-space metres.
     * @param options How far above the point the probe starts and how far below it reaches, in metres. 5 and 200 by default -- the 5 above is what lets a point already slightly underground still resolve -- and up to 512 each.
     * @returns What is underfoot, or null when the probe reached nothing.
     */
    resolveGround(position: Vector3, options?: { up?: number; down?: number }): WorldRayHit | null;

    /**
     * Every entity the engine has inside a sphere, nearest first.
     * 
     * This reads the engine's own spatial grid, so it costs what the sphere covers rather than what the level holds -- but it only sees what this client has streamed in, which for a distant point is nothing.
     * @param centre The centre of the sphere, in world-space metres.
     * @param radius Its radius in metres, up to 256.
     * @param options `class` reports only entities of that engine class -- `AnimDoor`, `NPC_NAI`, `GeomEntity` -- and a class the engine does not know matches nothing rather than everything; omitted, every class is reported. `max` is how many to report, nearest first, up to 64. `physicalOnly` skips entities the engine has built no physics for, which is the filter the game's own proximity query applies, and is off by default.
     * @returns The entities inside it, nearest first.
     */
    entitiesInRadius(centre: Vector3, radius: number, options?: { class?: string; max?: number; physicalOnly?: boolean }): WorldNearbyEntity[];
  };

  /**
   * The marker the player has dropped on the map, as `WorldMap.getWaypoint` reports it.
   */
  interface MapWaypoint {
    /**
     * Where the marker sits, in world-space metres. Its z is the terrain height under the point.
     */
    position: Vector3;

    /**
     * Which map it belongs to. The maps reuse one coordinate range.
     */
    mapId: number;
  }

  /**
   * The map screen at this machine, and the player's own marker on it. Nothing here is replicated: a marker everyone should see is one the server tells every client to make.
   */
  const WorldMap: {
    /**
     * Whether the map screen is up right now. False in the main menu and across a level load, when there is no map page at all.
     */
    readonly isOpen: boolean;

    /**
     * Reads the marker the player has dropped on the map they are looking at. The game keeps at most one per map.
     * @returns The marker's position and the map it belongs to, or null when there is none.
     */
    getWaypoint(): MapWaypoint | null;

    /**
     * Moves the marker the player has already dropped. It cannot create one, so it reads false when there is nothing down.
     * @param position Where to put it, in world-space metres. Nothing snaps it to the terrain.
     * @returns True when there was a marker to move.
     */
    moveWaypoint(position: Vector3): boolean;

    /**
     * Takes the player's marker off the map they are looking at. It runs the game's own removal, so `mapWaypointCleared` is raised as it would be for the player.
     * @returns True when there was a marker to take off.
     */
    clearWaypoint(): boolean;

    /**
     * Whether marks of this type are currently drawn on the compass and listed in the map legend.
     * @param type One of the game's own compass mark types, spelled the way the game spells it -- `Shop`, `QuestGiver`, `GeneralPoi`. The full list is the `type` a `Blip` accepts.
     * @returns True when they are shown.
     */
    isCategoryVisible(type: string): boolean;

    /**
     * Shows or hides a whole category of the game's own map markers -- every shop, every quest giver -- through the switch the map legend's rows drive. It affects the marks already placed as well as later ones. DLC icons share a legend row with the entry they belong to, so setting one sets the group.
     * @param type One of the game's own compass mark types, spelled the way the game spells it -- `Shop`, `QuestGiver`, `GeneralPoi`. The full list is the `type` a `Blip` accepts.
     * @param visible Whether marks of that type should be drawn.
     * @returns True when the map page was there to take it.
     */
    setCategoryVisible(type: string, visible: boolean): boolean;

    /**
     * Marks a place on this player's map as discovered, silently: no toast, no XP, no discovery trigger. What a resource that keeps a character's discoveries calls after the character is loaded, since every session starts on a fresh map. A point of interest raises `poiDiscovered` as it is marked; a location raises nothing.
     * @param id A point of interest or a location -- a town, a district -- as `poiDiscovered` or `locationDiscovered` handed it out. `data4` must be a Uint8Array of eight bytes.
     * @returns True when the id named a place in the loaded level; false otherwise, or before the level is up.
     */
    discover(id: LocationId): boolean;

    /**
     * Clears the fog of war around a point by the game's own reveal radius, as walking there does. What a resource that keeps where a character has been calls after the character is loaded. Only matters while the server's `reveal_map` is off; otherwise there is no fog.
     * @param position A world-space point; only x and y count.
     * @returns True when the level's fog took it; false before the level is up.
     */
    revealFog(position: Vector3): boolean;

    /**
     * Clears the fog of war over the whole level, as the game's own `player_revealFow` does, and leaves discovery alone: places still become discovered when the player finds them. The fog is also what hides the names of places not yet found, so from then on the map writes a town's name only once the player knows of it. What a server that keeps `reveal_map` off for the discoveries, but wants no fog, calls once the character is loaded. A level load brings the fog back.
     * @returns True when the level's fog took it; false before the level is up.
     */
    revealAllFog(): boolean;
  };

  /**
   * The compass strip along the top of the HUD, which is what this game has instead of a minimap. Marks go on it through `Blip`; this is the strip itself.
   */
  const Compass: {
    /**
     * How many marks the compass carried at the last tick, the game's own included. Sampled rather than read live: the vector it comes from is reallocated by the thread that adds to it.
     */
    readonly markCount: number;

    /**
     * Whether the compass is showing. Assignment pushes the game's own suppression, the one a cutscene uses, so it takes the whole strip rather than emptying it. It reads the mod's own view -- a suppression the game pushed is invisible here -- and the mod's is released when the session ends.
     */
    visible: boolean;
  };

  /**
   * The local player's fast travel, as something to read. There is deliberately no verb: starting a travel moves the player, advances the world clock and can fire a random event, so in a session that is the server's decision rather than one for a resource on the traveller's own machine.
   */
  const Travel: {
    /**
     * Whether a fast travel is running right now.
     */
    readonly active: boolean;

    /**
     * Where the travel has got to along its route, in world-space metres. Zero while none is running: check `active` first.
     */
    readonly position: Vector3;

    /**
     * Asks the game whether a fast travel could begin right now, without starting one. The answer is the game's own refusal code; only 0 is proven, and it means yes.
     * @returns The refusal code, or null before the player module is up.
     */
    canStart(): number | null;
  };

  /**
   * Where the free camera is and what it is doing.
   */
  interface NoClipState {
    /**
     * Whether the player's body is carried through the world or left behind.
     */
    mode: "body" | "camera";

    /**
     * Whether this machine's own keyboard and mouse are flying the camera.
     */
    input: boolean;

    /**
     * Where the camera is, in world-space metres.
     */
    position: Vector3;

    /**
     * The unit direction it is looking along.
     */
    forward: Vector3;

    /**
     * The unit direction to its right, level with the horizon.
     */
    right: Vector3;

    /**
     * The unit direction out of the top of the frame.
     */
    up: Vector3;

    /**
     * Its heading in degrees, wrapped to -180..180. Zero faces +Y and a rising yaw swings left.
     */
    yaw: number;

    /**
     * Its elevation in degrees, clamped to -89..89. Positive looks up.
     */
    pitch: number;

    /**
     * Metres a second at rest, before boost or crawl scales it.
     */
    speed: number;

    /**
     * The vertical field of view in degrees the view was asked for.
     */
    fov: number;

    /**
     * The vertical field of view the renderer settled on, which `Hor+` adaptation and the window's aspect both move.
     */
    renderedFov: number;

    /**
     * Width over height, as the renderer reports it.
     */
    aspectRatio: number;
  }

  /**
   * This machine's free camera, and what happens to the player's body while it is flying.
   * 
   * The view is a bodyless anchor entity made the active camera through the engine's own view system, so nothing is ever asked to sweep against the world. In `body` mode the player's entity is taken out of the physical world and placed at the camera each frame.
   * 
   * Client-only, and nothing here is an order another machine takes. It is not invisible to the session either: in `body` mode the thing being flown is the entity this client replicates, so the other players watch the flier go. In `camera` mode the body never moves.
   * 
   * The game's controls are held for as long as the flight lasts, whichever side is driving. The map editor (`MapEditor`) flies the same one camera: a flight in progress ends when the editor opens, and `enable` refuses while it is open.
   */
  const NoClip: {
    /**
     * Starts flying. The camera begins where the player was looking, so this reads as stopping rather than as a cut.
     * @param options `mode` decides what happens to the player. `body`, the default, carries them: they pass through geometry, the world streams around them, the other players see them fly, and they land wherever the flight ends. `camera` leaves the body where it stands and touches nothing about its physics. A name that is neither is rejected rather than guessed at.
     * 
     * `input` is whether this machine's keyboard and mouse fly the camera -- on the keys the game's own photo mode is bound to in the controls menu (forward, back, left, right, jump to rise, crouch to sink, fast movement to boost), read through the active keyboard layout so AZERTY and the like work as the game does; Alt to crawl, mouse to turn. On by default; off, drive it with `move`.
     * 
     * `speed` is metres a second at rest, 0.05 to 400 and 12 by default. `fov` is the vertical field of view in degrees, up to 140; omitted, the view keeps the game's.
     * @returns `enabled` when the flight started. `alreadyActive` when one was already running -- the options are not re-applied. `noCamera` when there is no level to spawn the camera's anchor into. `cameraBusy` when the map editor holds the camera.
     */
    enable(options?: { mode?: "body" | "camera"; input?: boolean; speed?: number; fov?: number }): "enabled" | "alreadyActive" | "noCamera" | "cameraBusy";

    /**
     * Ends the flight, hands the view back and puts the body back into the physical world. Does nothing when no flight is running.
     * @param options `keepPosition` leaves the body where the camera stopped, and is the default. False puts it back where it took off from, facing the way it was. Neither means anything in `camera` mode.
     * @returns Nothing. `noclipChanged` reports the end with reason `script`.
     */
    disable(options?: { keepPosition?: boolean }): void;

    /**
     * Whether a flight is running at this machine.
     * @returns True while the free camera has the view.
     */
    isActive(): boolean;

    /**
     * Everything about the flight in one read, rather than six calls that could each land on a different frame.
     * @returns The state, or null when no flight is running.
     */
    getState(): NoClipState | null;

    /**
     * Where the camera is, in world-space metres.
     * @returns Its position, or null when no flight is running.
     */
    getPosition(): Vector3 | null;

    /**
     * Puts the camera somewhere else without turning it.
     * @param position Where to put it, in world-space metres. A non-finite position is ignored.
     * @returns True when the camera took the position; false when no flight is running.
     */
    setPosition(position: Vector3): boolean;

    /**
     * How the camera is aimed, in degrees. Two angles rather than a quaternion: that is what flying with a mouse produces, and it keeps the horizon level for free.
     * @returns Its angles, or null when no flight is running.
     */
    getRotation(): { yaw: number; pitch: number } | null;

    /**
     * Aims the camera without moving it.
     * @param rotation The angles to take, in degrees. Either may be left out to keep the one the camera has. Yaw wraps to -180..180 and pitch is clamped to -89..89.
     * @returns True when the camera took the angles; false when no flight is running.
     */
    setRotation(rotation: { yaw?: number; pitch?: number }): boolean;

    /**
     * The unit direction the camera is looking along.
     * @returns Its view direction, or null when no flight is running.
     */
    getForward(): Vector3 | null;

    /**
     * Places and aims the camera in one go.
     * @param position Where to put the camera, in world-space metres.
     * @param forward The direction to aim it along. Need not be normalized; a direction of no length is ignored.
     * @returns True when the camera took the pose; false when no flight is running.
     */
    setPose(position: Vector3, forward: Vector3): boolean;

    /**
     * Turns the camera to face a point, leaving it where it is.
     * @param point The world-space point to face.
     * @returns True when the camera turned; false when no flight is running.
     */
    lookAt(point: Vector3): boolean;

    /**
     * Turns to face a point and then stands back from it, which is what framing an object wants.
     * @param target The world-space point to look at.
     * @param distance How far back to stand from it, in metres, from 1 to 4096. 6 by default.
     * @returns True when the camera moved; false when no flight is running.
     */
    focus(target: Vector3, distance?: number): boolean;

    /**
     * Metres a second the camera flies at rest, before boost or crawl scales it.
     * @returns The speed, or 0 when there is no application to ask.
     */
    getSpeed(): number;

    /**
     * Sets how fast the camera flies. Kept across flights, so a resource can set it once.
     * @param metresPerSecond The new speed, clamped to 0.05..400.
     * @returns Nothing.
     */
    setSpeed(metresPerSecond: number): void;

    /**
     * The vertical field of view in degrees the view was last asked for, which is not what came back: `getState` reports the rendered one separately as `renderedFov`.
     * @returns The requested field of view.
     */
    getFov(): number;

    /**
     * Sets the camera's lens.
     * @param degrees The vertical field of view, clamped to 20..140.
     * @returns Nothing.
     */
    setFov(degrees: number): void;

    /**
     * Whether this machine's own keyboard and mouse are flying the camera.
     * @returns True while the built-in driver is on.
     */
    isInputEnabled(): boolean;

    /**
     * Hands the camera between the built-in driver and the resource, mid-flight. The game's controls stay held either way: the choice is who flies the camera, not whether the player can walk.
     * @param enabled True to let this machine's keyboard and mouse fly the camera, false to leave it to `move`.
     * @returns Nothing.
     */
    setInputEnabled(enabled: boolean): void;

    /**
     * Moves the camera by one frame of flight the resource resolved itself. Refused while the built-in driver is on, so the two cannot both integrate the same frame.
     * @param input One frame of flight. `forward`, `strafe` and `lift` are -1..1 along the view's heading, its right and world up -- lift is along world up rather than the view's, so rising and falling stay vertical while looking down. `yaw` and `pitch` are this frame's turn in degrees. `boost` multiplies the speed by six and `crawl` divides it by five, as Shift and Alt do.
     * @param deltaSeconds How long this frame is. Anything over 0.1 is treated as 0.1, so a stalled frame does not launch the camera across the map.
     * @returns True when the frame was applied. False when no flight is running and when the built-in driver has the camera.
     */
    move(input: { forward?: number; strafe?: number; lift?: number; yaw?: number; pitch?: number; boost?: boolean; crawl?: boolean }, deltaSeconds: number): boolean;

    /**
     * The world direction through a point on the camera's near plane.
     * 
     * Normalized device coordinates rather than pixels, because the mod does not own the back buffer's size. Pair it with `World.raycast` from `getPosition()` to find what is under a point of the frame.
     * @param x Across the frame, -1 at the left edge and +1 at the right.
     * @param y Up the frame, -1 at the bottom and +1 at the top.
     * @returns A unit direction, or null when no flight is running.
     */
    rayThroughScreen(x: number, y: number): Vector3 | null;

    /**
     * Where a world point lands on the camera's frame, in the same normalized device coordinates `rayThroughScreen` takes.
     * @param point The world-space point to project.
     * @returns Its place on the frame, or null when no flight is running and when the point is behind the camera.
     */
    projectToScreen(point: Vector3): { x: number; y: number } | null;
  };

  /**
   * Where this client is drawing from, as the renderer settled on it this frame.
   */
  interface CameraPose {
    /**
     * The eye position, in world-space metres.
     */
    position: Vector3;

    /**
     * Where it looks, as a unit vector.
     */
    forward: Vector3;

    /**
     * Its own right, as a unit vector. Read off the camera rather than rebuilt from a yaw, so it carries roll.
     */
    right: Vector3;

    /**
     * Its own up, as a unit vector.
     */
    up: Vector3;

    /**
     * Vertical field of view in degrees. What the renderer settled on, which Hor+ adaptation and the window's aspect both move -- not what the game asked for.
     */
    fov: number;

    /**
     * Width over height.
     */
    aspectRatio: number;
  }

  /**
   * A ray out of the camera, ready to hand to `World.raycast`.
   */
  interface CameraRay {
    /**
     * Where it starts: the eye position.
     */
    origin: Vector3;

    /**
     * Where it points, as a unit vector.
     */
    direction: Vector3;

    /**
     * Where it ends, which is `origin` plus `direction` times the range asked for.
     */
    target: Vector3;
  }

  /**
   * The view this client is drawing through. Nothing here is replicated and nothing here is authority: it describes one machine's own camera at one moment.
   * 
   * This is the game's camera, not a camera of the mod's own -- it follows the player, a dialogue, a cutscene or a free flight, whichever the game currently has. `NoClip` is what takes it over, `setThirdPerson` puts it behind the player, and `setShot` stands it somewhere in the world.
   */
  const Camera: {
    /**
     * Where the camera is this frame, and what it can see from there.
     * @returns The pose, or null in the main menu and across a level load, when there is no active view at all.
     */
    getPose(): CameraPose | null;

    /**
     * A ray out of the camera through a point on screen. This is what turns "what is the player looking at" into the two points `World.raycast` takes.
     * @param options `x` and `y` are a point on screen in normalized device coordinates -- both run -1 at the left and bottom to +1 at the right and top, so the default (0, 0) is the centre of the screen, which is where the crosshair is. `range` is how far out to carry `target`, in metres, 100 by default and up to 4096.
     * @returns The ray, or null when there is no active view.
     */
    screenRay(options?: { x?: number; y?: number; range?: number }): CameraRay | null;

    /**
     * Puts this player's camera behind them, for as long as it is on.
     * 
     * The game has no third-person view to switch to, so this is a camera of the mod's own, placed every frame behind the body along the direction the player is looking. Nothing about their controls changes: they walk and turn as they did, and the mouse still turns the body. When a wall is behind them the camera comes in along the same line rather than going through it. Their head, which the game keeps out of its own first person, is drawn for as long as this camera is the one looking at them.
     * 
     * This is what a disguised player needs -- `player.setDisguise` hides the body the game's own camera sits in. It is this client's own view, so a gamemode turns it on from a client script, typically on an event the server sends the disguised player.
     * 
     * There is one view to draw through: `NoClip` cannot start while this holds it, and while `NoClip` flies this waits. The map, the inventory and the other pause-screen pages are filmed by the game's own camera, so this steps aside while one is open and comes back when it closes. It lasts until the session ends.
     * @param options Where the camera sits, in metres: `distance` back from the body along where the player looks (3.5 by default, 0.3 to 20), `height` of the point it orbits above the body's feet (1.6, up to 5 either way), and `shoulder` to the right of it, negative for the left (0, up to 3 either way). Null hands the view back to the game's own camera. Called again while on, it only moves the camera.
     */
    setThirdPerson(options?: { distance?: number; height?: number; shoulder?: number } | null): void;

    /**
     * Whether `setThirdPerson` is on.
     * @returns True from `setThirdPerson` until it is turned off, also while it waits for a body or for `NoClip`.
     */
    isThirdPerson(): boolean;

    /**
     * Films this player's view from a fixed point: what a cutscene, an intro or a kill cam is made of.
     * 
     * The camera holds there until `clearShot` -- or until another `setShot`, which glides on from wherever this one is. Nothing about the player's controls changes; a gamemode that wants them still says so through `Controls`. The player's head, which the game keeps out of its first person, is drawn for as long as the camera is looking at them from outside their eyes.
     * 
     * There is one view to draw through. A shot outranks `setThirdPerson`, which takes over again when the shot ends, and `NoClip` cannot start while it holds. The map, the inventory and the other pause-screen pages are filmed by the game's own camera, so the shot steps aside while one is open and comes back when it closes. A shot ends with the session.
     * 
     * The server can send the same shot with its own `Camera.setShot(player, options)`; whichever comes last wins.
     * @param options `position` is where the camera stands and `lookAt` the point it faces, both in world space. `duration` is how long it takes to get there from wherever the view is now, in milliseconds: 0, the default, is a cut, and the most is 60000. `fov` is the vertical field of view in degrees, 20 to 120; leave it out to keep the player's own.
     * @returns True, or false while `NoClip` is flying or when `position` and `lookAt` are the same point.
     */
    setShot(options: { position: Vector3; lookAt: Vector3; duration?: number; fov?: number }): boolean;

    /**
     * Gives the player their own view back. Does nothing without a shot.
     * @param duration How long the glide back into the player's eyes takes, in milliseconds: 0, the default, is a cut, and the most is 60000. The glide follows the player if they move meanwhile. With `setThirdPerson` on, it ends behind them instead and their head stays drawn.
     */
    clearShot(duration?: number): void;

    /**
     * Whether a shot holds the view.
     * @returns True from `setShot`, including its glide in, until `clearShot`; false already while gliding back.
     */
    isShotActive(): boolean;
  };

  /**
   * A position or offset in metres. Accepts plain objects and Vector3 instances.
   */
  interface GizmoPoint {
    /**
     * X coordinate.
     */
    x: number;

    /**
     * Y coordinate.
     */
    y: number;

    /**
     * Z coordinate.
     */
    z: number;
  }

  /**
   * Appearance and lifetime of a client debug drawing.
   */
  interface GizmoStyle {
    /**
     * Unsigned 0xAARRGGBB color. Defaults to 0xFF6EE678 (opaque green).
     */
    color?: number | undefined;

    /**
     * Line width in pixels, 0.5 to 8. Defaults to 1.5; polygon vertical edges use 60% of this width.
     */
    width?: number | undefined;

    /**
     * Defaults to true: scene geometry occludes the drawing. Set false to show through walls. Drawings never write scene depth.
     */
    depthTest?: boolean | undefined;

    /**
     * Unsigned 0xAARRGGBB fill color. Defaults to transparent (outline only). Supports boxes, spheres, circles, cones, cylinders, capsules, arrow heads and meshes. Throws for lines, polylines, grids, axes and polygon areas, which have no faces; fill a polygon with explicit mesh triangles.
     */
    fillColor?: number | undefined;

    /**
     * Integer lifetime from 0 to 3600000 milliseconds. Zero (default) persists until removed. Replacing the drawing restarts its lifetime.
     */
    durationMs?: number | undefined;
  }

  /**
   * Geometry from an Area.toJSON() or World Builder area export. Extra fields are ignored; this draws debug geometry and creates no trigger.
   */
  interface GizmoArea {
    /**
     * Which area geometry to draw.
     */
    type: 'box' | 'sphere' | 'shape';

    /**
     * World origin in metres; defaults to zero.
     */
    position?: GizmoPoint | undefined;

    /**
     * Local-to-world quaternion, normalized on input; defaults to identity. Plain {x,y,z,w} objects work.
     */
    rotation?: { x: number; y: number; z: number; w: number } | undefined;

    /**
     * Box minimum in local space; defaults to {-1,-1,-1}. Each component must be less than max.
     */
    min?: GizmoPoint | undefined;

    /**
     * Box maximum in local space; defaults to {1,1,1}.
     */
    max?: GizmoPoint | undefined;

    /**
     * Required for spheres: radius in metres, greater than zero and less than 16384.
     */
    radius?: number | undefined;

    /**
     * Required for shapes: 3 to 256 local points, forming a closed polygon.
     */
    points?: GizmoPoint[] | undefined;

    /**
     * Shape height above its lowest transformed point, 0 to less than 16384 metres. Zero (default) draws one ring at the points' own heights.
     */
    height?: number | undefined;
  }

  /**
   * Client debug line geometry.
   */
  interface GizmoLine {
    /**
     * Primitive kind.
     */
    type: 'line';

    /**
     * World start.
     */
    from: GizmoPoint;

    /**
     * World end.
     */
    to: GizmoPoint;
  }

  /**
   * Client debug arrow geometry.
   */
  interface GizmoArrow {
    /**
     * Primitive kind.
     */
    type: 'arrow';

    /**
     * World start.
     */
    from: GizmoPoint;

    /**
     * World tip.
     */
    to: GizmoPoint;

    /**
     * Head length in metres, greater than zero and no longer than the arrow. Defaults to the smaller of 0.5 and a quarter of the arrow length.
     */
    headLength?: number | undefined;
  }

  /**
   * Client debug capsule/cone/cylinder geometry.
   */
  interface GizmoRoundVolume {
    /**
     * Primitive kind.
     */
    type: 'capsule' | 'cone' | 'cylinder';

    /**
     * Capsule lower hemisphere center, or cone/cylinder base center.
     */
    from: GizmoPoint;

    /**
     * Capsule upper hemisphere center, cone tip or cylinder top center. Capsule endpoints may coincide for a sphere.
     */
    to: GizmoPoint;

    /**
     * Radius in metres, 0.001 to 16384.
     */
    radius: number;

    /**
     * Integer radial segments, 8 to 64; default 24.
     */
    segments?: number | undefined;
  }

  /**
   * Client debug circle geometry.
   */
  interface GizmoCircle {
    /**
     * Primitive kind.
     */
    type: 'circle';

    /**
     * World center.
     */
    position: GizmoPoint;

    /**
     * Non-zero plane normal; default {x:0,y:0,z:1}.
     */
    normal?: GizmoPoint | undefined;

    /**
     * Radius in metres, 0.001 to 16384.
     */
    radius: number;

    /**
     * Integer radial segments, 8 to 64; default 24.
     */
    segments?: number | undefined;
  }

  /**
   * Client debug grid geometry.
   */
  interface GizmoGrid {
    /**
     * Primitive kind.
     */
    type: 'grid';

    /**
     * World center.
     */
    position: GizmoPoint;

    /**
     * Non-zero plane normal; default {x:0,y:0,z:1}.
     */
    normal?: GizmoPoint | undefined;

    /**
     * Square side length in metres, 0.001 to 16384; default 10.
     */
    size?: number | undefined;

    /**
     * Integer subdivisions on each side, 1 to 64; default 10.
     */
    divisions?: number | undefined;
  }

  /**
   * Client debug axes geometry.
   */
  interface GizmoAxes {
    /**
     * Primitive kind.
     */
    type: 'axes';

    /**
     * World origin.
     */
    position: GizmoPoint;

    /**
     * Local-to-world quaternion, normalized on input; default identity.
     */
    rotation?: { x: number; y: number; z: number; w: number } | undefined;

    /**
     * Axis length in metres, 0.001 to 16384; default 1. X is red, Y green and Z blue; these fixed colors override style.color.
     */
    size?: number | undefined;
  }

  /**
   * Client debug polyline geometry.
   */
  interface GizmoPolyline {
    /**
     * Primitive kind.
     */
    type: 'polyline';

    /**
     * 2 to 256 world positions.
     */
    points: GizmoPoint[];

    /**
     * Join last to first; default false.
     */
    closed?: boolean | undefined;
  }

  /**
   * Client debug mesh geometry.
   */
  interface GizmoMesh {
    /**
     * Primitive kind.
     */
    type: 'mesh';

    /**
     * 3 to 1024 world positions.
     */
    points: GizmoPoint[];

    /**
     * 3 to 3072 integer vertex indices in triangle triples, each from 0 to points.length - 1. Both triangle sides render.
     */
    indices: number[];

    /**
     * Draw triangle edges with style.color and width; default true. Set fillColor to draw faces.
     */
    wireframe?: boolean | undefined;
  }

  /**
   * Resource-owned native debug geometry visible only on this client, without opening World Builder. Scene depth testing is on by default; depthTest: false opts out. Stop/error, disconnect and moving to another level clear drawings; reloading a save of the same level keeps them. No replication or input capture.
   */
  const Gizmos: {
    /**
     * Creates or replaces a named primitive owned by this resource. Finite input coordinates within +/-16384 metres. IDs are 1 to 128 UTF-8 bytes. Limit: 128 IDs per resource, 16384 geometry units across resources (one per line, three per triangle). Throws on invalid input or budget overflow and preserves the previous drawing. Copies geometry and reapplies style defaults; use setTransform to move it.
     */
    set(id: string, primitive: GizmoArea | GizmoLine | GizmoArrow | GizmoRoundVolume | GizmoCircle | GizmoGrid | GizmoAxes | GizmoPolyline | GizmoMesh, style?: GizmoStyle): void;

    /**
     * Moves this resource's named drawing without rebuilding it: every authored point p is drawn at position + rotation * p. Author the geometry around the origin to move it as a whole. The rotation is normalized and defaults to identity. set() resets the transform to identity.
     * @returns True if the drawing exists; false if this resource has no drawing with that ID.
     */
    setTransform(id: string, position: GizmoPoint, rotation?: { x: number; y: number; z: number; w: number }): boolean;

    /**
     * Removes this resource's named drawing. Throws for an invalid ID or unknown resource.
     * @returns True if removed; false if this resource has no drawing with that ID.
     */
    remove(id: string): boolean;

    /**
     * Removes all drawings owned by this resource. Other resources are unaffected. Throws if the caller's resource cannot be identified.
     */
    clear(): void;
  };

  /**
   * The box a ghost occupies, in world space.
   */
  interface PropGhostBounds {
    /**
     * The low corner, in world-space metres.
     */
    min: Vector3;

    /**
     * The high corner.
     */
    max: Vector3;

    /**
     * The middle of the box.
     */
    centre: Vector3;

    /**
     * How big it is on each axis, in metres.
     */
    size: Vector3;
  }

  /**
   * A preview this client is drawing: one or more of the game's meshes standing in the world with no body and no collision.
   */
  class PropGhost {
    /**
     * Creates a handle for a ghost that already exists; use PropGhost.create() to make one.
     * @param id Identifier of a ghost that already exists.
     */
    constructor(id: number);

    /**
     * Identifier of this ghost, unique for the lifetime of the session. Ids are not reused.
     */
    readonly id: number;

    /**
     * Whether the ghost is still standing. A handle to a destroyed one keeps reading; every call on it answers false. A level load destroys every ghost, because the nodes they are made of belong to the level that went.
     */
    readonly valid: boolean;

    /**
     * Formats this ghost handle for logging and debugging.
     * @returns The ghost's id, and whether it is still standing.
     */
    toString(): string;

    /**
     * Moves the whole ghost, parts and all. Cheap enough to call every frame: a part is moved with one engine call that re-sorts it into the world by itself.
     * @param position Where the ghost stands, in world-space metres.
     * @param rotation Which way it faces. The identity by default, which faces +Y.
     * @param scale Uniform scale, 1 by default and clamped to 0.01 - 100.
     * @returns True while the ghost is still standing.
     */
    setPose(position: Vector3, rotation?: Quaternion, scale?: number): boolean;

    /**
     * Recolours the ghost.
     * 
     * This is a real tint rather than a swapped material: the ghost's materials are private copies, and writing an opacity under 1 on one is what moves it into the renderer's transparent pass. A tint it already carries costs nothing, so this is safe to call every frame.
     * @param tint The colour to wash the ghost in, with `a` below 1 making it translucent. Omit it, or pass null, to put every mesh back to the materials it shipped with.
     * @returns True while the ghost is still standing.
     */
    setTint(tint?: Color): boolean;

    /**
     * Hides or shows the ghost without dropping it, which is cheaper than destroying and rebuilding one.
     * @param hidden Whether to take it out of the draw.
     * @returns True while the ghost is still standing.
     */
    setHidden(hidden: boolean): boolean;

    /**
     * The box the ghost occupies right now, in world space, as the engine reports it for the meshes themselves.
     * @returns Its bounds, or null once it is gone and for a ghost whose meshes carry none.
     */
    getBounds(): PropGhostBounds | null;

    /**
     * Takes the ghost out of the world.
     * @returns True when it was still there to take.
     */
    destroy(): boolean;

    /**
     * Puts a preview into the world: the game's own geometry, with no body, no collision and nothing replicated.
     * 
     * This is what a build preview, a placement outline or a menu turntable is made of. It is not a prop -- nobody else can see it, and it does not survive a level load.
     * @param options `model` is one mesh from the shared prop catalog; `models` is several, which is how a blueprint of more than one piece is previewed. `offsets` and `rotations` line up with `models` by index and place each piece inside the ghost's own frame, so the whole thing turns and scales as one. A path the catalog does not carry is refused, because it is a path the server could never be asked to spawn afterwards.
     * 
     * `position`, `rotation` and `scale` are where the ghost stands. `tint` washes it in a colour, with `a` below 1 making it translucent; omitted, every mesh keeps the materials it shipped with.
     * @returns The ghost, or null when this client has no world, at the ghost limit, and when not one of the meshes would load.
     */
    static create(options: { model?: string; models?: string[]; offsets?: Vector3[]; rotations?: Quaternion[]; position: Vector3; rotation?: Quaternion; scale?: number; tint?: Color }): PropGhost | null;

    /**
     * Takes every ghost this client is drawing out of the world, including other resources'.
     */
    static destroyAll(): void;
  }

  /**
   * Where a placement is aimed, and what it is aimed at.
   */
  interface PlacementPose {
    /**
     * Where the preview stands, in world-space metres.
     */
    position: Vector3;

    /**
     * Which way it faces, including whatever the surface and the rotate keys added.
     */
    rotation: Quaternion;

    /**
     * The surface normal under the aim, as a unit vector. Straight up when nothing was hit.
     */
    normal: Vector3;

    /**
     * The game's own name for what is underfoot -- `mat_dirt`, `mat_stone`. Empty when nothing was hit.
     */
    surface: string;

    /**
     * How far the aim point is from the camera, in metres.
     */
    distance: number;

    /**
     * Whether the aim actually met something. False when the ray left the level, and the preview is standing at the fallback distance in mid-air.
     */
    onSurface: boolean;
  }

  /**
   * Placement mode: a translucent preview under the crosshair, tinted for whether the spot is accepted, put down with a click.
   * 
   * The loop is native and runs with the game's own frame, so the preview tracks the camera exactly and no script runs per frame. While a session is up this client's input belongs to it, the way one of the game's own screens takes it -- which is what stops a confirm click also swinging the player's sword, and what stops the player walking away mid-placement.
   * 
   * One session at a time, and it belongs to the resource that started it: a resource that stops takes its session with it.
   * 
   * **Nothing here is authority.** The rules below colour a preview at one client. What actually gets built is the server's decision, made again from the pose the client confirmed -- two players aiming at the same patch of ground both see green.
   */
  const PropPlacer: {
    /**
     * Starts placement. The preview appears under the crosshair on the same frame.
     * @param options `model`, `models`, `offsets` and `rotations` describe the preview exactly as `PropGhost.create` takes them.
     * 
     * `validTint` and `invalidTint` are the two colours the preview flips between, translucent green and translucent red by default. `range` is how far the aim ray reaches in metres (40 by default, up to 512) and `fallbackDistance` is where the preview sits when the ray reaches nothing. `verticalOffset` lifts or sinks it, `yaw` is a fixed turn added to whatever the aim settles on, and the mouse wheel moves that while a session is up.
     * 
     * `alignToSurface` stands the preview on the slope it is aimed at instead of upright; `faceAwayFromPlayer` turns it to face away from where you stand, which is what a wall or a tower wants and is on by default. `keepOpen` leaves the session running after a confirm so several can be placed in one go, and is on by default.
     * 
     * `rules` is what the preview is tinted by, and every one of them is off unless named. `maxSlope` is degrees the ground may lean; `surfaces` is which surface names are allowed; `minDistance` and `maxDistance` bound how far from the player it may stand; `clearance` is metres that must be free of other entities, narrowed to `clearanceClasses` when given; `requireSurface` refuses a spot the ray never reached, and is on by default.
     * @returns True when the session started; false when one is already running, when this client has no world or camera, when another part of the mod holds the controls, and when the preview could not be built.
     */
    begin(options: { model?: string; models?: string[]; offsets?: Vector3[]; rotations?: Quaternion[]; scale?: number; validTint?: Color; invalidTint?: Color; range?: number; fallbackDistance?: number; verticalOffset?: number; yaw?: number; alignToSurface?: boolean; faceAwayFromPlayer?: boolean; keepOpen?: boolean; rules?: { maxSlope?: number; surfaces?: string[]; minDistance?: number; maxDistance?: number; clearance?: number; clearanceClasses?: string[]; requireSurface?: boolean } }): boolean;

    /**
     * Ends the session and gives the player their input back. `end` is raised. Doing nothing when no session is running is not an error.
     */
    end(): void;

    /**
     * Whether a placement session is running at this client right now.
     * @returns True while one is up.
     */
    isActive(): boolean;

    /**
     * Refuses every spot until cleared, which is where a rule the engine cannot answer belongs -- what the player can afford, what the server has already said no to. It latches, so it costs the placement loop nothing.
     * @param vetoed Whether to refuse the spot whatever the rules say.
     * @returns True while a session is running.
     */
    setVeto(vetoed: boolean): boolean;

    /**
     * Turns the preview, the same way the mouse wheel does while a session is up.
     * @param degrees How far to turn it, in degrees. Negative turns the other way.
     * @returns True while a session is running.
     */
    rotate(degrees: number): boolean;

    /**
     * Where the placement is aimed right now, without waiting for a `move`.
     * @returns The pose, or null when no session is running and before the first aim settles.
     */
    getPose(): PlacementPose | null;

    /**
     * Listens for one of the session's events. Handlers belong to the resource that registered them and go when it stops.
     * @param event `move` fires when the aim has travelled far enough to be worth reporting, and whenever the spot flips between accepted and refused -- it is throttled on purpose, because it is the one event that could otherwise fire every frame. `confirm` is a click the rules accepted, and is the one to send to the server. `rejected` is a click they refused, so the resource can say why; the session stays up. `cancel` is the player backing out. `end` is the session stopping, however it stopped, and is always the last one.
     * @param handler Called with where the placement is aimed and whether that spot is accepted. The pose is null for `cancel` and `end`.
     * @returns A function that removes this subscription.
     */
    on(event: "move" | "confirm" | "rejected" | "cancel" | "end", handler: PlacementHandler<PlacementPose>): Unsubscribe;
  };

  /**
   * The world builder: a free camera, the game's mesh catalog as an asset library, a placement brush, a gizmo that moves anything, and maps saved to and loaded from files.
   * 
   * F7 or a client resource opens it. Multiplayer requires the server to grant this player access with `player.setWorldBuilderEnabled(true)`; single-player and offline editing are always allowed. While it is open it holds the one free camera, so a `NoClip` flight in progress ends and `NoClip.enable` refuses. The player can still close it from its own window.
   */
  const MapEditor: {
    /**
     * Opens the editor.
     * @returns `opened` when it opened. `alreadyOpen` when it was open already. `disabled` when this multiplayer connection has not been granted access.
     */
    open(): "opened" | "alreadyOpen" | "disabled";

    /**
     * Closes the editor and hands the camera back. Doing nothing when it is closed is not an error.
     * @returns Nothing.
     */
    close(): void;

    /**
     * Whether the editor is open at this client.
     * @returns True while it is open.
     */
    isOpen(): boolean;

    /**
     * Whether this client may open World Builder.
     * @returns True offline or when the server has granted this player access.
     */
    isEnabled(): boolean;
  };

  /**
   * A screen composed inside one of the game's own Scaleform movies.
   */
  class NativeScreen {
    /**
     * Rebuilds a handle for a screen that already exists; use `NativeUI.createScreen()` to open one.
     * @param id Identifier of an existing screen.
     */
    constructor(id: number);

    /**
     * Identifier of this screen, unique for the lifetime of the session.
     */
    readonly id: number;

    /**
     * Whether the screen is still open.
     */
    readonly valid: boolean;

    /**
     * Whether the movie has composed it yet. A screen is asked for before its movie is ready, so this is false for the first few frames and `root` is null until it turns true.
     */
    readonly ready: boolean;

    /**
     * The screen's own empty root clip, and the parent everything else is created under. Null until `ready`.
     */
    readonly root: NativeClip | null;

    /**
     * Formats this handle for logging.
     */
    toString(): string;

    /**
     * Closes the screen, removing its subtree and every handler on it.
     * @returns True when it was still open.
     */
    close(): boolean;

    /**
     * Shows or hides this screen's own subtree, leaving anything else in the same movie alone.
     * @param visible Defaults to true.
     * @returns True when the movie accepted it.
     */
    setVisible(visible?: boolean): boolean;

    /**
     * Takes keyboard, controller and mouse input for this screen. One screen holds it at a time, and the game takes it back whenever a higher-priority context wants it.
     * @returns True when the screen now holds focus.
     */
    focus(): boolean;

    /**
     * Gives up input focus, if this screen holds it.
     */
    blur(): void;

    /**
     * Whether this screen currently holds input focus.
     * @returns True while it does.
     */
    hasFocus(): boolean;

    /**
     * The movie's authored stage size, which is what positions inside the screen are expressed in. It is independent of the player's resolution.
     * @returns The stage size, or null before the movie is composed.
     */
    stage(): { width: number; height: number } | null;

    /**
     * Converts a point on the player's screen into this movie's stage units, through the element's own viewport mapping.
     * @param x Horizontal viewport fraction, 0 to 1.
     * @param y Vertical viewport fraction, 0 to 1.
     * @returns The stage point, or null before the movie is composed.
     */
    screenToStage(x: number, y: number): { x: number; y: number } | null;

    /**
     * Listens for one of this screen's notifications. `ready` fires when the movie has composed the screen, which is not the same tick as the call that opened it; `unload` when the engine takes the movie away; `input` carries a focus action and its activation mode; `event` carries a declared movie event and its arguments.
     * @param event Which notification to listen for.
     * @param handler Called on the next tick after it is raised.
     */
    on(event: 'ready' | 'unload' | 'input' | 'event', handler: NativeScreenHandler): void;

    /**
     * Removes every handler this resource put on one notification.
     * @param event The notification to stop listening for.
     */
    off(event: string): void;
  }

  /**
   * One display object inside a native screen: a clip, an attached sprite or a text field.
   */
  class NativeClip {
    /**
     * Rebuilds a handle for a display object that already exists. Clips come from `screen.root` and the create calls on it.
     * @param screen Screen this handle belongs to.
     * @param key Handle key inside that screen.
     */
    constructor(screen: number, key: string);

    /**
     * Identifier of the screen this handle belongs to.
     */
    readonly screen: number;

    /**
     * The screen-local key this handle resolves through.
     */
    readonly key: string;

    /**
     * Whether the screen still holds this object. A handle to a removed object keeps reading and every call on it fails cleanly.
     */
    readonly valid: boolean;

    /**
     * Formats this handle for logging.
     */
    toString(): string;

    /**
     * Attaches one of the game's own exported sprites as a child of this clip. Which names exist depends on the library the screen was opened with.
     * @param exportName Linkage name of a sprite the screen's library exports.
     * @returns The new child, or null when the library has no such export.
     */
    attach(exportName: string): NativeClip | null;

    /**
     * Creates an empty child clip, for grouping and for drawing into.
     * @returns The new child, or null when the screen is not composed.
     */
    createClip(): NativeClip | null;

    /**
     * Creates a native text field as a child of this clip.
     * @param options Where the field goes in this clip's own coordinates, how big it is, and what it starts with.
     * @returns The new field, or null when the screen is not composed.
     */
    createText(options: { x: number; y: number; width: number; height: number; text?: string; html?: string }): NativeClip | null;

    /**
     * Moves, scales, rotates, fades or hides this display object.
     * @param options Only the fields present are applied. Position is in stage units, rotation in degrees, scale as a multiplier where 1 is natural size, alpha from 0 to 1.
     * @returns True when the movie accepted the update.
     */
    set(options: { x?: number; y?: number; rotation?: number; scaleX?: number; scaleY?: number; alpha?: number; visible?: boolean }): boolean;

    /**
     * Reads one scalar property off this display object. Property access can run an ActionScript getter.
     * @param member ActionScript property name, such as `_width` or `_currentframe`.
     * @returns The value, or null when it is absent or not a scalar.
     */
    get(member: string): any;

    /**
     * Assigns one scalar property on this display object.
     * @param member ActionScript property name.
     * @param value What to assign.
     * @returns True when the movie accepted it.
     */
    setMember(member: string, value: string | number | boolean | null): boolean;

    /**
     * Sets how this field draws, for the text it holds and the text it is given next. A field created through `createText` already has the default applied; this restyles it.
     * @param options `font` is one of the game's own mapped names -- DefaultFont, DefaultFontBold, DefaultFontItalic, LightFont, LightFontBold, LightFontItalic, DisplayFont, Manuscript -- and not a face name. `color` is 0xRRGGBB.
     * @returns True when the movie accepted the format.
     */
    setTextStyle(options: { font?: string; size?: number; color?: number; bold?: boolean; italic?: boolean; align?: 'left' | 'center' | 'right' | 'justify' }): boolean;

    /**
     * Writes this field's text. On a clip rather than a field it assigns the `text` property, which is not the same as writing a field inside it.
     * @param text The text to show.
     * @param html Parse it as the engine's HTML subset instead of plain text.
     * @returns True when the movie accepted the write.
     */
    setText(text: string, html?: boolean): boolean;

    /**
     * Reads this field's text back.
     * @param html Read the generated markup instead of the plain text.
     * @returns The text, or null when there is none to read.
     */
    getText(html?: boolean): string | null;

    /**
     * Moves this clip's own timeline. A timeline change can replace named children, so reacquire anything below it afterwards.
     * @param frameOrLabel A one-based frame number, or a frame label.
     * @param play Keep playing from there instead of stopping.
     * @returns True when the frame or label resolved.
     */
    goto(frameOrLabel: number | string, play?: boolean): boolean;

    /**
     * Calls a method on this display object. Only scalars cross, and the result is a scalar.
     * @param method Method name on this object.
     * @param args Scalar arguments. They may also be passed loose, one per parameter.
     * @returns What ActionScript returned, or null when the call failed.
     */
    invoke(method: string, args?: (string | number | boolean | null)[]): any;

    /**
     * Installs one ActionScript mouse handler on this clip and enables it as a mouse target. The screen also has to hold input focus for the mouse to reach it.
     * @param event Which mouse handler to install.
     * @param handler Called on the next tick after the movie raises it.
     * @returns True when the handler was installed.
     */
    on(event: 'press' | 'release' | 'releaseOutside' | 'rollOver' | 'rollOut' | 'dragOver' | 'dragOut', handler: NativeClipHandler<NativeClip>): boolean;

    /**
     * Removes one mouse handler, deleting the ActionScript member rather than assigning null to it.
     * @param event The handler to take off.
     */
    off(event: string): void;

    /**
     * Binds another clip as this one's mask. One mask belongs to one target: binding it elsewhere detaches it from here.
     * @param mask A clip from this same screen, or null to clear.
     * @returns True when the movie took the binding.
     */
    setMask(mask: NativeClip | null): boolean;

    /**
     * Makes another clip the region this one is clicked through, for a target whose artwork is the wrong shape to click.
     * @param area A clip from this same screen, or null to clear.
     * @returns True when the movie took the binding.
     */
    setHitArea(area: NativeClip | null): boolean;

    /**
     * Loads an external image or movie into this clip, replacing what it holds. A missing asset installs the engine's placeholder rather than failing, so a true result is not an existence check.
     * @param url `img://` or `imgps://` for the engine's image loader, or a movie path inside the mounted archive.
     * @returns True when the request was accepted.
     */
    load(url: string): boolean;

    /**
     * Takes this display object out of the screen, with its handlers.
     * @returns True when it was still there to remove.
     */
    remove(): boolean;
  }

  /**
   * A handle on one of the game's own UI elements, driven where it stands.
   */
  class NativeElement {
    /**
     * Rebuilds a handle for an element; use `NativeUI.element()` to get one.
     * @param name Element name, as its XML declares it.
     * @param instanceId Instance id.
     */
    constructor(name: string, instanceId: number);

    /**
     * The element's name, as its XML declares it.
     */
    readonly name: string;

    /**
     * Which instance this handle drives. Instance 0 is the one the game itself uses; any other number is a private copy of the same element.
     */
    readonly instanceId: number;

    /**
     * Whether the element is still registered.
     */
    readonly valid: boolean;

    /**
     * Whether the element is visible right now.
     */
    readonly visible: boolean;

    /**
     * Formats this handle for logging.
     */
    toString(): string;

    /**
     * Calls one of the element's declared functions. The element has to be loaded: show it first, or call this on one the game already has up.
     * @param fn A function the element's XML declares, by its display name or its ActionScript name.
     * @param args Scalar arguments. They may also be passed loose, one per parameter.
     * @returns What ActionScript returned, or null when the call failed.
     */
    call(fn: string, args?: (string | number | boolean | null)[]): any;

    /**
     * Assigns one of the element's declared variables.
     * @param name A variable the element's XML declares.
     * @param value What to assign.
     * @returns True when the movie accepted it.
     */
    setVariable(name: string, value: string | number | boolean | null): boolean;

    /**
     * Reads one of the element's declared variables.
     * @param name A variable the element's XML declares.
     * @returns The value, or null when it is absent.
     */
    getVariable(name: string): any;

    /**
     * Replaces one of the element's declared arrays. This is how the game's own list screens are filled.
     * @param name An array the element's XML declares.
     * @param values The new contents.
     * @returns True when the movie accepted it.
     */
    setArray(name: string, values: (string | number | boolean | null)[]): boolean;

    /**
     * Reads one of the element's declared arrays.
     * @param name An array the element's XML declares.
     * @returns Its contents, or null when it is absent.
     */
    getArray(name: string): any[] | null;

    /**
     * Moves one of the element's declared movie clips. Two of the game's elements declare any: the HUD and the lockpicking screen.
     * @param name A movie clip the element's XML declares.
     * @param options Only the fields present are applied.
     * @returns True when the movie accepted it.
     */
    setClip(name: string, options: { x?: number; y?: number; rotation?: number; scaleX?: number; scaleY?: number; alpha?: number; visible?: boolean }): boolean;

    /**
     * Reads one of the element's declared movie clips back. Scale and alpha come back as multipliers, the way `setClip` takes them.
     * @param name A movie clip the element's XML declares.
     * @returns Its display fields, or null when the clip is absent.
     */
    getClip(name: string): { x: number; y: number; rotation: number; scaleX: number; scaleY: number; alpha: number; visible: boolean } | null;

    /**
     * Moves a declared movie clip's timeline, which is how a clip with several authored looks is switched between them.
     * @param name A movie clip the element's XML declares.
     * @param frameOrLabel A one-based frame number, or a frame label.
     * @param play Keep playing from there instead of stopping.
     * @returns True when the frame or label resolved.
     */
    gotoClip(name: string, frameOrLabel: number | string, play?: boolean): boolean;

    /**
     * Shows the element, loading its movie if it was not loaded.
     * @returns True when the element took it.
     */
    show(): boolean;

    /**
     * Hides the element at once.
     * @returns True when the element took it.
     */
    hide(): boolean;

    /**
     * Asks the element to play its own hide transition instead of vanishing.
     * @returns True when the element took it.
     */
    requestHide(): boolean;

    /**
     * Rebuilds the element's movie from cached assets. Everything composed into it is gone afterwards.
     * @returns True when the element took it.
     */
    reload(): boolean;

    /**
     * Drops the element's movie. The next `show` loads it again.
     * @returns True when the element took it.
     */
    unload(): boolean;

    /**
     * Listens for one of the element's declared events. This is how a click on a game screen reaches script.
     * @param event An event the element's XML declares, by its display name.
     * @param handler Called on the next tick after the movie raises it.
     */
    on(event: string, handler: NativeElementHandler): void;

    /**
     * Removes every handler this resource put on one event.
     * @param event The event to stop listening for.
     */
    off(event: string): void;
  }

  /**
   * The game's own Scaleform UI, opened from script. This is the second way to draw a UI at this client, beside `Web`: a web view is a browser and lays out anything, a native screen is composed out of the game's own movies, sits inside the game's own layer stack and is navigable with a controller.
   */
  const NativeUI: {
    /**
     * Prevents a vanilla menu from opening or being selected through native navigation. An already-open disabled menu closes on the next client tick. Requests are idempotent and resource-owned: a menu stays disabled while any resource disables it. Restrictions clear when the resource stops or the session ends. Key bindings are unchanged. Tab labels remain in the native screen. The custom character creator can still use the player stage.
     * @param menu The vanilla menu, including its detail pages.
     * @param enabled False prevents opening the menu; true releases this resource's restriction. All menus are enabled by default.
     */
    setMenuEnabled(menu: "inventory" | "player" | "map" | "codex" | "journal" | "crafting", enabled: boolean): void;

    /**
     * Whether every resource permits this vanilla menu. Native gameplay rules may still prevent it from opening. Throws for unknown menu names.
     * @param menu The vanilla menu to query.
     * @returns True when no resource disables the menu.
     */
    isMenuEnabled(menu: "inventory" | "player" | "map" | "codex" | "journal" | "crafting"): boolean;

    /**
     * Opens a screen. It is composed a few frames later, when its movie reports ready -- listen for `ready` rather than drawing into it straight away.
     * @param options `library` names which of the game's movies the screen may attach exported sprites from, empty for the default. `movie` is a path inside this resource to a movie it ships, which opens a screen in that instead. `assets` are further files that movie loads, and `layer` is where it sits in the game's own stack.
     * @returns The screen handle.
     */
    createScreen(options?: { library?: string; movie?: string; assets?: string[]; layer?: number }): NativeScreen;

    /**
     * Lists the game movies a screen can be opened against and the linkage names each one exports, read from the bundle rather than from a list kept in step by hand. `name` is what `createScreen({library})` takes, and the empty one is the default host.
     * @returns One entry per bundled library.
     */
    libraries(): { name: string; movie: string; exports: string[] }[];

    /**
     * Takes a handle on one of the game's own UI elements. Picking an instance id other than 0 gives a private copy of that element, which is how a mod puts its own content on a game screen without fighting the game's copy.
     * @param name Element name, as the game's own XML declares it: `Menu`, `hud`, `ApseModalDialog` and the rest.
     * @param instanceId Which instance to drive; 0, the game's own, by default.
     * @returns The element handle.
     */
    element(name: string, instanceId?: number): NativeElement;
  };

  /**
   * Whether this client's gameplay input reaches the game, and whether the mod draws a pointer.
   * 
   * A focused web view takes both already, so a panel opened with `Web.focusView` needs nothing here. This is for what focus cannot say: a pointer without freezing the player, a freeze without a pointer, or no pointer at all for a page that draws its own.
   * 
   * Every hold is counted and belongs to the resource that took it, so two resources cannot release each other's, and one that stops gives back exactly what it took.
   */
  const Controls: {
    /**
     * Takes this client's gameplay input away from the game without drawing a pointer. The camera stops turning and the character stops moving.
     */
    disable(): void;

    /**
     * Gives back one hold taken by `disable`. Input returns once nothing else holds it -- a focused panel, the chat line, an overlay.
     */
    enable(): void;

    /**
     * Whether gameplay input is reaching the game right now.
     * @returns False while anything holds it, this resource or not.
     */
    isEnabled(): boolean;

    /**
     * Draws the mod's pointer without taking input. Its shape follows the topmost focused page, so a page's own `cursor` styling is what the player sees.
     */
    showCursor(): void;

    /**
     * Gives back one hold taken by `showCursor`.
     */
    hideCursor(): void;

    /**
     * Whether the mod's pointer is drawn right now.
     * @returns True while anything holds it, this resource or not.
     */
    isCursorVisible(): boolean;

    /**
     * Keeps one game action from doing anything, whatever key the player has it on, while the key itself still reaches `Key.bind`. For a resource whose own key shares a game key, or that takes an action's job over.
     * @param action An action of the game's action maps: the game's own, or one of the mod's keys, such as `kcdc_npc_interact` (Talk to NPC).
     */
    disableAction(action: string): void;

    /**
     * Gives back one hold taken by `disableAction`. The action works again once nothing else holds it.
     * @param action The action given to `disableAction`.
     */
    enableAction(action: string): void;
  };

  /**
   * One gesture of the shipped emote catalog, which is every gesture a wheel slot can choose.
   */
  interface EmoteCatalogEntry {
    /**
     * What a slot is set to, and what goes on the wire when the gesture plays.
     */
    id: number;

    /**
     * The label the wheel draws for it.
     */
    name: string;

    /**
     * How long the gesture plays, in seconds.
     */
    duration: number;

    /**
     * Whether it takes the whole body. A full-body gesture holds the player still while it plays; an upper-body one plays over a walk.
     */
    fullBody: boolean;
  }

  /**
   * Which gesture each slot of this client's emote wheel chooses.
   * 
   * The wheel has a fixed number of slots, numbered clockwise from the one straight up, and each chooses one row of the shipped catalog. They start out as the stock wheel -- wave, big wave, nod, respectful nod, bow, cheer -- and stay that way until a script changes them. A change is drawn on the next frame, including on a wheel held open.
   * 
   * Client-only and local: this arranges the wheel on this machine and nothing else. Every client can already play every gesture in the catalog, so a slot changes what is within reach, not what is allowed. The layout lasts for the session, and the next one starts from the default.
   */
  const EmoteWheel: {
    /**
     * How many slots the wheel has.
     * @returns The slot count, which the catalog growing does not change.
     */
    getSlotCount(): number;

    /**
     * The catalog id each slot chooses, in slot order.
     * @returns One id per slot.
     */
    getSlots(): number[];

    /**
     * Points one slot at a catalog gesture. The same gesture may sit in more than one slot.
     * @param slot Zero-based, clockwise from straight up.
     * @param emoteId An `id` from `getCatalog`.
     * @returns False, and nothing changes, when the slot is out of range or the id is not in the catalog.
     */
    setSlot(slot: number, emoteId: number): boolean;

    /**
     * Lays out the whole wheel at once.
     * @param emoteIds One catalog id per slot, in slot order; exactly `getSlotCount()` of them.
     * @returns False, and nothing changes, when the list is the wrong length or any id is not in the catalog.
     */
    setSlots(emoteIds: number[]): boolean;

    /**
     * Puts every slot back to the stock wheel: wave, big wave, nod, respectful nod, bow, cheer.
     */
    reset(): void;

    /**
     * Every gesture a slot can choose.
     * @returns The catalog, in its own order.
     */
    getCatalog(): EmoteCatalogEntry[];
  };

  /**
   * A picture for the skill-book and map layouts, which place pictures by page rather than inline.
   */
  interface BookPageImage {
    /**
     * Zero-based page the picture belongs to.
     */
    page: number;

    /**
     * Its name under the game's own `Libs/UI/Textures/Books/` Skills or Maps folder, without the `_ui.dds` suffix, e.g. `swords1`.
     */
    image: string;
  }

  /**
   * What `Book.open` shows and how it looks.
   */
  interface BookOptions {
    /**
     * One string per page, written in the book movie's own markup: `<title>`, `<subtitle>`, `<heading>`, `<paragraph>`, `<br/>`, `<accent>`, `<inc>X</inc>` for a decorated initial, `<poem>`, `<lpoem>`, `<note>`, `<p align>`, `<font size>`, `<i>` and `<img src='...' width='W' height='H' align='center'/>`. A page break follows every page but the last; `<newpage/>` inside a page breaks it further. The player's own reading skill never scrambles it; `legibility` decides that.
     */
    pages: string[];

    /**
     * The object in the player's hands: a red-covered book (the default), a plain-covered one, or a folded letter.
     */
    style?: "book" | "plainBook" | "letter" | undefined;

    /**
     * The layout the movie uses, by `document_class` id: 1 book (the default), 2 recipe, 3 skill book, 4 map, 5 letter (the default for `letter`), 6 plan.
     */
    type?: number | undefined;

    /**
     * How ornate the pages are, 1 plain to 7 embellished. Defaults to 1.
     */
    visual?: number | undefined;

    /**
     * How much of the text can be read, 0 to 1; values outside are clamped. Defaults to 1, fully legible. Below 1 the movie swaps letters for look-alike glyphs, the same effect a vanilla book shows a player with low reading skill. It works in tenths: each step brings roughly another tenth of the character classes back, so 0.31 and 0.39 look the same. The substitution is deterministic -- the same text at the same level always reads the same way -- and markup is never touched.
     */
    legibility?: number | undefined;

    /**
     * A picture `Book.image` returned, replacing the carrier's own diffuse texture: the paper and the cover together, which the model maps from one atlas. Paint it over the vanilla atlas to keep the UV layout -- `Objects/manmade/task_specific_props/read_and_write/books/book_alchemy_diff.dds` for the two book styles, `.../scrolls/scroll_diff.dds` for `letter`. The material's diffuse colour multiplies it: `book` darkens it to about 78% and `letter` to about 72%, while `plainBook` shows it as painted. It applies to this book's own copy of the material only, and goes with the book.
     */
    texture?: string | undefined;

    /**
     * Pictures for the skill-book and map layouts. Inline pictures go in the page text as `<img>` instead.
     */
    images?: BookPageImage[] | undefined;
  }

  /**
   * Books a resource composes and opens in this player's hands.
   * 
   * A book is shown through the game's own reading: the same book in hand, camera, page-turn animation, prompts and pagination as any book the player reads from their inventory. The player turns pages and leaves it with the game's own keys, and `bookClosed` says when they did.
   * 
   * Client-only and local: the book exists on this machine alone, and reading it grants nothing -- no XP, no read marker, no quest progress. A server that wants a player to read something sends the pages in its own event.
   * 
   * Pictures a resource ships go through `Book.image`, which returns what an `<img src>` and the `texture` option take.
   */
  const Book: {
    /**
     * Opens a book in the player's hands.
     * @param options The pages and how they look.
     * @returns The book's id, which `bookOpened` and `bookClosed` carry; null when the game will not open one now -- no body yet, a book already open, or a place the player cannot read in (on horseback, in combat). `getLastError` says which. Malformed options throw.
     */
    open(options: BookOptions): number | null;

    /**
     * Closes the open book the way the player's own exit does.
     * @returns False when no book is open.
     */
    close(): boolean;

    /**
     * Changes how legible the open book is, while it is in the player's hands.
     * @param value 0 fully scrambled to 1 fully legible; clamped.
     * @returns False when no book is in the hands yet (wait for `bookOpened`). The movie applies legibility while laying the text out, so the book is laid out again, which can return it to its first page. Throws for a non-number.
     */
    setLegibility(value: number): boolean;

    /**
     * The id of the book in the player's hands.
     * @returns Null when there is none.
     */
    getOpenBook(): number | null;

    /**
     * Why the last `open` returned null.
     * @returns Empty after one that succeeded.
     */
    getLastError(): string;

    /**
     * Makes a picture the resource ships reachable from page text.
     * @param path A `.dds` file the calling resource ships, relative to the resource.
     * @returns The value for an `<img src='...'>` attribute. Give the tag a `width` and `height` too: the movie sizes the picture to them and has no size of its own for it. Throws when the file is missing, not a `.dds`, or larger than 16 MiB.
     */
    image(path: string): string;
  };

  /**
   * A streamed player or human NPC. Its network identity is resolved again when follow starts; position and name are snapshots.
   */
  interface FollowTarget {
    /**
     * The target's network id.
     */
    id: number;

    /**
     * Which replicated entity the id identifies.
     */
    kind: "player" | "npc";

    /**
     * Player nickname or NPC name.
     */
    name: string;

    /**
     * Current native body position, in world-space metres.
     */
    position: Vector3;
  }

  /**
   * Follow another player or a streamed human NPC using the local player's native walking or riding action. Native combat, interaction and mount restrictions still apply. Only one scripted follow can be queued or active. Movement input, target loss, mount changes and resource shutdown stop it. This controls only the local player; it does not command an NPC to follow.
   */
  const Follow: {
    /**
     * Lists streamed players and human NPCs within 20 metres. Use canStart to check whether one can currently be followed.
     */
    targets(): FollowTarget[];

    /**
     * Checks the target and native follow restrictions, including the game's start distance.
     * @param target A remote player handle or a descriptor from targets().
     */
    canStart(target: Player | FollowTarget): boolean;

    /**
     * Queues native follow for the calling resource. True means queued; followStarted confirms entry. False means the request was refused.
     * @param target Who the local player should follow.
     */
    start(target: Player | FollowTarget): boolean;

    /**
     * The active scripted follow target, or null while idle or pending.
     */
    getTarget(): FollowTarget | null;

    /**
     * Whether a scripted follow is queued but has not entered yet.
     */
    isPending(): boolean;

    /**
     * Cancels the calling resource's queued or active follow. Other resources' follow actions are left alone.
     */
    stop(): void;
  };

  /**
   * What `CharacterCreator.open` shows.
   */
  interface CharacterCreatorOptions {
    /**
     * The look the stage starts on. Left out, it starts on the first gender offered with every part left to the figure -- the player's own look on the man, the game's default on the woman.
     */
    appearance?: Appearance | undefined;

    /**
     * Which bodies the player may choose between; `both` (the default) lets them switch.
     */
    genders?: "male" | "female" | "both" | undefined;

    /**
     * The panel's heading, at most 64 bytes. Defaults to `Character`.
     */
    title?: string | undefined;
  }

  /**
   * The game's own character stage, for the local player to choose how they look.
   * 
   * Opens the pause screen on its character page, hides the page's own panels and stands a panel of tabs -- body, face, hair, beard -- beside a figure the player dresses and turns. The keys are listed on the panel, named as the game names them.
   * 
   * Client-only and local: it changes nothing anywhere. `characterCreatorClosed` reports what the player accepted, and the resource decides what that means.
   */
  const CharacterCreator: {
    /**
     * Opens a creator, ending one already open as `replaced`. The stage goes up on the next ticks; one that cannot ends as `unavailable`.
     * @param options What to show.
     * @returns True. Malformed options throw.
     */
    open(options?: CharacterCreatorOptions): boolean;

    /**
     * Ends the open creator as `closed`.
     * @returns False when none was open.
     */
    close(): boolean;

    /**
     * Whether a creator is open or going up.
     * @returns True from `open` until its `characterCreatorClosed`.
     */
    isOpen(): boolean;
  };

  /**
   * "Press [key] to ..." hints, drawn by the game's own HUD beside its own, with a handler run when the player presses the key.
   * 
   * A hint names one of the game's keys by its control -- `primary_action`, `jump` and the rest of `keys()` -- and never a physical key: the glyph is whatever the player has bound to that control, keyboard or pad, and follows a rebind. A press hint fires on the press; a hold hint draws the game's ring and fires once it fills.
   * 
   * Hints are drawn while the player is in control of their character, on foot or in the saddle, and the game takes them down in dialogue, minigames and menus. Two hints on one key stack: the newest is drawn and gets the press, and removing it brings back the one under it. A hint normally leaves the game's own action on that key enabled. `chat_focus_follow_init` replaces the native focus/follow toggle while its hint is shown, so a scripted Follow action receives one press.
   * 
   * Client-only and local: a hint is shown to the player at this machine. Hints go when the resource that made them stops.
   */
  const ActionHint: {
    /**
     * Puts a hint up. It is drawn from the next frame the player is in control.
     * @param options `key` is one of `keys()`. `text` is what the HUD writes beside the key, as written, at most 256 bytes. `mode` is "press" by default. `holdDuration` is how long a hold hint's ring takes to fill, 0.2 to 10 seconds, 1 by default. `enabled` false draws the hint greyed out and keeps the handler from running.
     * @param handler Run with the hint's id each time it fires.
     * @returns The hint's id. Throws when an option is out of range or the key is not one of `keys()`.
     */
    create(options: { key: string; text: string; mode?: "press" | "hold"; holdDuration?: number; enabled?: boolean }, handler: ActionHintHandler): number;

    /**
     * Rewrites a hint's text in place.
     * @param hint An id from `create`.
     * @param text The new text, as written, at most 256 bytes.
     * @returns False when the hint is gone or the text is too long.
     */
    setText(hint: number, text: string): boolean;

    /**
     * Greys a hint out, or brings it back.
     * @param hint An id from `create`.
     * @param enabled False greys the hint out and its handler stops running.
     * @returns False when the hint is gone.
     */
    setEnabled(hint: number, enabled: boolean): boolean;

    /**
     * Takes a hint down for good.
     * @param hint An id from `create`.
     * @returns False when it was already gone.
     */
    remove(hint: number): boolean;

    /**
     * The controls a hint can be put on.
     * @returns Control names, in a fixed order.
     */
    keys(): string[];

    /**
     * Whether the HUD is drawing a hint on that control right now, the game's own hints and interaction prompts included, whoever put it up. Lets a script keep its own UI off a key a hint is waiting on.
     * @param key A control, such as `use` or `jump`; any of the game's, not only `keys()`.
     * @returns True while a drawn hint is on the control, as of the game's last hint refresh, at most a frame old.
     */
    isKeyInUse(key: string): boolean;

    /**
     * Every control a drawn hint is on right now, interaction prompts included, whoever put it up.
     * @returns Control names, each once; empty while no hints are drawn.
     */
    shownKeys(): string[];

    /**
     * Every hint the HUD draws right now -- the game's own, interaction prompts such as talking to someone or opening a door, and every script's -- in the HUD's order, with what it says. What a script needs to draw them in its own UI, with the game's hints hidden through `Hud.setElementVisible("Hints", false)`; the keys keep doing what they do.
     * @returns One entry per drawn hint, as of the game's last hint refresh, at most a frame old. `key` is what `shownKeys` names: the control a script's hint is on, or the action a game hint triggers (`talk`, `use`), whose key `Libs/Config/keybindSuperactions.xml` binds. `map` is the action map the hint belongs to: the world's prompts are `player`'s, the game's menu screens have their own (`apse`, `apse_inventory`, `help_dialog`). `text` is what the game writes beside the key, in the player's language. `enabled` is false for a hint drawn greyed out. `progress` is how far a hold hint's ring has filled, 0 to 1, and 0 for a hint nobody is holding. `target` is who the prompt is about as the game names them -- a person or an animal, an NPC of the server's under the name it was given -- and empty for anything else.
     */
    shown(): { key: string; map: string; text: string; mode: "press" | "hold"; enabled: boolean; progress: number; target: string }[];
  };

  /**
   * A local inventory action intercepted before the game applies it. A snapshot, not permission to spend an item on the server.
   */
  interface InventoryUse {
    /**
     * Item class GUID, using the logical GUID for custom items.
     */
    item: string;

    /**
     * Native instance UID as a lossless decimal string. Local to this client; splits, merges and reprojection may replace it.
     */
    uid: string;

    /**
     * A server inventory row represented by the selected native item. Send this to a server resource to request consumption.
     */
    id: string;

    /**
     * The inventory revision at the time of the action. The server must validate it and current ownership.
     */
    revision: number;

    /**
     * Always 1. The override requests one use, even when a native row represents a stack.
     */
    amount: number;

    /**
     * Which native path was intercepted. Eat/Drink and Learn use secondary; primary also includes equipment actions; activate is the native equip-toggle path.
     */
    action: "primary" | "secondary" | "activate";
  }

  /**
   * Client-side overrides for actions in the local player's native inventory. Register by catalog item name, class GUID, custom item id, or { uid } for one native instance. A matching handler replaces the action before native consumption or effects. Scripts run on the next feature update, outside the native detour; return values are ignored and errors do not restore the native action. Only owned, synchronized inventory items dispatch. Pending uses are discarded if their handler, native item, server row or inventory revision disappears or changes. Instance handlers take priority over class handlers; otherwise the newest matching registration wins. Removing it reveals the earlier handler. Resource stop and session end remove handlers. The server remains responsible for validating requests, consuming inventory and applying effects. This API does not intercept quick-slot actions outside the inventory.
   */
  const Inventory: {
    /**
     * Registers an inventory action override for this resource. Adds a Use action even for an item without a normal primary action. Duplicate presses on the same UID within one frame are combined.
     * @param item Catalog name such as apple, a class GUID, a custom item id, or a native UID from InventoryUse.uid.
     * @param handler Runs instead of the native action. No item is automatically consumed.
     * @returns A registration id for offUse. Throws for an unknown class, invalid UID, missing resource or more than 256 active registrations.
     */
    onUse(item: string | { uid: string }, handler: (use: InventoryUse) => void): number;

    /**
     * Removes a handler owned by the calling resource and discards its pending uses.
     * @param handlerId Id returned by onUse.
     * @returns False if the registration is missing or belongs to another resource.
     */
    offUse(handlerId: number): boolean;
  };

  /**
   * Mutable two-dimensional vector.
   */
  class Vector2 {
    /**
     * Creates a vector from two numeric components.
     * @param x Initial X component.
     * @param y Initial Y component.
     */
    constructor(x: number, y: number);

    /**
     * Mutable X component.
     */
    x: number;

    /**
     * Mutable Y component.
     */
    y: number;

    /**
     * Read-only Euclidean magnitude of this vector.
     */
    readonly length: number;

    /**
     * Read-only squared magnitude, avoiding a square-root calculation.
     */
    readonly lengthSquared: number;

    /**
     * Adds another vector to this vector in place.
     * @param other Vector to add component-wise.
     * @returns This mutated vector for chaining.
     */
    add(other: Vector2): this;

    /**
     * Subtracts another vector from this vector in place.
     * @param other Vector to subtract component-wise.
     * @returns This mutated vector for chaining.
     */
    sub(other: Vector2): this;

    /**
     * Multiplies this vector by a scalar in place.
     * @param scalar Multiplier applied to both components.
     * @returns This mutated vector for chaining.
     */
    mul(scalar: number): this;

    /**
     * Divides this vector by a scalar in place.
     * @param scalar Divisor applied to both components; must be non-zero.
     * @returns This mutated vector for chaining.
     */
    div(scalar: number): this;

    /**
     * Computes the dot product without changing either vector.
     * @param other Vector used for the dot product.
     * @returns Scalar dot product.
     */
    dot(other: Vector2): number;

    /**
     * Normalizes this vector in place; a zero vector remains unchanged.
     * @returns This mutated vector for chaining.
     */
    normalize(): this;

    /**
     * Linearly interpolates this vector toward a target in place.
     * @param target Destination vector.
     * @param t Interpolation factor; 0 keeps the current value and 1 reaches target.
     * @returns This mutated vector for chaining.
     */
    lerp(target: Vector2, t: number): this;

    /**
     * Replaces both components in place.
     * @param x Replacement X component.
     * @param y Replacement Y component.
     * @returns This mutated vector for chaining.
     */
    set(x: number, y: number): this;

    /**
     * Computes Euclidean distance to another vector.
     * @param other Vector to measure from this vector.
     * @returns Distance between the two vectors.
     */
    distance(other: Vector2): number;

    /**
     * Creates an independent copy of this vector.
     * @returns New vector with the same components.
     */
    clone(): Vector2;

    /**
     * Formats this vector for logging and debugging.
     * @returns Text in Vector2(x, y) form.
     */
    toString(): string;

    /**
     * Converts this vector to a plain object for JSON.stringify.
     * @returns Object containing the current components.
     */
    toJSON(): { x: number; y: number };

    /**
     * Creates a vector whose components are zero.
     * @returns New Vector2(0, 0).
     */
    static zero(): Vector2;

    /**
     * Creates a vector whose components are one.
     * @returns New Vector2(1, 1).
     */
    static one(): Vector2;
  }

  /**
   * Mutable three-dimensional vector for positions, directions, and Euler angles.
   */
  class Vector3 {
    /**
     * Creates a vector from three numeric components.
     * @param x Initial X component.
     * @param y Initial Y component.
     * @param z Initial Z component.
     */
    constructor(x: number, y: number, z: number);

    /**
     * Mutable X component.
     */
    x: number;

    /**
     * Mutable Y component.
     */
    y: number;

    /**
     * Mutable Z component.
     */
    z: number;

    /**
     * Read-only Euclidean magnitude of this vector.
     */
    readonly length: number;

    /**
     * Read-only squared magnitude, avoiding a square-root calculation.
     */
    readonly lengthSquared: number;

    /**
     * Adds another vector to this vector in place.
     * @param other Vector to add component-wise.
     * @returns This mutated vector for chaining.
     */
    add(other: Vector3): this;

    /**
     * Subtracts another vector from this vector in place.
     * @param other Vector to subtract component-wise.
     * @returns This mutated vector for chaining.
     */
    sub(other: Vector3): this;

    /**
     * Multiplies this vector by a scalar in place.
     * @param scalar Multiplier applied to every component.
     * @returns This mutated vector for chaining.
     */
    mul(scalar: number): this;

    /**
     * Divides this vector by a scalar in place.
     * @param scalar Divisor applied to every component; must be non-zero.
     * @returns This mutated vector for chaining.
     */
    div(scalar: number): this;

    /**
     * Computes the dot product without changing either vector.
     * @param other Vector used for the dot product.
     * @returns Scalar dot product.
     */
    dot(other: Vector3): number;

    /**
     * Replaces this vector with its cross product against another vector.
     * @param other Second vector in the cross product.
     * @returns This mutated perpendicular vector for chaining.
     */
    cross(other: Vector3): this;

    /**
     * Normalizes this vector in place; a zero vector remains unchanged.
     * @returns This mutated vector for chaining.
     */
    normalize(): this;

    /**
     * Linearly interpolates this vector toward a target in place.
     * @param target Destination vector.
     * @param t Interpolation factor; 0 keeps the current value and 1 reaches target.
     * @returns This mutated vector for chaining.
     */
    lerp(target: Vector3, t: number): this;

    /**
     * Replaces all components in place.
     * @param x Replacement X component.
     * @param y Replacement Y component.
     * @param z Replacement Z component.
     * @returns This mutated vector for chaining.
     */
    set(x: number, y: number, z: number): this;

    /**
     * Computes Euclidean distance to another vector.
     * @param other Vector to measure from this vector.
     * @returns Distance between the two vectors.
     */
    distance(other: Vector3): number;

    /**
     * Creates an independent copy of this vector.
     * @returns New vector with the same components.
     */
    clone(): Vector3;

    /**
     * Formats this vector for logging and debugging.
     * @returns Text in Vector3(x, y, z) form.
     */
    toString(): string;

    /**
     * Converts this vector to a plain object for JSON.stringify.
     * @returns Object containing the current components.
     */
    toJSON(): { x: number; y: number; z: number };

    /**
     * Creates a zero vector.
     * @returns New Vector3(0, 0, 0).
     */
    static zero(): Vector3;

    /**
     * Creates a vector whose components are one.
     * @returns New Vector3(1, 1, 1).
     */
    static one(): Vector3;

    /**
     * Creates the framework's positive-Y unit direction.
     * @returns New Vector3(0, 1, 0).
     */
    static up(): Vector3;

    /**
     * Creates the framework's positive-Z unit direction.
     * @returns New Vector3(0, 0, 1).
     */
    static forward(): Vector3;

    /**
     * Creates the framework's positive-X unit direction.
     * @returns New Vector3(1, 0, 0).
     */
    static right(): Vector3;
  }

  /**
   * Mutable four-dimensional vector.
   */
  class Vector4 {
    /**
     * Creates a vector from four numeric components.
     * @param x Initial X component.
     * @param y Initial Y component.
     * @param z Initial Z component.
     * @param w Initial W component.
     */
    constructor(x: number, y: number, z: number, w: number);

    /**
     * Mutable X component.
     */
    x: number;

    /**
     * Mutable Y component.
     */
    y: number;

    /**
     * Mutable Z component.
     */
    z: number;

    /**
     * Mutable W component.
     */
    w: number;

    /**
     * Read-only Euclidean magnitude of this vector.
     */
    readonly length: number;

    /**
     * Read-only squared magnitude, avoiding a square-root calculation.
     */
    readonly lengthSquared: number;

    /**
     * Adds another vector to this vector in place.
     * @param other Vector to add component-wise.
     * @returns This mutated vector for chaining.
     */
    add(other: Vector4): this;

    /**
     * Subtracts another vector from this vector in place.
     * @param other Vector to subtract component-wise.
     * @returns This mutated vector for chaining.
     */
    sub(other: Vector4): this;

    /**
     * Multiplies this vector by a scalar in place.
     * @param scalar Multiplier applied to every component.
     * @returns This mutated vector for chaining.
     */
    mul(scalar: number): this;

    /**
     * Divides this vector by a scalar in place.
     * @param scalar Divisor applied to every component; must be non-zero.
     * @returns This mutated vector for chaining.
     */
    div(scalar: number): this;

    /**
     * Computes the dot product without changing either vector.
     * @param other Vector used for the dot product.
     * @returns Scalar dot product.
     */
    dot(other: Vector4): number;

    /**
     * Computes Euclidean distance to another vector.
     * @param other Vector to measure from this vector.
     * @returns Distance between the two vectors.
     */
    distance(other: Vector4): number;

    /**
     * Normalizes this vector in place; a zero vector remains unchanged.
     * @returns This mutated vector for chaining.
     */
    normalize(): this;

    /**
     * Linearly interpolates this vector toward a target in place.
     * @param target Destination vector.
     * @param t Interpolation factor; 0 keeps the current value and 1 reaches target.
     * @returns This mutated vector for chaining.
     */
    lerp(target: Vector4, t: number): this;

    /**
     * Replaces all components in place.
     * @param x Replacement X component.
     * @param y Replacement Y component.
     * @param z Replacement Z component.
     * @param w Replacement W component.
     * @returns This mutated vector for chaining.
     */
    set(x: number, y: number, z: number, w: number): this;

    /**
     * Creates an independent copy of this vector.
     * @returns New vector with the same components.
     */
    clone(): Vector4;

    /**
     * Formats this vector for logging and debugging.
     * @returns Text in Vector4(x, y, z, w) form.
     */
    toString(): string;

    /**
     * Converts this vector to a plain object for JSON.stringify.
     * @returns Object containing the current components.
     */
    toJSON(): { x: number; y: number; z: number; w: number };

    /**
     * Creates a zero vector.
     * @returns New Vector4(0, 0, 0, 0).
     */
    static zero(): Vector4;

    /**
     * Creates a vector whose components are one.
     * @returns New Vector4(1, 1, 1, 1).
     */
    static one(): Vector4;
  }

  /**
   * Mutable quaternion for three-dimensional rotations. Components are scalar-first (w, x, y, z), matching GLM — not the x, y, z, w order some libraries use.
   */
  class Quaternion {
    /**
     * Creates a quaternion in scalar-first w, x, y, z component order. Watch the trap: the scalar w is the first argument, unlike the x, y, z, w order used by some other libraries.
     * @param w Initial scalar component (comes first — this is not x, y, z, w order).
     * @param x Initial X imaginary component.
     * @param y Initial Y imaginary component.
     * @param z Initial Z imaginary component.
     */
    constructor(w: number, x: number, y: number, z: number);

    /**
     * Mutable scalar component.
     */
    w: number;

    /**
     * Mutable X imaginary component.
     */
    x: number;

    /**
     * Mutable Y imaginary component.
     */
    y: number;

    /**
     * Mutable Z imaginary component.
     */
    z: number;

    /**
     * Read-only magnitude (norm) of this quaternion; 1 for a unit rotation.
     */
    readonly length: number;

    /**
     * Read-only squared magnitude, avoiding a square-root calculation.
     */
    readonly lengthSquared: number;

    /**
     * Composes this rotation with another quaternion in place.
     * @param other Rotation composed after this quaternion.
     * @returns This mutated quaternion for chaining.
     */
    mul(other: Quaternion): this;

    /**
     * Normalizes this quaternion in place; a zero quaternion becomes the identity rotation instead of NaN.
     * @returns This unit quaternion for chaining.
     */
    normalize(): this;

    /**
     * Computes the conjugate without changing this quaternion.
     * @returns New conjugated quaternion.
     */
    conjugate(): Quaternion;

    /**
     * Computes the inverse rotation without changing this quaternion.
     * @returns New inverse quaternion.
     */
    inverse(): Quaternion;

    /**
     * Spherically interpolates this quaternion toward a target in place.
     * @param target Destination rotation.
     * @param t Interpolation factor; 0 keeps the current rotation and 1 reaches target.
     * @returns This mutated quaternion for chaining.
     */
    slerp(target: Quaternion, t: number): this;

    /**
     * Computes the quaternion dot product.
     * @param other Quaternion used for the dot product.
     * @returns Scalar dot product.
     */
    dot(other: Quaternion): number;

    /**
     * Applies this rotation to a vector without mutating either value.
     * @param vector Vector to rotate.
     * @returns New rotated vector.
     */
    rotateVector(vector: Vector3): Vector3;

    /**
     * Converts this rotation to Euler angles in radians.
     * @returns Pitch, yaw, and roll as a Vector3.
     */
    toEuler(): Vector3;

    /**
     * Replaces all quaternion components in place.
     * @param w Replacement scalar component.
     * @param x Replacement X imaginary component.
     * @param y Replacement Y imaginary component.
     * @param z Replacement Z imaginary component.
     * @returns This mutated quaternion for chaining.
     */
    set(w: number, x: number, y: number, z: number): this;

    /**
     * Creates an independent copy of this quaternion.
     * @returns New quaternion with the same components.
     */
    clone(): Quaternion;

    /**
     * Formats this quaternion for logging and debugging.
     * @returns Text in Quaternion(w, x, y, z) form.
     */
    toString(): string;

    /**
     * Converts this quaternion to a plain object for JSON.stringify.
     * @returns Object containing the current components.
     */
    toJSON(): { w: number; x: number; y: number; z: number };

    /**
     * Creates the identity rotation.
     * @returns New Quaternion(1, 0, 0, 0).
     */
    static identity(): Quaternion;

    /**
     * Creates a rotation from Euler angles.
     * @param pitch Pitch angle in radians.
     * @param yaw Yaw angle in radians.
     * @param roll Roll angle in radians.
     * @returns New rotation quaternion.
     */
    static fromEuler(pitch: number, yaw: number, roll: number): Quaternion;

    /**
     * Creates a rotation around an axis.
     * @param axis Rotation axis; it is normalized internally.
     * @param angle Rotation angle in radians.
     * @returns New axis-angle rotation quaternion.
     */
    static fromAxisAngle(axis: Vector3, angle: number): Quaternion;
  }

  /**
   * Mutable RGBA color, with components stored from 0 to 1.
   */
  class Color {
    /**
     * Creates a color from normalized RGBA components.
     * @param r Initial red component from 0 to 1.
     * @param g Initial green component from 0 to 1.
     * @param b Initial blue component from 0 to 1.
     * @param a Optional alpha component from 0 to 1; defaults to 1.
     */
    constructor(r: number, g: number, b: number, a?: number);

    /**
     * Mutable normalized red component.
     */
    r: number;

    /**
     * Mutable normalized green component.
     */
    g: number;

    /**
     * Mutable normalized blue component.
     */
    b: number;

    /**
     * Mutable normalized alpha component.
     */
    a: number;

    /**
     * Linearly interpolates every component toward a target in place.
     * @param target Destination color.
     * @param t Interpolation factor; 0 keeps this color and 1 reaches target.
     * @returns This mutated color for chaining.
     */
    lerp(target: Color, t: number): this;

    /**
     * Replaces this color's normalized components in place.
     * @param r Replacement red component from 0 to 1.
     * @param g Replacement green component from 0 to 1.
     * @param b Replacement blue component from 0 to 1.
     * @param a Optional replacement alpha; defaults to 1 when omitted.
     * @returns This mutated color for chaining.
     */
    set(r: number, g: number, b: number, a?: number): this;

    /**
     * Creates an independent copy of this color.
     * @returns New color with the same components.
     */
    clone(): Color;

    /**
     * Converts normalized components to a hexadecimal CSS-style string.
     * @param includeAlpha Whether to append the alpha byte; defaults to false.
     * @returns Lowercase #rrggbb or #rrggbbaa string.
     */
    toHex(includeAlpha?: boolean): string;

    /**
     * Formats this color for logging and debugging.
     * @returns Text in Color(r, g, b, a) form.
     */
    toString(): string;

    /**
     * Converts this color to a plain object for JSON.stringify.
     * @returns Object containing the current normalized components.
     */
    toJSON(): { r: number; g: number; b: number; a: number };

    /**
     * Parses a hexadecimal color string.
     * @param hex Hexadecimal color in #RRGGBB or #RRGGBBAA form; the leading # is optional. Three- and four-digit shorthands are not supported.
     * @returns Parsed color, or opaque white when the input is invalid.
     */
    static fromHex(hex: string): Color;

    /**
     * Creates a normalized color from byte components.
     * @param r Red byte from 0 to 255.
     * @param g Green byte from 0 to 255.
     * @param b Blue byte from 0 to 255.
     * @param a Optional alpha byte from 0 to 255; defaults to 255.
     * @returns New normalized color.
     */
    static fromRGB(r: number, g: number, b: number, a?: number): Color;

    /**
     * Creates opaque white.
     * @returns New Color(1, 1, 1, 1).
     */
    static white(): Color;

    /**
     * Creates opaque black.
     * @returns New Color(0, 0, 0, 1).
     */
    static black(): Color;

    /**
     * Creates opaque red.
     * @returns New Color(1, 0, 0, 1).
     */
    static red(): Color;

    /**
     * Creates opaque green.
     * @returns New Color(0, 1, 0, 1).
     */
    static green(): Color;

    /**
     * Creates opaque blue.
     * @returns New Color(0, 0, 1, 1).
     */
    static blue(): Color;

    /**
     * Creates opaque yellow.
     * @returns New Color(1, 1, 0, 1).
     */
    static yellow(): Color;

    /**
     * Creates opaque cyan.
     * @returns New Color(0, 1, 1, 1).
     */
    static cyan(): Color;

    /**
     * Creates opaque magenta.
     * @returns New Color(1, 0, 1, 1).
     */
    static magenta(): Color;

    /**
     * Creates fully transparent black.
     * @returns New Color(0, 0, 0, 0).
     */
    static transparent(): Color;
  }

  /**
   * Asynchronous resource event bus.
   */
  interface EventBus {
    /**
     * Registers a persistent handler in the shared event namespace.
     * @param eventName Case-sensitive event name.
     * @param handler Resource-owned callback invoked with the emitted arguments.
     * @returns Function that removes this exact subscription.
     */
    on<K extends EventName>(eventName: K, handler: (...args: EventMap[K]) => unknown | Promise<unknown>): Unsubscribe;

    /**
     * Registers a persistent handler in the shared event namespace.
     * @param eventName Case-sensitive event name.
     * @param handler Resource-owned callback invoked with the emitted arguments.
     * @returns Function that removes this exact subscription.
     */
    on(eventName: string, handler: EventHandler): Unsubscribe;

    /**
     * Registers a handler that is removed before its first invocation.
     * @param eventName Case-sensitive event name.
     * @param handler Resource-owned callback invoked with the emitted arguments.
     */
    once<K extends EventName>(eventName: K, handler: (...args: EventMap[K]) => unknown | Promise<unknown>): void;

    /**
     * Registers a handler that is removed before its first invocation.
     * @param eventName Case-sensitive event name.
     * @param handler Resource-owned callback invoked with the emitted arguments.
     */
    once(eventName: string, handler: EventHandler): void;

    /**
     * Removes a matching handler owned by the calling resource.
     * @param eventName Case-sensitive event name.
     * @param handler Resource-owned callback invoked with the emitted arguments.
     */
    off<K extends EventName>(eventName: K, handler: (...args: EventMap[K]) => unknown | Promise<unknown>): void;

    /**
     * Removes a matching handler owned by the calling resource.
     * @param eventName Case-sensitive event name.
     * @param handler Resource-owned callback invoked with the emitted arguments.
     */
    off(eventName: string, handler: EventHandler): void;

    /**
     * Invokes every shared handler and waits for all synchronous and asynchronous results.
     * @param eventName Shared event name.
     * @param args Arguments delivered to every matching handler.
     * @returns Promise rejected with an AggregateError when one or more handlers fail.
     */
    emit(eventName: string, ...args: unknown[]): Promise<void>;

    /**
     * Invokes matching handlers belonging only to one resource.
     * @param resourceName Destination running resource.
     * @param eventName Shared event name.
     * @param args Arguments delivered to matching handlers owned by the destination.
     * @returns Promise rejected when one or more destination handlers fail.
     */
    emitTo(resourceName: string, eventName: string, ...args: unknown[]): Promise<void>;

    /**
     * Registers a handler in the calling resource's private local-event namespace.
     * @param eventName Case-sensitive event name.
     * @param handler Resource-owned callback invoked with the emitted arguments.
     */
    onLocal(eventName: string, handler: EventHandler): void;

    /**
     * Emits an event only within the calling resource.
     * @param eventName Private local event name.
     * @param args Arguments delivered only to handlers owned by the calling resource.
     * @returns Promise rejected when one or more local handlers fail.
     */
    emitLocal(eventName: string, ...args: unknown[]): Promise<void>;

    /**
     * Counts persistent and one-shot shared handlers across resources.
     * @param eventName Shared event name to inspect.
     * @returns Number of matching handlers.
     */
    listenerCount(eventName: string): number;

    /**
     * Sends a named event from this client to the server's isolated onClient handlers.
     * @param eventName Server-side client-event name.
     * @param payload Optional string payload sent verbatim; other values are JSON-serialized.
     */
    emitServer(eventName: string, payload?: unknown): void;
  }

  /**
   * Asynchronous resource event bus.
   */
  const Events: EventBus;

  /**
   * Typed request and notification channel between local resources through the global Messages object.
   */
  const Messages: {
    /**
     * Registers or replaces a message handler owned by the calling resource.
     * @param messageType Message type unique within the receiving resource.
     * @param handler Handler invoked with the payload and a reply callback; the reply is ignored for notifications.
     */
    handle(messageType: string, handler: MessageHandler): void;

    /**
     * Sends a request to another local resource and waits for its handler to call reply.
     * @param resourceName Destination running resource.
     * @param messageType Handler type registered by the destination.
     * @param payload Optional payload delivered to the handler.
     * @returns Promise resolved with the reply value or rejected when delivery or handling fails.
     */
    request(resourceName: string, messageType: string, payload?: unknown): Promise<unknown>;

    /**
     * Sends a fire-and-forget notification to another local resource.
     * @param resourceName Destination running resource.
     * @param messageType Handler type registered by the destination.
     * @param payload Optional payload delivered to the handler.
     */
    send(resourceName: string, messageType: string, payload?: unknown): void;
  };

  /**
   * Registration and lookup of cross-resource values through the global Exports object.
   */
  const Exports: {
    /**
     * Registers a value from the calling resource for use by dependent resources.
     * @param name Export name, preferably declared in the current resource manifest.
     * @param value JavaScript value or function retained by the current resource.
     * @returns True when the value was registered.
     */
    register(name: string, value: unknown): boolean;

    /**
     * Reads one registered export from another running resource; undeclared dependencies produce a warning.
     * @param resourceName Name of the running resource that owns the export.
     * @param exportName Registered export name.
     * @returns The exported value.
     */
    get(resourceName: string, exportName: string): unknown;
  };

  /**
   * Bulk access to values exported by another running resource through the global Imports object.
   */
  const Imports: {
    /**
     * Builds an object containing every currently registered export from another resource; undeclared dependencies produce a warning.
     * @param resourceName Name of the running resource whose exports should be read.
     * @returns Object keyed by export name, or an empty object when the resource has no exports.
     */
    get(resourceName: string): Record<string, unknown>;
  };

  /**
   * Resource-aware console that routes output through the Framework logger.
   */
  const console: {
    /**
     * Writes an informational log entry prefixed with the current resource name.
     * @param values Values formatted and joined with spaces.
     */
    log(...values: unknown[]): void;

    /**
     * Alias of console.log for informational output.
     * @param values Values formatted and joined with spaces.
     */
    info(...values: unknown[]): void;

    /**
     * Writes a warning log entry prefixed with the current resource name.
     * @param values Values formatted and joined with spaces.
     */
    warn(...values: unknown[]): void;

    /**
     * Writes an error log entry prefixed with the current resource name.
     * @param values Values formatted and joined with spaces.
     */
    error(...values: unknown[]): void;

    /**
     * Writes a debug log entry prefixed with the current resource name.
     * @param values Values formatted and joined with spaces.
     */
    debug(...values: unknown[]): void;
  };

  /**
   * Runtime-side flags exposed as the global ExecutionEnvironment.
   */
  const ExecutionEnvironment: {
    /**
     * True in the sandboxed client scripting runtime.
     */
    readonly isClient: boolean;

    /**
     * True in the authoritative server scripting runtime.
     */
    readonly isServer: boolean;

    /**
     * Local Framework release version.
     */
    readonly frameworkVersion: string;

    /**
     * Local mod version from InstanceOptions.modVersion; empty when unset.
     */
    readonly modVersion: string;
  };

  /**
   * Client-only, resource-owned CEF web-view API exposed as the global Web.
   */
  const Web: {
    /**
     * Creates an origin-locked web view owned by the calling resource.
     * @param url Resource-relative URL or allowed absolute URL loaded into the view.
     * @param options Optional initial pixel geometry, stacking, visibility, and focus settings.
     * @returns Numeric view ID used by the remaining Web methods.
     */
    createView(url: string, options?: { width?: number; height?: number; x?: number; y?: number; zIndex?: number; visible?: boolean; focus?: boolean }): number;

    /**
     * Destroys an owned view and removes all of its script event handlers.
     * @param viewId Owned view identifier.
     * @returns True when a view was destroyed.
     */
    destroyView(viewId: number): boolean;

    /**
     * Makes an owned view visible.
     * @param viewId Owned view identifier.
     * @returns True when the view exists and was updated.
     */
    showView(viewId: number): boolean;

    /**
     * Hides an owned view.
     * @param viewId Owned view identifier.
     * @returns True when the view exists and was updated.
     */
    hideView(viewId: number): boolean;

    /**
     * Changes input focus for an owned view.
     * @param viewId Owned view identifier.
     * @param focused Whether the view captures keyboard and mouse input; defaults to true.
     * @returns True when the view exists and focus was updated.
     */
    focusView(viewId: number, focused?: boolean): boolean;

    /**
     * Checks whether an owned view is currently visible.
     * @param viewId Owned view identifier.
     * @returns False for missing or unowned views.
     */
    isViewVisible(viewId: number): boolean;

    /**
     * Keeps a hidden view painting so something else can sample it, such as a render target drawing the page onto world geometry. A view that is merely hidden stops painting and the sampled picture freezes.
     * @param viewId Owned view identifier.
     * @param offscreen Whether the page keeps painting without being drawn on screen; defaults to true.
     * @returns True when the view exists and was updated.
     */
    setViewOffscreen(viewId: number, offscreen?: boolean): boolean;

    /**
     * Checks whether an owned view keeps painting while hidden.
     * @param viewId Owned view identifier.
     * @returns False for missing or unowned views.
     */
    isViewOffscreen(viewId: number): boolean;

    /**
     * Navigates a view and replaces its allowed origin with the new URL's origin.
     * @param viewId Owned view identifier.
     * @param url New resource-relative or allowed absolute URL.
     * @returns True when navigation was requested.
     */
    loadURL(viewId: number, url: string): boolean;

    /**
     * Resizes an owned view's viewport.
     * @param viewId Owned view identifier.
     * @param width New viewport width in pixels.
     * @param height New viewport height in pixels.
     * @returns True when the view exists and was resized.
     */
    resizeView(viewId: number, width: number, height: number): boolean;

    /**
     * Moves an owned view on screen.
     * @param viewId Owned view identifier.
     * @param x New horizontal screen position in pixels.
     * @param y New vertical screen position in pixels.
     * @returns True when the view exists and was moved.
     */
    setViewPosition(viewId: number, x: number, y: number): boolean;

    /**
     * Registers a handler for an event emitted by an owned same-origin page.
     * @param viewId Owned view identifier.
     * @param eventName Page-to-script event name.
     * @param handler Resource-owned callback invoked by the view's callEvent bridge.
     */
    on(viewId: number, eventName: string, handler: WebEventHandler): void;

    /**
     * Removes page-event handlers from an owned view.
     * @param viewId Owned view identifier.
     * @param eventName Page-to-script event name.
     * @param handler Optional exact callback; omitting it removes every matching handler owned by the resource.
     * @returns True when at least one handler was removed.
     */
    off(viewId: number, eventName: string, handler?: WebEventHandler): boolean;

    /**
     * Dispatches a CustomEvent into an owned view.
     * @param viewId Owned view identifier.
     * @param eventName CustomEvent name dispatched in the page.
     * @param payload Optional JSON-serializable event detail.
     * @returns True when the dispatch script was queued.
     */
    emit(viewId: number, eventName: string, payload?: unknown): boolean;

    /**
     * Returns the current client viewport size.
     * @returns Width and height in physical pixels.
     */
    getScreenSize(): { width: number; height: number };
  };

  /**
   * A web view's browser finished being created.
   */
  interface BrowserCreatedEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * URL the view was created with.
     */
    url: string;
  }

  /**
   * A frame inside a web view started loading.
   */
  interface BrowserLoadingStartEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * URL being loaded.
     */
    url: string;

    /**
     * False for sub-frame loads.
     */
    isMainFrame: boolean;
  }

  /**
   * A web view's main frame finished loading.
   */
  interface BrowserDocumentReadyEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * URL that finished loading.
     */
    url: string;
  }

  /**
   * A load inside a web view was aborted.
   */
  interface BrowserLoadingFailedEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * URL that failed to load.
     */
    url: string;

    /**
     * CEF error text, such as ERR_CONNECTION_REFUSED.
     */
    description: string;

    /**
     * CEF error code.
     */
    errorCode: number;

    /**
     * False for sub-frame failures.
     */
    isMainFrame: boolean;
  }

  /**
   * A web view was asked to navigate.
   */
  interface BrowserNavigateEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * Requested URL.
     */
    url: string;

    /**
     * False for sub-frame navigation.
     */
    isMainFrame: boolean;

    /**
     * Whether the request was refused.
     */
    blocked: boolean;
  }

  /**
   * A page tried to open a new window or tab.
   */
  interface BrowserPopupEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * Target URL of the blocked popup.
     */
    url: string;

    /**
     * URL of the frame that requested it.
     */
    openerUrl: string;
  }

  /**
   * The cursor shape a page is asking for.
   */
  interface BrowserCursorChangeEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * CSS-style cursor name, or "custom" for shapes without one.
     */
    cursor: string;

    /**
     * Raw CEF cursor type.
     */
    cursorType: number;
  }

  /**
   * A page wants to display a tooltip.
   */
  interface BrowserTooltipEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * Tooltip text; empty when the tooltip is dismissed.
     */
    text: string;
  }

  /**
   * A form control inside a page gained or lost focus.
   */
  interface BrowserInputFocusChangeEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * True while the page holds keyboard input.
     */
    focused: boolean;
  }

  /**
   * A web view refused a request.
   */
  interface BrowserResourceBlockedEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * URL that was refused.
     */
    url: string;

    /**
     * Host component of that URL, empty when unparsable.
     */
    domain: string;

    /**
     * Why the request was refused.
     */
    reason: "cross-origin" | "invalid-url" | "host-filter" | "foreign-event";
  }

  /**
   * A console call made by a page.
   */
  interface BrowserConsoleMessageEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * Console message body.
     */
    message: string;

    /**
     * Script URL that logged it.
     */
    source: string;

    /**
     * Line number within that script.
     */
    line: number;

    /**
     * Console severity.
     */
    severity: "debug" | "info" | "warning" | "error" | "fatal";
  }

  /**
   * A web view's allowed origin changed.
   */
  interface BrowserOriginChangeEvent {
    /**
     * Identifier of the view the event belongs to.
     */
    viewId: number;

    /**
     * New locked origin, or "null" when the URL has none.
     */
    origin: string;

    /**
     * URL the lock was derived from.
     */
    url: string;
  }

  /**
   * Client-only, resource-owned physical key bindings exposed as the global Key.
   */
  const Key: {
    /**
     * Returns the current layout's label for a physical key, even while UI owns input. Query again when refreshing prompts. Throws for unknown keys or non-string arguments.
     * @param key Case-insensitive physical key name, using the same names as bind.
     * @returns Uppercase printable text, or an English name such as Enter or Mouse 1. Falls back to the uppercase canonical key name when translation is unavailable.
     */
    getLabel(key: string): string;

    /**
     * Binds a resource-owned handler that fires while the game has foreground input and no UI is capturing it. Each wheelup/wheeldown notch fires down, then up.
     * @param key Case-insensitive supported keyboard or mouse key name.
     * @param stateOrHandler Trigger state, or the handler itself to use the default down state.
     * @param handler Handler required when an explicit trigger state is provided.
     * @returns True after the binding is installed; invalid keys or states throw.
     */
    bind(key: string, stateOrHandler: "down" | "up" | "both" | KeyHandler, handler?: KeyHandler): boolean;

    /**
     * Removes matching bindings owned by the calling resource; omitting filters removes every binding for the key.
     * @param key Case-insensitive supported key name.
     * @param state Optional trigger-state filter.
     * @param handler Optional exact handler filter.
     * @returns True when at least one binding was removed.
     */
    unbind(key: string, state?: "down" | "up" | "both", handler?: KeyHandler): boolean;

    /**
     * Queries live physical key state using the same foreground and UI-input gate as binding dispatch.
     * @param key Case-insensitive supported key name.
     * @returns False when the key is up, the game is backgrounded, UI owns input, or the key name is invalid. Always false for the wheel, which is never held.
     */
    isDown(key: string): boolean;
  };

  /**
   * Client chat transport and native chat-box controls.
   */
  const Chat: {
    /**
     * Sends a chat line to the server, bypassing the chatSend event; incoming lines arrive through the reserved chatMessage event.
     * @param text Player-authored chat text sent to the server.
     */
    send(text: string): void;

    /**
     * Changes visibility of the native chat overlay without opening its input field. Hidden, the overlay also stops opening on its key. The choice holds for the rest of the session and resets to visible on disconnect, so a resource that replaces the chat calls this once at startup.
     * @param visible Whether the chat overlay is rendered.
     */
    setUIVisible(visible: boolean): void;

    /**
     * Checks whether the native chat overlay is visible.
     * @returns True when the overlay is currently rendered.
     */
    isUIVisible(): boolean;

    /**
     * Opens and focuses the native chat input field.
     */
    open(): void;

    /**
     * Closes the native chat input field without submitting its contents.
     */
    close(): void;

    /**
     * Checks whether the native chat input field is active.
     * @returns True while chat is capturing keyboard input.
     */
    isOpen(): boolean;
  };

  /**
   * Client-only Discord rich-presence composer exposed as the global Discord; setters stage values until update or setPresence publishes them.
   */
  const Discord: {
    /**
     * Stages the Discord activity verb or numeric enum value.
     * @param type the Discord activity verb or numeric enum value.
     */
    setType(type: "playing" | "streaming" | "listening" | "watching" | number): void;

    /**
     * Stages the activity name.
     * @param name the activity name.
     */
    setName(name: string): void;

    /**
     * Stages the top presence line.
     * @param details the top presence line.
     */
    setDetails(details: string): void;

    /**
     * Stages the bottom presence line.
     * @param state the bottom presence line.
     */
    setState(state: string): void;

    /**
     * Stages the elapsed-time Unix timestamp in seconds; zero clears it.
     * @param seconds the elapsed-time Unix timestamp in seconds; zero clears it.
     */
    setStartTimestamp(seconds: number): void;

    /**
     * Stages the countdown Unix timestamp in seconds; zero clears it.
     * @param seconds the countdown Unix timestamp in seconds; zero clears it.
     */
    setEndTimestamp(seconds: number): void;

    /**
     * Stages the supported-platform bitmask: Desktop 1, Android 2, and iOS 4.
     * @param flags the supported-platform bitmask: Desktop 1, Android 2, and iOS 4.
     */
    setSupportedPlatforms(flags: number): void;

    /**
     * Stages the large image asset key.
     * @param assetKey the large image asset key.
     */
    setLargeImage(assetKey: string): void;

    /**
     * Stages the large image tooltip.
     * @param text the large image tooltip.
     */
    setLargeText(text: string): void;

    /**
     * Stages the small image asset key.
     * @param assetKey the small image asset key.
     */
    setSmallImage(assetKey: string): void;

    /**
     * Stages the small image tooltip.
     * @param text the small image tooltip.
     */
    setSmallText(text: string): void;

    /**
     * Stages the party grouping identifier.
     * @param id the party grouping identifier.
     */
    setPartyId(id: string): void;

    /**
     * Stages the party join-privacy name or numeric enum value.
     * @param privacy the party join-privacy name or numeric enum value.
     */
    setPartyPrivacy(privacy: "private" | "public" | number): void;

    /**
     * Stages the opaque match secret.
     * @param secret the opaque match secret.
     */
    setMatchSecret(secret: string): void;

    /**
     * Stages the opaque join secret that enables invitations.
     * @param secret the opaque join secret that enables invitations.
     */
    setJoinSecret(secret: string): void;

    /**
     * Stages the opaque spectate secret.
     * @param secret the opaque spectate secret.
     */
    setSpectateSecret(secret: string): void;

    /**
     * Stages whether the activity represents an instanced session.
     * @param instance whether the activity represents an instanced session.
     */
    setInstance(instance: boolean): void;

    /**
     * Stages multiple Discord image fields at once.
     * @param assets Asset keys and tooltips to merge into the staged activity.
     */
    setAssets(assets: { largeImage?: string; largeText?: string; smallImage?: string; smallText?: string }): void;

    /**
     * Stages the party's current and maximum size.
     * @param current Current party member count.
     * @param max Maximum party capacity.
     */
    setPartySize(current: number, max: number): void;

    /**
     * Stages multiple Discord party fields at once.
     * @param party Party fields to merge into the staged activity.
     */
    setParty(party: { id?: string; size?: [number, number]; privacy?: "private" | "public" | number }): void;

    /**
     * Stages multiple Discord activity secrets at once.
     * @param secrets Opaque secrets to merge into the staged activity.
     */
    setSecrets(secrets: { match?: string; join?: string; spectate?: string }): void;

    /**
     * Merges a complete option batch into the staged activity and publishes it immediately.
     * @param options Batch of supported activity, timestamp, asset, party, secret, instance, and platform fields.
     * @returns True when the update was dispatched; false when Discord is unavailable.
     */
    setPresence(options: Record<string, unknown>): boolean;

    /**
     * Publishes the currently staged activity as one rate-limited Discord update.
     * @returns True when dispatched; false when Discord is unavailable.
     */
    update(): boolean;

    /**
     * Clears what scripts published and resets staged state.
     * @returns True when dispatched; false when Discord is unavailable.
     */
    clear(): boolean;

    /**
     * Resets staged activity fields without publishing a Discord update.
     */
    reset(): void;

    /**
     * Returns the signed-in Discord user's snowflake.
     * @returns User ID string, or an empty string until available.
     */
    getUserId(): string;

    /**
     * Checks whether Discord is connected and can publish presence.
     * @returns True when rich presence is initialized.
     */
    isAvailable(): boolean;
  };

  /**
   * Local player's proximity voice chat settings: on/off, playback volume, hearing range and the push-to-talk binding.
   */
  const Voice: {
    /**
     * Turns voice chat on or off. Off closes the microphone and playback devices and tells the server to stop relaying voice to this client.
     * @param enabled Whether voice chat runs at all for this player.
     */
    setEnabled(enabled: boolean): void;

    /**
     * Checks whether voice chat is enabled for this player.
     * @returns True unless the player turned voice chat off.
     */
    isEnabled(): boolean;

    /**
     * Sets the playback volume of incoming voice. A game that plays voice through its own audio engine applies the same gain there.
     * @param volume Playback gain, where 1 is unattenuated. Clamped to 0..4.
     */
    setVolume(volume: number): void;

    /**
     * Reads the voice playback volume.
     * @returns Current gain, where 1 is unattenuated.
     */
    getVolume(): number;

    /**
     * Narrows how far this player hears others. Can only reduce the server's range, since a talker beyond it is never relayed.
     * @param range Audibility radius in world units; 0 removes the local limit.
     */
    setHearingRange(range: number): void;

    /**
     * Reads the local hearing-range limit.
     * @returns Radius in world units, or 0 when the server's range applies unreduced.
     */
    getHearingRange(): number;

    /**
     * Reads the server's proximity range for talkers with no override of their own.
     * @returns Radius in world units.
     */
    getRange(): number;

    /**
     * Switches how far this player's voice carries. A request: the server paces it, decides the radius, and may move the player itself.
     * @param tier Voice tier: 0 whisper, 1 normal, 2 shout.
     */
    setTier(tier: number): void;

    /**
     * Reads the voice tier this player is on.
     * @returns 0 whisper, 1 normal, 2 shout.
     */
    getTier(): number;

    /**
     * Rebinds push-to-talk. Unknown key names and the wheel, which is never held, throw.
     * @param key Case-insensitive key name, using the same names as Key.bind.
     */
    setPushToTalkKey(key: string): void;

    /**
     * Reads the push-to-talk binding.
     * @returns Canonical key name, e.g. "v".
     */
    getPushToTalkKey(): string;

    /**
     * Sets how long transmission continues after push-to-talk is released, so letting go slightly early does not clip the end of a word. Muting, turning voice off and losing input focus still stop it at once.
     * @param milliseconds How long to keep transmitting after the key goes up. Clamped to 0..2000.
     */
    setPushToTalkReleaseDelay(milliseconds: number): void;

    /**
     * Reads the push-to-talk release delay.
     * @returns Delay in milliseconds; 0 when transmission stops the moment the key is released.
     */
    getPushToTalkReleaseDelay(): number;

    /**
     * Checks whether the local player is speaking right now. The same state raises the voiceStart and voiceStop events.
     * @returns True while push-to-talk is open -- held, or still inside the release delay -- and the microphone is producing audio.
     */
    isTalking(): boolean;

    /**
     * Checks whether a capture device opened for this session.
     * @returns False when the player has no working microphone, i.e. they are listen-only.
     */
    hasMicrophone(): boolean;

    /**
     * Chooses what opens the microphone. Switching closes whatever the previous mode had open. Unknown modes throw.
     * @param mode pushToTalk sends while the key is held; voiceActivity sends whenever the microphone is louder than the activation threshold.
     */
    setTransmitMode(mode: 'pushToTalk' | 'voiceActivity'): void;

    /**
     * Reads what opens the microphone.
     * @returns The current transmit mode.
     */
    getTransmitMode(): 'pushToTalk' | 'voiceActivity';

    /**
     * Sets the voice-activation sensitivity: lower sends quieter speech, higher ignores more background noise. Clamped to 0..1.
     * @param level Microphone level, 0 to 1 on the scale getInputLevel reports, above which voice activation sends.
     */
    setActivationThreshold(level: number): void;

    /**
     * Reads the voice-activation threshold.
     * @returns Level from 0 to 1.
     */
    getActivationThreshold(): number;

    /**
     * Reads how loud the microphone is right now, whether or not anything is being sent -- the value to draw a level meter from and to tune the activation threshold against.
     * @returns Smoothed level from 0 to 1; 0 while no microphone is open.
     */
    getInputLevel(): number;

    /**
     * Turns noise suppression on or off. It runs before the level is measured, so it also keeps steady noise from tripping voice activation.
     * @param enabled Whether to filter background noise out of the microphone.
     */
    setNoiseSuppression(enabled: boolean): void;

    /**
     * Checks whether noise suppression is on.
     * @returns True while background noise is being filtered.
     */
    isNoiseSuppressionEnabled(): boolean;

    /**
     * Lists the microphones voice can record from.
     * @returns Device names as the player would recognise them; empty when the game chooses the device itself.
     */
    getInputDevices(): string[];

    /**
     * Chooses the microphone. A running microphone is reopened on the new device; a device that is no longer connected falls back to the default.
     * @param name A name from getInputDevices, or an empty string for the system default.
     */
    setInputDevice(name: string): void;

    /**
     * Reads the chosen microphone.
     * @returns Its name, or an empty string for the system default.
     */
    getInputDevice(): string;

    /**
     * Hides or shows the mod's own voice indicator: the microphone low on the left while this player is sending, and the talking icon on the nametag of each player they hear. Hiding it is for a resource that draws its own from `isTalking`, `getTier`, `getInputLevel`, `isPlayerTalking` and the `voiceStart` and `voiceStop` events; voice itself carries on unchanged. A hidden indicator belongs to the resource that hid it, and comes back when that resource stops or the session ends. Throws unless `visible` is a boolean.
     * @param visible False hides it; true shows it again.
     */
    setIndicatorVisible(visible: boolean): void;

    /**
     * Checks whether the mod draws its own voice indicator.
     * @returns True unless a resource hid it with `setIndicatorVisible(false)`.
     */
    isIndicatorVisible(): boolean;

    /**
     * Checks whether this player hears another player speaking right now -- the state the talking icon on their nametag follows, held across the pauses between words. A fact about this client's playback: someone out of range, muted for this player or beyond the talkers it plays at once reads as silent. For the local player it answers `isTalking`. Throws unless `player` is a Player.
     * @param player Player to query.
     * @returns True while their voice is playing here.
     */
    isPlayerTalking(player: Player): boolean;
  };

  /**
   * The local player's view of the nametags above players: whether they draw at all, whether they carry a health bar, and whether their own is shown.
   */
  const Nametags: {
    /**
     * Shows or hides all nametags for this player only. A player hidden with Player.setNametagVisible stays hidden either way.
     * @param visible True to draw nametags, false to hide every one of them.
     */
    setVisible(visible: boolean): void;

    /**
     * Checks whether this player draws nametags.
     * @returns True unless they were hidden locally.
     */
    isVisible(): boolean;

    /**
     * Shows or hides the health bar on all nametags for this player only, leaving the names alone.
     * @param visible True to draw the health bar under each name, false to hide it.
     */
    setHealthVisible(visible: boolean): void;

    /**
     * Checks whether this player draws health bars on nametags.
     * @returns True unless they were hidden locally.
     */
    isHealthVisible(): boolean;

    /**
     * Shows or hides this player's own nametag, drawn as others see it, with any label set on their own id.
     * @param visible True to draw this player's own nametag, false (default) to leave it off.
     */
    setSelfVisible(visible: boolean): void;

    /**
     * Checks whether this player draws their own nametag.
     * @returns False unless it was turned on locally.
     */
    isSelfVisible(): boolean;

    /**
     * Hangs a transient line on that entity's nametag, above its name -- speech, an emote, a status. It follows the body, fades with distance and hides behind cover exactly as the name does. Local to this player: the line is not replicated, and a nametag hidden with Nametags.setVisible draws neither. Player.setNametagText is the server-side counterpart for a lasting name.
     * @param entityId Network id of the entity to label (server-side `player.id`).
     * @param text Label text; '\n' splits lines. Empty clears the label.
     * @param durationMs How long to hold it (default 6000). <= 0 holds until cleared.
     * @param color Packed 0xAARRGGBB; 0 (default) uses the nametag's own colour.
     */
    setLabel(entityId: number, text: string, durationMs?: number, color?: number): void;

    /**
     * Removes an entity's label before its duration elapses.
     * @param entityId Network id of the entity whose label to remove.
     */
    clearLabel(entityId: number): void;

    /**
     * Removes every label this player is drawing.
     */
    clearLabels(): void;
  };

  /**
   * Base handle for a live replicated network entity.
   */
  class Entity {
    /**
     * Creates a script wrapper for an existing entity with this ID; it does not spawn an entity.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * Immutable network entity identifier.
     */
    readonly id: number;

    /**
     * Current virtual-world identifier.
     */
    readonly virtualWorld: number;

    /**
     * Authoritative world-space position; assignment forces replicated state.
     */
    position: Vector3;

    /**
     * Authoritative rotation; reads return a quaternion and assignments accept a quaternion or Euler angles in degrees.
     */
    rotation: Quaternion | Vector3;

    /**
     * Arbitrary key/value state carried by this entity. The server writes it and every client that can see the entity receives it; see StateBag.
     */
    readonly state: StateBag;

    /**
     * Formats this entity handle for logging and debugging.
     * @returns Text containing the network entity ID.
     */
    toString(): string;
  }

  /**
   * Arbitrary key/value state attached to one replicated entity, reached as `entity.state`. Keys set on the server replicate to every client that can currently see the entity.
   */
  class StateBag {
    private constructor();

    /**
     * Reads one key from this entity's state.
     * @param key Key to read.
     * @returns The stored value, or undefined when the key is not set.
     */
    get(key: string): any;

    /**
     * Checks whether this entity's state holds a key.
     * @param key Key to test.
     * @returns True when the key is set.
     */
    has(key: string): boolean;

    /**
     * Lists the keys this entity's state holds, sorted.
     * @returns Every key currently set, in ascending order.
     */
    keys(): string[];

    /**
     * Copies this entity's whole state into a plain object.
     * @returns Every key and value currently set. The copy does not track later changes.
     */
    toObject(): Record<string, any>;

    /**
     * Watches this entity's state. The filter is applied before the handler runs, so a listener watching one key of one entity is not woken by unrelated changes. The subscription is dropped when the registering resource stops.
     * @param key Only report this key, or null to report every key of this entity.
     * @param handler Called as (key: string, value: any, previous: any) with the changed key, its new value and the value before it. `value` is undefined when the key was removed and `previous` is undefined when it held nothing.
     * @returns A zero-argument function that cancels this subscription.
     */
    onChange(key: string | null, handler: Function): Function;
  }

  /**
   * Framework-owned base handle for a connected player, extended by each game or mod.
   */
  class BasePlayer {
    /**
     * Creates a wrapper for an existing connected player with this ID; it does not connect or spawn a player.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * Formats this player handle for logging and debugging.
     * @returns Text containing the player's network entity ID.
     */
    toString(): string;

    /**
     * Checks whether this player's nametag is shown to other players.
     * @returns True unless the nametag was hidden; false also when this game has no nametags.
     */
    isNametagVisible(): boolean;

    /**
     * Checks whether the health bar under this player's nametag is shown.
     * @returns True unless the health bar was hidden; false also when this game has no nametags.
     */
    isNametagHealthVisible(): boolean;

    /**
     * Reads this player's nametag text override.
     * @returns The override, or an empty string when the player's own name is drawn.
     */
    getNametagText(): string;

    /**
     * Reads this player's nametag color.
     * @returns Packed 0xAARRGGBB color; opaque white when untinted.
     */
    getNametagColor(): number;
  }

  interface BasePlayer extends Entity {}

  /**
   * The player sitting at this machine, or null while this client has no body -- before the session spawns one, and again once it ends. Read it fresh each time rather than holding onto it.
   */
  const LocalPlayer: Player | null;

  /**
   * Calls a function once after a delay.
   * @param handler Function to call.
   * @param milliseconds Delay before the call.
   * @param args Arguments passed to the handler.
   * @returns Handle to pass to the matching clear function.
   */
  function setTimeout(handler: (...args: any[]) => void, milliseconds?: number, ...args: unknown[]): number;

  /**
   * Calls a function repeatedly, waiting the delay between calls.
   * @param handler Function to call.
   * @param milliseconds Delay before the call.
   * @param args Arguments passed to the handler.
   * @returns Handle to pass to the matching clear function.
   */
  function setInterval(handler: (...args: any[]) => void, milliseconds?: number, ...args: unknown[]): number;

  /**
   * Cancels a pending setTimeout.
   * @param handle Handle returned when the timer was scheduled; anything else is ignored.
   */
  function clearTimeout(handle?: number): void;

  /**
   * Cancels a setInterval.
   * @param handle Handle returned when the timer was scheduled; anything else is ignored.
   */
  function clearInterval(handle?: number): void;

  /**
   * Queues a function on the microtask queue.
   * @param callback Function to run once the current task completes.
   */
  function queueMicrotask(callback: () => void): void;
}

export {};
