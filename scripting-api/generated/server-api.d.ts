import type { EventHandler, MessageHandler, MessageReply, Quaternion, Unsubscribe, Vector3 } from "../shared.js";

declare global {
  /**
   * Native events dispatched through `Events.on`. Each property is the exact callback argument tuple for that event.
   */
  interface EventMap {
    /**
     * Dispatched once a connecting player's body exists and can be resolved to a handle. The body has no pose yet -- `player.ready` is false until the owner reports one.
     */
    playerConnect: [player: Player];

    /**
     * Dispatched while a player is leaving, before their body is destroyed, so the handle still reads. Anything keyed on the player must be cleaned up here: a dropped connection raises no other event.
     */
    playerDisconnect: [player: Player];

    /**
     * Dispatched once per accepted death report. Call player.revive() to request revival; there is no automatic respawn.
     * 
     * `killer` is the player or server NPC whose blow took the last of the health, as the dying player's own client saw it -- null for a fall, a bleed-out, or anything without an identified attacker. `reason` is the game's own for that last write: `combat` for a blade or a bow, `fall`, `bleeding`, `poison` and so on.
     */
    playerDied: [player: Player, killer: Player | Npc | null, reason: "unknown" | "combat" | "gunshot" | "starvation" | "collision" | "scripted" | "disintegrate" | "fall" | "poison" | "bleeding" | "selfHarm"];

    /**
     * Dispatched when something takes health off a player: a weapon, an arrow, a fall, a collision, a scripted hit. Player combat and server NPC melee are raised by the server as it rules on them, after `playerHit` had its say, so `amount` is what the ruling takes; anything else is reported by the hit player's own client, which resolved it against its armour and its skills. It arrives ahead of the `playerDied` a killing blow causes.
     * 
     * `attacker` is the player or server NPC who dealt it, or null. `bodyPart` is where it landed, or null for damage that lands nowhere in particular. A player attacker's weapon is available through `rightHandItem` or `leftHandItem`; an NPC's equipped items are in `wearing`.
     * 
     * Bleeding, poison and hunger wear health down a tick at a time without raising this -- `bleeding`, `poisoning` and `hunger` are live numbers already. The tick that kills does arrive, with its own reason, just before the `playerDied` it causes.
     */
    playerDamage: [player: Player, attacker: Player | Npc | null, amount: number, bodyPart: "head" | "torso" | "leftArm" | "rightArm" | "leftLeg" | "rightLeg" | null, reason: "unknown" | "combat" | "gunshot" | "starvation" | "collision" | "scripted" | "disintegrate" | "fall" | "poison" | "bleeding" | "selfHarm"];

    /**
     * A melee swing aimed at another player was admitted by the server, including a miss. Raised once per attack generation, after presentation admission. Use it to track combat inactivity in your resource.
     */
    playerAttack: [player: Player, opponent: Player];

    /**
     * A resource called cancelCombat for this pair. Raised after the cancellation was sent to both players and their observers.
     */
    playerCombatCancelled: [first: Player, second: Player];

    /**
     * Dispatched when another player's blade or arrow, or a server NPC's melee attack, lands on a player, before anything is taken -- the place for teams, safe zones, friendly fire and damage rules. The game resolved the blow against the guard the victim really held -- whether it was blocked, perfectly or not, and where it landed -- and the server works out what a swing takes from there; it has checked that the swing was one it relayed and could still land, or that the arrow was one whose impact it accepted, and that the two stood within reach. Only what is ruled here comes off the victim's health, on every client at once.
     * 
     * Return `false` from a handler to refuse the blow: no health is taken and no `playerDamage` follows. NPC melee uses the victim client's native damage with `priced: false`; set `hit.damage` to change it. For priced player swings, scale `hit.modifiers` to change how a swing is worked out -- a stronger attack, weaker armour -- or set `hit.damage` to say outright what it takes: `0` for a blow that lands harmlessly, more for a heavier one. Either way the blow is still seen and heard: the blades met. Handlers run synchronously, so the decision cannot wait on anything awaited.
     */
    playerHit: [player: Player, attacker: Player | Npc, hit: PlayerHit];

    /**
     * Dispatched when a limb becomes injured -- usually a blow landing there, sometimes a fall on both legs. `player.injuries` already says so. A limb hit again while injured stays injured and raises nothing new.
     */
    playerInjured: [player: Player, bodyPart: "head" | "torso" | "leftArm" | "rightArm" | "leftLeg" | "rightLeg"];

    /**
     * Dispatched when a limb injury is gone: it healed by itself, a bandage took it, or `player.heal` did.
     */
    playerInjuryHealed: [player: Player, bodyPart: "head" | "torso" | "leftArm" | "rightArm" | "leftLeg" | "rightLeg"];

    /**
     * Dispatched when a player's body needs somewhere to stand, while their own client waits on the answer behind its loading screen. This is the one moment a spawn can be chosen without anybody seeing the body move -- call `player.spawn(position)` from the handler and that becomes where they arrive.
     * 
     * Handlers run synchronously, so the choice has to be made in the handler itself rather than in something awaited from it. A handler that names no placement leaves the player at the level's own start point, which is the same tile for everybody.
     * 
     * `reason` says whether they are arriving or coming back: `join` for a connection, `respawn` after a death.
     */
    playerSpawning: [player: Player];

    /**
     * Dispatched once when the animation a `playAnimation` request started is over. `fragment` is the request's. `reason` says how: `finished` when a one-shot played to its end, `interrupted` when it was playing and something cut it short (a hit, a weapon drawn, a teleport), `refused` when the game would not start it at all, `stopped` for `stopAnimation`, `replaced` for another `playAnimation`.
     * 
     * The first three come from the player's own client, sent from the moment the game's animation system ended it, so this is the place to chain the next animation. A `loop` never finishes by itself and only ends `stopped` or `replaced`. A request with no fragment -- one that only holds a prop or a tag -- raises nothing.
     */
    playerAnimationEnd: [player: Player, fragment: string, reason: 'finished' | 'interrupted' | 'refused' | 'stopped' | 'replaced'];

    /**
     * Dispatched when a player has picked up another player's body, before the server accepts it -- the place to decide who may carry whom. Return `false` from a handler and the carry is refused: the carrier's client is told to let go at once, and no `playerCarry` follows. Handlers run synchronously, so the decision cannot wait on anything awaited.
     * 
     * The pick-up is the game's own "grab" prompt, which it offers only over a body that is down -- dead or unconscious -- so a player a script revives at once is never there to carry. The carrier's game has already started the pick-up when this runs.
     */
    playerCarrying: [carrier: Player, carried: Player];

    /**
     * Dispatched once a carry is accepted: the carried body is on the carrier's shoulder on every client, and `carrier.carrying` names it. It rides there until `playerPutDown`.
     */
    playerCarry: [carrier: Player, carried: Player];

    /**
     * Dispatched once a carry is over, however it ended: the carrier dropped the body, `putDown` asked them to, the carried player was revived, or either of them left. A player who is leaving is still readable here; the event comes before their body is destroyed.
     */
    playerPutDown: [carrier: Player, carried: Player];

    /**
     * Dispatched when a player starts a carry of their own, before the server accepts it: they chose the game's pick-up prompt over a carryable `groundItem`, or took an item from one of the world's own piles (`groundItem` null). Return `false` from a handler and the carry is refused -- the prop stays where it is, and a pile's item is let go at once. Handlers run synchronously, so the decision cannot wait on anything awaited. A carry a script ordered with `carryItem` is not asked. `item` is the item's name.
     */
    playerCarryingItem: [player: Player, item: string, groundItem: GroundItem | null];

    /**
     * Dispatched once an item carry is accepted: everybody sees the player pick it up and carry it, and `player.carriedItem` names it. `groundItem` is the prop it was taken up from, still readable here and gone a moment later, or null. It is carried until `playerPutDownItem`.
     */
    playerCarryItem: [player: Player, item: string, groundItem: GroundItem | null];

    /**
     * Dispatched once an item carry is over, however it ended: the player put it down, `putDownItem` asked them to, it went into a pile, or they left. `groundItem` is the carryable prop it became where it fell -- still falling, settling where the game's physics puts it -- or null when it went into a pile or they have no room left on the ground.
     */
    playerPutDownItem: [player: Player, item: string, groundItem: GroundItem | null];

    /**
     * Dispatched once a joining player is really standing in the world: their level is up, their body is where `playerSpawning` put it, and the ground under it has loaded. Anything that acts on an arriving player -- kit, a welcome line, a marker -- belongs here rather than in `playerConnect`, which fires while they are still loading the level, or in `playerSpawning`, where the body is still mid-placement.
     * 
     * The player may still be looking at the game's loading screen; `playerReady` follows once they are not.
     */
    playerSpawned: [player: Player];

    /**
     * Dispatched once per connection, after `playerSpawned`, when the player can play: their own client reports that the game has taken its loading screen down. The game raises that moment itself -- it is its own gameplay-start signal, not a guess from timing.
     * 
     * That player's client resources were already running at `playerSpawned`, so this is not needed to make sure a `player.emit` is received. It is for what the player should actually see on arrival -- a welcome, a first page, a camera shot -- which shown earlier would play behind the loading screen. A client that never gets that far, or disconnects first, never raises it.
     */
    playerReady: [player: Player];

    /**
     * After the server receives changed player stats, including poison strength, drunkenness, health loss and potion-related readings. Changes are snapshots batched per server tick. The first valid report and each new body establish a baseline. These are observations, not cancellable requests; the client can coalesce intermediate values. No cause is inferred. Use consumption vetoes and custom server effects to decide gameplay before it happens.
     */
    playerStatsChanged: [player: Player, changes: { stat: "health" | "stamina" | "exhaust" | "hunger" | "bleeding" | "sleeping" | "consciousness" | "drunkenness" | "poisoning" | "charisma" | "visibility" | "conspicuousness" | "noise" | "dirtiness" | "bloodiness" | "smell" | "smellIntensity" | "fragrance" | "carriedWeight" | "inventoryCapacity" | "encumbrance" | "hangover" | "alcoholism" | "armorRating" | "overallArmorDefense" | "overallWeaponAttack" | "normalizedRunSpeed" | "runSpeedBase" | "morale"; previous: number; current: number }[]];

    /**
     * Before reserving a food or potion unit or pot serving. Return false synchronously to refuse without spending stock or applying effects. Async handlers cannot veto. Do not apply replacement effects here: native use can still fail.
     */
    playerConsuming: [player: Player, consumption: Consumption];

    /**
     * After playerConsuming accepts, before reservation. Return false synchronously to consume without native nutrition, energy, alcohol, potion, spoilage or added pot poison effects. Apply custom effects in playerConsumed using player.setStat, addBuff and clearBuffs.
     */
    playerConsumptionEffects: [player: Player, consumption: Consumption];

    /**
     * Before applying a reserved pot meal's additional poison on its eating receipt. Return false synchronously to suppress it. Runs only when nativeEffects is true. Native food spoilage and item buffs can be replaced through playerConsumptionEffects and Buffs.claim.
     */
    playerPoisonAbsorbing: [player: Player, consumption: Consumption, buff: string];

    /**
     * Once after the owning client confirms successful use. Use for custom stat changes and buffs after suppressing native effects. Missing receipts and expired or replaced bodies never raise it. Inventory uses also raise playerItemUsed.
     */
    playerConsumed: [player: Player, consumption: Consumption];

    /**
     * Dispatched when a player's game dried, smoked or cooked items, before the server converts them in its inventory. The game converts at once when its animation ends; nothing about it takes time. Return `false` from a handler to refuse it: the items stay as they were, and the player's game puts them back. Every handler runs, synchronously: handler promises are not awaited, so an async handler cannot refuse. To make it take time, refuse it, take the rows with `Inventory.remove` and give the products with `Inventory.add` when your timer is up. The inventory change is `dry`, `smoke` or `cook`. Only an accepted one earns the game's survival XP, which the server grants per unit converted as `player.addXp` grants XP: the player's own multipliers apply; `playerXpGaining`, `Progression.setXpRate`, `setXpBudget`, `setLevelCap` and `setNativeXp(false)` do not.
     */
    playerPreparing: [player: Player, preparation: Preparation];

    /**
     * A validated custom-item action. The frozen row includes server-only data. Scripts decide what happens and explicitly consume items.
     */
    playerCustomItemUse: [player: Player, item: InventoryRow, revision: number];

    /**
     * An inventory changed. Raised on the next server tick in the order the changes happened, once per inventory an operation touched: a transfer raises it for both players. A handler may change inventories; those changes arrive on a later tick. Every change of a player who disconnects is raised before `playerDisconnect`. Save from here to keep inventories across sessions -- the server keeps nothing itself.
     */
    playerInventoryChanged: [player: Player, change: InventoryChange];

    /**
     * The player's game spent items: ate, drank, applied or fired them. Raised after the `playerInventoryChanged` that removed them. `item` is the class GUID.
     * 
     * This is the player's own game saying so: the server checked that the units existed and were of a kind that can be spent that way, and took them, but did not see the meal or the shot. A handler that rewards a use -- a heal, a buff -- can be had for the price of the item.
     */
    playerItemUsed: [player: Player, item: string, kind: "food" | "potion" | "ointment" | "shot", amount: number];

    /**
     * A player's game shows their inventory for the first time this session. Their inventory exists from `playerConnect`; restore a saved one there with `Inventory.set`, and it is what they load in with.
     */
    playerInventoryReady: [player: Player];

    /**
     * A move or patrol leg reached its destination, was blocked, or failed. Intermediate path corners do not emit this event.
     */
    horseIntentDone: [horse: Horse, status: string];

    /**
     * Dispatched immediately after a horse is created and replicated, whether by `Horse.spawn`, the `/horse` command, or anything else.
     */
    horseSpawn: [horse: Horse];

    /**
     * Dispatched while a horse is being despawned, including the despawn its owner's disconnect implies. The handle still resolves, so its owner and name can be read one last time.
     */
    horseDestroy: [horse: Horse];

    /**
     * Dispatched after a horse is handed to another player, or left ownerless -- by `giveTo`, or by its owner disconnecting while `destroyWithOwner` is false.
     */
    horseOwnerChanged: [horse: Horse, player: Player | null];

    /**
     * Dispatched after a player climbs into a saddle and the horse's authority has been handed to their client.
     */
    horseMount: [horse: Horse, player: Player | null];

    /**
     * Dispatched after a rider leaves a saddle, including the dismount a disconnect implies, the one a destroyed or dead horse forces, and the one a rider's own death forces.
     */
    horseDismount: [horse: Horse, player: Player | null];

    /**
     * Dispatched when a player has climbed into a saddle, before the server accepts it -- the place to decide who may ride which horse. Return `false` from a handler and the ride is refused: the player's client is told to get back off, and no `horseMount` follows. Handlers run synchronously, so the decision cannot wait on anything awaited.
     * 
     * The player's own game has already started the mount when this runs, so a refusal plays the get-off.
     */
    horseMounting: [horse: Horse, player: Player];

    /**
     * Dispatched when health comes off a horse: a blade, an arrow, a fall. The client running the horse reports it, because its copy of the animal is the one whose soul counts -- a blow resolved on the attacker's machine is handed to it first -- so `amount` is what was really taken. `attacker` is the player who dealt it, or null.
     */
    horseDamage: [horse: Horse, attacker: Player | null, amount: number, reason: "unknown" | "combat" | "gunshot" | "starvation" | "collision" | "scripted" | "disintegrate" | "fall" | "poison" | "bleeding" | "selfHarm"];

    /**
     * Dispatched once when a horse dies, whatever killed it: its body falls where it stood, and anyone in the saddle has already been taken off (with a `horseDismount`). `killer` is the player whose blow took the last of the health, or null. It follows the `horseDamage` of the blow that caused it: the client running the horse reports both, in that order.
     */
    horseDeath: [horse: Horse, killer: Player | null, reason: "unknown" | "combat" | "gunshot" | "starvation" | "collision" | "scripted" | "disintegrate" | "fall" | "poison" | "bleeding" | "selfHarm"];

    /**
     * Dispatched when a player has changed a horse's gear -- in the game's own horse inventory screen -- before the server accepts it. `gear` is what the horse would wear. Return `false` from a handler and the change is refused: every copy of the horse, the player's own included, is dressed back in what it wore before. Not dispatched while `gearLocked` is set, which refuses first, nor for a script's own `setGear`. Handlers run synchronously.
     */
    horseGearChanging: [horse: Horse, player: Player | null, gear: { saddle: string | null; head: string | null; torso: string | null; shoe: string | null }];

    /**
     * Dispatched after a horse's gear changed on every client. `player` is the rider who changed it, or null for a script.
     */
    horseGearChanged: [horse: Horse, player: Player | null, gear: { saddle: string | null; head: string | null; torso: string | null; shoe: string | null }];

    /**
     * Dispatched when a player picks an option. The option's own id comes back, never its index, so a handler stays correct when the page is rebuilt with different rows. Anything the option costs is checked here, not when the page was built: `enabled` on the wire is a rendering hint and the server is what decides.
     */
    dialogueChoice: [session: number, player: Player, optionId: string];

    /**
     * Dispatched when a conversation ends, whoever ended it. 0 completed, 1 the player cancelled, 2 another conversation replaced it, 3 it was interrupted -- a disconnect, most often.
     */
    dialogueClosed: [session: number, player: Player, reason: number];

    /**
     * Dispatched once a deal has settled: everything in it has already moved, in one commit on the player's inventory whose playerInventoryChanged reason is `trade`. `balance` is what the player came out with in money units -- positive when the vendor paid them. What the player sold does not join the vendor's stock; add it with `setStock` here if this vendor resells.
     */
    vendorTrade: [vendor: number, player: Player, bought: VendorTradeLine[], sold: VendorTradeLine[], balance: number];

    /**
     * Dispatched when a trading session ends, whoever ended it. 0 the player closed the screen, 1 the server closed it, 2 another vendor replaced it, 3 the client could not bring the screen up, 4 the player left.
     */
    vendorClosed: [vendor: number, player: Player, reason: number];

    /**
     * Dispatched when a player's charge begins on the game's own Rob entry against another player, before the victim's pockets are opened to them -- the place to decide who may rob whom. Return `false` from a handler and the attempt is refused: the thief's minigame ends before its loot screen opens, and nothing else follows. Handlers run synchronously, so the decision cannot wait on anything awaited.
     * 
     * Whether the victim notices is the game's own rule, played on the thief's machine against the victim's body: it turns on the victim's facing and on both players' skills, exactly as against an NPC.
     */
    playerPickpocketStart: [thief: Player, victim: Player];

    /**
     * Dispatched once a theft has settled: everything in `items` has already left the victim and reached the thief. The thief's loot screen is only a claim -- each line was taken from the victim first and only what really came out was handed over -- so `items` can be shorter than what the thief took, or empty when they closed the screen with nothing. Nothing else happens to either player: whether this is a crime, and what it costs, is the gamemode's.
     */
    playerPickpocketed: [thief: Player, victim: Player, items: PickpocketLine[]];

    /**
     * Dispatched when the victim noticed the thief, while charging or with the loot screen up. Nothing was taken, and no NPC reacts on the victim's behalf: the gamemode decides what being caught means.
     */
    playerPickpocketCaught: [thief: Player, victim: Player];

    /**
     * A new turn begins, including turns after automatic busts.
     */
    diceTurnStart: [event: DiceProgressEvent];

    /**
     * The server dealt a roll. Includes all six face values and the mask rolled this time. When `player` is the NPC on the second seat and `reason` is `rolled`, the decision is the script's: answer with Dice.move.
     */
    diceRoll: [event: DiceProgressEvent];

    /**
     * The server accepted Continue or Pass, from a player or through Dice.move. Invalid or repeated requests emit nothing.
     */
    diceMove: [event: DiceProgressEvent];

    /**
     * A turn ended by banking, busting, winning, or a match ending. A round means one player's turn.
     */
    diceTurnEnd: [event: DiceProgressEvent];

    /**
     * Dispatched once a match has started and its players have been sent to the table. `second` is the Npc when one holds the second seat. Which seat throws first is drawn at random.
     */
    diceMatchStart: [match: number, first: Player, second: Player | Npc, targetScore: number];

    /**
     * Dispatched once a match is over and both players have been told. `winner` is 0 when `first` won, 1 when `second` did, 255 when nobody did. `reason` is `won` (a seat banked the target score), `gaveUp` (a player left the table), `left` (a player disconnected, or the NPC was removed or died), `timedOut` (the seat to throw, player or NPC, did not move for three minutes), `failed` (a client could not play the table) or `stopped` (`Dice.stop`). A player or NPC who has gone since is null. Nothing changes hands on its own: what a match was played for is the gamemode's to settle here.
     */
    diceMatchEnd: [match: number, first: Player | null, second: Player | Npc | null, winner: number, reason: string, firstScore: number, secondScore: number];

    /**
     * Synchronous decision before an unfinished craft settles, including when no native refund is due. Call refund() to request a full refund; return values are ignored. No handler or no call preserves native rules. JavaScript exceptions are logged and do not cancel a call already made. The callback expires on return; delayed calls do nothing. The `cancelled`, `interrupted` and `clientError` reasons are reported by the player's own client: a modified client can send any of them, so refunding on one lets that client keep its materials. Never grants a completed craft twice. craftingEnded follows the actual inventory changes.
     */
    craftingRefunding: [player: Player, proposal: CraftRefundProposal, refund: () => void];

    /**
     * A player asks to open an alchemy table, or to begin a smithing recipe. Every handler runs; one returning literal `false` refuses it. An async handler cannot refuse.
     */
    craftingStarting: [player: Player, proposal: CraftStartProposal];

    /**
     * A batch is brewing, or a workpiece took its materials.
     */
    craftingStarted: [player: Player, event: CraftStartedEvent];

    /**
     * A batch or workpiece finished and its result is computed. Every handler runs; one returning literal `false` refuses it: alchemy ends the batch as failed, what was spent stays spent, and nothing is granted; smithing fails the workpiece, spending its failure share.
     */
    craftingCompleting: [player: Player, proposal: CraftCompletionProposal];

    /**
     * A result was granted, followed by `craftingEnded`. Raised after the `playerInventoryChanged` it caused.
     */
    craftingCompleted: [player: Player, event: CraftCompletedEvent];

    /**
     * A batch or workpiece is over. Raised after the `playerInventoryChanged` of any refund.
     */
    craftingEnded: [player: Player, event: CraftEndedEvent];

    /**
     * Dispatched immediately after a dog is created and replicated, whether by `Dog.spawn` or anything else.
     */
    dogSpawn: [dog: Dog];

    /**
     * Dispatched while a dog is being despawned. The handle still resolves, so its owner and name can be read one last time.
     */
    dogDestroy: [dog: Dog];

    /**
     * Dispatched after a dog is handed to another player, or left masterless.
     */
    dogOwnerChanged: [dog: Dog, player: Player | null];

    /**
     * Dispatched after a dog's companion mode actually changes.
     */
    dogModeChanged: [dog: Dog, mode: number];

    /**
     * Dispatched immediately after a prop is created and replicated, whether by `Prop.spawn`, the `/prop` command, or anything else.
     */
    propSpawn: [prop: Prop];

    /**
     * Dispatched while a prop is being despawned. The handle still resolves, so its model and its pose can be read one last time.
     */
    propDestroy: [prop: Prop];

    /**
     * Dispatched when a `rigid` prop's body stops moving, whatever moved it: a script's `applyImpulse`, `setVelocity`, `wake` or `sleep`, a fall, or a player walking into it. The client simulating it says where, and every player already sees it there. This is that client's word, checked only to lie within 100 m of where the prop last rested or was spawned; a prop reported farther is put back there and raises nothing.
     */
    propRest: [prop: Prop, position: Vector3, rotation: Quaternion];

    /**
     * Dispatched immediately after a cart is created and replicated, whether by `Cart.spawn`, the `/cart` command, or anything else.
     */
    cartSpawn: [cart: Cart];

    /**
     * Dispatched while a cart is being despawned, after everyone in it has been let out. The handle still resolves, so its blueprint and its pose can be read one last time.
     */
    cartDestroy: [cart: Cart];

    /**
     * Dispatched before a player is put in a seat -- by their own use of the cart's prompt, or `putPlayer`. Return `false` from a handler and the seat is refused: nothing changes, and the player's game never climbs in. Handlers run synchronously.
     */
    cartEntering: [cart: Cart, player: Player, seat: string];

    /**
     * Dispatched after a player is given a seat, named as in `cart.seats`; the `driver` seat's client runs the cart from here on.
     */
    cartEnter: [cart: Cart, player: Player | null, seat: string];

    /**
     * Dispatched after a player leaves a seat: their own climb down, `removePlayer`, a disconnect, or the cart being destroyed.
     */
    cartExit: [cart: Cart, player: Player | null, seat: string];

    /**
     * Dispatched immediately after a replicated particle effect is placed, whether by `Vfx.spawn`, a command, or anything else.
     */
    vfxSpawn: [vfx: Vfx];

    /**
     * Dispatched while a replicated particle effect is being stopped. The handle still resolves, so its name and its pose can be read one last time.
     */
    vfxDestroy: [vfx: Vfx];

    /**
     * Dispatched immediately after a ground marker is drawn, whether by `Marker.place`, a command, or anything else.
     */
    markerPlace: [marker: Marker];

    /**
     * Dispatched while a ground marker is being removed. The handle still resolves, so its material and its pose can be read one last time.
     */
    markerRemove: [marker: Marker];

    /**
     * Dispatched when a player walks into a marker whose `trigger` is on. The client detects the crossing and the server confirms it against the position it already replicates, so a claim it does not agree with never reaches here.
     */
    markerEnter: [marker: Marker, player: Player];

    /**
     * Dispatched when a player walks out of a marker whose `trigger` is on. Not raised when the marker is removed, when `trigger` is turned off, or when the player leaves the world -- none of those is the player walking out.
     */
    markerExit: [marker: Marker, player: Player];

    /**
     * Dispatched immediately after an NPC is spawned or adopted, whether by `Npc.create`, a command, or anything else.
     */
    npcSpawn: [npc: Npc];

    /**
     * Its initial inventory has been established. The default stock is censused once by its elected simulator; Inventory.set establishes authored stock immediately.
     */
    npcInventoryReady: [npc: Npc];

    /**
     * An accepted butchering completion. The prepopulated stock is now accessible and every client applies the butchered appearance. Emitted once per life; save Inventory.get(npc) here too to persist the harvested flag.
     */
    npcHarvested: [npc: Npc, player: Player];

    /**
     * Committed stock changes, including loot transfers. Save this stock and restore it with Inventory.set. Harvest completion is reported by npcHarvested.
     */
    npcInventoryChanged: [npc: Npc, change: InventoryChange];

    /**
     * Dispatched while an NPC is being despawned. The handle still resolves, so what it was and where it stood can be read one last time.
     */
    npcDestroy: [npc: Npc];

    /**
     * Dispatched once when an NPC finishes what it was told to do. `status` is `reached` when it arrived, `blocked` when it could not make progress, or `failed` when the order could not be carried out at all, or when a patrol gives up. Server-planned intermediate corners do not raise this event, and neither does a change of simulator: a finished order is not reported twice. A follow, which never finishes, raises `reached` the first time it catches up and again only after a change of simulator. A patrol steps on this event, so a handler that re-orders the NPC here replaces the route rather than racing it.
     */
    npcIntentDone: [npc: Npc, status: string];

    /**
     * Dispatched after health came off the ledger. For melee, the victim's simulator resolves the native hit and the server checks the accepted swing before changing health. `amount` is what was actually taken.
     */
    npcDamage: [npc: Npc, attacker: Player | Npc | null, amount: number];

    /**
     * Dispatched when the last of an NPC's health goes. The body stays as a corpse and the handle keeps resolving.
     */
    npcDeath: [npc: Npc, attacker: Player | Npc | null];

    /**
     * Dispatched when a dead NPC is brought back. Every client makes a new body for it.
     */
    npcRevive: [npc: Npc];

    /**
     * Dispatched once when the animation a `playAnimation` request started is over; `fragment` is the request's. `reason` is `finished`, `interrupted`, `refused`, `stopped` or `replaced`, as for `playerAnimationEnd`. The first three come from the client simulating the NPC. One that nobody simulates plays nothing, so its one-shot ends `refused` as soon as it is asked for, and one cut off by a change of simulator ends `interrupted`: the new simulator does not replay what it did not see start.
     */
    npcAnimationEnd: [npc: Npc, fragment: string, reason: 'finished' | 'interrupted' | 'refused' | 'stopped' | 'replaced'];

    /**
     * Dispatched when a player presses use on an NPC whose `interactable` is on. The client reports the press and the server confirms the distance against the position it already replicates, so a claim it does not agree with never reaches here.
     */
    npcInteract: [npc: Npc, player: Player];

    /**
     * Dispatched when the client running an NPC changes -- somebody walked into range, out of it, or disconnected. `player` is null when it went dormant. Nothing about the NPC changes with it: intent, ledger and identity are the server's, and the new simulator re-derives from them.
     */
    npcSimulatorChange: [npc: Npc, player: Player | null];

    /**
     * One accepted waypoint outcome; intermediate path corners do not emit it.
     */
    patrolWaypoint: [actor: Npc, info: PatrolState & { status: 'reached' | 'blocked' | 'failed' }];

    /**
     * A patrol ended, failed, or was replaced. Edits to its definition do not replace its snapshot.
     */
    patrolFinished: [actor: Npc, info: PatrolState & { status: 'completed' | 'failed' | 'cancelled' }];

    /**
     * Dispatched when a player starts or stops following a quest in the game's journal, whichever way it happened: the track button, the game auto-tracking a quest that turns active, or the untrack that follows finishing or failing one. The client reports it -- following a quest is decided there and cannot be refused here -- so a handler reacts rather than vetoes. It fires only on an actual change, and only for a quest that player was given.
     */
    questTrackingChanged: [quest: Quest, player: Player, tracked: boolean];

    /**
     * Dispatched immediately after a stack is laid in the world and replicated, whether by `GroundItem.spawn`, the `/drop` command, or a player dropping something from their own inventory.
     */
    groundItemSpawn: [groundItem: GroundItem];

    /**
     * Dispatched while a stack is being taken out of the world, including the removal a completed pickup performs. The handle still resolves, so its item and its pose can be read one last time.
     */
    groundItemDestroy: [groundItem: GroundItem];

    /**
     * Dispatched when a player's pickup has been granted and the stack is about to go, so what was taken and by whom can both still be read. It reports a pickup rather than deciding one -- the server has already told that client the stack is theirs by the time this is raised -- and `groundItemDestroy` follows it.
     */
    groundItemPickup: [groundItem: GroundItem, player: Player | null];

    /**
     * A player picked a plant within their reach, and the server is about to give them the herb. Return false to refuse it: nothing is given and the plants stay. The proposal is frozen; every handler runs whatever an earlier one returned, and an async handler cannot refuse.
     */
    gatheringHarvest: [player: Player, proposal: GatheringProposal];

    /**
     * A player was given a herb, after the `playerInventoryChanged` it caused. The plant and the same-kind plants harvested with it are picked for everyone in the virtual world until timed or scripted regrowth; nothing about them survives a restart.
     */
    gatheringHarvested: [player: Player, event: GatheringHarvestedEvent];

    /**
     * Dispatched when a player's client reports working a door, before the server applies it. `action` is `open`, `close`, `unlock` or `lockpick`; a key turned in the same use as the push arrives as `unlock` and then `open`, and refusing the `unlock` refuses the report without asking about the `open`. `keySide` says whether the player stood on the side with the keyhole, which is where an unlock needs a key.
     * 
     * Return false to refuse it: the door is put back on every client, the player's included, and nobody else sees it happen. Every handler runs whatever an earlier one returned, and an async handler cannot refuse. The server has already refused what the game's own rules forbid -- a player out of reach, a door the server locked, opening a locked door, picking a door with no keyhole, a door a script made unusable -- so this only sees what the game would allow.
     * 
     * The player's own client moves the door as soon as they use it and reports afterwards, so a refusal here reaches them a round trip later, as the door swinging back. It is the authority, for what only the server knows and for a client that ignored its own rules. To refuse without that, set `door.usable`, or refuse in the client's own `doorInteract`: both stop the use on the player's machine before anything moves.
     */
    doorInteract: [player: Player, door: Door, action: "open" | "close" | "unlock" | "lockpick", keySide: boolean];

    /**
     * Dispatched immediately after a siege engine is built.
     */
    siegeEngineSpawn: [engine: SiegeEngine];

    /**
     * Dispatched while a siege engine is being removed. The handle still resolves.
     */
    siegeEngineDestroy: [engine: SiegeEngine];

    /**
     * Dispatched when an engine's load cycle finishes and it can fire.
     */
    siegeEngineReady: [engine: SiegeEngine];

    /**
     * Dispatched when a player asks something of a `usable` engine, standing at it: to take its place (`operate`) or a free winch (`winch`) from the game's own prompt, or -- in its place -- to `load` it or `fire` it where they laid it, with the player as the attacker. Return false to refuse. `leave` reports a player stepping away from either and cannot be refused. A handler that works the engine itself should refuse, so it is not worked twice.
     */
    siegeEngineUse: [player: Player, engine: SiegeEngine, action: "operate" | "winch" | "leave" | "load" | "fire"];

    /**
     * Dispatched when a blast takes `amount` off an engine with a `maxHealth`, before it is wrecked by it. `health` already reads what is left.
     */
    siegeEngineDamage: [engine: SiegeEngine, amount: number, attacker: Player | null];

    /**
     * Dispatched when an engine's health runs out. Its operator and crew have been let go, and a stone still in its sling went down with it.
     */
    siegeEngineWreck: [engine: SiegeEngine, attacker: Player | null];

    /**
     * Dispatched the moment an engine lets its projectile go, partway through the shot `fire` started.
     */
    siegeFire: [engine: SiegeEngine, attacker: Player | null, target: Vector3];

    /**
     * Dispatched where a projectile came down: what the reporting player -- the attacker when near enough, else whoever is nearest the target -- saw it hit, checked against the arc it was thrown on, or the target itself when nobody could see it. Players within `damageRadius` have already been told to take their share, and NPCs have taken theirs. `engine` is null when it was destroyed while the stone was in the air.
     */
    siegeImpact: [engine: SiegeEngine | null, position: Vector3, attacker: Player | null];

    /**
     * Dispatched immediately after a siege ladder is placed.
     */
    siegeLadderSpawn: [ladder: SiegeLadder];

    /**
     * Dispatched while a siege ladder is being removed. The handle still resolves.
     */
    siegeLadderDestroy: [ladder: SiegeLadder];

    /**
     * Dispatched when a player asks to push a `usable` ladder from the walk at its top, or to raise a fallen one from beside its foot. Return false to refuse.
     */
    siegeLadderUse: [player: Player, ladder: SiegeLadder, action: "push" | "raise"];

    /**
     * Dispatched when a pushed ladder hits the ground. Anyone who was on it was thrown off as it began to fall, and took the game's own fall.
     */
    siegeLadderFall: [ladder: SiegeLadder, pusher: Player | null];

    /**
     * Dispatched when a raised ladder leans on its wall again and can be climbed.
     */
    siegeLadderRaise: [ladder: SiegeLadder];

    /**
     * Dispatched immediately after a stone pile is placed.
     */
    stonePileSpawn: [pile: StonePile];

    /**
     * Dispatched while a stone pile is being removed. The handle still resolves.
     */
    stonePileDestroy: [pile: StonePile];

    /**
     * Dispatched when a player takes a stone off a pile. The game's own stone throwing has already lifted it and heaves it over the wall at once, so it cannot be refused; make the pile not `usable` to stop the next one.
     */
    stonePileThrow: [player: Player, pile: StonePile];

    /**
     * Dispatched where a thrown stone first met something, as its thrower saw it, checked to lie within 30 m of its pile. Players within the pile's `damageRadius` have been told to take their share, and NPCs have taken theirs.
     */
    stoneImpact: [pile: StonePile | null, position: Vector3, attacker: Player];

    /**
     * Dispatched when a body comes to be inside an enabled area: it walked or rode in, or the area was created, moved, reshaped or enabled around it. The server decides this itself from the pose it already replicates, by the game's own rule for that kind of area, so a client cannot claim it. `matchingVirtualWorld` is false when a script area bound to one world is crossed by a body in another; a level area is in every world.
     */
    areaEnter: [area: Area, entity: Player | Horse | Cart | Npc, matchingVirtualWorld: boolean];

    /**
     * Dispatched when a body stops being inside an area: it walked out, or the area was moved, reshaped, disabled or destroyed. A body that leaves the server entirely -- a player disconnecting, a horse despawned -- raises nothing here; its own event already says it is gone.
     */
    areaExit: [area: Area, entity: Player | Horse | Cart | Npc, matchingVirtualWorld: boolean];

    /**
     * Raised once after every configured world export has loaded successfully, including an empty list. WorldResource.ready is true inside the handler. A script starting later must check WorldResource.ready first; the event is not replayed. Do not await this event inside resourceStart: exports load after script startup. Handler promises are not awaited.
     */
    worldResourcesReady: [];

    /**
     * A container now exists: a scripted chest, a virtual stash, or one the level places, built in a virtual world the first time anything there asked for it. Every container starts empty; restore saved contents here with `setInventory`. The level's containers in the global world are built before any resource runs, so restore those from `resourceStart` by walking `Stash.all()`.
     */
    stashSpawn: [stash: Stash];

    /**
     * A player holds a container view. Physical chests report the native opening; virtual stashes report the server grant before requesting the native screen.
     */
    stashOpen: [stash: Stash, player: Player];

    /**
     * A player no longer has a container open: they closed it, walked away, died or changed world (`lostAccess`), sent nothing for two minutes, left, or the container went.
     */
    stashClose: [stash: Stash, player: Player, reason: "closed" | "lostAccess" | "timeout" | "disconnected" | "destroyed"];

    /**
     * What a container holds changed. `player` moved the units -- `change.reason` is `deposit` or `withdraw`, and their inventory raises `playerInventoryChanged` too -- or is null for a script's `addItem`, `removeItem` or `setInventory`, or `destroy` when the container went with something in it. Save from here to keep contents across restarts: the server keeps nothing itself. A linked child's `stash` is the one the player used; its stock is its master's.
     */
    stashInventoryChanged: [stash: Stash, player: Player | null, change: InventoryChange];

    /**
     * A container is about to go. Raised after its close and its contents' removal, while it can still be read.
     */
    stashDestroy: [stash: Stash];

    /**
     * Dispatched for a physical chest prompt or virtual stash open(player), before access is granted. For a prompt: the player's client holds the game's own handler until the server answers. `action` is `open`, `unlock` -- a key turned, which opens it in the same use -- or `lockpick`, the minigame starting; a successful pick then opens it without asking again.
     * 
     * Return false to refuse it: the prompt does nothing, and no transfer screen, key or minigame ever appears. Every handler runs whatever an earlier one returned, and an async handler cannot refuse. The server has already refused what the game's own rules forbid -- a player out of reach, a container another player has open, opening a locked one, picking one that cannot be -- so this only sees what the game would allow. Virtual opening has no distance check; world, player readiness, lock and exclusive access checks still apply. Closing is never asked.
     */
    stashInteract: [player: Player, stash: Stash, action: "open" | "unlock" | "lockpick"];

    /**
     * Dispatched when a player submits a plain chat line. Turn `Chat.setDefaultRelay(false)` off to own delivery yourself.
     */
    playerChat: [player: Player, text: string];

    /**
     * Dispatched when a player's chat line begins with `/` and no built-in command claimed it. A `/` line is never relayed to anyone else.
     */
    playerCommand: [player: Player, command: string, args: string[]];

    /**
     * Dispatched when the world clock crosses midnight, whether it walked over or `World.setTime` jumped past. The day it carries is the one that just began.
     */
    worldDayChange: [day: number];

    /**
     * Dispatched when the sky starts blending to another time-of-day preset, from `World.setWeather` or the `/world weather` command. It is raised when the blend starts, not when it settles.
     */
    worldWeatherChange: [preset: string, previous: string, seconds: number];

    /**
     * Dispatched when a player's swing meets something solid that is not a body: a tree, a wall, a rock, a prop, the ground. Once per thing per attack, at the first contact, as the game itself counts it. `hit` is named as a trace names it -- `category` tells a tree from a rock, and `levelObject` is what `LevelEdit.apply` takes to fell or break it -- with `distance` measured from the player's feet. It is that player's client's word, checked only to lie within 8 m of them and in the server's level, and at most 16 a second are taken from one player. Fists and kicks are not reported, and neither is a blow on a player, an NPC or an animal, which `playerHit` and the damage events cover.
     */
    playerWeaponHit: [player: Player, hit: WorldRayHit];

    /**
     * A native custom effect instance attached. A refresh and each additional stack raise this event after the owner reports them.
     */
    playerCustomBuffApply: [player: Player, buff: BuffInfo];

    /**
     * A reported active custom effect reached its tickInterval on the server clock. At most one callback per instance per server update; missed ticks are skipped. Native modifiers and duration use the owner's simulation clock.
     */
    playerCustomBuffTick: [player: Player, buff: BuffInfo];

    /**
     * One custom instance left the body. Expired also covers native removal. Disconnect clears lifecycle callbacks for this connection.
     */
    playerCustomBuffRemove: [player: Player, buff: BuffInfo, reason: "server" | "expired" | "refreshed" | "disconnect"];

    /**
     * Dispatched when a status effect appears on a player's body. `source` says whether this server added it; `native` means the game did -- a potion they drank, an injury they took. It fires for whatever they are already carrying when their client first reports in, so a handler sees the full picture without asking for it.
     */
    playerBuffAdded: [player: Player, buff: string, source: "server" | "native"];

    /**
     * Dispatched when a status effect leaves a player's body. `server` is a removal this server asked for; `expired` is everything else -- it ran out, or the game replaced it. Effect timers run on each player's own machine, so the server never expires one itself.
     */
    playerBuffRemoved: [player: Player, buff: string, reason: "server" | "expired"];

    /**
     * Dispatched when the game tried to give a player an effect of a kind this server claimed, and their client turned it down. This is the other half of `Buffs.claim`: the drink was still drunk and the item still consumed, so the handler decides what really happens -- usually `player.addBuff` with the resource's own rule applied. Nothing raises it until something is claimed, and repeats of the same effect are limited to twice a second per player.
     */
    playerBuffBlocked: [player: Player, buff: string];

    /**
     * Dispatched when a player's own game produced XP -- a hit landed, a lock picked, a page read -- and asks for it. Return `false` to refuse it; the gain then never happens. `xp` is before this server's rate and the player's own multipliers. `source` is the game's name for what caused it: `Attack`, `LockpickingResult` and so on, or empty. Nothing is asked while `Progression.setNativeXp(false)` is in force.
     */
    playerXpGaining: [player: Player, track: string, xp: number, source: string];

    /**
     * Dispatched when XP landed on a player's character, from their game or from this server, after every multiplier.
     */
    playerXpGained: [player: Player, track: string, xp: number];

    /**
     * Dispatched when one of a player's tracks reaches a new level. `perkPoints` is what that track's tree now has unspent.
     */
    playerLevelUp: [player: Player, track: string, level: number, perkPoints: number];

    /**
     * Dispatched when a player confirmed a perk on the perk screen and every rule of the game allows it: the tree's level, the parent perk, the exclusive partner, a point to spend, and this server's block list. Return `false` to refuse it; the point is not spent and their client raises `progressionPerkRefused`.
     */
    playerPerkLearning: [player: Player, perk: string];

    /**
     * Dispatched when a perk appeared on a player's character: learnt on the perk screen, given by this server, or granted by the game on its own -- a codex entry, a recipe, a combat move it teaches.
     */
    playerPerkAdded: [player: Player, perk: string, source: "learned" | "server" | "native"];

    /**
     * Dispatched when a perk left a player's character: taken or respecced by this server, or removed by the game.
     */
    playerPerkRemoved: [player: Player, perk: string, source: "server" | "native"];

    /**
     * Dispatched when a player's client reported progression this server never ordered: a track that rose further than any gain it granted could take it, or a perk-screen perk nobody learnt. The report is kept -- the game has no way to take a level back -- so what happens to the player is the handler's call; a kick is the usual answer.
     */
    playerProgressionRejected: [player: Player, reason: string];

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
     * Dispatched for a line typed into the server console that no built-in command (help, ensure, refresh, ...) claimed. `args` is the rest of the line, split on whitespace. The console is the operator's, so this is the place for commands no player may run.
     */
    consoleCommand: [command: string, args: string[]];

    /**
     * Dispatched when a player asks to join, before the connection exists: they hold no player slot, are not counted as online, and are sent nothing -- no resource list, no download, no body. The request waits until every handler has returned and every Promise a handler returned has settled, then it is let in if a player slot is free (otherwise it is refused as full). `connection.reject()` turns it away instead, and so does a handler that throws or rejects, or handlers that have not settled within the server's admission timeout (30 seconds by default, restarted by every `connection.update()`). With no handler at all, every request is let in at once.
     */
    playerConnecting: [connection: PendingConnection];
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
   * The pace rule a server puts on one player. Both halves default to off, and a call to `setMovementMode` states both of them.
   */
  interface MovementMode {
    /**
     * Whether walking, rather than jogging, is the pace this player keeps coming back to. A preference: they start each body walking and return to it after every sprint, and their own toggle key works normally in between.
     */
    walkByDefault: boolean;

    /**
     * Whether this player may not run or sprint at all. A rule rather than a preference: it holds the engine's own movement permissions, so the toggle key and the sprint key both stop raising the pace, and lifting it hands back whatever the body had before.
     */
    walkEnforced: boolean;
  }

  /**
   * One number per damage type a blow is worked out in. A blade's stab, slash and smash are priced separately and added; a blow runs only the passes its attack and weapon call for.
   */
  interface DamagePasses {
    /**
     * The stab pass.
     */
    stab: number;

    /**
     * The slash pass.
     */
    slash: number;

    /**
     * The smash pass.
     */
    smash: number;
  }

  /**
   * Scales a `playerHit` handler puts on the server's price of a melee blow, each 1 until a handler changes it. The server works the blow out again with them, step by step as the game does: a heavier attack has to get through the same armour and the same block, so doubling `attack` does not simply double the damage. Never negative.
   */
  interface PlayerHitModifiers {
    /**
     * Scales the attacker's attack in every pass, after their weapon, strength, skill and the swing itself.
     */
    attack: number;

    /**
     * Scales the victim's armour where the blow landed.
     */
    defense: number;

    /**
     * Scales what the victim's block put up. Nothing for a blow that was not blocked.
     */
    block: number;

    /**
     * Scales the health the blow takes, once everything else is worked out. The game's own cap of 200 a blow is applied before it, so this can go past it.
     */
    health: number;
  }

  /**
   * A player or NPC blow, as `playerHit` shows it before the referee rules. `damage` is what the blow will take and `modifiers` scale how the server works it out; a handler may change either, from any resource, and the next handler sees the change. Every other field describes what happened and is read-only in effect. It is live only while the handlers run: one kept for later reads `damage` and `modifiers` as undefined and changes nothing.
   */
  interface PlayerHit {
    /**
     * Health the blow takes. For melee between players the server works it out itself, the way the game does, from the attacker's weapon and its wear, both players' strength and skills, the armour the victim wears where it landed, the block and the perks and buffs it can vouch for; for NPC melee or an arrow it is what the victim's own game worked out. Set it and it stands as set: `modifiers` are then ignored. Never negative.
     */
    damage: number;

    /**
     * Health the game itself took for the blow on the machine that saw it land. Matches `damage` unless a perk that depends on the moment -- a heavy weapon's, a first strike's -- was in play, which the server leaves out.
     */
    engineDamage: number;

    /**
     * Whether the server worked `damage` out itself: true for a swing, false for an arrow.
     */
    priced: boolean;

    /**
     * The attacker's weapon as 32 hex digits, or null for a bare hand.
     */
    weapon: string | null;

    /**
     * The attacker's attack in each pass, after the swing's own strength and before `modifiers`.
     */
    attack: DamagePasses;

    /**
     * The victim's armour in each pass where the blow landed, before `modifiers`.
     */
    armor: DamagePasses;

    /**
     * What the victim's block put up, or 0 when nothing blocked it.
     */
    blockDefense: number;

    /**
     * How much the place it landed multiplies the damage by: 1.5 for the head, 1 for the torso, 0.7 for a limb.
     */
    bodyPartCoefficient: number;

    /**
     * Scales on the server's price; see `PlayerHitModifiers`. Ignored for an arrow.
     */
    modifiers: PlayerHitModifiers;

    /**
     * Stamina the blow cost the victim. Already spent: stamina decides their next block, which cannot wait for a ruling.
     */
    stamina: number;

    /**
     * The limb it landed on, or null for none in particular.
     */
    bodyPart: "head" | "torso" | "leftArm" | "rightArm" | "leftLeg" | "rightLeg" | null;

    /**
     * The game's own reason for the damage: `combat` for a blade or a bow.
     */
    reason: "unknown" | "combat" | "gunshot" | "starvation" | "collision" | "scripted" | "disintegrate" | "fall" | "poison" | "bleeding" | "selfHarm";

    /**
     * Whether it was a swing or an arrow.
     */
    source: "melee" | "missile";

    /**
     * The victim's block caught it.
     */
    blocked: boolean;

    /**
     * The victim's perfect block caught it. The game takes nothing for one.
     */
    perfectBlock: boolean;

    /**
     * The victim's block gave way under it.
     */
    blockBroken: boolean;
  }

  /**
   * Which of a player's limbs carry an injury, one flag per limb. An injury is the game's own: it lowers the stamina ceiling (`healthyStamina`), can bleed, and heals slowly by itself or at once with a bandage or `player.heal`.
   */
  interface Injuries {
    /**
     * Whether the head is injured.
     */
    head: boolean;

    /**
     * Whether the torso is injured.
     */
    torso: boolean;

    /**
     * Whether the left arm is injured.
     */
    leftArm: boolean;

    /**
     * Whether the right arm is injured.
     */
    rightArm: boolean;

    /**
     * Whether the left leg is injured.
     */
    leftLeg: boolean;

    /**
     * Whether the right leg is injured.
     */
    rightLeg: boolean;
  }

  /**
   * A connected KCDC player and the body they occupy.
   */
  class Player {
    /**
     * Creates a script wrapper for an existing connected player with this ID; it neither connects nor spawns anyone.
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
     * The player's main level, the one the game derives from the stats, as their client last reported it. 0 until the first report.
     */
    readonly level: number;

    /**
     * The level of every skill and stat, keyed by track name, as this server last accepted it. All zero until the first report.
     */
    readonly levels: TrackLevels;

    /**
     * Every perk the player owns, by name.
     */
    readonly perks: string[];

    /**
     * Everything about this player's progression, as plain JSON to store however the resource likes. A player starts every session with a fresh character, so `restoreProgression` with a stored snapshot is how progression outlives a disconnect. Null until their client's first report.
     */
    readonly progression: ProgressionSnapshot | null;

    /**
     * Whether this player is in a saddle.
     */
    readonly mounted: boolean;

    /**
     * The player whose body this one carries on their shoulder, or null. Set once `playerCarry` has fired, cleared once `playerPutDown` has.
     */
    readonly carrying: Player | null;

    /**
     * The player carrying this one's body, or null.
     */
    readonly carriedBy: Player | null;

    /**
     * The item this player carries in their arms, by its item name, or null. Set once `playerCarryItem` has fired, cleared once `playerPutDownItem` has.
     */
    readonly carriedItem: string | null;

    /**
     * The horse this player is riding, or null when they are on foot.
     */
    readonly horse: Horse | null;

    /**
     * The dog companion this player has, or null when they have none. One dog per player, as in the game.
     */
    readonly dog: Dog | null;

    /**
     * The status effects on this player's body, as their own client last reported them: potions, poison, injury, drunkenness, illness, unconsciousness, and anything this server added. Perks and equipment effects are not in here -- they follow state that already replicates.
     * 
     * This is the list of *named effects*. For how drunk, poisoned, hurt or tired someone actually is, read `drunkenness`, `poisoning`, `bleeding`, `consciousness`, `hunger` and `exhaust` instead: those are live numbers and need no name to look up.
     * 
     * Empty until the player's client sends its first report, shortly after they connect; `playerBuffAdded` fires for whatever it was already carrying.
     */
    readonly buffs: BuffState[];

    /**
     * Which limbs are injured, as the player's own client last reported them -- the same report `buffs` reads, named by limb so nobody has to know the six buff names. All false until the first report arrives. `playerInjured` and `playerInjuryHealed` fire as a flag changes.
     * 
     * What an injury does is the game's own: a hurt head, torso or arm weakens its stats, a hurt leg takes away running and sprinting, and a badly hurt limb bleeds. Everybody else sees the consequences rather than the injury: the owner's slower pace, the lower stamina ceiling, bleeding, and the hurt gait the game plays once health is low. The game has no leg-specific limp animation, so there is nothing more to show.
     */
    readonly injuries: Injuries;

    /**
     * Formats this player handle for logging and debugging.
     * @returns The player's network entity ID and nickname.
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
     * Where this player stands on one track: level, XP towards the next one, and unspent perk points.
     * @param track A track name, from `Progression.tracks()`.
     * @returns The progress, or null before their client's first report. Throws for a track name the tables do not carry.
     */
    getTrack(track: string): TrackProgress | null;

    /**
     * Whether this player owns a perk.
     * @param perk A perk's name or GUID.
     * @returns True when their last report carried it.
     */
    hasPerk(perk: string): boolean;

    /**
     * Grants XP on one track, through the game's own path on the player's client -- so their perks' XP multipliers apply, a level crossed pops the game's own level-up, and a level grants its perk points. Rates, budgets and caps do not apply: those rule over what a player earns, and this is the server giving.
     * @param track A track name. Not `fencing`: the game derives it from the weapon skills.
     * @param xp XP, in the game's own units.
     * @returns True when the order went out. Throws for an unknown track or an xp that is not positive.
     */
    addXp(track: string, xp: number): boolean;

    /**
     * Sets an exact level and clears its XP remainder, including on a live player. Increases use native XP effects and grant points; decreases notify native property listeners and recompute induced skills. Decreasing does not remove perks or revoke points: change those explicitly, or restore a complete snapshot.
     * @param track A track name. Not `fencing`.
     * @param level Exact level, from zero to the track cap; resets XP inside that level.
     * @returns True when the order went out; false for a level past the cap.
     */
    setLevel(track: string, level: number): boolean;

    /**
     * Sets a track's total earned XP exactly, including reductions inside a level and across levels. Does not revoke perks or unspent points.
     * @param track A progression track other than fencing.
     * @param xp Absolute cumulative XP from level zero, in game XP units. Quantized down to 1/128 XP; cannot exceed the track cap.
     * @returns True when the order went out; false for an unavailable player or XP above the cap.
     */
    setXp(track: string, xp: number): boolean;

    /**
     * Subtracts unscaled cumulative XP from the reported track plus pending orders. Requires the first progression report. Level and remainder can both decrease; perks and points stay unchanged.
     * @param track A progression track other than fencing.
     * @param xp Positive XP to remove, at least 1/128. Clamps at zero total XP.
     * @returns True when the order went out.
     */
    removeXp(track: string, xp: number): boolean;

    /**
     * Gives this player a perk outright, spending no point and skipping the perk screen's requirements. It still has to be one the game can add: an owned or blocked perk is turned down by the game itself.
     * @param perk A perk's name or GUID.
     * @returns True when the order went out. Throws for an unknown perk.
     */
    addPerk(perk: string): boolean;

    /**
     * Takes a perk away. The point it cost is not refunded; `respecPerks` is the way to hand points back.
     * @param perk A perk's name or GUID.
     * @returns True when the order went out. Throws for an unknown perk.
     */
    removePerk(perk: string): boolean;

    /**
     * Gives this player unspent perk points in one tree, on top of what their levels earned.
     * @param track The tree to add to: a track name, or `main` for the main-level bank.
     * @param count How many, from 1 to 65535.
     * @returns True when the order went out.
     */
    addPerkPoints(track: string, count: number): boolean;

    /**
     * The game's own respec: every perk learnt on the perk screen is taken away and every tree's points are rebuilt from the player's current levels. Perks the game granted on its own stay.
     * @returns True when the order went out.
     */
    respecPerks(): boolean;

    /**
     * Restores the supplied tracks exactly, including lower levels and XP remainders on live players. Supplied perks replace the reported perk set, and supplied point banks are set after progression events drain. Omitted sections stay unchanged. The entire snapshot is validated before any order is sent. Fencing is recomputed from weapon skills. A restore is not a gain: a raised track shows no level-up popup and grants no perk points.
     * @param snapshot What `player.progression` returned, as the resource stored it.
     * @returns True when every order went out.
     */
    restoreProgression(snapshot: ProgressionSnapshot): boolean;

    /**
     * Requests revival of this player after playerDied.
     * @returns True when sent; false when disconnected, no death was reported, or revival was already requested.
     */
    revive(): boolean;

    /**
     * Ends melee combat between these two players and retires their pending blows against each other. Other opponents are left alone. A later attack can start combat again. Consent and timeout rules belong to your resource.
     * @param opponent The other player. Both sides are cancelled.
     * @returns True when sent; false for disconnected players, the same player, or different virtual worlds.
     */
    cancelCombat(opponent: Player): boolean;

    /**
     * Puts this player somewhere, as a spawn rather than as a teleport: their client holds the body still until there is real ground under it, so it cannot fall through a world that has not streamed in yet.
     * 
     * Called from a `playerSpawning` handler this *is* the answer to that request -- the player is still behind their loading screen, and nothing is seen. Called at any other time it moves a player who is already in the world, which is visible.
     * 
     * Nothing else names a spawn: with no handler calling this, everybody arrives at the level's own start point, because every client asks the game for the identical map start.
     * @param position Where the body goes; a Vector3 or any object carrying x, y and z.
     * @param rotation Which way they face: a Quaternion, or a Vector3 of Euler degrees.
     * @returns True when the placement was accepted; false for a position that is not somewhere in the world, or a player with no connection to ask.
     */
    spawn(position: Vector3 | Partial<Vector3>, rotation?: Quaternion | Vector3): boolean;

    /**
     * Asks this player's own client to put them somewhere else. The owning client is authoritative for its body's pose, so this is a request that lands on their next frame rather than a write.
     * @param position World-space destination; a Vector3 or any object carrying x, y and z.
     * @param label Optional name for the destination, echoed back in the client's own teleport panel. Display only.
     * @returns True when the request went out; false when the position is not finite or the player has no connection to ask.
     */
    teleport(position: Vector3 | Partial<Vector3>, label?: string): boolean;

    /**
     * Makes this player's body play one of the game's animations, on their own screen and on everybody else's -- a wave, a bow, a woodcutter's swing with the axe in hand. It is state rather than a one-off: a player who streams in or joins while it plays sees it too, and it lasts until `stopAnimation` or the next `playAnimation`, even after a one-shot has finished.
     * 
     * A new `playAnimation`, `stopAnimation` and `teleport` cut a running animation at once rather than waiting for it to finish. The game can cut it short too: a hit, a fall or drawing a weapon ends it, and so can the player walking off from one that leaves the legs free. `loop` starts it again when that happens, and keeps a one-shot going pass after pass with no gap between them.
     * 
     * `playerAnimationEnd` fires once when the animation this request started is over, and says how. A loop ends only by being stopped or replaced.
     * 
     * A hand tag is only half of holding something. `r_bucket` makes the body move as if it carried a bucket, but the bucket itself is a `props` entry.
     * @param fragment A Mannequin fragment, as `Animations.list` names it. `""` plays none and only puts the props and tags on, which with a hand tag like `r_bucket` makes the player carry something while they walk.
     * @param options `clip` plays a clip the server streams instead of a fragment (`animations/kcdc/<resource>/wave.caf`, the fragment left `""`), `tags` pick the variant, `loop` repeats it until stopped, `lockMovement` holds the player still, `props` puts up to two models in their hands.
     * @returns True when the request went out; false for a player with no body yet. Throws for a prop or option it cannot use.
     */
    playAnimation(fragment: string, options?: { clip?: string; tags?: string; loop?: boolean; lockMovement?: boolean; props?: { item?: string; model?: string; hand?: 'right' | 'left'; joint?: string; position?: Vector3 | Partial<Vector3>; rotation?: Quaternion | Vector3 | Partial<Vector3> } | { item?: string; model?: string; hand?: 'right' | 'left'; joint?: string; position?: Vector3 | Partial<Vector3>; rotation?: Quaternion | Vector3 | Partial<Vector3> }[] }): boolean;

    /**
     * Ends what `playAnimation` started at once, takes its props away and hands the body back to the game. A stance tag the animation set goes with it, so without `standUp` a seated player pops upright.
     * 
     * The stand-up is an animation of its own: it raises its own `playerAnimationEnd`, fragment `StandUp` or `GetUp`, once the player is on their feet, and a `playAnimation` during it replaces it.
     * @param options `standUp` plays the game's own way back to standing when the animation held the body in a sitting or lying stance tag (`sittingNoTable`, `lyingGround`): `StandUp` or `GetUp`, in the variant that fits that stance. Without it, or for a stance the game authors no way out of (`beggarKneel`), the body goes straight back to standing.
     * @returns True when the request went out; false for a player with no body yet.
     */
    stopAnimation(options?: { standUp?: boolean }): boolean;

    /**
     * Sets this player's facial expression, on their own screen and on everybody else's, alongside whatever their body plays. It is state: a player who streams in sees it, and it lasts until the next call.
     * 
     * The expression is the eyes, brows and mood. It never moves the mouth -- the game's own `FE_DialogueSpeaking` animates only the eyes and leaves the mouth to the voice line being spoken. `setTalking` is the mouth.
     * 
     * A mood tag is one of the body's own tags, so it can change the mood of the body's idle too, for as long as the expression holds.
     * @param fragment A fragment on the face's own scope: `FE_Default`, `FE_DialogueIdle` or `FE_DialogueSpeaking` for a held mood, or an `ADLG_FA_*` gesture (`ADLG_FA_Smile`, `ADLG_FA_Wink`, `ADLG_FA_Laugh`, `ADLG_FA_Surprise`...) that plays once. `""` hands the face back to the game.
     * @param options `tags` pick the variant: for the `FE_*` fragments the mood, `happy`, `angry`, `sad`, `nervous`, `pensive`, `arogant` or `drunk`, as `Animations.list("FE_")` spells them.
     * @returns True when the request went out; false for a player with no body yet. Throws for an argument it cannot use.
     */
    setFacialExpression(fragment: string, options?: { tags?: string }): boolean;

    /**
     * Moves this player's mouth as if they were talking, on their own screen and on everybody else's, until told to stop -- for text chat, voice chat or a scene. It is a looping talk animation, not lip-sync: it follows no words.
     * 
     * The game's own lip-sync wins while it plays a voice line on the same body, and the loop comes back after it.
     * @param talking Whether the mouth moves.
     * @param style How: `neutral` (the default), `happy`, `drunk` or `chew`.
     * @returns True when the request went out; false for a player with no body yet. Throws for a style it does not know.
     */
    setTalking(talking: boolean, style?: 'neutral' | 'happy' | 'drunk' | 'chew'): boolean;

    /**
     * Asks this player's own client to wear a different body -- face, hair, beard and skin. The owning client is authoritative for its body, so this is a request that lands on their next frame rather than a write; read `player.appearance` back to see what they actually put on.
     * 
     * Gender is applied before the parts, because it decides which half of the game's catalog the names come from -- and it is the one part of this with a price. A soul's gender lives on its archetype, so changing it moves the player to the game's own counterpart archetype, which also carries four authored numbers: base armour, the conspicuousness and visibility pair, and unarmed attack. Nothing reads those on the puppets other people see, but on the body its owner plays they are a real change. Ask for a gender only when you mean it; a male-to-male change never touches the archetype.
     * 
     * Everyone starts as Henry, so a server that wants people to tell each other apart has to call this.
     * 
     * Face and beard travel together whether or not both are named. Beards are modelled per face, so changing either into a pair that was never modelled is refused whole rather than applied without the beard; `Appearances.beards` says which pairs are real.
     * @param appearance The parts to change. Anything left out keeps what the player is wearing, and an empty string hands that part back to the game. Names come from `Appearances.options`, except the beard, which comes from `Appearances.beards`.
     * @returns True when the request went out; false for a name that is not in the catalog, a gender it does not belong to, a beard that face cannot wear, or a player with no connection to ask.
     */
    setAppearance(appearance: Partial<Appearance>): boolean;

    /**
     * Grants or revokes World Builder access for this player. Multiplayer connections start without access. Enabling allows F7 and the client MapEditor.open API; disabling also closes an open editor. The permission lasts until changed or disconnected and does not affect other players. Offline editing is always allowed.
     * @param enabled Whether this player may open World Builder.
     * @returns True when the permission was sent; false for a player with no connection. Throws unless enabled is a boolean.
     */
    setWorldBuilderEnabled(enabled: boolean): boolean;

    /**
     * Puts a pace rule on this player. `walkByDefault` makes walking the pace they keep coming back to and leaves their own key working; `walkEnforced` forbids running and sprinting outright, and their key stops mattering.
     * 
     * The two are different things and both are worth having: a default is a preference, an enforcement is a rule. Nothing about either goes out to anybody else -- the pace every other player draws comes from this body's own published animation tags, so a walking player already looks like one everywhere.
     * 
     * A default holds across sprints. The game itself ends every sprint in a jog, deliberately, so the client puts the walk back once the sprint is over rather than during it, and a player who chose to jog keeps jogging: only the pace the sprint took is given back. On a gamepad the stick's own deflection decides the pace, which the rule neither reads nor overrides.
     * 
     * Enforcement holds the engine's own run and sprint permissions, the same pair the game clears while somebody carries a body, and hands back what it found when the rule is lifted. Sprinting cannot shake it off.
     * @param mode The whole rule. A key left out is off, because the server keeps no copy of what this player is currently under.
     * @returns True when the rule went out; false for a player with no connection to ask.
     */
    setMovementMode(mode: Partial<MovementMode>): boolean;

    /**
     * Grants items into this player's inventory, which the server holds; their game shows them on the next tick and announces them with its own "You received" toast. `Inventory.add` does the same, can set the items' properties, and can leave out the toast.
     * @param item Item class GUID, or the exact name the game's own item tables use.
     * @param amount How many to grant; defaults to 1, and at most 10000.
     * @returns True when the items were added; false for an unknown item, an amount outside 1..10000, or a player with no inventory.
     */
    giveItem(item: string, amount?: number): boolean;

    /**
     * Takes items of a class out of this player's inventory, across its rows, all of them or none. The server holds the inventory, so the promise is already settled when it is returned; it stays a promise so existing scripts keep working. Their game announces the loss with its own toast; `Inventory.remove` can leave it out. To move items between players use `Inventory.transfer`, which cannot lose them half way.
     * @param item Item class GUID, or the exact name the game's own item tables use. The same spelling `giveItem` takes.
     * @param amount How many units to take; defaults to 1, and at most 10000. Zero is refused rather than read as "all of them".
     * @returns An object carrying `removed` (the amount when it happened, otherwise 0), `requested`, `ok`, and `reason` (empty on success, otherwise the inventory code, such as `insufficientItems`).
     */
    takeItem(item: string, amount?: number): Promise<{ removed: number; requested: number; ok: boolean; reason: string }>;

    /**
     * Moves this player's items into a container spawned at their body, as a death drop: `playerInventoryChanged` (reason `drop`) and `stashInventoryChanged` follow, then the container is anyone's to open. At most 128 rows go. What happens to it next -- who may loot it, when it goes -- is the script's.
     * @param options `keepEquipped` keeps the units their body wears or holds.
     * @returns The container, or null when there was nothing to drop or the player has no inventory.
     */
    dropInventory(options?: { keepEquipped?: boolean }): Stash | null;

    /**
     * Dresses this player in one of the game's own outfits. Every piece of clothing they wear comes off and stays in their inventory, and a new set of the outfit's clothes is added and worn; weapons and everything else are left as they are. Their game dresses them a moment later, as it does after `Inventory.set`.
     * @param preset One of the game's own outfits for this player's gender, by a name from `Player.outfitPresets(player.appearance.gender)` or its GUID.
     * @returns True when the inventory was changed; false for a preset that is not in the catalog or is the other gender's, a player with no inventory, or an inventory with no room.
     */
    setOutfit(preset: string): boolean;

    /**
     * Reads this player's inventory; the same as `Inventory.get(player)`.
     * @returns A copy of it, or null for a player who is not connected.
     */
    getInventory(): InventoryState | null;

    /**
     * Puts a status effect on this player's body. The server runs no effects of its own, so this asks their client rather than writing anything, and it lands a moment later -- `hasBuff` right after this call still says false. Watch `playerBuffAdded` for the moment it is really on.
     * 
     * The ask is not a promise, either: the game refuses an effect that conflicts with one already there, and a second drink folds into the first rather than stacking.
     * @param buff Buff GUID, or the exact name the game's own buff tables use. `Buffs.find` resolves either.
     * @returns True when the instruction went out; false for an unknown buff or a player with no connection to ask.
     */
    addBuff(buff: string): boolean;

    /**
     * Asks this player's own client to take a status effect off: the exact instance when a report named one, and every instance of that definition otherwise.
     * @param buff Buff GUID, or the exact name the game's own buff tables use.
     * @returns True when the instruction went out; false for an unknown buff or a player with no connection to ask.
     */
    removeBuff(buff: string): boolean;

    /**
     * Clears every effect in one of the game's own families at once: one call for all poisons, or all bleeding, without naming them. `Buffs.tags` lists the families. Note that `World.setTime` does not fast-forward effects -- their time runs off each client's frame delta and never reads the calendar -- so a scripted sleep has to clear what it means to end.
     * @param tag An effect family, from `Buffs.tags` -- `poison`, `bleed`, `alcohol_drunk`, `unconscious`.
     * @returns True when the instruction went out; false for a tag no buff table uses or a player with no connection to ask.
     */
    clearBuffs(tag: string): boolean;

    /**
     * Whether this player's last report carried that status effect.
     * @param buff Buff GUID, or the exact name the game's own buff tables use.
     * @returns True when it did; false for an unknown buff, or a player who has reported nothing yet.
     */
    hasBuff(buff: string): boolean;

    /**
     * Sets health, stamina, energy or nourishment on the living body. Call once player.ready is true. Lowering health raises playerDamage with a null attacker; a fatal change also raises playerDied with a null killer. Zero health can kill. Does not clear buffs or revive; normal regeneration and consumption continue.
     * @param stat Writable PlayerStat; derived stats are rejected.
     * @param value Finite native value from 0 to 1000000, clamped to the native maximum. Positive health must exceed 0.00001; positive values must not underflow to zero.
     * @returns True when sent, not confirmation of application; false for a missing, unready or dead body. Throws for invalid arguments or a read-only stat. Requests for a previous life are discarded. Read getStat after the client's next report to observe the result.
     */
    setStat(stat: "health" | "stamina" | "exhaust" | "hunger", value: number): boolean;

    /**
     * Sets this living player's health, raising or lowering it. Lowering raises playerDamage with a null attacker and the amount removed; a fatal change also raises playerDied with a null killer. This includes restoring a lower saved value. Zero can kill; this never revives a dead player. Leaves injuries, poison and bleeding in place. Call after playerSpawned or playerReady when restoring a saved value. The client applies the request asynchronously; health continues to show its last report until a new one arrives.
     * @param value Health in the same units as player.health. Clamped to maxHealth on the player's client.
     * @returns True when sent; false when the body is missing, not ready or dead. Throws unless value is a finite number from 0 to 1000000, with positive health greater than 0.00001 after conversion to native float units. A request for a life that has since ended is ignored.
     */
    setHealth(value: number): boolean;

    /**
     * Sets this living player's nourishment, raising or lowering it. Call after playerSpawned or playerReady when restoring a saved value. The client applies the request asynchronously; hunger continues to show its last report until a new one arrives.
     * @param value Nourishment in the same units as player.hunger. Higher means better fed. Clamped to maxHunger on the player's client.
     * @returns True when sent; false when the body is missing, not ready or dead. Throws unless value is a finite number from 0 to 1000000 that does not round from positive to zero in native float units. A request for a life that has since ended is ignored.
     */
    setHunger(value: number): boolean;

    /**
     * Nurses this player back, on their own client and with the game's own recipe -- the one its quests use for a full heal: the `remove_injuries` and `remove_all_posions` cures, which each wipe their whole kind of effect as they land, then health raised through the soul's own setter, the way a potion raises it. What comes off raises `playerInjuryHealed` and `playerBuffRemoved` as usual.
     * 
     * It does not revive anybody: a dead player stays dead until `revive`.
     * @param options What to restore; everything when left out. `health`: true or absent for all of it, a number to raise it to that much (never lowers it), false to leave it. `injuries`: clear every limb injury, and with it the bleeding a badly hurt limb causes. `poisons`: clear every poison. `bleeding`: clear the standalone bleeding effect.
     * @returns True when the orders went out; false for a player with no connection to ask. Throws for a health that is not a finite, non-negative number.
     */
    heal(options?: { health?: number | boolean; injuries?: boolean; poisons?: boolean; bleeding?: boolean }): boolean;

    /**
     * Draws this player as a static mesh instead of themselves -- a barrel, a haystack, a cart wheel -- on their own screen and on everybody else's. It is state: a player who streams in or joins sees it too, and it lasts until the next call.
     * 
     * Only the look and the collision change. The body is hidden rather than replaced, and its collision cylinder is resized around the mesh, so the player walks and is traced against in roughly the mesh's shape -- a client's `World.raycast` with `mode: "anything"` finds them where the mesh is drawn, and says which player it found. The mesh turns with the body. They keep everything else a person has: they can still be hurt, bleed and die, and nothing stops them drawing a weapon, which a gamemode that does not want that has to take away.
     * 
     * The game's own camera sits in the head, which the mesh now covers: pair this with the client's `Camera.setThirdPerson` for the disguised player. Nametags are not touched; `setNametagVisible` is the call for that. NPCs still see a person.
     * 
     * The owning client is authoritative for its body, so this is a request that lands on their next frame; `player.disguise` reads what they actually have on.
     * @param model A mesh from the prop catalog, by its `objects/...cgf` path or its file stem, or one this server streams by its full path -- anything `Prop.spawn` takes. Null puts the player back in their own body.
     * @returns True when the request went out; false for a player with no connection to ask. Throws for a model the catalog does not carry.
     */
    setDisguise(model: string | null): boolean;

    /**
     * Stops drawing this player's body, or draws it again -- on their own screen and on everybody else's. The body goes with everything on it: what it wears, the weapons on its back and belt, and whatever it is holding. It is state, like a disguise: somebody who streams in or joins sees nothing of a hidden player either, and it lasts until the next call or until they leave.
     * 
     * Nothing but the drawing changes. They stand where they stood, keep their collider, their soul and their weapons, so a hidden player still blocks a doorway, can still be hit and traced against, still fights and still dies -- and the NPCs around them never stopped seeing a person. Nametags are separate; `setNametagVisible` is the call for that.
     * 
     * Their own camera sits in their head, so a hidden player simply sees no arms in first person. The owning client is authoritative for its body, so this is a request that lands on their next frame; `player.visible` reads what they actually have.
     * @param visible False to stop drawing this player's body, true to draw it again.
     * @returns True when the request went out; false for a player with no connection to ask. Throws unless `visible` is a boolean.
     */
    setVisible(visible: boolean): boolean;

    /**
     * Takes this player out of whatever saddle they are in: their own client gets them off, and `horseDismount` is raised.
     * @returns The horse they were taken off, or null when they were not riding one.
     */
    dismount(): Horse | null;

    /**
     * Has this player put down the body on their shoulder, with the game's own put-down. It is an instruction to their client, so the carry ends when that put-down lands: `playerPutDown` follows then, and `carrying` reads null from that moment.
     * @returns True when the order went out; false when they carry nobody or have no connection.
     */
    putDown(): boolean;

    /**
     * Has this player carry an item in their arms, the way the game's own people carry firewood, sacks and baskets: the carrying walk, and the put-down at the end. While it is carried they walk, since the game has no running with a load.
     * 
     * It is an instruction to their client, so the carry lands when that client reports it: `playerCarryItem` and `carriedItem` follow. A carry a script ordered is not put to `playerCarryingItem`. The item never enters their inventory. Put down -- with the game's own key or `putDownItem` -- it falls from their hands and becomes a carryable `GroundItem` where it lands, which `playerPutDownItem` hands over and anybody can take up again.
     * 
     * Their hands have to be free -- a drawn weapon or a torch makes their game refuse the carry, and nothing is reported.
     * @param item What to carry. A name from `Animations.props()` whose `hand` the game has a carrying walk for -- the baskets (`basket_full_wood`, `basket_b_apples`, `stonebasket`, `eggbasket`, `shoppingbasket`), `firewoodChipsHand`, the buckets (`waterBucket`, `milkBucket`), and for a man only the sacks (`sack`, `sack_miller`), `barrel` and `crate_with_silver` -- puts a new one straight into their hands. A `GroundItem` of such a class has them pick that one up off the ground with the game's own bend-down; it has to be a single item within their reach.
     * @returns True when the order went out; false when they cannot act, are riding, already carry an item or a body, the stack is not resting within their reach or somebody else is taking it, or they have no connection. Throws for an item with no carrying walk, or none for this player's body.
     */
    carryItem(item: string | GroundItem): boolean;

    /**
     * Has this player put down the item in their arms, with the game's own put-down. It is an instruction to their client, so the carry ends when that put-down lands: `playerPutDownItem` follows then.
     * @param options `immediate` lets go at once, without the put-down.
     * @returns True when the order went out; false when they carry no item or have no connection.
     */
    putDownItem(options?: { immediate?: boolean }): boolean;

    /**
     * Puts this player in a horse's saddle. Everyone near enough to watch sees it the way it was asked for -- the get-on, or the instant seat -- while a client that only streams the rider in later finds them already seated. It is an instruction to their client, so the seat is reported back like any other mount: `horseMounting` can still refuse it, and `horseMount` and `player.horse` follow once it lands. The player has to be standing near the horse; teleport them beside it first.
     * @param horse The horse to get on.
     * @param options `instant` puts them straight into the seat, as the game's own forced mount does; by default they play the get-on a player plays at a horse.
     * @returns True when the order went out; false when the horse is dead or somebody else is riding it, or the player has no connection.
     */
    mount(horse: Horse, options?: { instant?: boolean }): boolean;

    /**
     * The game's own outfits, from its clothing presets: what `player.setOutfit` and `Npc.setOutfit` dress a body in. Many belong to one named character.
     * @param gender Only the outfits authored for this gender. The game will not put a man's clothes on a woman or the reverse, so `player.setOutfit` takes only the presets of the player's own `appearance.gender`.
     * @returns Preset names, men's first, each gender's in name order.
     */
    static outfitPresets(gender?: "male" | "female"): string[];

    /**
     * Lists every player currently connected, including those whose body has no pose yet.
     * @returns One handle per connected player, in no particular order.
     */
    static all(): Player[];

    /**
     * Looks a player up by their network entity ID.
     * @param id Network entity identifier.
     * @returns The player's handle, or null when no connected player has that ID.
     */
    static getById(id: number): Player | null;
  }

  interface Player extends BasePlayer {}

  /**
   * A server-validated serving. Item properties and pot poison come from server state; changing the snapshot does not change the decision.
   */
  interface Consumption {
    /**
     * Where the serving comes from.
     */
    source: "inventory" | "cookPot";

    /**
     * Native food or potion item class GUID.
     */
    item: string;

    /**
     * Native food type; potions use the game's food type 1.
     */
    kind: "food" | "potion";

    /**
     * Native table inputs before item condition, spoilage, perks or soul modifiers. Health effects may run over time. These are not promised final stat deltas. Null when the catalog has no row.
     */
    baseEffects: { buff: string | null; health: number; energy: number; nourishment: number; alcohol: number } | null;

    /**
     * Inventory row, or null for a pot meal.
     */
    rowId: string | null;

    /**
     * Cooking pot GUID, or null for an inventory item.
     */
    potGuid: string | null;

    /**
     * Server item properties, including condition. Empty for a pot meal.
     */
    metadata: Record<string, unknown>;

    /**
     * Additional cooking pot poison buff GUID, or null. Spoilage and the item's own effects are part of nativeEffects.
     */
    poison: string | null;

    /**
     * Whether native nutrition, energy, alcohol, potion and spoilage effects are allowed. Final in playerConsumed.
     */
    nativeEffects: boolean;
  }

  /**
   * Items a player's game dried on a drying rack, smoked in a smokehouse or cooked at a fire, and what the server is about to make of them. Frozen: a handler can refuse it, not change it.
   */
  interface Preparation {
    /**
     * A drying rack dries herbs and food; a smokehouse smokes food; a fire cooks food.
     */
    kind: "dry" | "smoke" | "cook";

    /**
     * The drying rack, smokehouse or fire the player stands at, as `<level>:<guid>`; the same on every machine and across restarts. A fire's guid is its cook pot's, as `CookPot.get` takes it.
     */
    station: string;

    /**
     * Each inventory row about to be taken: `amount` units of class `item`, which become class `product`.
     */
    items: { id: string; item: string; amount: number; product: string }[];
  }

  /**
   * One candidate from the game's animal loot preset; probability and inventory population belong to the gamemode.
   */
  interface AnimalLootItem {
    /**
     * Item class GUID.
     */
    item: string;

    /**
     * Maximum authored unit count.
     */
    amount: number;

    /**
     * Authored quantity fraction, not a drop probability.
     */
    fraction: number;

    /**
     * Uniform variation around the quantity fraction.
     */
    variation: number;

    /**
     * The item is food.
     */
    food: boolean;

    /**
     * Tagged as a special Harvester part in the game data. This is metadata; the gamemode decides eligibility.
     */
    harvester: boolean;
  }

  /**
   * A count of units from one row.
   */
  interface InventoryUnit {
    /**
     * The row.
     */
    id: string;

    /**
     * How many of its units.
     */
    amount: number;
  }

  /**
   * A stack of one item class with one set of properties. Two rows never hold the same class with equal properties unless a restore put them there. Stackable custom rows merge only with matching properties. Nonstackable custom rows each contain one unit.
   */
  interface InventoryRow {
    /**
     * The row's identity within its owner's inventory. Stable while the row exists; save it with the row and pass it back to `Inventory.set`.
     */
    id: string;

    /**
     * Item class GUID.
     */
    item: string;

    /**
     * Internal table name, or Items.register id for a custom type. Display overrides are in metadata.custom.name.
     */
    name: string;

    /**
     * Units in the row.
     */
    amount: number;

    /**
     * Quality, from 1 up to the class's maximum. Same as `metadata.quality`.
     */
    quality: number;

    /**
     * Absolute item health. Same as `metadata.health`.
     */
    health: number;

    /**
     * Health as a fraction of what this quality allows, 0 to 1. Same as `metadata.condition`.
     */
    condition: number;

    /**
     * Units the body wears or holds: reported by a player, authored stock for an NPC. Reads include them; pass them to `Inventory.set` to dress a restored body.
     */
    equipped: number;

    /**
     * Every property the server keeps: `quality`, `health`, `condition`, and on missiles `poison` (a buff GUID) and `poisonCharges`, and `onEquipBuffs` (buff GUIDs). Custom items also carry `custom: { name?, description?, data? }`. Data stays server-side. A default is left out. Pass it back as it is to keep an item exactly.
     */
    metadata: Record<string, unknown>;
  }

  /**
   * One player's or NPC's inventory as the server holds it.
   */
  interface InventoryState {
    /**
     * For a player, their game shows this revision. For an NPC, its initial stock has been established. Wait for `playerInventoryReady` or `npcInventoryReady` respectively.
     */
    ready: boolean;

    /**
     * Advances by one with every change. Pass it as a request's `revision` to refuse the request if anything changed in between.
     */
    revision: number;

    /**
     * Every row.
     */
    items: InventoryRow[];

    /**
     * NPC stock only: the carcass has already been harvested. Pass it to Inventory.set when restoring a corpse; absent preserves its existing state.
     */
    harvested: boolean | undefined;

    /**
     * The current native outfit's quick-access assignments and selections, using row IDs. Null until reported or restored. Save with items and pass both to Inventory.set.
     */
    quickslots: InventoryQuickslots | null;
  }

  /**
   * The current outfit's weapon and item quickslots. Indices are zero-based; each array has four entries. Empty references are null.
   */
  interface InventoryQuickslots {
    /**
     * Four weapon slots, each with primary and secondary row IDs. A row may appear in several slots.
     */
    weapons: { primary: string | null; secondary: string | null }[];

    /**
     * Four item slots, containing row IDs or null. Save equipped belts and pouches with the inventory rows to retain slot capacity.
     */
    items: (string | null)[];

    /**
     * Selected weapon slot, 0 through 3.
     */
    activeWeapon: number;

    /**
     * Selected item slot, 0 through 3.
     */
    activeItem: number;
  }

  /**
   * What one operation did to one inventory.
   */
  interface InventoryChange {
    /**
     * The revision the operation produced.
     */
    revision: number;

    /**
     * `add`, `remove`, `properties`, `set`, `transfer`, `wear` when the player's game wore an item down in use, `use` for food, potions and ointments, `shot` for a fired round, `pickpocket`, `trade` for a vendor deal, `repair` for a repair kit, `sharpen` for a grindstone, `dry`, `smoke` and `cook` for a drying rack, a smokehouse and a fire, `drop` for `player.dropInventory`, `loot`, `npcDeposit`, `harvest`, `outfit`, or the ground, stash and gathering reasons. `equipment` reports an equipment or quickslot change: items is empty, the item revision does not advance, and Inventory.get returns the new snapshot.
     */
    reason: string;

    /**
     * Only the rows that changed. `before: null` is a new row and `after: null` an emptied one.
     */
    items: { id: string; before: InventoryRow | null; after: InventoryRow | null }[];
  }

  /**
   * What an inventory operation did.
   */
  interface InventoryResult {
    /**
     * It happened. Nothing changes when it did not.
     */
    ok: boolean;

    /**
     * Empty on success, otherwise why not: `inventoryUnavailable`, `invalidRequest`, `staleRevision`, `invalidItem`, `invalidItems`, `invalidAmount`, `unknownItem`, `insufficientItems`, `inventoryCapacity`, `sameInventory`, `invalidEquipment`, or a property policy code (`invalidMetadata`, `unknownItemClass`, `questItem`, `invalidQuality`, `immutableItemHealth`, `invalidItemHealth`, `contradictoryItemHealth`, `invalidCreationSentinel`, `unsupportedPoisonProperties`, `unsupportedOnEquipBuffs`).
     */
    code: string;

    /**
     * The revision after the operation; for a transfer, the source's. 0 when refused.
     */
    revision: number;

    /**
     * The rows that received units: the row an `add` merged into or created, a transfer's rows in the target, every row a `set` wrote. Empty for `remove`.
     */
    items: InventoryUnit[];
  }

  /**
   * Player and NPC inventories. The server holds the stock and clients project it. Native NPC default stock is censused once by its elected simulator; `Inventory.set` establishes scripted stock immediately. Other NPC writes require ready stock. Every operation happens at once and in full, or not at all. `revision` is optional everywhere: give the one you read to refuse the operation when the inventory changed since.
   */
  const Inventory: {
    /**
     * Lists every equippable item the server knows: the game's own and those the running resources' KCD2 mods add through their item table patches. Names are table identifiers; nameKey is the native localization key. Maximum quality is the same bound Inventory.add enforces.
     */
    classes(): { id: string; name: string; nameKey: string; category: string; maximumQuality: number; quest: boolean }[];

    /**
     * Reads a player's or NPC's inventory.
     * @returns A copy, or null for an owner that no longer exists.
     */
    get(owner: Player | Npc): InventoryState | null;

    /**
     * Gives an owner items of a class, 1 to 10000 at a time. `item` is the class GUID or the exact name the game's own item tables use, the same spelling `giveItem` takes. Left-out properties mean quality 1 at full condition; `metadata.quality` asks for a higher tier, up to what the class is made in, and `invalidQuality` refuses one past it. The units join the row that already holds this class with these properties, if there is one. A player's game announces them with its own "You received" toast unless `notify` is false.
     */
    add(owner: Player | Npc, request: { item: string; amount: number; metadata?: Record<string, unknown>; revision?: number; notify?: boolean }): InventoryResult;

    /**
     * Takes exact units from named rows, every one of them or none. A player's game announces the loss with its own toast unless `notify` is false.
     */
    remove(owner: Player | Npc, request: { units: InventoryUnit[]; revision?: number; notify?: boolean }): InventoryResult;

    /**
     * Updates only named custom fields. Null resets a display override or clears data. Supplied data replaces the old object. The whole row keeps its identity, amount and native properties.
     */
    updateCustom(player: Player, request: { id: string; name?: string | null; description?: string | null; data?: Record<string, unknown> | null; revision?: number }): InventoryResult;

    /**
     * Replaces a row's properties. The row keeps its identity and amount. It replaces rather than merges: carry over anything you want to keep, and give `health` or `condition`, not two that disagree.
     */
    setProperties(owner: Player | Npc, request: { id: string; metadata: Record<string, unknown>; revision?: number }): InventoryResult;

    /**
     * Replaces an owner's whole inventory, which is how a saved one comes back. It is never announced in the player's game. Rows keep the ids they are given (letters, digits and `-_:.`, up to 64) and get new ones when they have none. `equipped` is how many units of a gear row the body wears, up to 48 in all; their game dresses in them. Optional quickslots restores assignments and selected slots in the current native outfit; every referenced ID must occur in items. Invalid shape or missing references returns invalidQuickslots before changing anything. Omitted or null quickslots leaves assignment to the game's normal equipment handling. Native item eligibility and slot capacity still apply. An empty list clears the inventory.
     */
    set(owner: Player | Npc, request: { items: { id?: string; item: string; amount: number; metadata?: Record<string, unknown>; equipped?: number }[]; quickslots?: InventoryQuickslots | null; revision?: number; harvested?: boolean }): InventoryResult;

    /**
     * Moves exact units between players or NPCs with their properties, all or nothing. They join matching rows in the target, so the target's row ids are the ones in the result's `items`. The script decides access, distance and consent. Players' games announce what they lost and gained unless `notify` is false.
     */
    transfer(source: Player | Npc, target: Player | Npc, request: { units: InventoryUnit[]; sourceRevision?: number; targetRevision?: number; notify?: boolean }): InventoryResult;

    /**
     * What the game prices one unit of an item at, in money units, worked out the way the game does it. `pristine` is the price at the best health its quality allows, `current` at its own health. Quality changes the price only through that health. The metadata reads as `Inventory.add` reads it -- left out, quality 1 at full condition -- so `Inventory.getItemPrice(row.item, row.metadata)` prices a row. This is the item's own worth: what the game's shopkeepers would ask depends on their terms and the haggling, and a `Vendor` charges whatever its script says.
     * @param metadata The item's properties, as a row's `metadata` holds them.
     * @returns Null for a class the server does not know, or metadata `Inventory.add` would refuse.
     */
    getItemPrice(item: string, metadata?: Record<string, unknown>): { pristine: number; current: number } | null;

    /**
     * Changes what every player's game calls an item, in their inventory, the shops, loot and anywhere else the game names it; null gives the game its own name back. Every player sees it, including those who join later, until the server stops.
     * 
     * The game does not name classes one by one: many classes show the same name -- every kite shield is a Kite Shield -- and renaming one renames all of them. The result lists them. A name is up to 128 bytes with no control characters, and is not translated.
     * @returns The GUIDs of every class that now shows this name.
     */
    setItemName(item: string, name: string | null): string[];

    /**
     * Rewrites the combat numbers of an item class on every player's game, now and for whoever joins later, until the server stops; null gives the class its authored numbers back. The game keeps these on the class, not the item -- every `longsword` shares one row, and the hit calculation reads it on every swing -- so a change here reaches every copy of that class at once and takes effect on the next hit, with nothing to re-equip. A set that names only some stats leaves the others as authored, and replaces the previous set whole.
     * 
     * Which names a class takes depends on its table element: a `MeleeWeapon` has `attack`, `attackModStab`/`Slash`/`Smash`, `defense`, `strReq`, `agiReq`; `Armor`, `Helmet` and `Hood` have `defenseStab`/`Slash`/`Smash`, `noise`, `strReq`; a `MissileWeapon` has `power`, `limbResistance`, `defense`, `strReq`, `agiReq`; `Ammo` has `attackStab`/`Slash`/`Smash`, `powerMod`. Any other name, a name the element lacks, or a class that is none of these throws. Per-player or per-item numbers are not what this does: for those, put a buff on the body (`player.addBuff`, `horse.addBuff`) with a `weaponAttack`, `weaponDefense` or `attackSpeed` modifier from `Buffs.register`, or register a class of your own with `Items.register`.
     * @returns True when the numbers were written; false when the class carries none of these stats.
     */
    setItemStats(item: string, stats: { attack?: number; attackModStab?: number; attackModSlash?: number; attackModSmash?: number; defense?: number; strReq?: number; agiReq?: number; defenseStab?: number; defenseSlash?: number; defenseSmash?: number; noise?: number; power?: number; limbResistance?: number; attackStab?: number; attackSlash?: number; attackSmash?: number; powerMod?: number } | null): boolean;

    /**
     * What this server has rewritten on a class with `setItemStats`, by stat name. The authored numbers are not listed: the server does not carry the game's tables.
     * @returns The overridden stats, or null for a class left as authored.
     */
    getItemStats(item: string): Record<string, number> | null;
  };

  /**
   * Server-defined resource items, drawn with an existing game appearance or with their own model and icon. Register types before restoring their instances.
   */
  const Items: {
    /**
     * Registers an immutable miscellaneous type and returns its stable GUID. Identical repeats succeed; conflicts throw. Defaults: empty description, price 0, stackable true, usable false, useLabel Use. visual names a miscellaneous item, crafting material or document whose model, material and icon the type borrows; without it, model and icon are required. model is a path relative to Objects/, as the game's item tables write it, which the game prefixes with Objects/ itself (a path starting with objects/ throws) and completes with .cgf when it has no extension: a model the resource streams as stream/objects/kcdc/<resource>/box.cgf is kcdc/<resource>/box.cgf. It draws the item on the ground, in hand and in the inventory preview, and needs a physics proxy for the item to be picked up from the ground: a model without one shows but cannot be picked up. material is a material path in the same form, allowed only with model; without it the model's own material is used. icon is a base64 PNG of at most 256 KiB and 4096x4096 pixels; it is scaled to the stock 64x64 icon size, aspect kept.
     */
    register(id: string, definition: { name: string; description?: string; visual?: string; model?: string; material?: string; icon?: string; weight: number; price?: number; stackable?: boolean; usable?: boolean; useLabel?: string }): string;
  };

  /**
   * Replicated KCD2 horse handle.
   */
  class Horse {
    /**
     * Creates a script wrapper for an existing horse with this ID; use Horse.spawn() to spawn one.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * The current route's status: idle, running, reached, blocked, or failed.
     */
    readonly navigationStatus: string;

    /**
     * The effects this server has put on the horse, as `addBuff` names them: the table name of a native effect, the registered id of a custom one, in the order they were added.
     */
    readonly buffs: string[];

    /**
     * What this horse wears, by slot: `saddle` (the saddlebags are part of it, and so is the carrying capacity they add), `head` (a bridle or chanfron), `torso` (a caparison), `shoe`. Item class names, null for an empty slot. A spawned horse wears the game's own tack for it unless `Horse.spawn` said otherwise. The gear changes the horse's stats as well as its look.
     */
    readonly gear: { saddle: string | null; head: string | null; torso: string | null; shoe: string | null };

    /**
     * While true, players' own changes to this horse's gear are refused and put back, before `horseGearChanging` is asked. Scripts can still change it. False by default.
     */
    gearLocked: boolean;

    /**
     * GUID of the soul this horse was spawned against, which decides its appearance.
     */
    readonly soul: string;

    /**
     * Name every client shows for this horse. Assignment renames it on all of them; empty means the name its soul carries stands. A rider keeps the name it mounted with until it dismounts.
     */
    name: string;

    /**
     * What this horse can carry, in the game's own weight units, or 0 before any client has reported it. Derived by the engine, so read-only.
     */
    readonly inventoryCapacity: number;

    /**
     * Network ID of the player in the saddle, or 0 when the horse is riderless.
     */
    readonly riderId: number;

    /**
     * The player in the saddle, or null when the horse is riderless.
     */
    readonly rider: Player | null;

    /**
     * Whether a player is currently riding this horse.
     */
    readonly mounted: boolean;

    /**
     * Network ID of the player this horse belongs to, or 0 when it is nobody's. Not necessarily its rider.
     */
    readonly ownerId: number;

    /**
     * The player this horse belongs to, or null when it is nobody's. Set with `giveTo`.
     */
    readonly owner: Player | null;

    /**
     * Whether this horse is despawned when its owner disconnects. True by default. Set it to false to keep the horse in the world: it is left ownerless instead (`horseOwnerChanged` with a null player) until `giveTo` hands it to someone. Saving it across server restarts is the script's own business.
     */
    destroyWithOwner: boolean;

    /**
     * Health, as the client running this horse last reported it off the animal's own soul -- 0 before any client has. Assigning sets it on that client, clamped to `maxHealth`; 0 kills. A dead horse ignores the write: `revive` is what brings one back.
     */
    health: number;

    /**
     * The most health this horse can have, from its soul; 0 before any client has run it.
     */
    readonly maxHealth: number;

    /**
     * Stamina, as its client last reported it -- the bar the game draws for a ridden horse, spent by galloping. Read-only: the game runs it.
     */
    readonly stamina: number;

    /**
     * The most stamina this horse can have; 0 before any client has run it.
     */
    readonly maxStamina: number;

    /**
     * False once the horse has died. A dead horse is still an entity: its body stays where it fell, nobody can ride it, and `revive` or `destroy` are the ways out.
     */
    readonly alive: boolean;

    /**
     * True while the client running this horse reports it knocked out: alive, but lying where it fell. The game knocks a horse out rather than killing it when it holds the horse immortal, as it holds a player's own horse. The horse comes round by itself when the knockout wears off, some minutes later, as in the game, or at once with `wake`. False for a dead horse, and before any client has run this one.
     */
    readonly unconscious: boolean;

    /**
     * Formats this horse handle for logging and debugging.
     * @returns The horse ID, its soul, and its rider.
     */
    toString(): string;

    /**
     * Despawns this horse on every client after emitting horseDestroy.
     */
    destroy(): void;

    /**
     * Routes an unridden horse over the server navigation mesh. Mounting cancels the order. Returns false without a mesh, for a dead or mounted horse, or invalid input.
     * @param position Destination in world coordinates.
     * @param options Defaults to walking and a 1.5 metre arrival radius.
     */
    moveTo(position: Vector3, options?: { speed?: 'walk' | 'jog' | 'run'; radius?: number }): boolean;

    /**
     * Patrols using the NPC route state machine. Requires a loaded mesh and an alive, unridden horse. Mounting cancels the patrol.
     * @param points One to 256 waypoints.
     * @param options Defaults to walking, looping, and no wait.
     */
    patrol(points: Vector3[], options?: { speed?: 'walk' | 'jog' | 'run'; loop?: boolean; waitSeconds?: number }): boolean;

    /**
     * Cancels navigation and holds an unridden horse where it stands. Does not take control from a rider.
     */
    hold(): void;

    /**
     * Makes this horse another player's. Every client makes it that player's horse in the game's own terms -- theirs is never a stolen horse -- and `horseOwnerChanged` follows. Ownership decides nothing about who may ride it: that is `horseMounting`'s to refuse. Throws when `player` is not a connected player.
     * @param player The horse's new owner, or null to leave it ownerless.
     */
    giveTo(player: Player | null): void;

    /**
     * Rears the horse on every client so it throws its rider off. The game's own animation, not a pose.
     * @returns True when the request went out; false when nobody is riding it.
     */
    rearAndThrowDown(): boolean;

    /**
     * Has a player pull this horse's rider down, on every client.
     * @param attacker The player dragging the rider out of the saddle.
     * @returns True when the request went out; false when nobody is riding it.
     */
    pullDownRider(attacker: Player): boolean;

    /**
     * Takes whoever is in the saddle out of it: their own client is told to get off, and `horseDismount` is raised.
     * @returns The player taken off, or null when the saddle was already empty.
     */
    dismountRider(): Player | null;

    /**
     * Kills this horse on the client running it, the way any other death happens there: the body falls, anyone riding it comes off, and `horseDeath` follows with a null killer and the reason `scripted`.
     * @returns True when the horse was alive; false when it was already dead.
     */
    kill(): boolean;

    /**
     * Brings a dead horse back where it lies, at its full health.
     * @returns True when the horse was dead; false when it was alive.
     */
    revive(): boolean;

    /**
     * Brings a knocked-out horse round now, the way the game cuts a knockout short: the client running it ends the knockout and the horse gets up on every client. Its health is left as it is -- a knockout in place of a death leaves 1. The request reaches that client a moment later, so `unconscious` stays true until it reports the horse up.
     * @returns True when the request went out; false when the horse is not knocked out, is dead, or no client is running it.
     */
    wake(): boolean;

    /**
     * Dresses this horse in exactly this gear on every client, the one running it included: what it wore before is taken off. Throws on an item that is not horse gear, one keyed under the wrong slot, or a caparison (`torso`) with no saddle under it. Not subject to `gearLocked` or `horseGearChanging`, which govern players; `horseGearChanged` follows with a null player.
     * @param gear A preset from `Horse.gearPresets()` (`horse_noble05`), or item class names or GUIDs by slot from `Horse.gearItems()`, where a missing key or null leaves that slot empty; `{}` is a bare horse.
     * @returns True when the gear was written; false when the horse is gone.
     */
    setGear(gear: string | { saddle?: string | null; head?: string | null; torso?: string | null; shoe?: string | null }): boolean;

    /**
     * Puts one piece of gear in its own slot, replacing what was there and keeping the rest.
     * @param item An item class name or GUID from `Horse.gearItems()`.
     * @returns True when it was put on; false for an item that is not horse gear, a caparison on a horse with no saddle, or a horse that is gone.
     */
    equipGear(item: string): boolean;

    /**
     * Takes one piece of gear off. Removing the saddle removes the caparison too, since it lies on the saddle.
     * @param slot The slot to empty.
     * @returns True when something was taken off; false when the slot was already empty.
     */
    removeGear(slot: 'saddle' | 'head' | 'torso' | 'shoe'): boolean;

    /**
     * Puts a status effect on this horse, on every client: the one running it, every observer's copy and whoever joins later. A horse's stats are its soul's derived stats and the game itself moves them with buffs -- horseshoes carry `rsb+0.04`, the race boosts `mst+30,rms+0.01`, horse plate `courage+5,mst+10` -- so this is how a server changes a horse's speed (`runSpeed`, `moveSpeed`), stamina (`stamina`), courage (`courage`) or carrying capacity (`carryCapacity`); `Buffs.register` builds an effect from those modifiers. The list is state rather than a one-off order: a copy that loses the effect, or a body re-spawned after a level change, is given it again. Throws for a name that is not a buff.
     * @param buff Buff GUID, the exact name the game's own buff tables use, or the id of one registered with `Buffs.register`.
     * @returns True when it is on the horse's list, already or now; false for a horse that is gone or one already carrying sixteen.
     */
    addBuff(buff: string): boolean;

    /**
     * Takes one of the server's effects off this horse, on every client. Effects the game put on the horse itself are not on the list and are left alone.
     * @param buff Buff GUID, table name or registered id.
     * @returns True when it was on the list; false otherwise.
     */
    removeBuff(buff: string): boolean;

    /**
     * Whether this server has put that effect on the horse. The server's own record, so it answers at once; it says nothing about effects the game gave the horse.
     * @param buff Buff GUID, table name or registered id.
     * @returns True when it is on the list.
     */
    hasBuff(buff: string): boolean;

    /**
     * Takes every effect this server put on the horse off again.
     * @returns True when there was something to take off.
     */
    clearBuffs(): boolean;

    /**
     * Spawns and replicates a horse.
     * @param position Optional world-space spawn position; omitted components default to zero.
     * @param rotation Optional initial orientation: a Quaternion, or a Vector3 of Euler angles in degrees.
     * @param soul Optional breed from `Horse.breeds()` (`pebbles`), or a soul GUID from the game's tables; omitted spawns the generic riding horse.
     * @param name Optional name every client shows for the horse; omitted leaves the name its soul carries.
     * @param gear Optional gear the horse is born wearing on every client, as `setGear` takes it; `{}` spawns it bare. Omitted, it wears the game's own tack: a named horse's own preset, or one of the generic ones (`horse_common01`, `horse_common02`).
     * @returns The newly spawned horse handle.
     */
    static spawn(position?: Vector3 | Partial<Vector3>, rotation?: Vector3 | Quaternion, soul?: string, name?: string, gear?: string | { saddle?: string | null; head?: string | null; torso?: string | null; shoe?: string | null }): Horse;

    /**
     * The game's own sets of horse tack, from its clothing presets: the generic `horse_common`, `horse_noble`, `horse_nomad` and `horse_draft` families, and the named horses' own. Any of them goes to `setGear` or `Horse.spawn` by name.
     * @returns Preset names, alphabetical.
     */
    static gearPresets(): string[];

    /**
     * Every item class a horse can wear, from the game's own tables: saddles, bridles and chanfrons, caparisons and trappings, horseshoes.
     * @param slot Only the gear for this slot; omitted lists all of it.
     * @returns Item class names, saddles first.
     */
    static gearItems(slot?: 'saddle' | 'head' | 'torso' | 'shoe'): string[];

    /**
     * The horses `Horse.spawn` knows by name: every horse the game gives a character of its own -- the ones its traders sell and the named horses of its quests -- plus `horse`, the generic one.
     * @returns Breed names, `horse` first and the rest alphabetical.
     */
    static breeds(): string[];

    /**
     * Lists every horse the server currently has.
     * @returns One handle per live horse, in no particular order.
     */
    static all(): Horse[];

    /**
     * Looks a horse up by its network entity ID.
     * @param id Network entity identifier.
     * @returns The horse's handle, or null when no live horse has that ID.
     */
    static getById(id: number): Horse | null;
  }

  interface Horse extends Entity {}

  /**
   * One row of a page.
   */
  interface DialogueOption {
    /**
     * What comes back on `dialogueChoice` when this row is picked. An id, never an index, so a handler stays correct when the page is rebuilt with different rows.
     */
    id: string;

    /**
     * The line the player reads. Server-authored text reaches the game's own list as written; it is not a localization key.
     */
    text: string;

    /**
     * Draws the row greyed and unpickable when false. A rendering hint only -- the server re-checks the choice when it arrives, so nothing a row costs may rely on this.
     */
    enabled?: boolean | undefined;
  }

  /**
   * A line and the options under it.
   */
  interface DialoguePage {
    /**
     * Shown above the options. Omit it for a list with no preamble.
     */
    line?: string | undefined;

    /**
     * Draws the list on the right of the screen instead of the left.
     */
    onRight?: boolean | undefined;

    /**
     * The rows, at least one and at most eight. A page with none is refused.
     */
    options: DialogueOption[];
  }

  /**
   * Conversations the server drives on a player, in the game's own dialogue list.
   * 
   * A conversation is attached to nothing. It runs on a player and what it is about -- a smith, a notice board, a chest, a timer -- is this gamemode's own business, held in its own closures. That is deliberate and it has a cost: nothing here watches distance, line of sight or whether anyone died, because none of that is knowable without the relationship it refuses to hold. Close the conversation yourself when the fiction says it should end.
   */
  const Dialogue: {
    /**
     * Opens a conversation and shows its first page. A player already in one has it closed with reason 2 first, because the game has a single choice list and stacking them would lie about which is live.
     * @param player NetworkID of the player the conversation runs on.
     * @param page The first page. `options` is required and carries at most 8 rows; each needs an `id` and a `text`, and may set `enabled`. `line` and `onRight` are optional.
     * @returns The session id, or 0 when the page was unusable or the player is gone.
     */
    open(player: number, page: DialoguePage): number;

    /**
     * Replaces the page a live conversation is showing. Each page carries a generation the client echoes, so an answer still in flight against the page this replaces is dropped rather than misapplied.
     * @param session The session to advance.
     * @param page The page to show instead.
     * @returns False when that session has already ended.
     */
    update(session: number, page: DialoguePage): boolean;

    /**
     * Ends a conversation and takes the list off that player's screen.
     * @param session The session to end.
     * @returns False when the session had already ended.
     */
    close(session: number): boolean;

    /**
     * The conversation that player is in.
     * @param player NetworkID of the player to ask about.
     * @returns The session id, or 0 when they are not in one.
     */
    sessionOf(player: number): number;
  };

  /**
   * How a vendor starts out.
   */
  interface VendorOptions {
    /**
     * Shown in the server's log only. The trade screen names whoever keeps the shop.
     */
    name?: string | undefined;

    /**
     * Money units the vendor starts with, and all it can pay out. Omit it for a purse that never runs out.
     */
    purse?: number | undefined;

    /**
     * Whether the vendor takes the player's items at all. On by default; `setBuyPrices` says which ones.
     */
    buys?: boolean | undefined;
  }

  /**
   * One thing a vendor sells.
   */
  interface VendorStockRow {
    /**
     * The item class, by GUID or by the game's own item name.
     */
    item: string;

    /**
     * How many the vendor has. A row the players buy out disappears.
     */
    amount: number;

    /**
     * What one costs, in money units -- the amount of the game's `money` item, which is also what `player.giveItem('money', n)` hands out.
     */
    price: number;

    /**
     * Item properties, including custom display overrides and private data. Preserved when bought.
     */
    metadata?: Record<string, unknown> | undefined;
  }

  /**
   * One thing a vendor buys from players.
   */
  interface VendorBuyRow {
    /**
     * The item class, by GUID or by the game's own item name.
     */
    item: string;

    /**
     * What the vendor pays for one, in money units.
     */
    price: number;
  }

  /**
   * One settled line of a deal.
   */
  interface VendorTradeLine {
    /**
     * Exact item properties, including private custom data. Sold variants appear as separate lines.
     */
    metadata: Record<string, unknown>;

    /**
     * The item class GUID.
     */
    item: string;

    /**
     * The game's own name for the item class.
     */
    name: string;

    /**
     * How many changed hands.
     */
    amount: number;

    /**
     * What one was priced at when the deal settled.
     */
    price: number;
  }

  /**
   * Price lists and purses the server owns, traded on the game's own shop screen.
   * 
   * A vendor is attached to nothing: open one in front of a player from wherever the gamemode decides the counter is -- `npcInteract`, a dialogue option, a command. The screen is a preview. Its prices come from here, and pressing Trade sends a basket that the server re-prices, settles and moves itself; nothing the client shows is trusted.
   * 
   * Prices count in money units, the amount of the game's `money` item.
   */
  const Vendor: {
    /**
     * Creates a vendor with nothing to sell and nothing it buys.
     * @param options Name, starting purse and whether it buys. All optional.
     * @returns The vendor id.
     */
    create(options?: VendorOptions): number;

    /**
     * Closes every session at the vendor and forgets it. A deal settles the moment its basket arrives, so none is ever left half done.
     * @param vendor The vendor to remove.
     * @returns False when there was no such vendor.
     */
    destroy(vendor: number): boolean;

    /**
     * Replaces what a vendor sells. Every player with its screen open sees the new shelf straight away, and a basket priced against the old one is refused.
     * @param vendor The vendor to stock.
     * @param rows Everything it sells, at most 128 rows and each item class once. Replaces the old list.
     * @returns False when there is no such vendor.
     */
    setStock(vendor: number, rows: VendorStockRow[]): boolean;

    /**
     * Replaces what a vendor buys from players and what it pays. Anything not listed is shown at no value and a basket selling it is refused.
     * @param vendor The vendor to change.
     * @param rows Everything it buys, at most 128 rows and each item class once. Replaces the old list.
     * @returns False when there is no such vendor.
     */
    setBuyPrices(vendor: number, rows: VendorBuyRow[]): boolean;

    /**
     * Sets what a vendor can pay out. Deals keep it current: what players pay goes in, what they are paid comes out.
     * @param vendor The vendor to change.
     * @param purse Money units it holds, or null for a purse that never runs out.
     * @returns False when there is no such vendor.
     */
    setPurse(vendor: number, purse: number | null): boolean;

    /**
     * What a vendor holds.
     * @param vendor The vendor to ask about.
     * @returns Money units, or null for a purse that never runs out or a vendor that does not exist.
     */
    getPurse(vendor: number): number | null;

    /**
     * Opens the game's own trade screen on a player. A player already trading has that session closed with reason 2 first, because the game has one trade screen.
     * @param vendor The vendor to trade with.
     * @param player NetworkID of the player to show it to.
     * @param npc NetworkID of the NPC who keeps the shop. The screen shows that NPC as the trader; omit it to trade with nobody in particular.
     * @returns The session id, or 0 when the vendor or the player is gone.
     */
    open(vendor: number, player: number, npc?: number): number;

    /**
     * Takes the trade screen off that player.
     * @param session The session to end.
     * @returns False when the session had already ended.
     */
    close(session: number): boolean;

    /**
     * The trading session a player is in.
     * @param player NetworkID of the player to ask about.
     * @returns The session id, or 0 when they are not trading.
     */
    sessionOf(player: number): number;

    /**
     * The vendor a session trades with.
     * @param session The session to ask about.
     * @returns The vendor id, or 0 when the session has ended.
     */
    vendorOf(session: number): number;
  };

  /**
   * One item class that left a victim's pockets for a thief's.
   */
  interface PickpocketLine {
    /**
     * The item class GUID.
     */
    item: string;

    /**
     * The game's own name for the item class.
     */
    name: string;

    /**
     * How many moved.
     */
    amount: number;
  }

  /**
   * A dice table placed by the current level. The catalog includes quest-layer tables that may not currently be loaded on a client.
   */
  interface DiceTableInfo {
    /**
     * The table's GUID as sixteen lowercase hex digits: the level's own, or the one a placed table's clients build it under.
     */
    id: string;

    /**
     * Whether a script or a World Builder export placed the table, rather than the level.
     */
    placed: boolean;

    /**
     * The world-space midpoint between the table's two seat alignment points, in metres. The behavior entity itself may be offset from this interaction centre.
     */
    position: Vector3;

    /**
     * The two sitting smart-object GUIDs, in native seat order. Compare with a seated player's seatGuid; these are not the minigame's alignment tagpoints.
     */
    seats: [string, string];
  }

  /**
   * How a dice match is played.
   */
  interface DiceMatchOptions {
    /**
     * The score that wins, banked at the end of a turn. 2000 when omitted, at most 100000.
     */
    targetScore?: number | undefined;

    /**
     * First seat's scoreboard label, up to 64 UTF-8 bytes without control characters. Empty or omitted uses the native label.
     */
    firstName?: string | undefined;

    /**
     * Second seat's scoreboard label. Each client maps the seat to its local or opponent column.
     */
    secondName?: string | undefined;
  }

  /**
   * An accepted server dice action. Notifications describe server decisions, which can precede the native animation. Seats and die-mask bits start at zero; turns and rolls start at one.
   */
  interface DiceProgressEvent {
    /**
     * Match ID.
     */
    match: number;

    /**
     * Network ID of the acting player, or of the NPC on the second seat.
     */
    player: number;

    /**
     * Acting seat: 0 for first, 1 for second.
     */
    seat: number;

    /**
     * Turn number across both seats, starting at 1.
     */
    turn: number;

    /**
     * Roll within the turn; 0 before its first roll.
     */
    roll: number;

    /**
     * Number of accepted moves so far.
     */
    moveIndex: number;

    /**
     * Unbanked points in this turn, before they are lost on a bust or banked on a pass.
     */
    turnScore: number;

    /**
     * This seat's banked score.
     */
    totalScore: number;

    /**
     * Six pip values (1 through 6) in die-index order on diceRoll; empty on other events. rolledMask identifies the dice rolled this time.
     */
    faces: number[];

    /**
     * Bit mask of dice rolled, on diceRoll; zero otherwise.
     */
    rolledMask: number;

    /**
     * Bit mask of dice selected in the accepted move; zero otherwise.
     */
    heldMask: number;

    /**
     * Points added by diceMove or banked by a passed diceTurnEnd; zero otherwise.
     */
    points: number;

    /**
     * continue or pass on diceMove; bust or rolled on diceRoll; pass, bust, won, gaveUp, left, timedOut, failed, or stopped on diceTurnEnd; empty on diceTurnStart.
     */
    reason: string;
  }

  /**
   * Dice matches between two players, or a player and a server NPC, on the game's own dice table.
   * 
   * Both players sit down at the dice table nearest them and play it as they would against an NPC, the other player's body opposite. Every throw is dealt and every move checked by the server, by the game's own scoring rules; badges and dice perks are off, so a match is decided by the dice and the decisions alone.
   * 
   * An Npc may hold the second seat. Its body sits opposite the player, its throws are dealt like anyone's, and its decisions are the script's: `diceRoll` says what it threw, `Dice.move` plays its move.
   */
  const Dice: {
    /**
     * Lists the dice tables players in a virtual world can play at, and their seats: the level's own, minus any a level edit took out of that world, and every table `SmartObject.spawn` or a World Builder export placed there or in the global world. Readiness and occupancy belong to the gamemode.
     * @param virtualWorld Optional virtual world whose tables to list; the global one when omitted.
     * @returns The tables, in no particular order.
     */
    tables(virtualWorld?: number): DiceTableInfo[];

    /**
     * Starts a match. Both players must stand together, within a few metres, at a dice table, and neither may be playing already. An NPC is held to the same: it must stand at the table with the player, in the same virtual world. Throws an Error saying why when it cannot start.
     * @param first NetworkID of the player on the table's first seat.
     * @param second NetworkID of the player on the second seat, or of a living human Npc to seat there and play through Dice.move.
     * @param options The target score. Optional.
     * @returns The match id.
     */
    start(first: number, second: number, options?: DiceMatchOptions): number;

    /**
     * Plays the move of the NPC on the second seat, checked by the rules a player's move is. Legal only while it is the NPC's turn and its throw is on the table, so after a `diceRoll` naming it with reason `rolled`; calling it from that handler is supported. An accepted move raises `diceMove` and what follows from it. The NPC forfeits like a player when no move is accepted for three minutes.
     * @param match Match ID.
     * @param heldMask Bit mask of the dice to set aside, bit 0 for die 0 through bit 5 for die 5, among those `diceRoll` reported in `rolledMask`. Every die in it must count towards a scoring combination.
     * @param pass True banks the turn after scoring the held dice, if any. False scores them, which needs at least one, and throws the rest.
     * @returns False, with nothing changed, when the match has ended or has no NPC seat, it is not the NPC's turn, or the move is not legal.
     */
    move(match: number, heldMask: number, pass: boolean): boolean;

    /**
     * Changes both scoreboard labels on the next native scoreboard refresh. Empty restores a native label. Names allow at most 64 UTF-8 bytes and no control characters.
     * @param match Match ID.
     * @param first First seat label.
     * @param second Second seat label.
     * @returns False if the match has ended.
     */
    setNames(match: number, first: string, second: string): boolean;

    /**
     * Ends a match with nobody winning; both tables close.
     * @param match The match to end.
     * @returns False when it had already ended.
     */
    stop(match: number): boolean;

    /**
     * The match a player is in.
     * @param player NetworkID of the player to ask about.
     * @returns The match id, or 0.
     */
    matchOf(player: number): number;
  };

  /**
   * A table a player is about to open, or a workpiece about to take its materials. Frozen: a handler can refuse it, not change it.
   */
  interface CraftStartProposal {
    /**
     * Which craft.
     */
    kind: 'alchemy' | 'smithing';

    /**
     * Station id, as `Crafting.stations` names it.
     */
    station: string;

    /**
     * The player's virtual world; each world has its own stations.
     */
    virtualWorld: number;

    /**
     * Smithing only: the recipe id the player chose, as `Crafting.recipes` keys it.
     */
    recipe: string | undefined;

    /**
     * The batch this one follows at a table the player kept, or empty for a fresh entry. Always empty for smithing.
     */
    continuationOf: string;
  }

  /**
   * A batch that is now brewing, or a workpiece whose materials were taken.
   */
  interface CraftStartedEvent {
    /**
     * Which craft.
     */
    kind: 'alchemy' | 'smithing';

    /**
     * The batch's or workpiece's session id.
     */
    session: string;

    /**
     * Station id.
     */
    station: string;

    /**
     * The station's virtual world.
     */
    virtualWorld: number;

    /**
     * Smithing only: the recipe being worked.
     */
    recipe: string | undefined;

    /**
     * The previous batch at this table, or empty for the first. Always empty for smithing.
     */
    continuationOf: string;
  }

  /**
   * What a finished batch or workpiece is about to grant, computed by the server. Frozen: a handler can refuse it, not change it.
   */
  interface CraftCompletionProposal {
    /**
     * Which craft.
     */
    kind: 'alchemy' | 'smithing';

    /**
     * The session id.
     */
    session: string;

    /**
     * Station id.
     */
    station: string;

    /**
     * `failed` brews the game's failed potion. A smithing proposal is always `success`: a failed workpiece grants nothing to review.
     */
    outcome: 'success' | 'failed';

    /**
     * The recipe the brew matched, or empty when it matched none; the smithing recipe worked.
     */
    recipe: string;

    /**
     * The product's native rank; for smithing, its item quality tier.
     */
    grade: number;

    /**
     * Item class GUID of what would be granted, or empty when nothing is: a zero alchemy yield, or a smithing quest product its quest creates.
     */
    product: string;

    /**
     * How many.
     */
    amount: number;

    /**
     * Brewing quality, 0 to 1, after perks and the table-entry bonus; for smithing, the workpiece quality the player's game reported.
     */
    quality: number;

    /**
     * Base alchemy or craftsmanship XP; the player's own multipliers apply on top.
     */
    xp: number;

    /**
     * Smithing only: lockpicks granted alongside, drawn by the server from the player's perks.
     */
    lockpicks: number | undefined;
  }

  /**
   * A batch or workpiece whose result was granted: the output is in the inventory and the XP was ordered.
   */
  interface CraftCompletedEvent {
    /**
     * Which craft.
     */
    kind: 'alchemy' | 'smithing';

    /**
     * The session id.
     */
    session: string;

    /**
     * Station id.
     */
    station: string;

    /**
     * The station's virtual world.
     */
    virtualWorld: number;

    /**
     * `failed` granted the game's failed potion.
     */
    outcome: 'success' | 'failed';

    /**
     * The recipe matched, or empty.
     */
    recipe: string;

    /**
     * The product's native rank; for smithing, its item quality tier.
     */
    grade: number;

    /**
     * Brewing or workpiece quality, 0 to 1.
     */
    quality: number;

    /**
     * Base XP ordered.
     */
    xp: number;

    /**
     * Smithing only: lockpicks granted.
     */
    lockpicks: number | undefined;

    /**
     * The inventory rows that received the output, lockpicks included. Do not grant it again.
     */
    outputs: InventoryUnit[];

    /**
     * Alchemy only: the player's recipe knowledge after this batch, as `Crafting.knowledge` returns it.
     */
    knowledge: Record<string, number> | undefined;
  }

  /**
   * An original ingredient still held by this session, including its original inventory properties.
   */
  interface CraftRefundMaterial {
    /**
     * Item class GUID.
     */
    item: string;

    /**
     * Units originally removed and not already returned.
     */
    amount: number;

    /**
     * Original canonical inventory properties, preserved by refunds.
     */
    metadata: Record<string, unknown>;
  }

  /**
   * An unfinished craft before material settlement. Frozen. Call the event's refund callback synchronously to request all its materials back; otherwise the native refund rules apply.
   */
  interface CraftRefundProposal {
    /**
     * Which craft.
     */
    kind: 'alchemy' | 'smithing';

    /**
     * The session being settled, once only.
     */
    session: string;

    /**
     * Station id.
     */
    station: string;

    /**
     * The session's virtual world.
     */
    virtualWorld: number;

    /**
     * No product was granted for this craft.
     */
    outcome: 'failed' | 'cancelled';

    /**
     * Why it ended: cancelled for normal native or script cancellation; interrupted for native teardown without a normal exit; clientError for an alchemy adapter failure; craftingFailed for a broken smithing workpiece. Server closures also include disconnected, timeout and contextInvalidated; settlement refusals retain their existing reasons. Client exit reasons are observations, not server-verified combat evidence.
     */
    reason: string;

    /**
     * All remaining original ingredients, including processed alchemy ingredients. Station liquids and ingredients already returned are excluded. Do not grant these yourself; call refund().
     */
    materials: CraftRefundMaterial[];
  }

  /**
   * A batch or workpiece that is over, for any reason. A table kept for the next batch is not closed by this.
   */
  interface CraftEndedEvent {
    /**
     * Which craft.
     */
    kind: 'alchemy' | 'smithing';

    /**
     * The session id.
     */
    session: string;

    /**
     * Station id.
     */
    station: string;

    /**
     * The station's virtual world.
     */
    virtualWorld: number;

    /**
     * How it ended.
     */
    outcome: 'success' | 'failed' | 'cancelled';

    /**
     * Empty for a completed batch or a successful workpiece. `craftingFailed` for a broken workpiece; `cancelled` for normal native or script cancellation; `interrupted` for unexpected native teardown; `clientError` for alchemy adapter errors. Other reasons: `craftingCompletingRejected`, `disconnected`, `timeout`, `contextInvalidated`, and for smithing `abandoned`, `abandonTooLate`, `invalidQuality`, `inventoryUnavailable`. See CraftRefundProposal.
     */
    reason: string;

    /**
     * The inventory rows ingredients went back to. Alchemy: wholly unmilled bowl, mortar and herb groups come back; everything else put on the table was spent. Smithing: a failed workpiece spends half of each divisible material, rounded down, and a coin decides a single unit; the rest comes back. A full refund requested through craftingRefunding returns all remaining original materials instead.
     */
    refunded: InventoryUnit[];
  }

  /**
   * Units required by a shipped recipe.
   */
  interface CraftMaterialRequirement {
    /**
     * Item class GUID.
     */
    item: string;

    /**
     * Native item name, useful with Player.giveItem.
     */
    name: string;

    /**
     * Units required for one attempt.
     */
    amount: number;

    /**
     * A quest item, which only its quest hands out: the game cannot create one, so the inventory refuses it (`questItem`) and Player.giveItem returns false. It must be held to craft, and is never taken.
     */
    quest: boolean;
  }

  /**
   * One possible product row. These are alternatives, not a reward list.
   */
  interface CraftProductInfo {
    /**
     * Product class GUID.
     */
    item: string;

    /**
     * Alchemy product rank or smithing minimum quality percent.
     */
    threshold: number;

    /**
     * Alchemy base maximum yield before modifiers; one for smithing.
     */
    maxYield: number;
  }

  /**
   * A recipe allowed by server DLC policy: the game's, a streamed mod's or a script's. Presence is not proof of player knowledge, skill, quest eligibility or materials.
   */
  interface CraftRecipeInfo {
    /**
     * Tagged native recipe identity.
     */
    key: { kind: 'alchemy'; id: number } | { kind: 'smithing'; id: string };

    /**
     * Localization key from the native table.
     */
    name: string;

    /**
     * Native DLC id; zero for untagged content.
     */
    dlcId: number;

    /**
     * Minimum craftsmanship skill, or zero for alchemy.
     */
    minSkill: number;

    /**
     * Smithing recipe unlock perk GUID, empty for alchemy.
     */
    perk: string;

    /**
     * Alchemy base liquid, empty for smithing. Supplied by the station.
     */
    base: string;

    /**
     * Required ingredient quantities.
     */
    ingredients: CraftMaterialRequirement[];

    /**
     * Possible products by native rank or threshold.
     */
    products: CraftProductInfo[];

    /**
     * Where the recipe comes from: the game's tables, a streamed mod's table patch (which may replace a game recipe of the same id), or `Crafting.registerRecipe`.
     */
    source: 'game' | 'mod' | 'script';
  }

  /**
   * One step of a custom alchemy recipe, as a row of the game's `AlchemyRecipe.xml` has it.
   */
  interface CraftRecipeStepDefinition {
    /**
     * 1 to 28, distinct within the recipe: the step's bit in the knowledge mask. The first step listed identifies the recipe: a brew that fails it is not this recipe.
     */
    id: number;

    /**
     * The step's XP: 0, 12 or 24, from `AlchemyRecipeStepType.xml`.
     */
    type: 'Trivial' | 'Easy' | 'Hard';

    /**
     * The game's own condition syntax, terms joined by ` AND `: `PotionBase()`, `IngredientCount('<item>') == n` or `>= n`, `IsMilled('<item>', 0 | 1)`, `WeakBoilingTime('<item>', time)`, `StrongBoilingTime('<item>', time)`, `IsBaseDistilled(0 | 1)` and `IsResultMilled(0 | 1)`. Item GUIDs in lower case; a time is in the table's time units. Anything else is refused.
     */
    condition: string;

    /**
     * The order the recipe book lists failed steps in. Default 0.
     */
    feedbackPriority: number | undefined;

    /**
     * The step's line in the recipe book, shown as written. Default none.
     */
    text: string | undefined;
  }

  /**
   * A recipe for `Crafting.registerRecipe`, field for field a row of the game's `AlchemyRecipe.xml` or `BlacksmithRecipes.xml`. Items are item class GUIDs of the game or of a streamed mod; custom items from `Items.register` cannot be used. Texts are shown as written. Pictures are base64 PNGs, each at most 4096x4096 pixels, 256 KiB of PNG for the recipe's pictures together.
   */
  interface CraftRecipeDefinition {
    /**
     * Which craft.
     */
    kind: 'alchemy' | 'smithing';

    /**
     * Alchemy: a RecipeId from 1 to 2147483647 the game does not use; when two recipes would identify the same brew, the lower id wins, as in the game. Smithing: an Id of letters, digits, `_`, `-` and `.`, at most 128 characters, the game does not use.
     */
    id: number | string;

    /**
     * The recipe's name in the recipe book or the smithy.
     */
    name: string;

    /**
     * UIIcon: one of the game's item icons, by name, such as `potion_hair_o_dog` or `shortswordCommon`. Leave it out when `iconImage` brings one.
     */
    icon: string | undefined;

    /**
     * The recipe's own icon in the recipe lists, a base64 PNG. Fitted onto the game's 64x64 item icon square, aspect kept, so a square image with the item reaching the edges looks like the game's.
     */
    iconImage: string | undefined;

    /**
     * Alchemy: the recipe book's picture of the product, beside the description, a base64 PNG. Fitted onto a 256x256 square, as the game's are.
     */
    productImage: string | undefined;

    /**
     * Alchemy: the recipe book's recipe picture, a base64 PNG. Fitted onto a 1024x1024 square, as the game's are.
     */
    recipeImage: string | undefined;

    /**
     * Alchemy: the RecipeId of one of the game's alchemy recipes whose two book pictures the recipe book shows for this one, instead of productImage and recipeImage. `Crafting.recipes(player, 'alchemy')` lists the ids (`key.id`).
     */
    bookPicturesFrom: number | undefined;

    /**
     * Native DLC id the recipe belongs to, filtered by `required_dlc` as the game's are. Default 0.
     */
    dlcId: number | undefined;

    /**
     * Smithing: the craftsmanship it takes. Default 0.
     */
    minSkill: number | undefined;

    /**
     * 1 to 64 distinct items and the units one attempt takes. A quest item must be held but is never taken.
     */
    ingredients: { item: string; amount: number }[];

    /**
     * 1 to 64 products, each with its own threshold. Alchemy: threshold is the potion grade, 1 to 4, and maxYield the potions it makes, 1 to 64; the potion is an item the inventory holds, not a quest item. Smithing: threshold is the minimal workpiece quality percent, 0 to 100; the product is a melee weapon, armor that never changes condition, or a misc item, and not a quest item.
     */
    products: { item: string; threshold: number; maxYield?: number }[];

    /**
     * Alchemy, required: the base liquid.
     */
    base: 'Water' | 'Wine' | 'Oil' | 'Spiritus' | undefined;

    /**
     * Alchemy: the recipe book's usage text.
     */
    usage: string | undefined;

    /**
     * Alchemy: BaseQualityRewardXP, the brewing XP before grade multipliers and step XP. Default 0.
     */
    baseXP: number | undefined;

    /**
     * Alchemy, required: 1 to 28 steps.
     */
    steps: CraftRecipeStepDefinition[] | undefined;

    /**
     * Smithing: the perk GUID that teaches the recipe. Without one, every smith knows it.
     */
    perk: string | undefined;

    /**
     * Smithing, required: the recipe screen's tab.
     */
    category: 'ShortSword' | 'LongSword' | 'Axe' | 'Other' | undefined;

    /**
     * Smithing, required: the blank on the anvil.
     */
    workpiece: 'sword' | 'longsword' | 'axe' | 'horseshoe' | undefined;

    /**
     * Smithing: the description the smithy shows.
     */
    description: string | undefined;
  }

  /**
   * A station in the configured level: one the level authors, or one a script or a World Builder export placed. The catalog does not guarantee that a level station's quest layer is loaded or that native use is currently permitted.
   */
  interface CraftStationInfo {
    /**
     * `<level>:<guid>`. A level station's GUID is stable; a placed one's lasts until the server stops.
     */
    id: string;

    /**
     * Whether a script or a World Builder export placed the station, rather than the level. Its `layer` is empty.
     */
    placed: boolean;

    /**
     * The station's recipe family.
     */
    kind: 'alchemy' | 'smithing';

    /**
     * Level directory name.
     */
    level: string;

    /**
     * Authored editor layer, for identifying the location.
     */
    layer: string;

    /**
     * World position of the station.
     */
    position: { x: number; y: number; z: number };

    /**
     * Horizontal facing direction.
     */
    forward: { x: number; y: number };
  }

  /**
   * Who holds a station. A player keeps an alchemy table between batches until they leave it; a smithery is held for one workpiece.
   */
  interface CraftStationOccupant {
    /**
     * The player's id.
     */
    playerId: number;

    /**
     * The first batch of this visit; the same across every batch of it.
     */
    entryId: string;

    /**
     * The batch now running, or the last one to finish.
     */
    sessionId: string;

    /**
     * `entering` until the opening animation finishes; `finishing` while a finished batch waits to settle; `working` at a smithery.
     */
    phase: 'entering' | 'brewing' | 'finishing' | 'betweenBatches' | 'working';

    /**
     * Milliseconds until the table is taken back. Each batch has 30 minutes.
     */
    expiresInMs: number;
  }

  /**
   * One station in one virtual world. A free station may still be unusable in the game, for instance behind a quest layer.
   */
  interface CraftStationOccupancy {
    /**
     * Station id.
     */
    station: string;

    /**
     * The virtual world asked about.
     */
    virtualWorld: number;

    /**
     * Someone holds it, brewing or between batches.
     */
    occupied: boolean;

    /**
     * Who, or null when free.
     */
    occupant: CraftStationOccupant | null;
  }

  /**
   * A player's alchemy session or kept table, or their smithing workpiece. A copy: changing it changes nothing.
   */
  interface CraftSessionInfo {
    /**
     * Which craft.
     */
    kind: 'alchemy' | 'smithing';

    /**
     * The session id.
     */
    id: string;

    /**
     * Station id.
     */
    station: string;

    /**
     * The station's virtual world.
     */
    virtualWorld: number;

    /**
     * Where the batch is; `working` for a smithing workpiece.
     */
    phase: 'entering' | 'brewing' | 'finishing' | 'betweenBatches' | 'working';

    /**
     * Alchemy only: native actions accepted so far.
     */
    sequence: number | undefined;

    /**
     * Alchemy only: what is on the table: item class, the inventory row it came from (empty for a base liquid), and the native table position.
     */
    resources: { id: number; item: string; itemId: string; position: number; base: boolean; milled: boolean; distilled: boolean }[] | undefined;

    /**
     * Smithing only: the recipe being worked.
     */
    recipe: string | undefined;

    /**
     * Smithing only: the units taken from the inventory for this workpiece. Quest materials are required but never taken.
     */
    materials: { item: string; amount: number }[] | undefined;

    /**
     * Milliseconds until the session times out.
     */
    expiresInMs: number;
  }

  /**
   * Crafting catalogs, station occupancy, alchemy and smithing sessions, and alchemy recipe knowledge. Held in memory for each connection; nothing is saved.
   */
  const Crafting: {
    /**
     * True: alchemy and smithing are both ruled by the server. Omitted kind checks alchemy.
     */
    isAvailable(kind?: 'alchemy' | 'smithing'): boolean;

    /**
     * The stations players in a virtual world can craft at: the level's own, minus any a level edit took out of that world, and every station `SmartObject.spawn` or a World Builder export placed there or in the global world. Does not load deferred layers or bypass quests.
     * @param virtualWorld Optional virtual world whose stations to list; the global one when omitted.
     */
    stations(kind?: 'alchemy' | 'smithing', virtualWorld?: number): readonly CraftStationInfo[];

    /**
     * Every recipe enabled by server DLC policy: the game's, then streamed mods' and scripts' in the order they were added. A mod's table patch row with a game recipe's id replaces it in place. The player must be connected but does not change the list. Catalog presence does not establish knowledge or skill.
     */
    recipes(player: Player, kind?: 'alchemy' | 'smithing'): readonly CraftRecipeInfo[];

    /**
     * Adds an alchemy or smithing recipe for the rest of the server's life. The server brews and forges it by the same rules as the game's recipes, `Crafting.recipes` lists it, and every client, including one that joins later, adds it to the game's own recipe book and smithy. Registering the same definition again does nothing. Throws for a malformed definition, an item the catalog does not know, an id the game or another resource uses, or a different definition under an id already registered.
     */
    registerRecipe(definition: CraftRecipeDefinition): void;

    /**
     * The player's alchemy session or the table they kept between batches, else their smithing workpiece, else null.
     */
    session(player: Player): CraftSessionInfo | null;

    /**
     * Who holds a station. World defaults to 0. Null for an unknown station.
     */
    occupancy(stationId: string, virtualWorld?: number): CraftStationOccupancy | null;

    /**
     * Ends the player's session as leaving the station would, and their game closes it. Alchemy: wholly unmilled groups go back to the inventory, the rest is spent. Smithing: the workpiece fails, spending its failure share. `craftingEnded` follows next tick. False when there was nothing to end.
     */
    cancel(player: Player): boolean;

    /**
     * The player's recipe knowledge: recipe id to a mask of its 29 authored steps, 536870911 when the whole recipe is known. A recipe not listed is unknown. Starts empty every session; save it from `craftingCompleted` or here and restore it with `setKnowledge`.
     */
    knowledge(player: Player): Record<string, number>;

    /**
     * Sets which steps of one recipe the player knows, and sends it to their game's recipe book. 0 forgets the recipe. False for an unknown recipe, a mask above 536870911, or a player who is not connected.
     */
    setKnowledge(player: Player, recipeId: string | number, mask: number): boolean;
  };

  /**
   * Replicated KCD2 dog companion handle.
   */
  class Dog {
    /**
     * Creates a script wrapper for an existing dog with this ID; use Dog.spawn() to spawn one.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * GUID of the soul this dog was spawned against, which decides its appearance.
     */
    readonly soul: string;

    /**
     * Name every client shows for this dog. Assignment renames it on all of them; empty means the name its soul carries stands.
     */
    name: string;

    /**
     * Network ID of the player this dog belongs to, or 0 when it has no master.
     */
    readonly ownerId: number;

    /**
     * The player this dog belongs to, or null when it has no master.
     */
    readonly owner: Player | null;

    /**
     * Whether this dog is despawned when its owner disconnects. True by default. Set it to false to keep the dog in the world: it is left masterless instead (`dogOwnerChanged` with a null player), and with nobody's game running its brain it stands where it was until `giveTo` hands it to someone. Saving it across server restarts is the script's own business.
     */
    destroyWithOwner: boolean;

    /**
     * The dog's companion mode: 0 Wait, 1 Follow, 2 Free, 3 Aggressive, 4 Search, 5 Hunt, 6 Guard, 7 Ambush. Assignment applies it on every client.
     */
    mode: number;

    /**
     * What the owner's game currently has this dog doing, as an E_DogObjective ordinal: 0 Wait, 2 Bark, 4 Follow, 5 FollowHeel, 7 Search, 8 MeleeCombat, 9 Fetch, 10 Hunt, 19 Eat, 21 Distract, 22 Pet, 25 Invalid when idle. Reported by the owner, so read-only.
     */
    readonly objective: number;

    /**
     * The dog's morale as its owner last read it off the game, or 0 before any report. Below the game's own threshold the dog stops obeying commands. Owner-reported, so read-only.
     */
    readonly morale: number;

    /**
     * Formats this dog handle for logging and debugging.
     * @returns The dog ID, its soul, its owner and its mode.
     */
    toString(): string;

    /**
     * Despawns this dog on every client after emitting dogDestroy.
     */
    destroy(): void;

    /**
     * Hands this dog to another player. Every client re-possesses the body onto the new master's soul, and authority over its pose moves with it. Throws when `player` is not a connected player.
     * @param player The dog's new master, or null to leave it masterless.
     */
    giveTo(player: Player | null): void;

    /**
     * Spawns and replicates a dog companion for a player.
     * @param owner The player the dog belongs to. A dog is somebody's from the moment it exists.
     * @param position Optional world-space spawn position; omitted spawns the dog on its owner.
     * @param rotation Optional initial orientation: a Quaternion, or a Vector3 of Euler angles in degrees.
     * @param soul Optional soul GUID from the game's tables; omitted spawns the generic dog.
     * @param name Optional name every client shows for the dog; omitted leaves the name its soul carries.
     * @returns The newly spawned dog handle.
     */
    static spawn(owner: Player, position?: Vector3 | Partial<Vector3>, rotation?: Vector3 | Quaternion, soul?: string, name?: string): Dog;

    /**
     * Lists every dog the server currently has.
     * @returns One handle per live dog, in no particular order.
     */
    static all(): Dog[];

    /**
     * Looks a dog up by its network entity ID.
     * @param id Network entity identifier.
     * @returns The dog's handle, or null when no live dog has that ID.
     */
    static getById(id: number): Dog | null;
  }

  interface Dog extends Entity {}

  /**
   * Replicated static mesh handle.
   */
  class Prop {
    /**
     * Creates a script wrapper for an existing prop with this ID; use Prop.spawn() to spawn one.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * Catalog path of the mesh this prop was built from, e.g. `objects/manmade/barrels/barrel_a.cgf`. Read-only: the collision is built from the mesh when the entity is spawned and never re-read, so a prop is the model it was created with.
     */
    readonly model: string;

    /**
     * File stem of the mesh, e.g. `barrel_a`, which is the short name `Prop.spawn` also accepts.
     */
    readonly modelName: string;

    /**
     * Browsing bucket the mesh sits in, e.g. `manmade/structures`; `kcdc/<resource>` for a mesh the server streams.
     */
    readonly modelGroup: string;

    /**
     * What this prop's collision does: `static` collides but never moves, `rigid` falls and can be pushed, `none` has none at all. Assignment rebuilds the prop on every client. A rigid one is simulated by the nearest player's client within 60 m, and every other player sees the pose it streams; with nobody that near, it stays where it last rested.
     */
    physics: string;

    /**
     * A `rigid` prop's mass in kilograms, given to its body as it is built; 0, the default, takes the mass the mesh's collision gives it. Up to 100,000; a value outside 0 to 100,000 is ignored. Set it for a mesh whose collision carries no mass, such as a tree's: without one, every client builds such a prop as a `static` one, which stands still and cannot be pushed, and logs why. Assignment rebuilds the prop on every client, so set it before pushing it.
     */
    mass: number;

    /**
     * Uniform scale, from 0.01 to 100; a value outside that is clamped into it. Assignment rebuilds the prop on every client, because the collision is sized from the scale at spawn and never re-read.
     */
    scale: number;

    /**
     * Formats this prop handle for logging and debugging.
     * @returns The prop ID, its model, its physics and its scale.
     */
    toString(): string;

    /**
     * Despawns this prop on every client after emitting propDestroy.
     */
    destroy(): void;

    /**
     * Pushes a `rigid` prop's body and wakes it. The nearest player's client simulates the body and every other player sees the pose it streams. `propRest` is raised once it stops.
     * @param impulse The push in kg m/s, world space. The body's velocity changes by this divided by its mass, so a heavy prop needs a large number.
     * @param point Where the push lands, in world space. Off the centre of mass it also turns the body, which is what topples a tall one. Omitted, it lands on the centre of mass.
     * @param angularImpulse A twist on top, in kg m^2/s about each world axis.
     * @returns True when it was sent to the client simulating the prop; false when the prop is gone or is not `rigid`, a value is not finite or is out of range, or no player is within 60 m to simulate it.
     */
    applyImpulse(impulse: Vector3, point?: Vector3, angularImpulse?: Vector3): boolean;

    /**
     * Sets a `rigid` prop's velocity outright, whatever its mass, and wakes it. `propRest` is raised once it stops.
     * @param velocity Its new velocity in m/s, world space, up to 200.
     * @param angularVelocity Its new spin in radians per second about each world axis, up to 100. Omitted, it stops turning.
     * @returns True when it was sent to the client simulating the prop; false when the prop is gone or is not `rigid`, a value is not finite or is out of range, or no player is within 60 m to simulate it.
     */
    setVelocity(velocity: Vector3, angularVelocity?: Vector3): boolean;

    /**
     * Starts a resting `rigid` prop simulating where it lies, so it falls if nothing holds it up. `propRest` is raised once it stops.
     * @returns True when it was sent to the client simulating the prop; false when the prop is gone or is not `rigid`, a value is not finite or is out of range, or no player is within 60 m to simulate it.
     */
    wake(): boolean;

    /**
     * Stops a `rigid` prop's body where it is, in mid-air if need be. It wakes again when something hits it or a script moves it. `propRest` then reports where it stopped. To fix a prop in place for good, set `physics` to `static` once `propRest` has been raised.
     * @returns True when it was sent to the client simulating the prop; false when the prop is gone or is not `rigid`, a value is not finite or is out of range, or no player is within 60 m to simulate it.
     */
    sleep(): boolean;

    /**
     * Stops a `rigid` prop's body and puts it back where it was spawned, for every player. Raises no `propRest`.
     * @returns True when it was put back; false when the prop is gone or is not `rigid`.
     */
    resetPhysics(): boolean;

    /**
     * Spawns and replicates a static mesh from the game's own object catalog.
     * @param model Mesh to build, as either a full catalog path (`objects/manmade/barrels/barrel_a.cgf`) or its file stem (`barrel_a`). A stem several meshes share resolves to the first of them, so pass the path when it matters which. A mesh this server streams -- a `.cgf` in a resource's `stream/objects/kcdc/<resource>/` folder -- is named by its full path, `objects/kcdc/<resource>/chair.cgf`; a player whose game does not have it yet sees the prop as soon as it does.
     * @param position Optional world-space spawn position; omitted components default to zero.
     * @param rotation Optional initial orientation: a Quaternion, or a Vector3 of Euler angles in degrees.
     * @param scale Uniform size or separate X/Y/Z sizes, each from 0.01 to 100. Omitted uses the mesh size. Nonuniform collision follows the native engine limitations.
     * @param physics Optional collision: `static` (the default), `rigid` or `none`.
     * @param virtualWorld Optional virtual world the prop belongs to; omitted puts it in the global one.
     * @returns The newly spawned prop handle.
     */
    static spawn(model: string, position?: Vector3 | Partial<Vector3>, rotation?: Vector3 | Quaternion, scale?: number | Vector3, physics?: string, virtualWorld?: number): Prop;

    /**
     * Lists every prop the server currently has.
     * @returns One handle per live prop, in no particular order.
     */
    static all(): Prop[];

    /**
     * Looks a prop up by its network entity ID.
     * @param id Network entity identifier.
     * @returns The prop's handle, or null when no live prop has that ID.
     */
    static getById(id: number): Prop | null;

    /**
     * Despawns props, emitting propDestroy for each one.
     * @param virtualWorld Optional virtual world to clear; omitted clears every one of them.
     * @returns How many props were removed.
     */
    static destroyAll(virtualWorld?: number): number;
  }

  interface Prop extends Entity {}

  /**
   * Replicated cart or wagon handle.
   */
  class Cart {
    /**
     * Creates a script wrapper for an existing cart with this ID; use Cart.spawn() to spawn one.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * The game prefab this cart was built from, e.g. `wagon_b_covered`. `Cart.blueprints()` lists them all.
     */
    readonly blueprint: string;

    /**
     * How many horses the cart is harnessed for: 2 for a wagon, 1 for the two-wheeled cart, 0 for `wagon_b_covered_empty`, whose chassis has nowhere to hitch one and so can be sat in but never pulled.
     */
    readonly horseCount: number;

    /**
     * The player at the reins, or null when nobody drives it.
     */
    readonly driver: Player | null;

    /**
     * How the driver last asked the cart to go: `stand`, `walk`, `trot` or `reverse`.
     */
    readonly pace: string;

    /**
     * The names of this cart's seats, the reins first: six on a wagon, three on the two-wheeler.
     */
    readonly seats: string[];

    /**
     * The fastest the driver may take it urged on (the trot key held), in metres a second, 0 to 15; 7 by default. Unurged the team trots at 3.6 or this, whichever is less. A value outside the range is ignored.
     */
    maxSpeed: number;

    /**
     * Formats this cart handle for logging and debugging.
     * @returns The cart ID, its blueprint, its driver and its pace.
     */
    toString(): string;

    /**
     * Despawns this cart on every client. Everyone in it is let out first, each with a `cartExit`, then `cartDestroy` is raised.
     */
    destroy(): void;

    /**
     * Puts a player in a seat. They are taken out of any cart they were in, their own game walks them to the seat and climbs in, and `cartEnter` follows; the driver's client runs the cart from then on. `cartEntering` handlers are asked first.
     * @param player The player to seat.
     * @param seat One of the cart's `seats`. A wagon has `driver` (the reins, on the bench's left), `bench` (beside them), `rightBack`, `leftBack`, `rightFront` and `leftFront` (on the rails); the two-wheeler `driver`, `rightBack` and `leftBack`.
     * @returns Whether the player was seated: false for a taken seat, a player that is not connected, or a handler's refusal.
     */
    putPlayer(player: Player, seat: string): boolean;

    /**
     * Lets a player out of this cart: their own game climbs down, and `cartExit` is raised.
     * @param player The player to let out.
     * @returns False when the player is not in this cart.
     */
    removePlayer(player: Player): boolean;

    /**
     * Who sits in a seat.
     * @param seat One of the cart's `seats`.
     * @returns The player in it, or null when it is empty.
     */
    getOccupant(seat: string): Player | null;

    /**
     * Where a player sits in this cart.
     * @param player The player to look for.
     * @returns The seat's name, one of `seats`, or null when they are not in it.
     */
    seatOf(player: Player): string | null;

    /**
     * Moves the cart outright, standing, with everyone in it; a driver carries on from there.
     * @param position Where the cart's front axle stands.
     * @param rotation Which way it faces; omitted keeps its heading.
     * @returns False for a pose that is not finite.
     */
    teleport(position: Vector3, rotation?: Vector3 | Quaternion): boolean;

    /**
     * Spawns and replicates a cart or wagon from the game's own prefabs, with its horses already in the shafts. It stands where it was put until somebody takes the reins.
     * @param blueprint Which of the game's cart prefabs to build; omitted builds `wagon_b_covered`. `Cart.blueprints()` lists them.
     * @param position Where the cart's front axle stands; omitted components default to zero.
     * @param rotation Which way it faces: a Quaternion, or a Vector3 of Euler angles in degrees.
     * @param virtualWorld Optional virtual world the cart belongs to; omitted puts it in the global one.
     * @returns The newly spawned cart handle.
     */
    static spawn(blueprint?: string, position?: Vector3 | Partial<Vector3>, rotation?: Vector3 | Quaternion, virtualWorld?: number): Cart;

    /**
     * Lists the carts the server has.
     * @param virtualWorld Optional virtual world to list; omitted lists every one.
     * @returns One handle per live cart, in no particular order.
     */
    static all(virtualWorld?: number): Cart[];

    /**
     * Looks a cart up by its network entity ID.
     * @param id Network entity identifier.
     * @returns The cart's handle, or null when no live cart has that ID.
     */
    static getById(id: number): Cart | null;

    /**
     * Despawns carts, emitting cartDestroy for each one.
     * @param virtualWorld Optional virtual world to clear; omitted clears every one of them.
     * @returns How many carts were removed.
     */
    static destroyAll(virtualWorld?: number): number;

    /**
     * Names every cart prefab `Cart.spawn` can build.
     * @returns The blueprint names, in catalog order.
     */
    static blueprints(): string[];
  }

  interface Cart extends Entity {}

  /**
   * Replicated particle effect handle.
   */
  class Vfx {
    /**
     * Creates a script wrapper for an existing effect with this ID; use Vfx.spawn() to place one.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * Name of the particle effect this is playing, e.g. `WH_Particels.fires.campfire_a`. Read-only: every spawn parameter is read once when the emitter is created, so an effect is the one it was placed as.
     */
    readonly effect: string;

    /**
     * Particle library the effect came out of, e.g. `WH_Particels`, which is the part of the name before the first dot.
     */
    readonly library: string;

    /**
     * Authored bucket inside that library, e.g. `fires`.
     */
    readonly group: string;

    /**
     * Multiplies every size the effect authored, from 0.01 to 32; a value outside that is clamped into it. Assignment rebuilds the emitter on every client that can see it, so the effect restarts.
     */
    scale: number;

    /**
     * Multiplies how many particles are emitted, from 0.01 to 16. Assignment restarts the effect.
     */
    countScale: number;

    /**
     * Multiplies emission speed, from 0 to 16. Assignment restarts the effect.
     */
    speedScale: number;

    /**
     * Multiplies how fast the emitter's own time runs, from 0.01 to 16. Assignment restarts the effect.
     */
    timeScale: number;

    /**
     * Feeds the effect's own strength curves, from -1 to 1; -1 leaves them where the effect authored them. Assignment restarts the effect.
     */
    strength: number;

    /**
     * Seconds between restarts of the whole emitter, from 0 to 600; 0 never restarts it. Assignment restarts the effect.
     */
    pulsePeriod: number;

    /**
     * Formats this effect handle for logging and debugging.
     * @returns The effect ID, its name and its scale.
     */
    toString(): string;

    /**
     * Stops this effect on every client that can see it, after emitting vfxDestroy.
     */
    destroy(): void;

    /**
     * Places a particle effect in the world and leaves it running. It is a replicated entity, so the interest grid streams it to whoever comes near and takes it away again when they leave -- which is what a campfire, a torch or a plume of smoke needs and what `burst` cannot do. How far it streams is taken from the effect's own draw distance, between 25 and 250 metres.
     * @param effect Effect to place, spelled as the game's particle libraries spell it -- `WH_Particels.fires.campfire_a`. `Vfx.list()` is the whole vocabulary.
     * @param position Optional world-space position; omitted components default to zero.
     * @param options `rotation` aims the emitter; `scale`, `countScale`, `speedScale` and `timeScale` multiply what the effect authored; `strength` feeds its strength curves; `pulsePeriod` restarts it on that many seconds; `prime` starts it already running, which is what a fire wants. `durationMs` is ignored here -- a placed effect lasts until it is destroyed.
     * @param virtualWorld Optional virtual world the effect belongs to; omitted puts it in the global one.
     * @returns The newly placed effect handle.
     */
    static spawn(effect: string, position?: Vector3 | Partial<Vector3>, options?: { rotation?: Quaternion | Vector3; scale?: number; countScale?: number; speedScale?: number; timeScale?: number; strength?: number; pulsePeriod?: number; prime?: boolean; durationMs?: number }, virtualWorld?: number): Vfx;

    /**
     * Plays an effect once for the players who can see the point it happens at, and keeps nothing. Recipients are chosen by the effect's own draw distance -- there is no point telling a client about something its engine would not draw -- so a blood spray reaches the people standing there and nobody else. A player who arrives afterwards sees nothing, which is the right answer for a spark and the wrong one for a campfire; use `spawn` for those.
     * @param effect Effect to play, spelled as the game's particle libraries spell it.
     * @param position Where it happens, in world-space metres.
     * @param options As `spawn`, and here `durationMs` does apply: how long each client keeps the emitter, two seconds by default and a minute at most.
     * @param virtualWorld Optional virtual world; omitted uses the global one.
     * @returns How many clients were told.
     */
    static burst(effect: string, position: Vector3, options?: { rotation?: Quaternion | Vector3; scale?: number; countScale?: number; speedScale?: number; timeScale?: number; strength?: number; pulsePeriod?: number; prime?: boolean; durationMs?: number }, virtualWorld?: number): number;

    /**
     * Plays an effect once on a replicated entity, where it follows that entity: blood on the body that was hit, sparks on the weapon that struck. A client that has not streamed the entity in drops it rather than playing it somewhere else.
     * @param effect Effect to play, spelled as the game's particle libraries spell it.
     * @param entity What to hang it on: any replicated handle -- a Player, Prop, Horse or Dog -- or a bare network entity ID.
     * @param options As `burst`, plus `offset`: where the emitter sits in the entity's own space, in metres, up to 8 m from its origin.
     * @returns How many clients were told.
     */
    static burstOn(effect: string, entity: Entity | number, options?: { offset?: Vector3; rotation?: Quaternion | Vector3; scale?: number; countScale?: number; speedScale?: number; timeScale?: number; strength?: number; pulsePeriod?: number; prime?: boolean; durationMs?: number }): number;

    /**
     * Lists every replicated effect the server currently has.
     * @param virtualWorld Optional virtual world to list; omitted lists every one of them.
     * @returns One handle per live effect, in no particular order.
     */
    static all(virtualWorld?: number): Vfx[];

    /**
     * Looks a replicated effect up by its network entity ID.
     * @param id Network entity identifier.
     * @returns The effect's handle, or null when no live effect has that ID.
     */
    static getById(id: number): Vfx | null;

    /**
     * Stops replicated effects, emitting vfxDestroy for each one.
     * @param virtualWorld Optional virtual world to clear; omitted clears every one of them.
     * @returns How many effects were stopped.
     */
    static destroyAll(virtualWorld?: number): number;

    /**
     * Every effect name the shipped game declares, in order. Mined from the game's particle libraries at build time, so it is the same list the client has.
     * @param prefix Keep only the names starting with this, e.g. `WH_Particels.fires` or `collisions.combat`.
     * @returns The matching names.
     */
    static list(prefix?: string): string[];
  }

  interface Vfx extends Entity {}

  /**
   * Replicated point light that illuminates its surroundings.
   */
  class Light {
    /**
     * Creates a handle for an existing entity.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * World-space position in metres. Invalid positions throw before mutation.
     */
    position: Vector3;

    /**
     * RGB color using x, y and z, each from 0 to 1. Invalid values throw before mutation.
     */
    color: Vector3;

    /**
     * Diffuse intensity multiplier, from 0 to 100. Invalid values throw before mutation.
     */
    intensity: number;

    /**
     * Light radius in metres, from 0.1 to 100. Invalid values throw before mutation.
     */
    radius: number;

    /**
     * Whether this light casts native shadows. False by default; only booleans are accepted.
     */
    castShadows: boolean;

    /**
     * Removes this light from every client. Safe to repeat.
     */
    destroy(): void;

    /**
     * Creates a persistent point light, streamed to nearby players including late arrivals. Removed when its creating script resource stops. Exported world lights are owned by the server.
     * @param position World-space position in metres.
     * @param options RGB color 0..1, intensity 0..100 and radius 0.1..100 metres. Defaults to warm orange, intensity 1, radius 5 and castShadows false. Set castShadows to true for native shadows.
     * @returns The new light handle.
     */
    static spawn(position: Vector3 | { x: number; y: number; z: number }, options?: { color?: Vector3 | { x: number; y: number; z: number }; intensity?: number; radius?: number; castShadows?: boolean }): Light;
  }

  interface Light extends Entity {}

  /**
   * One player's view, directed from the server. A shot is sent and forgotten: the server keeps no record of it, so a player who reconnects or changes level has their own view until they are sent another.
   */
  const Camera: {
    /**
     * Films one player's view from a fixed point: what a cutscene, an intro or a kill cam is made of. It holds until `clearShot`, or glides on to the next `setShot`.
     * 
     * This is the client's `Camera.setShot`, sent to that player; whichever of the two comes last wins. Nothing about the player's controls changes, and nothing about where their body is: their character stays where it stood, drawn head and all for as long as the camera is looking at it from outside its eyes. A player flying with `NoClip` refuses the shot.
     * @param player Whose view to film.
     * @param options `position` is where the camera stands and `lookAt` the point it faces, both in world space. `duration` is how long it takes to get there from wherever the player's view is, in milliseconds: 0, the default, is a cut, and the most is 60000. `fov` is the vertical field of view in degrees, 20 to 120; leave it out to keep the player's own.
     * @returns True when the shot was sent, false when `position` and `lookAt` are the same point.
     */
    setShot(player: Player, options: { position: Vector3; lookAt: Vector3; duration?: number; fov?: number }): boolean;

    /**
     * Gives a player their own view back. Harmless for a player without a shot.
     * @param player Whose view to give back.
     * @param duration How long the glide back into the player's eyes takes, in milliseconds: 0, the default, is a cut, and the most is 60000.
     * @returns True when it was sent.
     */
    clearShot(player: Player, duration?: number): boolean;
  };

  /**
   * Replicated ground marker handle.
   */
  class Marker {
    /**
     * Creates a script wrapper for an existing marker with this ID; use Marker.place() to draw one.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * Decal material this marker is drawn with, e.g. `materials/decals/chalk_cross`. Read-only: a decal node reads its material once, so a marker is the one it was placed as.
     */
    readonly material: string;

    /**
     * Subfolder the material came out of under `materials/decals/`, empty at the top level.
     */
    readonly group: string;

    /**
     * What the marker is made of: `decal` for the projected one, or `cylinder`, `sphere`, `chevron`, `cube` or `plane` for a mesh standing in the world. Read-only: changing it is a rebuild, so place a new marker instead.
     */
    readonly shape: string;

    /**
     * How wide the marker is, in metres, from 0.25 to 64; a value outside that is clamped into it. Assignment restates it in place rather than rebuilding, and widens how far the marker streams.
     */
    size: number;

    /**
     * How tall a mesh marker stands, in metres, from 0.25 to 64. Ignored by `decal`, `sphere` and `plane`, which are sized by width alone.
     */
    height: number;

    /**
     * Degrees per second the marker turns about its own up axis, from -720 to 720; 0 stands still. Played on each client's own clock, so two players need not see it at the same angle. Ignored by `decal`.
     */
    spin: number;

    /**
     * Packed 0xRRGGBBAA tint, or 0 for the material as it ships. The shipped material is shared by everything that names it, so a tint is a private clone rather than a write to the original -- which is why assignment rebuilds the marker.
     */
    color: number;

    /**
     * Whether clients watch the local player against this marker's volume and report crossings, which is what raises `markerEnter` and `markerExit`. Off by default: a marker nobody listens to should cost nothing but its draw.
     */
    trigger: boolean;

    /**
     * How far the marker rises and falls from its resting height, in metres, up to 4; 0 stands still. Played on each client's own clock. Ignored by `decal`, which cannot leave the surface it projects onto.
     */
    bob: number;

    /**
     * Formats this marker handle for logging and debugging.
     * @returns The marker ID, its material and its size.
     */
    toString(): string;

    /**
     * Removes this marker from every client that can see it, after emitting markerRemove.
     */
    remove(): void;

    /**
     * Puts a marker in the world and leaves it there. It is a replicated entity, so the interest grid streams it to whoever comes near and takes it away again when they leave -- which is what a zone or a checkpoint needs and what a one-shot message cannot do. How far it streams is taken from its own extent, between 50 and 400 metres.
     * @param material Decal material to project, spelled as the game's own data spells it -- `materials/decals/chalk_cross`. `Marker.list()` is the whole vocabulary.
     * @param position Optional world-space position; omitted components default to zero.
     * @param options `shape` picks what it is made of -- `decal` projects onto the surface under it, the rest stand in the world as meshes and can be seen from below a ridge. `rotation` aims it, so a marker laid on a wall is the same call with a different rotation; `size` and `height` are its extent in metres; `spin` and `bob` give a mesh marker motion; `color` tints it.
     * @param virtualWorld Optional virtual world the marker belongs to; omitted puts it in the global one.
     * @returns The newly drawn marker handle.
     */
    static place(material: string, position?: Vector3 | Partial<Vector3>, options?: { shape?: 'decal' | 'cylinder' | 'sphere' | 'chevron' | 'cube' | 'plane'; rotation?: Quaternion | Vector3; size?: number; height?: number; spin?: number; bob?: number; color?: number; trigger?: boolean }, virtualWorld?: number): Marker;

    /**
     * Lists every replicated marker the server currently has.
     * @param virtualWorld Optional virtual world to list; omitted lists every one of them.
     * @returns One handle per live marker, in no particular order.
     */
    static all(virtualWorld?: number): Marker[];

    /**
     * Looks a replicated marker up by its network entity ID.
     * @param id Network entity identifier.
     * @returns The marker's handle, or null when no live marker has that ID.
     */
    static getById(id: number): Marker | null;

    /**
     * Removes markers, emitting markerRemove for each one.
     * @param virtualWorld Optional virtual world to clear; omitted clears every one of them.
     * @returns How many markers were removed.
     */
    static removeAll(virtualWorld?: number): number;

    /**
     * Every decal material the shipped game preloads, in order. Mined from the game's own material folder at build time, so it is the same list the client has.
     * @param prefix Keep only the names starting with this, e.g. `materials/decals/burglar`.
     * @returns The matching names.
     */
    static list(prefix?: string): string[];
  }

  interface Marker extends Entity {}

  /**
   * Replicated compass and map-screen blip handle.
   */
  class Blip {
    /**
     * Creates a script wrapper for an existing blip with this ID; use Blip.create() to place one.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * Which of the game's own compass icons the blip draws as, by the game's own name for it -- `Blip.types()` lists them. Assigning a name the game does not know leaves the blip as it was.
     */
    type: string;

    /**
     * The mark's state, forwarded to the compass and map movies as-is -- what the client Blip calls `state`, renamed here because every entity's `state` is its StateBag. What a given value draws is undocumented; 0 is what a fresh game mark carries.
     */
    markState: number;

    /**
     * The blip's name on the map screen, shown verbatim in place of the name the game gives its icon type; empty keeps that name. Cut to 64 bytes. The compass has no text, so it never shows this.
     */
    label: string;

    /**
     * Whether the blip is drawn on players' HUD compasses.
     */
    compass: boolean;

    /**
     * Whether the blip is drawn on players' map screens, for an icon the map has art for: `Main`, `Side`, `Micro`, `Checkpoint`, `Dlcs`, `Bailiff`, `FistFight` and `Racing` have none and stay on the compass. The map legend's switch for the blip's icon type hides it like one of the game's own.
     */
    map: boolean;

    /**
     * Formats this blip handle for logging and debugging.
     * @returns The blip ID, its type and its label.
     */
    toString(): string;

    /**
     * Takes this blip off every compass and map screen that shows it.
     */
    remove(): void;

    /**
     * Puts a blip on the compass and map screen of every player in its virtual world, or of the one player it is private to. It is a replicated entity that is never culled by distance -- a blip is for finding something far away -- so it reaches players however far they are and whenever they join, and assigning `position` moves it for all of them. `setVisibleTo` works on it like on any entity.
     * @param options Where the blip goes, which icon it draws with -- one of `Blip.types()`, `GeneralPoi` by default -- its mark state, its name on the map screen, and whether it shows on the compass and the map screen, both by default. `player` makes it private to that player from the start; `virtualWorld` puts a public one in that world, the global one by default.
     * @returns The new blip handle.
     */
    static create(options: { position: Vector3; type?: string; markState?: number; label?: string; compass?: boolean; map?: boolean; player?: Player; virtualWorld?: number }): Blip;

    /**
     * Lists every blip the server currently has.
     * @param virtualWorld Optional virtual world to list; omitted lists every one of them.
     * @returns One handle per live blip, in no particular order.
     */
    static all(virtualWorld?: number): Blip[];

    /**
     * Looks a blip up by its network entity ID.
     * @param id Network entity identifier.
     * @returns The blip's handle, or null when no live blip has that ID.
     */
    static getById(id: number): Blip | null;

    /**
     * Removes blips.
     * @param virtualWorld Optional virtual world to clear; omitted clears every one of them.
     * @returns How many blips were removed.
     */
    static removeAll(virtualWorld?: number): number;

    /**
     * Every icon a blip can draw with, by the game's own name for it, misspellings included.
     * @returns The names, in the game's own order.
     */
    static types(): string[];
  }

  interface Blip extends Entity {}

  /** */
  interface LevelEditDefinition {
    /**
     * What the object is; a brush when omitted. A brush has no identity of its own, so it is found by its mesh and where the level put it; a level entity by its EntityGuid.
     */
    kind?: 'brush' | 'entity' | undefined;

    /**
     * A brush's mesh path, `objects/...cgf`, as the World Builder shows it; required for a brush. An entity's name, kept for reading only.
     */
    name?: string | undefined;

    /**
     * An entity's level EntityGuid as sixteen hex digits; required for an entity.
     */
    guid?: string | undefined;

    /**
     * An entity's class, kept for reading only.
     */
    class?: string | undefined;

    /**
     * Where the level put the object: its pivot, matched within 5 cm. A brush's key, with its mesh.
     */
    at: Vector3 | number[];

    /**
     * Whether it is taken out of the world: not drawn and not solid.
     */
    hidden?: boolean | undefined;

    /**
     * A mesh that stands in its place: a prop catalog path such as `objects/natural/vegetation/trees/stumps/stump_fagus_a.cgf`, or a mesh this server streams. The object goes out of the world, collision included, and this mesh stands where it stood -- or at `position` -- as solid static scenery.
     */
    model?: string | undefined;

    /**
     * Where it stands instead; present, the edit is a move.
     */
    position?: Vector3 | number[] | undefined;

    /**
     * A move's orientation: a Quaternion, {x, y, z, w} or [x, y, z, w]. None when omitted.
     */
    rotation?: Quaternion | number[] | undefined;

    /**
     * A move's scale per axis, from 0.01 to 100. None when omitted.
     */
    scale?: Vector3 | number[] | undefined;
  }

  /**
   * One change the server makes, for every player, to an object the level itself placed -- a brush, a tree or a level entity taken out of the world, moved, swapped for another mesh, or a mix of those. It is what a World Builder map's `world` array carries. `WorldRayHit.levelObject` names the object a trace met in the form `apply` takes.
   */
  class LevelEdit {
    /**
     * Wraps an edit the server already holds; use LevelEdit.apply to make one, or WorldResource for a whole export.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * Immutable network entity identifier.
     */
    readonly id: number;

    /**
     * False once the edit has been restored; every other read then returns its empty value.
     */
    readonly exists: boolean;

    /**
     * Whether the object is a brush, found by its mesh and where the level put it, or a level entity, found by its EntityGuid.
     */
    readonly kind: 'brush' | 'entity';

    /**
     * A brush's mesh path, or the name the level gives the entity.
     */
    readonly name: string;

    /**
     * An entity's level EntityGuid as sixteen lowercase hex digits; empty for a brush.
     */
    readonly guid: string;

    /**
     * The virtual world whose players see the edit, fixed when it was applied.
     */
    readonly virtualWorld: number;

    /**
     * Whether the object is taken out of the world: not drawn and not solid. Assignment takes it out or puts it back for every player, keeping any move.
     */
    hidden: boolean;

    /**
     * Whether the object stands somewhere other than where the level placed it; `move` and `clearMove` change it.
     */
    readonly moved: boolean;

    /**
     * The mesh standing in the object's place, or empty. Assigning a prop catalog path or a mesh this server streams swaps the object for it on every player: the object goes, collision included, and the mesh stands where it stood -- or where `move` put it -- as solid static scenery. A felled tree leaves its stump this way. Assigning an empty string gives the object back. Throws for any other path.
     */
    model: string;

    /**
     * Formats this edit for logging and debugging.
     * @returns Its id, kind, object and what it does to it.
     */
    toString(): string;

    /**
     * Moves the object for every player, collision included.
     * @param position Where the object stands instead.
     * @param rotation Its orientation: a Quaternion, or Euler angles in degrees. Left out, it keeps the last move's.
     * @param scale A uniform scale or one per axis, from 0.01 to 100. Left out, it keeps the last move's.
     * @returns False for a gone edit or a transform that is not finite.
     */
    move(position: Vector3, rotation?: Quaternion | Vector3, scale?: number | Vector3): boolean;

    /**
     * Puts the object back where the level placed it, keeping it out of the world if the edit takes it out.
     */
    clearMove(): void;

    /**
     * Drops the edit: every player gets the object back exactly as the level had it, collision included.
     */
    restore(): void;

    /**
     * The definition `LevelEdit.apply` takes to make this edit again, which is what a script saves: the server keeps nothing past a restart. It is one entry of a World Builder map's `world` array.
     * @returns Null once the edit is gone.
     */
    toJSON(): LevelEditDefinition | null;

    /**
     * Takes an object the level placed out of the world, moves it, or swaps it for another mesh, for every player in the virtual world -- whenever they join, and however far away they are. Collision goes with it. An edit already made to the same object in that world is restated rather than doubled, so applying one twice changes nothing. Kept in memory only: apply it again on boot. A `WorldRayHit.levelObject` is a definition already: spread it and add what to do, as in `LevelEdit.apply({ ...hit.levelObject, model: stump })`.
     * @param definition The object and what to do to it, in the format of one entry of a World Builder map's `world` array.
     * @param virtualWorld Optional virtual world whose players see it; the global one when omitted.
     * @returns The edit. Throws for a definition no client could find the object by, or a `model` no client could load.
     */
    static apply(definition: LevelEditDefinition, virtualWorld?: number): LevelEdit;

    /**
     * Lists every edit the server currently has.
     * @param virtualWorld Optional virtual world to list; omitted lists every one of them.
     * @returns One handle per live edit, in no particular order.
     */
    static all(virtualWorld?: number): LevelEdit[];

    /**
     * Looks an edit up by its network entity ID.
     * @param id Network entity identifier.
     * @returns The edit, or null when no live edit has that ID.
     */
    static getById(id: number): LevelEdit | null;

    /**
     * Drops edits, giving every player the objects back as the level had them.
     * @param virtualWorld Optional virtual world to clear; omitted clears every one of them.
     * @returns How many edits were dropped.
     */
    static restoreAll(virtualWorld?: number): number;
  }

  /**
   * A server-owned NPC: spawned by a resource, simulated by whichever client is nearest.
   */
  class Npc {
    /**
     * Creates a script wrapper for an existing NPC with this ID; use Npc.create() to spawn one.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * GUID of the soul the body was spawned against, or empty for an adopted level body. Read-only: a soul decides what the body is, so a different one is a different NPC.
     */
    readonly soul: string;

    /**
     * Native entity class, including `NPC`, `NPC_Female`, or the matching animal class from `Npc.animals()`.
     */
    readonly actorClass: string;

    /**
     * `EntityGuid` of the level body this NPC adopted, or 0 for a spawned one.
     */
    readonly levelGuid: number;

    /**
     * What every client shows over the body and in conversation. Empty leaves the name its soul was born with.
     */
    name: string;

    /**
     * Faction row used for relationship and crime decisions; 0 is no faction.
     */
    faction: number;

    /**
     * The server's ledger of this body's health, clamped to `maxHealth`. Combat damage arrives here after the server has agreed to it.
     */
    health: number;

    /**
     * Health the body starts with and is revived to.
     */
    readonly maxHealth: number;

    /**
     * False once health reached zero. A dead NPC is still an entity -- it is a corpse, and it can still be looted or revived.
     */
    readonly alive: boolean;

    /**
     * Whether damage is refused before it reaches the ledger.
     */
    invulnerable: boolean;

    /**
     * Whether the body holds its pose whatever its intent says.
     */
    frozen: boolean;

    /**
     * Whether clients watch the use key against this body and raise `npcInteract`. On by default.
     */
    interactable: boolean;

    /**
     * Whether its name is drawn over it the way a player's is.
     */
    nametag: boolean;

    /**
     * Whether its corpse keeps its inventory for whoever searches it.
     */
    lootable: boolean;

    /**
     * How the client simulator moves the body. `native` (default) uses the game's movement controller and walk animations. `kinematic` moves the body directly without walk animation. Only these exact, case-sensitive strings are accepted; invalid values throw without changing the mode. Neither finds a way around anything: the game's movement controller steers straight at the point it is given. Routes come from server pathfinding, which hands either mode mesh corners -- see `moveTo`.
     */
    locomotion: 'native' | 'kinematic';

    /**
     * What the NPC has been told to do: `hold`, `moveTo`, `follow`, `flee`, `lookAt`, `playAnim`, `talk` or `attack`.
     */
    readonly intent: string;

    /**
     * Current patrol snapshot and progress, or null when not patrolling. Updating a route does not change a running patrol.
     */
    readonly patrolState: PatrolState | null;

    /**
     * What has become of the current order: `idle`, `running`, `reached`, `blocked` or `failed`. A move reads `running` from the moment it is given -- while its route is planned and while it walks the route's corners -- until it ends, and then the outcome `npcIntentDone` announced, which stays until the next order. A follow reads `reached` while it is within its radius. Dormant moves in `auto` or `server` mode are carried out and decided by the server; dormant `game` moves wait for a client simulator.
     */
    readonly status: string;

    /**
     * The body under the clothes, as the game's own component names. Write it with `setAppearance`.
     */
    readonly appearance: Appearance;

    /**
     * Its worn item classes. Before default stock is established an empty list leaves the soul's own outfit; afterward empty means undressed. Write it with `wear`, `setOutfit` or `Inventory.set`.
     */
    readonly wearing: string[];

    /**
     * The player whose client is currently running this NPC, or null while it is dormant. Dormant is not broken: nobody is near enough for it to matter, and the server keeps its route advancing until somebody is.
     */
    readonly simulator: Player | null;

    /**
     * Formats this NPC handle for logging and debugging.
     * @returns The NPC's ID, name, intent and last reported status.
     */
    toString(): string;

    /**
     * Checks whether this NPC's nametag is drawn; the same switch as the `nametag` property.
     * @returns True while the nametag is shown.
     */
    isNametagVisible(): boolean;

    /**
     * Shows or hides the name over this NPC's head for every player; the same switch as the `nametag` property. Each player can still hide all nametags locally.
     * @param visible True to draw this NPC's nametag for every player, false to hide it.
     */
    setNametagVisible(visible: boolean): void;

    /**
     * Checks whether the health bar under this NPC's nametag is shown.
     * @returns True unless the health bar was hidden.
     */
    isNametagHealthVisible(): boolean;

    /**
     * Shows or hides the health bar under this NPC's nametag, leaving the name itself alone.
     * @param visible True to show the health bar under this NPC's name, false to hide it.
     */
    setNametagHealthVisible(visible: boolean): void;

    /**
     * Reads this NPC's nametag text override.
     * @returns The override, or an empty string when the NPC's `name` is drawn.
     */
    getNametagText(): string;

    /**
     * Overrides the text drawn on this NPC's nametag without renaming it: `name` still reaches conversation and the soul.
     * @param text Text to show instead of the NPC's name; empty or omitted restores the name.
     */
    setNametagText(text?: string): void;

    /**
     * Reads this NPC's nametag color.
     * @returns Packed 0xAARRGGBB color; opaque white when untinted.
     */
    getNametagColor(): number;

    /**
     * Tints the text on this NPC's nametag.
     * @param color Packed 0xAARRGGBB color.
     */
    setNametagColor(color: number): void;

    /**
     * Returns game-data loot candidates for this animal. Does not roll or populate stock. The gamemode chooses chances and writes Inventory.set at spawn. Null for non-animals, excluded souls or unresolved adopted bodies; an empty array is a known empty preset.
     */
    getAnimalLoot(): AnimalLootItem[] | null;

    /**
     * Reads server-owned stock and worn row counts. Inventory.get/add/remove/setProperties/set/transfer also accept Npc handles. Default native stock becomes ready at npcInventoryReady; Inventory.set may provide it before a body exists.
     */
    getInventory(): InventoryState | null;

    /**
     * Despawns this NPC everywhere, after emitting npcDestroy.
     */
    remove(): void;

    /**
     * Cancels whatever it was doing and leaves it standing where it is.
     */
    hold(): void;

    /**
     * Moves to `position`, cancels any patrol, and emits `npcIntentDone` once when the move ends.
     * 
     * The game's own movement steers a body straight at the point it is given and finds no way around anything, so routes come from the server's navigation mesh. The server plans the route and hands the simulating client one corner at a time, the next before the body reaches the last, so it walks through bends without stopping; a dormant NPC is walked along the same route by the server. Routes only use doorways whose door stands open.
     * 
     * `auto` (the default) routes on the server whenever a mesh is loaded. Without one, a simulating client steers straight at `position` and a dormant NPC moves in a straight line. `game` steers straight at `position` and waits while dormant; if the body reports `blocked` and a mesh is loaded, the server tries a route before emitting `npcIntentDone`. `server` always routes on the server and throws without a loaded mesh, preserving the previous order.
     * 
     * A missing or partial route ends with `blocked`, and a blocked route is not retried. Route plans are queued and spread over ticks; the move reads `running` meanwhile.
     * @param position Where to walk to.
     * @param options `speed` defaults to `walk`; `radius` is the arrival distance in metres (default 1.5). `pathfinding` defaults to `auto`.
     * @returns True when the order went out; false for an invalid NPC or destination.
     */
    moveTo(position: Vector3 | Partial<Vector3>, options?: { speed?: 'walk' | 'jog' | 'run'; radius?: number; pathfinding?: 'auto' | 'game' | 'server' }): boolean;

    /**
     * Walks the waypoints in order. The server stores the route and replicates the current target. `npcIntentDone` fires once per leg, after its final mesh corner or a movement failure.
     * 
     * A leg that ends `blocked` or `failed` is skipped after at least 2 seconds. When every leg of a lap has failed in a row, the patrol is abandoned and that last leg reports `failed` instead.
     * @param points Waypoints, a named route handle, or its stable id. Named routes are copied at start.
     * @param options Point arrays retain their defaults: loop, walk, zero wait. Named routes use their authored defaults. Call options override route defaults; waypoint overrides take priority. Use mode for once, loop or pingPong; do not combine mode and loop. radius is the arrival distance. pathfinding uses the moveTo modes for every leg.
     * @returns True when the route was accepted; false for an empty route, more than 256 waypoints, a waypoint that is not finite or lies outside the world, or a `waitSeconds` that is not a number. Throws if server mode has no loaded mesh, leaving the previous order intact.
     */
    patrol(points: (Vector3 | Partial<Vector3>)[] | PatrolRoute | string, options?: { speed?: 'walk' | 'jog' | 'run'; loop?: boolean; mode?: 'once' | 'loop' | 'pingPong'; radius?: number; waitSeconds?: number; pathfinding?: 'auto' | 'game' | 'server' }): boolean;

    /**
     * Orders native combat against a live player or NPC in the same visible world within 128 metres. Another order stops it. The order fails when its target dies, disappears or leaves that range. It waits while dormant; no offline damage is simulated. Bow attacks stop within 18 metres and use native projectiles. Equipped NPC arrows are replenished while shooting.
     * @param target The combatant to pursue and attack.
     * @param options Defaults to melee. Bow requires an equipped bow and compatible arrows.
     * @returns True when accepted. Invalid targets leave the previous order unchanged.
     */
    attack(target: Player | Npc | number, options?: { weapon?: "melee" | "bow" }): boolean;

    /**
     * Keeps the NPC near somebody as they move, steering straight at them. `npcIntentDone` reports `reached` the first time it catches up, and the follow carries on. If the body reports `blocked`, a loaded mesh lets the server route to the target's current position before reporting it. After recovery it resumes following the moving target.
     * @param target Who to follow, as a handle or a network ID.
     * @param options `radius` is how close it tries to stay, in metres; `speed` is the pace.
     * @returns True when the order went out.
     */
    follow(target: Player | Npc | number, options?: { speed?: 'walk' | 'jog' | 'run'; radius?: number }): boolean;

    /**
     * Sends the NPC away from a place, or from a player or NPC; `npcIntentDone` reports `reached` once the requested distance separates them.
     * 
     * An animal runs the game's own escape: its own mind picks the pace and the path, and runs as far as its kind runs, so `reached` comes only when that run takes it the requested distance clear. A mind that has not taken the escape within five seconds fails the order. That mind keeps the animal until another order replaces this one, so once clear it goes on as the game's animals do; a client that takes the animal over after `reached` holds it where it stands. A dormant animal waits for a client to run it.
     * 
     * Anyone else steers straight away from the place, or from where the player or NPC stood when ordered, until the requested distance separates them. If that body reports `blocked`, a loaded mesh lets the server try a route to a point past that distance, on the ground at the NPC's own height, before reporting it.
     * @param from Who or what to run away from: a player or NPC, as a handle or a network ID, or a place.
     * @param options `radius` is how far away is far enough; `speed` is the pace, `run` by default. An animal picks its own pace.
     * @returns True when the order went out; false for a player or NPC that is not there, or for the NPC itself.
     */
    flee(from: Player | Npc | number | Vector3 | Partial<Vector3>, options?: { speed?: 'walk' | 'jog' | 'run'; radius?: number }): boolean;

    /**
     * Turns the NPC's head, and its body when it has to, without moving it.
     * @param target Who or what to look at.
     * @returns True when the order went out.
     */
    lookAt(target: Player | Npc | number | Vector3 | Partial<Vector3>): boolean;

    /**
     * Moves the body outright, whoever is simulating it. Unlike a player teleport this is a write rather than a request: the server bumps the body's epoch, so a pose the simulator had already sent cannot put it back.
     * @param position Where to put it.
     * @param rotation Optional facing; a Quaternion, or Euler angles in degrees.
     * @returns True when the position was usable.
     */
    teleport(position: Vector3 | Partial<Vector3>, rotation?: Quaternion | Vector3 | Partial<Vector3>): boolean;

    /**
     * Makes the NPC play one of the game's animations -- chopping wood with an axe in hand, drawing water, sitting, drinking -- on every client, the one simulating it included. It is state rather than a one-off: a client that streams the NPC in, and the next one to simulate it, play it too, until `stopAnimation` or the next `playAnimation`, which both cut it at once.
     * 
     * Give it something to do that keeps it in place, `hold` or `lookAt`: an order to walk makes the legs fight a full-body animation. A hit or a fall can cut the animation short; `loop` starts it again, and keeps a one-shot going pass after pass with no gap.
     * 
     * `npcAnimationEnd` fires once when the animation this request started is over, and says how.
     * @param fragment A Mannequin fragment, as `Animations.list` names it -- or, as before, a row of the shipped emote catalog, which plays that gesture once and is not remembered.
     * @param options `clip` plays a clip the server streams instead of a fragment (`animations/kcdc/<resource>/wave.caf`, the fragment left `""`), `tags` pick the variant, `loop` repeats it until stopped, `props` puts up to two models in its hands. `lockMovement` does nothing here: an NPC standing still is its intent's business.
     * @returns True when the request went out; false for a dead NPC. Throws for a prop or option it cannot use.
     */
    playAnimation(fragment: string | number, options?: { clip?: string; tags?: string; loop?: boolean; lockMovement?: boolean; props?: { item?: string; model?: string; hand?: 'right' | 'left'; joint?: string; position?: Vector3 | Partial<Vector3>; rotation?: Quaternion | Vector3 | Partial<Vector3> } | { item?: string; model?: string; hand?: 'right' | 'left'; joint?: string; position?: Vector3 | Partial<Vector3>; rotation?: Quaternion | Vector3 | Partial<Vector3> }[] }): boolean;

    /**
     * Ends what `playAnimation` started at once, takes its props away and hands the body back to its intent. The stand-up `standUp` asks for is an animation of its own and raises its own `npcAnimationEnd`.
     * @param options `standUp` plays the game's own way back to standing when the animation held the body in a sitting or lying stance tag: `StandUp` or `GetUp`, in the variant that fits. Without it the body goes straight back to standing.
     * @returns True when the request went out.
     */
    stopAnimation(options?: { standUp?: boolean }): boolean;

    /**
     * Sets the NPC's facial expression on every client, alongside whatever its body plays. It is state: a client that streams the NPC in, and the next one to simulate it, show it too, until the next call.
     * 
     * The expression is the eyes, brows and mood; it never moves the mouth, which is `setTalking`. A mood tag is one of the body's own tags, so it can change the mood of its idle too.
     * @param fragment A fragment on the face's own scope: `FE_Default`, `FE_DialogueIdle` or `FE_DialogueSpeaking` for a held mood, or an `ADLG_FA_*` gesture (`ADLG_FA_Smile`, `ADLG_FA_Wink`, `ADLG_FA_Laugh`, `ADLG_FA_Surprise`...) that plays once. `""` hands the face back to the game.
     * @param options `tags` pick the variant: for the `FE_*` fragments the mood, `happy`, `angry`, `sad`, `nervous`, `pensive`, `arogant` or `drunk`, as `Animations.list("FE_")` spells them.
     * @returns True when the request went out; false for a dead NPC. Throws for an argument it cannot use.
     */
    setFacialExpression(fragment: string, options?: { tags?: string }): boolean;

    /**
     * Moves the NPC's mouth as if it were talking, on every client, until told to stop. Pair it with `say` for a line of text. It is a looping talk animation, not lip-sync: it follows no words, and the game's own lip-sync wins while the NPC speaks a voice line of its own.
     * @param talking Whether the mouth moves.
     * @param style How: `neutral` (the default), `happy`, `drunk` or `chew`.
     * @returns True when the request went out; false for a dead NPC. Throws for a style it does not know.
     */
    setTalking(talking: boolean, style?: 'neutral' | 'happy' | 'drunk' | 'chew'): boolean;

    /**
     * Puts a line of speech over the body on every client that can see it.
     * @param text What it says, up to 256 characters.
     * @returns True when the line went out.
     */
    say(text: string): boolean;

    /**
     * Dresses the body in exactly these items, replacing what it had on. Existing items are reused and missing ones are added to stock. An empty list undresses it and retains its clothes as loose inventory.
     * @param itemClasses Item class GUIDs in the dashed form the game's own tables spell them.
     * @returns True when every GUID names an item class this build has. Otherwise nothing changes, and each refused GUID is logged.
     */
    wear(itemClasses: string[]): boolean;

    /**
     * Dresses the body in one of the game's own outfits.
     * @param preset Clothing preset GUID from the game's own table.
     * @returns True when the preset is one this build has.
     */
    setOutfit(preset: string): boolean;

    /**
     * Changes the body under the clothes -- face, hair, beard and skin. Unlike a player's, this is a write rather than a request: nobody owns an NPC's body but the server.
     * @param appearance The parts to change; anything left out keeps what it is wearing.
     * @returns True when the appearance was accepted.
     */
    setAppearance(appearance: Partial<Appearance>): boolean;

    /**
     * Takes health off the ledger, raising `npcDamage` and, if it is the last of it, `npcDeath`. Refused for an invulnerable or already-dead body.
     * @param amount Health to take off.
     * @param attacker Who did it, for the events this raises.
     */
    damage(amount: number, attacker?: Player | number): void;

    /**
     * Kills it outright, through the same path damage takes -- including an invulnerable one, which is the difference between this and `damage`.
     * @param attacker Who to credit with it.
     */
    kill(attacker?: Player | number): void;

    /**
     * Brings a dead NPC back at full health. Every client makes a fresh body for it, because the one they have is a corpse.
     */
    revive(): void;

    /**
     * Pins simulation of this NPC to one player's client, whatever the distances say. What a scripted scene wants: the actor has to be run by the machine the scene is being played to. A pinned NPC goes dormant rather than migrating when that player leaves range, and is unpinned automatically if they disconnect.
     * @param player Whose client should run it, or null to hand it back to the election.
     */
    pin(player: Player | number | null): void;

    /**
     * Spawns an NPC and replicates it. Omitted or undefined `locomotion` defaults to `native`; other values must be exactly `native` or `kinematic`, otherwise it throws before spawning. It exists on the server from this moment: every client near enough makes a body for it, one of them is elected to run it, and the rest draw what that one reports.
     * 
     * The body is not simulated until somebody is close enough to run it, which is not a failure -- a guard on the other side of the map has nothing to do that anybody can see. Read `simulator` to tell.
     * @param options `soul` is a role from `Npc.roles()`, an animal name from `Npc.animals()`, or a soul GUID; `position` is where to put it. Everything else has a default.
     * @returns The new NPC's handle.
     */
    static create(options: { soul?: string; class?: string; name?: string; outfit?: string; wearing?: string[]; appearance?: Partial<Appearance>; position?: Vector3 | Partial<Vector3>; rotation?: Quaternion | Vector3 | Partial<Vector3>; faction?: number; health?: number; maxHealth?: number; locomotion?: 'kinematic' | 'native'; invulnerable?: boolean; frozen?: boolean; interactable?: boolean; nametag?: boolean; lootable?: boolean; virtualWorld?: number }): Npc;

    /**
     * Takes over one of the level's own NPCs instead of spawning a new body. Locomotion is validated as for `create`. A level `EntityGuid` is the same number on every machine, so every client finds the same body -- which is how doors, gates and stashes are already addressed. Adopting the same guid twice returns the NPC that already has it without applying the options again.
     * @param levelGuid `EntityGuid` of the body the level already placed.
     * @param options The same options a spawn takes; `soul` and `class` are ignored, since the body already exists.
     * @returns The NPC handle for that body.
     */
    static adopt(levelGuid: number, options?: { soul?: string; class?: string; name?: string; outfit?: string; wearing?: string[]; appearance?: Partial<Appearance>; position?: Vector3 | Partial<Vector3>; rotation?: Quaternion | Vector3 | Partial<Vector3>; faction?: number; health?: number; maxHealth?: number; locomotion?: 'kinematic' | 'native'; invulnerable?: boolean; frozen?: boolean; interactable?: boolean; nametag?: boolean; lootable?: boolean; virtualWorld?: number }): Npc;

    /**
     * Lists every server-owned NPC.
     * @param virtualWorld Optional virtual world to list; omitted lists every one of them.
     * @returns One handle per live NPC, in no particular order.
     */
    static all(virtualWorld?: number): Npc[];

    /**
     * Looks an NPC up by its network entity ID.
     * @param id Network entity identifier.
     * @returns The NPC's handle, or null when no live NPC has that ID.
     */
    static getById(id: number): Npc | null;

    /**
     * Despawns NPCs, emitting npcDestroy for each.
     * @param virtualWorld Optional virtual world to clear; omitted clears every one of them.
     * @returns How many were despawned.
     */
    static removeAll(virtualWorld?: number): number;

    /**
     * The named kinds of NPC this build ships -- `guard`, `bandit`, `townswoman` and the rest. Each is a real soul out of the game's own tables, so a role spawns a body that already looks the part. Animal names are listed separately by `Npc.animals()`. Other values are taken as soul GUIDs.
     * @returns The role names.
     */
    static roles(): string[];

    /**
     * Authored human NPC placements in the configured level with matching catalog souls. These are initial placements, not current NPC schedules or proof that a quest layer is loaded. Does not spawn or adopt anything.
     */
    static placements(): { name: string; guid: string; level: string; layer: string; soul: string; actorClass: string; position: { x: number; y: number; z: number }; rotation: { w: number; x: number; y: number; z: number } }[];

    /**
     * Animal souls with matching native classes. Pass a name or soul to Npc.create. Chickens use flock entities and are excluded.
     */
    static animals(): { name: string; soul: string; actorClass: string }[];

    /**
     * Level-authored animal spawners. Positions are markers and must be projected onto navigation. GUIDs are hex strings. Counts and respawn days describe the original game; scripts decide their population and respawn policy.
     * @param level Defaults to the running server's level. Pass * for all levels.
     */
    static animalSpawnpoints(level?: string): { level: string; guid: string; name: string; layer: string; soul: string; actorClass: string; position: Vector3; count: number; respawnDays: number; spawnInFlock: boolean; spawnAreaGuid: string }[];
  }

  interface Npc extends Entity {}

  /**
   * One authored stop, independent of actor type.
   */
  interface PatrolPoint {
    /**
     * World-space waypoint.
     */
    position: Vector3;

    /**
     * Overrides the route and call speed for this leg.
     */
    speed?: 'walk' | 'jog' | 'run' | undefined;

    /**
     * Wait after arrival, 0 to 3600 seconds.
     */
    waitSeconds?: number | undefined;

    /**
     * Arrival radius in metres.
     */
    radius?: number | undefined;
  }

  /**
   * The format saved by World Builder and accepted by PatrolRoute.create.
   */
  interface PatrolRouteDefinition {
    /**
     * Stable globally unique id, up to 128 bytes. Use a namespace such as town.guard-loop.
     */
    id: string;

    /**
     * Display name, up to 256 bytes.
     */
    name?: string | undefined;

    /**
     * 1 to 256 ordered waypoints.
     */
    points: PatrolPoint[];

    /**
     * Defaults to loop. Ping-pong visits each endpoint once per turn.
     */
    mode?: 'once' | 'loop' | 'pingPong' | undefined;

    /**
     * Default leg speed; walk when omitted.
     */
    speed?: 'walk' | 'jog' | 'run' | undefined;

    /**
     * Default arrival wait, 0 to 3600; defaults to zero.
     */
    waitSeconds?: number | undefined;

    /**
     * Default arrival radius; 1.5 metres when omitted.
     */
    radius?: number | undefined;
  }

  /**
   * A running patrol's snapshot. Indices and laps start at zero.
   */
  interface PatrolState {
    /**
     * Stable route id; null for an anonymous point-array patrol.
     */
    routeId: string | null;

    /**
     * Target waypoint index.
     */
    waypointIndex: number;

    /**
     * Completed loops or round trips.
     */
    lap: number;

    /**
     * 1 forward, -1 backward.
     */
    direction: number;

    /**
     * Whether the actor is approaching or waiting at the waypoint.
     */
    phase: 'moving' | 'waiting';
  }

  /**
   * Named patrol definition. Actors snapshot it when ordered; update or destroy affects future starts.
   */
  class PatrolRoute {
    private constructor();

    /**
     * False after destruction.
     */
    readonly exists: boolean;

    /**
     * Stable id; empty after destruction.
     */
    readonly id: string;

    /**
     * Display name.
     */
    readonly name: string;

    /**
     * Returns an independent definition, or null after destruction.
     */
    toJSON(): PatrolRouteDefinition | null;

    /**
     * Removes the definition. Active patrols keep their snapshots.
     */
    destroy(): boolean;

    /**
     * Updates defaults or waypoints atomically. Invalid input throws; a destroyed handle returns false.
     * @param changes Fields to replace; id cannot change.
     */
    update(changes: Partial<PatrolRouteDefinition>): boolean;

    /**
     * Registers a route. Invalid data or a duplicate id throws.
     * @param definition The route to register.
     */
    static create(definition: PatrolRouteDefinition): PatrolRoute;

    /**
     * Looks up a registered route.
     * @param id Exact route id.
     */
    static getById(id: string): PatrolRoute | null;

    /**
     * Lists registered routes in creation order.
     */
    static all(): PatrolRoute[];
  }

  /**
   * Replicated journal quest handle.
   */
  class Quest {
    /**
     * Creates a script wrapper for an existing quest with this ID; use Quest.give() to write one.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * The key the quest is filed under, and the name the game knows the quest node by. Read-only: it is the quest's identity.
     */
    readonly key: string;

    /**
     * The line the journal row and the quest toast show. Assignment is silent; it does not raise a toast.
     */
    title: string;

    /**
     * The body of the journal's diary page for this quest. Assignment is silent.
     */
    description: string;

    /**
     * Which section of the journal the quest files under. Read-only: it is decided when the quest is given.
     */
    readonly type: string;

    /**
     * `active`, `done` or `failed`. Assignment announces the change; `setProgress` can do it quietly.
     */
    progress: string;

    /**
     * Network id of the one player this quest was written for, or 0 when it went to everyone in the world. Read-only: who a quest belongs to is decided when it is given.
     */
    readonly player: number;

    /**
     * How many objectives the quest carries, up to eight.
     */
    readonly objectiveCount: number;

    /**
     * Formats this quest handle for logging and debugging.
     * @returns The quest ID, its key, its state and how many objectives it carries.
     */
    toString(): string;

    /**
     * Takes this quest out of every journal it was written into.
     */
    remove(): void;

    /**
     * Moves the quest on. There is no way back to unstarted: a quest nobody should see any more is removed.
     * @param progress Where the quest now stands.
     * @param announce Whether to raise the game's quest-updated toast; defaults to true. Pass false for a correction the player should not be told about.
     */
    setProgress(progress: 'active' | 'done' | 'failed', announce?: boolean): void;

    /**
     * Rewrites one line of the quest in place.
     * @param index Which objective to rewrite, counting from zero. An index past the end is ignored.
     * @param objective The line, as text or as an object carrying its state.
     * @param announce Whether to raise the quest-updated toast; defaults to true.
     */
    setObjective(index: number, objective: string | { text: string; progress?: 'active' | 'done' | 'failed'; optional?: boolean; position?: Vector3 }, announce?: boolean): void;

    /**
     * Replaces the quest's objective list.
     * @param objectives The whole list, at most eight entries; anything past that is dropped.
     * @param announce Whether to raise the quest-updated toast; defaults to true.
     */
    setObjectives(objectives: (string | { text: string; progress?: 'active' | 'done' | 'failed'; optional?: boolean; position?: Vector3 })[], announce?: boolean): void;

    /**
     * Reads the quest's objectives back.
     * @returns One entry per objective, in journal order. `position` is present only on an objective that has one.
     */
    objectives(): { text: string; progress: 'active' | 'done' | 'failed' | 'none'; optional: boolean; position?: Vector3 }[];

    /**
     * Makes the game follow this quest, exactly as the journal's track button does: it is listed in the HUD tracker, and every active objective that has a `position` is drawn as the game's own quest marker on the map and the compass. It is an instruction, not a lock -- the player can untrack it from the journal -- and the outcome arrives as `questTrackingChanged` like any other change. A quest given a moment ago can be tracked at once; the client waits for it to arrive.
     * @param player Network id of the player who should follow the quest. Omitted, everyone who has the quest follows it: its one owner, or every player in its world.
     * @returns How many clients were told. Zero when the player does not have this quest.
     */
    track(player?: number): number;

    /**
     * Makes the game stop following this quest, which takes its markers off the map and the compass. The outcome arrives as `questTrackingChanged`.
     * @param player Network id of the player who should stop following the quest. Omitted, everyone who has the quest stops.
     * @returns How many clients were told.
     */
    untrack(player?: number): number;

    /**
     * Writes a quest into the game's own journal. It is a replicated entity, so a player who joins late, reloads or walks away still finds it in their log -- which is what a quest needs and what a one-shot notification cannot do. What the player sees is the game's quest UI: its journal row, its diary page, its objective tracker and its quest-updated toast, all reading a quest node the client builds with the game's own constructor.
     * @param key What the quest is filed under: up to 64 letters, digits, underscores or dashes, unique within its virtual world.
     * @param title The line the journal row shows.
     * @param options `description` is the diary page, `type` the journal section, `objectives` the lines under it -- each with an optional world `position` the game marks on the map and compass while the quest is followed -- `player` the network id of the one player it belongs to, `announce` whether to raise the toast, and `track` whether its recipients start following it.
     * @param virtualWorld Optional virtual world the quest belongs to; omitted puts it in the global one.
     * @returns The newly written quest handle.
     */
    static give(key: string, title: string, options?: { description?: string; type?: 'main' | 'side' | 'activity' | 'event' | 'micro' | 'racing'; objectives?: (string | { text: string; progress?: 'active' | 'done' | 'failed'; optional?: boolean; position?: Vector3 })[]; player?: number; announce?: boolean; track?: boolean }, virtualWorld?: number): Quest;

    /**
     * Lists every replicated quest the server currently has.
     * @param virtualWorld Optional virtual world to list; omitted lists every one of them.
     * @returns One handle per live quest, in no particular order.
     */
    static all(virtualWorld?: number): Quest[];

    /**
     * Looks a quest up by its key.
     * @param key The key the quest was given under.
     * @param virtualWorld Optional virtual world to search; omitted searches every one of them.
     * @param player Network id of the player whose copy to find. Keys are unique per recipient, so this is how one player's errand is told from another's.
     * @returns The quest's handle, or null when no live quest has that key.
     */
    static find(key: string, virtualWorld?: number, player?: number): Quest | null;

    /**
     * Looks a replicated quest up by its network entity ID.
     * @param id Network entity identifier.
     * @returns The quest's handle, or null when no live quest has that ID.
     */
    static getById(id: number): Quest | null;

    /**
     * Takes quests out of every journal they were written into.
     * @param virtualWorld Optional virtual world to clear; omitted clears every one of them.
     * @returns How many quests were removed.
     */
    static removeAll(virtualWorld?: number): number;
  }

  interface Quest extends Entity {}

  /**
   * Replicated world stack handle.
   */
  class GroundItem {
    /**
     * Creates a script wrapper for an existing ground item with this ID; use GroundItem.spawn() to spawn one.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * The class of item lying here, as the same 32 hex digits `player.rightHandItem` and `player.equipment` use, so the three can be compared directly. The server holds no name for a class: naming one is an item database's job.
     */
    readonly itemClass: string;

    /**
     * How many units are in the stack. One entity however many that is: a stack is picked up whole or not at all.
     */
    readonly amount: number;

    /**
     * Quality the stack was made with, or 0 when it was left to the class's own.
     */
    readonly quality: number;

    /**
     * Absolute item health from 0 to 1, or -1 when it was left to the class's own.
     */
    readonly health: number;

    /**
     * Displayed condition from 0 to 1, or -1 when it was left to the class's own.
     */
    readonly condition: number;

    /**
     * Whether the stack has settled where it will stay. A stack a script spawned is resting from the start; one a player dropped is false while the nearest client simulates its fall, and its position moves until then. A resting stack's pose is the server's and does not move again.
     */
    readonly resting: boolean;

    /**
     * Whether this is a prop people carry in their arms -- one a carry put down, or one spawned `carryable` -- rather than stock. Taking it up is a carry: the game's pick-up prompt or `player.carryItem(groundItem)`, never an inventory.
     */
    readonly carryable: boolean;

    /**
     * The name `GroundItem.spawn` or a World Builder export gave the stack, so a script finds it again; empty for none.
     */
    readonly name: string;

    /**
     * Network ID of the player who dropped this stack, or 0 when the server spawned it.
     */
    readonly droppedById: number;

    /**
     * The player who dropped this stack, or null when the server spawned it or that player has since left.
     */
    readonly droppedBy: Player | null;

    /**
     * Formats this ground item handle for logging and debugging.
     * @returns The stack ID, its item class, how many of it there are and whether it has settled.
     */
    toString(): string;

    /**
     * Takes this stack back out of the world on every client after emitting groundItemDestroy.
     */
    destroy(): void;

    /**
     * Lays a pickable stack of an item on the ground and replicates it, spawning it already at rest.
     * @param item Item class to lay down, as either its GUID or its name (`arrow_crude`). The same spelling `player.giveItem` takes.
     * @param position Optional world-space spawn position; omitted components default to zero. The stack is placed there rather than dropped, so put it where the ground is.
     * @param rotation Optional resting orientation: a Quaternion, or a Vector3 of Euler angles in degrees.
     * @param amount Optional number of units in the stack, from 1 to 10000; omitted lays down one. A stack is picked up whole.
     * @param virtualWorld Optional virtual world the stack belongs to; omitted puts it in the global one.
     * @param properties Optional condition of the item itself: `quality` as the game grades it, `health` and `condition` from 0 to 1. Each defaults to the class's own. Arrows and other missile classes are held to tighter bounds by the receiving client and are given a quality of 1 and full health when these are left out, since a stack outside those bounds is one no client would build. `carryable` makes it a prop people carry in their arms rather than stock: the game's pick-up prompt over it starts a carry (raising `playerCarryingItem`) and never puts it in an inventory. Only for a single item `player.carryItem` takes by name. `name`, up to 64 bytes of UTF-8, is what `groundItem.name` reads back, so a script finds the stack again.
     * @returns The newly spawned ground item handle. Throws when the item is not a class in the game's tables, or when no client could build the stack as described.
     */
    static spawn(item: string, position?: Vector3 | Partial<Vector3>, rotation?: Vector3 | Quaternion, amount?: number, virtualWorld?: number, properties?: { quality?: number; health?: number; condition?: number; carryable?: boolean; name?: string }): GroundItem;

    /**
     * Lists every stack lying in the world, however it got there.
     * @returns One handle per live stack, in no particular order.
     */
    static all(): GroundItem[];

    /**
     * Looks a stack up by its network entity ID.
     * @param id Network entity identifier.
     * @returns The stack's handle, or null when no live stack has that ID.
     */
    static getById(id: number): GroundItem | null;

    /**
     * Removes stacks from the world, emitting groundItemDestroy for each one.
     * @param virtualWorld Optional virtual world to clear; omitted clears every one of them.
     * @returns How many stacks were removed.
     */
    static destroyAll(virtualWorld?: number): number;
  }

  interface GroundItem extends Entity {}

  /**
   * One available authored herb, sampled when getHerbs is called.
   */
  interface GatheringHerb {
    /**
     * The game's pickable area ID for this species.
     */
    kind: number;

    /**
     * Item class GUID yielded by this species.
     */
    item: string;

    /**
     * The source position in world metres.
     */
    position: Vector3;
  }

  /**
   * The server-wide override for herb regrowth, measured in game hours.
   */
  interface GatheringRespawnTime {
    /**
     * Lower bound, inclusive. Both bounds zero disable timed regrowth.
     */
    minHours: number;

    /**
     * Upper bound, inclusive. Equal positive bounds give a fixed duration.
     */
    maxHours: number;
  }

  /**
   * Finds and removes authored herbs and controls regrowth. Defaults are five times faster than the game's authored species timings. Changes are held in server memory.
   */
  const Gathering: {
    /**
     * Current frozen override, or null for the species defaults at one fifth of their authored duration.
     */
    readonly respawnTime: GatheringRespawnTime | null;

    /**
     * Lists available authored herbs in a sphere, without requiring nearby players. Excludes picked or removed herbs and custom map assets. Invalid arguments throw.
     * @param position Centre in world metres; finite coordinates within +/-100000.
     * @param radius Inclusive 3D radius in metres, from 0 to 100000.
     * @param virtualWorld One virtual-world bucket, default 0. Pass player.virtualWorld for a player's bucket. 0xffffffff selects only the global bucket. Invalid IDs throw.
     */
    getHerbs(position: Vector3, radius: number, virtualWorld?: number): GatheringHerb[];

    /**
     * Removes authored herbs in a sphere, including already-picked plants. Gives no rewards or harvest events. Removal lasts until an explicit respawn or server restart, regardless of regrowth timing changes.
     * @param position Centre in world metres; finite coordinates within +/-100000.
     * @param radius Inclusive 3D radius in metres, from 0 to 100000.
     * @param virtualWorld One virtual-world bucket, default 0. Pass player.virtualWorld for a player's bucket. 0xffffffff selects only the global bucket. Invalid IDs throw.
     * @returns Number newly marked for removal; repeated removal returns 0. Invalid arguments throw before mutation.
     */
    removeHerbs(position: Vector3, radius: number, virtualWorld?: number): number;

    /**
     * Restarts active picked-herb deadlines from now and sets the policy for future picks. Script-removed herbs stay removed. Samples uniformly per plant. Equal bounds fix the duration; (0, 0) disables timed regrowth while manual respawn remains available. Applies to every virtual world.
     * @param minHours Minimum game hours, from 0 to maxHours.
     * @param maxHours Maximum game hours, up to 87600. Positive durations must reach at least one game millisecond.
     * @returns True on success; false for non-finite, reversed or out-of-range bounds.
     */
    setRespawnTime(minHours: number, maxHours: number): boolean;

    /**
     * Restores five-times-faster species defaults and restarts active picked-herb timers from now. Script-removed herbs stay removed.
     */
    resetRespawnTime(): void;

    /**
     * Respawns picked and script-removed plants in a sphere, including plants with timed regrowth disabled. Restores client vegetation as well as server availability. Does not create new plants.
     * @param position Centre of the sphere in world metres; finite coordinates within +/-100000.
     * @param radius Inclusive 3D radius in metres, from 0 to 100000.
     * @param virtualWorld Only this virtual world; defaults to 0.
     * @returns Number of active picked or script-removed sources restored. Invalid arguments throw.
     */
    respawn(position: Vector3, radius: number, virtualWorld?: number): number;

    /**
     * Respawns every picked or script-removed plant in one virtual world, including plants with timed regrowth disabled.
     * @param virtualWorld Only this virtual world; defaults to 0.
     * @returns Number of active picked or script-removed sources restored. Invalid arguments throw.
     */
    respawnAll(virtualWorld?: number): number;
  };

  /**
   * A plant a player picked and what it is about to give them.
   */
  interface GatheringProposal {
    /**
     * Item class GUID of the herb.
     */
    item: string;

    /**
     * How many, as the game computes it from the player's survival level and the plants harvested together.
     */
    amount: number;

    /**
     * The game's pickable area id for the plant.
     */
    kind: number;

    /**
     * Where the plant grows, as the player's game reported it.
     */
    position: number[];

    /**
     * The player's virtual world.
     */
    virtualWorld: number;
  }

  /**
   * A plant a player picked, and what they were given.
   */
  interface GatheringHarvestedEvent {
    /**
     * Item class GUID of the herb.
     */
    item: string;

    /**
     * How many.
     */
    amount: number;

    /**
     * The game's pickable area id for the plant.
     */
    kind: number;

    /**
     * Where the plant grows.
     */
    position: number[];

    /**
     * The player's virtual world.
     */
    virtualWorld: number;

    /**
     * The inventory rows that received the herb.
     */
    items: InventoryUnit[];
  }

  /**
   * A frozen snapshot of one stew pot in one virtual world.
   */
  interface CookPotSnapshot {
    /**
     * Meal served by this pot, including its native nutrition and appearance.
     */
    food: "goulash" | "lentil" | "soup";

    /**
     * Base nutrition per meal: goulash 25, lentil 10, soup 15.
     */
    nutrition: number;

    /**
     * Ingested buff GUID applied to future meals, or null. Visible to server scripts only.
     */
    poison: string | null;

    /**
     * The fireplace's level GUID.
     */
    guid: string;

    /**
     * Position of the eating trigger.
     */
    position: { readonly x: number; readonly y: number; readonly z: number };

    /**
     * Unreserved meals remaining.
     */
    portions: number;

    /**
     * Portions in the most recent refill; sets the halfway point.
     */
    capacity: number;

    /**
     * Whether eating is allowed. Disabling preserves the contents.
     */
    enabled: boolean;

    /**
     * Visible food level, derived from remaining portions.
     */
    state: "empty" | "half" | "full";

    /**
     * Whether the fire burns for every player in this world, as set by CookPot.setFireLit; null while each player's own game lights and puts it out, as in single player.
     */
    fireLit: boolean | null;

    /**
     * Virtual world that owns this stock.
     */
    virtualWorld: number;
  }

  /**
   * Server-owned stew pots. Stocked pots begin with four shared meals, with no automatic refill. State lasts until the server stops.
   */
  const CookPot: {
    /**
     * Lists every catalogued pot on the server's level in one virtual world; empty for a world no player or write has used yet.
     * @param virtualWorld Virtual world id; defaults to 0. The global visibility sentinel is not a stock world.
     */
    all(virtualWorld?: number): CookPotSnapshot[];

    /**
     * Returns a snapshot, or null for an unknown pot.
     * @param guid Fireplace level GUID from CookPot.all().
     * @param virtualWorld Virtual world id; defaults to 0. The global visibility sentinel is not a stock world.
     */
    get(guid: string, virtualWorld?: number): CookPotSnapshot | null;

    /**
     * Replaces remaining stock and capacity and clears poison, preserving food and enabled. Returns false for an unknown pot.
     * @param guid Fireplace level GUID from CookPot.all().
     * @param portions Integer from 0 to 1000; defaults to four.
     * @param virtualWorld Virtual world id; defaults to 0. The global visibility sentinel is not a stock world.
     */
    refill(guid: string, portions?: number, virtualWorld?: number): boolean;

    /**
     * Allows or blocks eating. Returns false for an unknown pot.
     * @param guid Fireplace level GUID from CookPot.all().
     * @param enabled False blocks eating without changing stock or its appearance.
     * @param virtualWorld Virtual world id; defaults to 0. The global visibility sentinel is not a stock world.
     */
    setEnabled(guid: string, enabled: boolean, virtualWorld?: number): boolean;

    /**
     * Changes food level and stock together, preserving food, poison and enabled. Returns false for an unknown pot.
     * @param guid Fireplace level GUID from CookPot.all().
     * @param state Empty, two portions, or four portions; resets capacity to four.
     * @param virtualWorld Virtual world id; defaults to 0. The global visibility sentinel is not a stock world.
     */
    setState(guid: string, state: "empty" | "half" | "full", virtualWorld?: number): boolean;

    /**
     * Item class GUIDs accepted by CookPot.poison, including each native strength variant.
     */
    poisons(): string[];

    /**
     * Changes future meals and appearance, preserving stock, poison and enabled. Already reserved meals keep their previous type.
     * @param guid Fireplace level GUID from CookPot.all().
     * @param food Native meal type.
     * @param virtualWorld Virtual world id; defaults to 0. The global visibility sentinel is not a stock world.
     */
    setFood(guid: string, food: "goulash" | "lentil" | "soup", virtualWorld?: number): boolean;

    /**
     * Lights or puts out the pot's fire, with its steam and smoke, for every player in that world. From then on the fireplace AI no longer changes it: villagers stoking it and its burn-out timer leave it as set, until null hands it back. Returns false for an unknown pot.
     * @param guid Fireplace level GUID from CookPot.all().
     * @param lit True lights the fire, false puts it out, null hands it back to each player's own game.
     * @param virtualWorld Virtual world id; defaults to 0. The global visibility sentinel is not a stock world.
     */
    setFireLit(guid: string, lit: boolean | null, virtualWorld?: number): boolean;

    /**
     * Spends ingredients atomically and fills an empty enabled pot. Clears old poison. Validates player, distance, ability to act, world and synchronized inventory. Never pass unvalidated client recipe data.
     * @param player Connected player within four metres; their current virtual world selects the pot.
     * @param guid Fireplace level GUID from CookPot.all().
     * @param recipe Ingredients and yield chosen by this server script.
     */
    refillFromInventory(player: Player, guid: string, recipe: CookPotRecipe): CookPotResult;

    /**
     * Poisons a stocked enabled pot with the item's ingested effect. Refuses a second poison until refill. A meal takes the poison present when reserved, applied once on its eating receipt; NPC poisoning and native crime reactions are not included.
     * @param player Connected player within four metres; their current virtual world selects the pot.
     * @param guid Fireplace level GUID from CookPot.all().
     * @param rowId Inventory row containing a native Poison item; spends exactly one.
     */
    poison(player: Player, guid: string, rowId: string): CookPotResult;
  };

  /**
   * Server-defined ingredients and portions for refilling an empty pot.
   */
  interface CookPotRecipe {
    /**
     * Meal to put in the pot.
     */
    food: "goulash" | "lentil" | "soup";

    /**
     * Integer from 1 to 1000.
     */
    portions: number;

    /**
     * One to sixteen food item classes by name or GUID, with positive integer amounts. Split stacks are combined; spoiled rows are skipped.
     */
    ingredients: { item: string; amount: number }[];
  }

  /**
   * An atomic pot action. A refusal changes neither items nor contents.
   */
  interface CookPotResult {
    /**
     * Whether the action succeeded.
     */
    ok: boolean;

    /**
     * ok, invalid_player, unknown_pot, disabled, too_far, cannot_act, inventory_not_ready, not_empty, invalid_recipe, missing_ingredients, empty, already_poisoned, unknown_item, not_poison, or an inventory refusal code.
     */
    code: string;
  }

  /** */
  interface NearestDoor {
    /**
     * Whether the client had a door in sight and it belongs to this level.
     */
    found: boolean;

    /**
     * Why there is no door: nothing in sight, another level, no answer, or a caller who left. Empty when one was found.
     */
    reason: string;

    /**
     * The door handle. Null whenever `found` is false.
     */
    door: Door | null;

    /**
     * The door's level EntityGuid as hex, present only when one was found.
     */
    guid: string | undefined;

    /**
     * The name the level gives the door, often empty. Present only when one was found.
     */
    name: string | undefined;

    /**
     * How far the player is from it, in metres. Present only when one was found.
     */
    distance: number | undefined;

    /**
     * Whether it is standing open, as the client at it sees right now. Present only when one was found.
     */
    open: boolean | undefined;

    /**
     * Whether it is locked, as the client at it sees right now. Present only when one was found.
     */
    locked: boolean | undefined;
  }

  /**
   * A door to hang where the level has none, as `Door.spawn` takes it and a World Builder map's `doors` array stores it.
   */
  interface DoorDefinition {
    /**
     * The leaf mesh, by path or file stem, from `Door.models()`: `lvl1_door_a_left` or `objects/characters/assets/doors/lvl1_door_a_left.cdf`. Which way it is hinged is part of the mesh.
     */
    model: string;

    /**
     * Where the door hangs: the leaf's pivot, on its hinge side, as the level's own doors stand. Each coordinate within 16384 of the origin.
     */
    position: Vector3 | number[];

    /**
     * Which way it faces: a Quaternion, {x, y, z, w} or [x, y, z, w]. None when omitted.
     */
    rotation?: Quaternion | number[] | undefined;

    /**
     * What a script finds it by, up to 64 bytes of UTF-8; `door.name` reads it.
     */
    name?: string | undefined;

    /**
     * Whether it is hung locked. Unlocked when omitted. A key or a lockpick opens it as the level's own doors open; the server's lock (`door.locked = true`) is a separate thing.
     */
    locked?: boolean | undefined;

    /**
     * What each side takes to get through it while it is locked: `none` (only a script opens it), `bolt` (a bolt inside, nothing outside), `boltAndKey` (a bolt inside, a keyhole outside, as most of the level's doors) or `key` (a keyhole on both sides). `boltAndKey` when omitted.
     */
    lock?: 'none' | 'bolt' | 'boltAndKey' | 'key' | undefined;

    /**
     * Whether its keyhole can be picked. True when omitted.
     */
    lockpickable?: boolean | undefined;

    /**
     * How hard the lockpicking minigame is, from 0 to 1, read in fifths from very easy to very hard. The game's own when omitted.
     */
    lockDifficulty?: number | undefined;

    /**
     * The item class GUID of a key that works the keyhole, as `player.giveItem` takes it. None when omitted.
     */
    keyItem?: string | undefined;
  }

  /**
   * A leaf mesh the levels hang doors from.
   */
  interface DoorModelInfo {
    /**
     * The mesh path `Door.spawn` takes, `objects/characters/assets/doors/...cdf`.
     */
    path: string;

    /**
     * The file stem, which `Door.spawn` also takes.
     */
    name: string;

    /**
     * Which side the leaf is hinged on.
     */
    hinge: 'left' | 'right';

    /**
     * How many doors the shipped levels hang from it.
     */
    placements: number;
  }

  /**
   * A door the level places that a level edit has taken out of a virtual world.
   */
  interface RemovedDoor {
    /**
     * Its level EntityGuid as sixteen lowercase hex digits, as `Door.restore` takes it.
     */
    guid: string;

    /**
     * The entity name the level gives it.
     */
    name: string;

    /**
     * Where the level hangs it.
     */
    position: Vector3;

    /**
     * The virtual world it is out of. Out of the global world, it is out of every world.
     */
    virtualWorld: number;
  }

  /**
   * Replicated handle for a door: one of the level's own, or one a script or a world resource hung with `Door.spawn`.
   */
  class Door {
    /**
     * Creates a script wrapper for a door the server already knows; use `Door.spawn` to hang a new one, or `Door.find` for one the level places.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * The door's EntityGuid, as sixteen lowercase hex digits, the same on every machine; `Door.find` takes it back. A level door's never changes, so it is the identity to store one under. A hung door's lasts only until the server stops: store its `toJSON()` instead.
     */
    readonly guid: string;

    /**
     * Bumped on every durable change. Clients act on the edges rather than the value, so it only tells one change from the next.
     */
    readonly stateToken: number;

    /**
     * Whether the door is standing open. Change it with `setOpen`, which also says which way it swings.
     */
    readonly open: boolean;

    /**
     * Which side the leaf was last worked from, which decides the way it swings. A player arriving later poses the door from this, so everyone sees it swung the same way.
     */
    readonly openedFromFront: boolean;

    /**
     * Whether the door is locked. Assignment reaches every client that has the door streamed in, including one already standing at it. A door locked from here can only be opened again from here or with a lockpick: a player unlocking it by hand or with a key is refused.
     */
    locked: boolean;

    /**
     * Whether the lock was set by a script or an operator rather than by the level. It clears when the door is unlocked, by any route the server accepted.
     */
    readonly lockedByServer: boolean;

    /**
     * Whether players may work the door. False hides its prompt to open, close, unlock or pick it, and a player's use is refused on their own machine before the door or the body moves -- no round trip, no swing back. A report against it is refused here too, for a client that did not. Assignment reaches every client that has the door streamed in, including one already standing at it. The door stays as it is: `setOpen` and `locked` still work, and NPCs still pass it. Per door, not per player: refuse one player with a client `doorInteract` handler.
     */
    usable: boolean;

    /**
     * The entity name the level gives the door; for a hung door, the `name` it was hung with, or `spawned_door_<n>` without one.
     */
    readonly name: string;

    /**
     * The item class GUID of the key the level assigns to the door's keyhole, as `player.giveItem` takes it. Empty when it takes none of its own -- most doors open to the generated home or shop key instead. The server does not see inventories, so this is for a resource that keeps its own.
     */
    readonly keyItem: string;

    /**
     * Whether the level lets this door be lockpicked. A lockpick reported on one that cannot be is refused.
     */
    readonly lockpickable: boolean;

    /**
     * The level areas that link this door as one of their `crime_door`s -- the house, workshop or storage room it belongs to, as the level itself records it. Nothing is inferred from where the door stands.
     */
    readonly areas: Area[];

    /**
     * Whether the level builds this door locked, which is the state it has at boot; for a hung door, whether it was hung locked.
     */
    readonly startsLocked: boolean;

    /**
     * Whether a script or a world resource hung this door with `Door.spawn`, rather than the level placing it.
     */
    readonly spawned: boolean;

    /**
     * The leaf mesh the door hangs, as `Door.models()` lists it: `objects/characters/assets/doors/...cdf`.
     */
    readonly model: string;

    /**
     * Formats this door handle for logging and debugging.
     * @returns The door ID, its level GUID and whether it is open and locked.
     */
    toString(): string;

    /**
     * Opens or closes the door with nobody pushing it. Every client that has the door built plays the game's own swing; one arriving later finds it posed the same way. Opening a locked door unlocks it, as the game's own `Open` does.
     * @param open True to open the door, false to close it.
     * @param fromFront Which side the leaf is worked from, and so which way it swings. Omitted keeps the side it was last worked from.
     * @returns False when the door is already that way.
     */
    setOpen(open: boolean, fromFront?: boolean): boolean;

    /**
     * Takes the door out. A door a script or a world resource hung is taken down for good. A door the level places is taken out of this handle's virtual world, collision and all, through a level edit that every client seeing that world applies -- a player in the global world sees every world, so it is hidden for them too; the copy in the global world takes it out of every world. `Door.restore` puts a level door back, and `Door.removed` lists the ones that are out. Kept in memory only, like every level edit: a removal is undone when the server restarts or the resource that made it stops.
     */
    destroy(): void;

    /**
     * The definition `Door.spawn` takes to hang this door again, with whether it is locked now: what a script saves. The server keeps nothing past a restart. It is one entry of a World Builder map's `doors` array.
     * @returns Null for a door the level places.
     */
    toJSON(): DoorDefinition | null;

    /**
     * Lists every door: the level's own, all of which exist from boot in the global world, and every door a script or a world resource hung. In another virtual world a level door is built the first time anything there reaches it, so this lists only those. A level door taken out of a world is not listed for it; `Door.removed` lists those.
     * @returns One handle per known door, in no particular order.
     */
    static all(): Door[];

    /**
     * Hangs a working door where the level has none: every player in the virtual world sees it, opens and closes it, keys or picks its lock, and the server judges every use by the same rules as the level's own doors. It starts closed. Kept in memory only: hang it again on boot. The resource that hangs it owns it, so it goes when that resource stops. NPCs do not path through it: the level's navigation mesh has no link for it.
     * @param definition The leaf, where it hangs and how it locks, in the format of one entry of a World Builder map's `doors` array.
     * @param virtualWorld Optional virtual world whose players see it; the global one when omitted.
     * @returns The door. Throws for an unknown model, a transform that is not finite, a lock name that is not one of the four, or a key that is not an item class.
     */
    static spawn(definition: DoorDefinition, virtualWorld?: number): Door;

    /**
     * Lists every leaf mesh the shipped levels hang doors from, which is every model `Door.spawn` takes.
     * @returns One row per mesh, sorted by path.
     */
    static models(): DoorModelInfo[];

    /**
     * Takes a door the level places out of a virtual world, collision and all, as `door.destroy()` does, without needing its handle. Taking out a door that is already out of that world, or of the global one, does nothing; taking out one that has a level edit in that world, a move or a swap, replaces that edit.
     * @param guid The level door's EntityGuid as hex, as `door.guid` prints it.
     * @param virtualWorld Optional virtual world to take it out of; the global one, and so every world, when omitted.
     * @returns Throws for a GUID that is not one of the level's doors.
     */
    static remove(guid: string, virtualWorld?: number): void;

    /**
     * Puts back a level door that a script, a world resource or any other level edit took out of a virtual world. It comes back closed, and locked as the level builds it.
     * @param guid The level door's EntityGuid as hex, as `Door.removed` lists it.
     * @param virtualWorld Optional virtual world to put it back in; the global one when omitted. A door taken out of the global world is only put back from there.
     * @returns The door, or null when that world had no such removal.
     */
    static restore(guid: string, virtualWorld?: number): Door | null;

    /**
     * Lists the doors the level places that are taken out of a virtual world, by a script or a World Builder export alike.
     * @param virtualWorld Optional virtual world to list; omitted lists every one of them.
     * @returns One row per removal, in no particular order.
     */
    static removed(virtualWorld?: number): RemovedDoor[];

    /**
     * Looks a door up by its network entity ID.
     * @param id Network entity identifier.
     * @returns The door's handle, or null when no live door has that ID.
     */
    static getById(id: number): Door | null;

    /**
     * Looks a door up by the level's own identity for it, which survives a restart and is the same on every machine. A door the level places is found in any virtual world it has not been taken out of. A door a script hung is found by `door.guid` in the world it hangs in.
     * @param guid The door's level EntityGuid as hex, with or without an `0x` prefix, as `door.guid` prints it.
     * @param virtualWorld Optional virtual world to look in; omitted looks in the global one, where every body starts.
     * @returns The door's handle, or null when there is no such door there.
     */
    static find(guid: string, virtualWorld?: number): Door | null;

    /**
     * Lists the doors the level links to an area as its `crime_door`s, with their GUID, name, position and link type, straight from the level catalog -- so a door that no client has reported yet is listed too. Pass a `guid` to `Door.find` for the live handle and its `locked` state. Only native links: a script area has none.
     * @param areaId The area's id, as `area.id` prints it.
     * @returns The doors; empty for an unknown area or one with no links.
     */
    static getForArea(areaId: string): AreaDoor[];

    /**
     * Asks a player's client which door their body is standing at. The server knows every door the level places but has no world to measure distances in, so this is how a command finds the door in front of a player, and the answer is what that client sees now rather than what the replica last carried.
     * 
     * The promise always settles: on the answer, on a five-second timeout, or when the player leaves, with `found` false and `reason` saying which.
     * @param player The player to ask. The question goes to their own client.
     * @returns The answer, once that client has given one.
     */
    static queryNearest(player: Player): Promise<NearestDoor>;
  }

  interface Door extends Entity {}

  /**
   * A smart object to place, as `SmartObject.spawn` takes it and a World Builder map's `smartObjects` array stores it.
   */
  interface SmartObjectDefinition {
    /**
     * What to place, by the name `SmartObject.blueprints()` lists: `Bench/bench_2places_low`, `Bed/bed_low`, `Table/table_dice`, `profession/apothecary/alchemyTable_wip`.
     */
    blueprint: string;

    /**
     * Where it stands: the prefab's own origin, as the level's copies of it stand.
     */
    position: Vector3 | number[];

    /**
     * Which way it faces: a Quaternion, {x, y, z, w} or [x, y, z, w]. None when omitted.
     */
    rotation?: Quaternion | number[] | undefined;

    /**
     * What a script finds it by, up to 64 bytes of UTF-8.
     */
    name?: string | undefined;

    /**
     * True to place it without the blueprint's furniture: only the place a body takes, or the station, and the prompts that offer it, for a prop of your own choosing to stand there instead. Only a blueprint `SmartObject.blueprints()` lists with `bare` takes it. False when omitted.
     */
    bare?: boolean | undefined;
  }

  /**
   * Something `SmartObject.spawn` can place.
   */
  interface SmartObjectBlueprintInfo {
    /**
     * The name `SmartObject.spawn` takes: the game's prefab path under `Prefabs/`.
     */
    name: string;

    /**
     * What it is for.
     */
    kind: 'seat' | 'bed' | 'dice' | 'alchemy' | 'smithing' | 'drying' | 'smoking';

    /**
     * How many bodies can sit or lie on it at once.
     */
    seats: number;

    /**
     * Whether it can be placed `bare`: false for one whose furniture is one of its own entities, like a chair, or that has none.
     */
    bare: boolean;
  }

  /**
   * A station players can use: one the level places, or a placed smart object's.
   */
  interface SmartObjectStation {
    /**
     * Its GUID as sixteen lowercase hex digits, as `SmartObject.remove` takes it for one of the level's.
     */
    id: string;

    /**
     * What it is for.
     */
    kind: 'dice' | 'alchemy' | 'smithing' | 'drying' | 'smoking';

    /**
     * Where it stands.
     */
    position: Vector3;

    /**
     * Whether a script or a World Builder export placed it, rather than the level.
     */
    placed: boolean;

    /**
     * The placed smart object it belongs to; null for one of the level's own.
     */
    smartObject: SmartObject | null;
  }

  /**
   * One of the level's stations a level edit has taken out of a virtual world.
   */
  interface RemovedStation {
    /**
     * Its GUID, as `SmartObject.restore` takes it.
     */
    id: string;

    /**
     * What it was for.
     */
    kind: 'dice' | 'alchemy' | 'smithing' | 'drying' | 'smoking';

    /**
     * Where the level places it.
     */
    position: Vector3;

    /**
     * The virtual world it is out of. Out of the global world, it is out of every world.
     */
    virtualWorld: number;
  }

  /**
   * A bench, a bed, a dice table, an alchemy table, a smithy, a drying rack or a smokehouse a script or a World Builder export placed. Every player in its virtual world can sit on it, sleep in it or work at it as they would at the level's own. It never moves: assigning `position` or `rotation`, or calling `setVirtualWorld`, is put back on the next tick.
   */
  class SmartObject {
    /**
     * Wraps a smart object the server already holds; use SmartObject.spawn to place one.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * The blueprint it was placed from, as `SmartObject.blueprints()` lists it.
     */
    readonly blueprint: string;

    /**
     * What it is for.
     */
    readonly kind: 'seat' | 'bed' | 'dice' | 'alchemy' | 'smithing' | 'drying' | 'smoking';

    /**
     * The name it was placed with; empty for none.
     */
    readonly name: string;

    /**
     * Whether it was placed without its blueprint's furniture: only the place a body takes, or the station, and the prompts that offer it.
     */
    readonly bare: boolean;

    /**
     * The GUID of its station part as sixteen lowercase hex digits: the `id` `Dice.tables()` and `SmartObject.stations()` give it. `Crafting.stations()` lists an alchemy table or a smithy as `<level>:<stationId>`. Empty for a seat or a bed. It lasts only until the server stops.
     */
    readonly stationId: string;

    /**
     * The GUIDs of the places a body sits or lies on, as a seated player's stance and `DiceTableInfo.seats` name them.
     */
    readonly seats: string[];

    /**
     * Formats this handle for logging and debugging.
     * @returns Its id, blueprint and kind.
     */
    toString(): string;

    /**
     * Takes the smart object down for every player. A player sitting on it or working at it is let go.
     */
    destroy(): void;

    /**
     * The definition `SmartObject.spawn` takes to place it again: what a script saves. The server keeps nothing past a restart. It is one entry of a World Builder map's `smartObjects` array.
     * @returns Null once it is gone.
     */
    toJSON(): SmartObjectDefinition | null;

    /**
     * Places one of the game's interactive prefabs -- its furniture, solid, and the entities its interaction runs on -- for every player in the virtual world, built on each client under the same GUIDs, so sitting on it and working at it replicate as they do at the level's own. A placed dice table, crafting station, drying rack or smokehouse joins `Dice.tables()`, `Crafting.stations()` and the server's checks like the level's own. Kept in memory only: place it again on boot. The resource that places it owns it, so it goes when that resource stops. NPCs do not use it. A `bare` definition places the entities without the furniture, where a prop of your own stands.
     * @param definition What to place and where, in the format of one entry of a World Builder map's `smartObjects` array.
     * @param virtualWorld Optional virtual world whose players see it; the global one when omitted.
     * @returns The smart object. Throws for an unknown blueprint, a transform that is not finite, or `bare` on a blueprint that cannot stand bare.
     */
    static spawn(definition: SmartObjectDefinition, virtualWorld?: number): SmartObject;

    /**
     * Lists the smart objects scripts and World Builder exports placed.
     * @param virtualWorld Optional virtual world to list; omitted lists every one.
     * @returns One handle per placed object, in no particular order.
     */
    static all(virtualWorld?: number): SmartObject[];

    /**
     * Looks a placed smart object up by its network entity ID.
     * @param id Network entity identifier.
     * @returns The handle, or null.
     */
    static getById(id: number): SmartObject | null;

    /**
     * Lists everything `SmartObject.spawn` can place.
     * @returns One row per blueprint, sorted by name.
     */
    static blueprints(): SmartObjectBlueprintInfo[];

    /**
     * Lists every station players in a virtual world can use: the level's dice tables, crafting stations, drying racks and smokehouses, minus any a level edit took out of that world, and every placed one there or in the global world.
     * @param virtualWorld Optional virtual world to list for; the global one when omitted.
     * @returns One row per station, in no particular order.
     */
    static stations(virtualWorld?: number): SmartObjectStation[];

    /**
     * Takes one of the level's stations out of a virtual world through a level edit on its entity: listed nowhere, refused for any new session by the server (one already running at it finishes), and hidden on every client seeing that world -- a player in the global world sees every world. The furniture around it is the level's own scenery and stays; take it out with `LevelEdit.apply` or the World Builder. A placed smart object goes with `destroy()` instead. Kept in memory only, like every level edit.
     * @param id The level station's GUID, as `SmartObject.stations()` lists it.
     * @param virtualWorld Optional virtual world to take it out of; the global one, and so every world, when omitted.
     * @returns Throws for an id that is not one of the level's stations.
     */
    static remove(id: string, virtualWorld?: number): void;

    /**
     * Puts back one of the level's stations that a level edit took out of a virtual world.
     * @param id The level station's GUID, as `SmartObject.removed()` lists it.
     * @param virtualWorld Optional virtual world to put it back in; the global one when omitted.
     * @returns False when that world had no such removal.
     */
    static restore(id: string, virtualWorld?: number): boolean;

    /**
     * Lists the level's stations a level edit has taken out, by a script or a World Builder export alike.
     * @param virtualWorld Optional virtual world to list; omitted lists every one.
     * @returns One row per removal, in no particular order.
     */
    static removed(virtualWorld?: number): RemovedStation[];
  }

  interface SmartObject extends Entity {}

  /** */
  interface GateToggle {
    /**
     * Whether a cycle started.
     */
    accepted: boolean;

    /**
     * Why nothing moved, as a phrase that reads after the gate's name: `is already open`, `is already closing`. Empty when a cycle started.
     */
    reason: string;

    /**
     * The direction taken, or the one that was refused.
     */
    opening: boolean;

    /**
     * How long the motion still has to run. 0 when nothing moved.
     */
    seconds: number;
  }

  /**
   * Replicated handle for one of the level's castle gates.
   */
  class Gate {
    /**
     * Creates a script wrapper for a gate the server already knows; gates are the level's own and are never spawned.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * The level's own EntityGuid, as sixteen lowercase hex digits. The same on every machine, so it is the identity to store a gate under; `Gate.find` takes it back. A `gate` has no entity of its own, so its key is the GUID of the layer that draws it shut.
     */
    readonly guid: string;

    /**
     * What piece of architecture this is: `drawbridge` or `portcullis`, which animate, or `gate`, a pair of leaves the level swaps between drawn open and drawn shut -- the Ratborsch and Nebakov fortress gates and the Ruthard palace gate. A `gate` opens and closes at once.
     */
    readonly kind: string;

    /**
     * Where the gate is in its cycle: `closed`, `opening`, `open` or `closing`. Open always means passable -- a portcullis raised, a drawbridge lowered. Named `cycle` because every entity already has a `state`, which is its state bag.
     */
    readonly cycle: string;

    /**
     * How far open the gate is, from 0 shut to 1 fully open, read off the pose curve mined from the asset's own animation at the point the server clock says the motion has reached. It follows the bars, not the clock: a portcullis reads 0 within the first fifth of its close, and holds there while the clip finishes.
     */
    readonly openness: number;

    /**
     * Whether a cycle is running. `toggle` reverses a moving gate from where it has got to rather than refusing it.
     */
    readonly moving: boolean;

    /**
     * How long opening takes, in seconds, from the key range the asset's animation database stores. A gate with no opening clip runs its closing one backwards, so this is that clip's length. 0 for a `gate`.
     */
    readonly openDuration: number;

    /**
     * How long closing takes, in seconds, from the asset's animation database.
     */
    readonly closeDuration: number;

    /**
     * Formats this gate handle for logging and debugging.
     * @returns The gate ID, its level GUID, what kind it is and where it is in its cycle.
     */
    toString(): string;

    /**
     * Starts a cycle. The motion is never streamed: every client plays it out from the server clock it started on, and a gate reversed mid-cycle picks up where it is rather than snapping to the far end.
     * @param open true opens, false closes. Omitted heads away from whichever end the gate is at, reversing one already moving.
     * @returns What happened, and the phrase to explain it with when nothing did.
     */
    toggle(open?: boolean): GateToggle;

    /**
     * Lists every gate the level places, all of which exist from boot in the global world. In another virtual world a gate is built the first time anything there reaches it, so this lists only those.
     * @returns One handle per gate, in no particular order.
     */
    static all(): Gate[];

    /**
     * Looks a gate up by its network entity ID.
     * @param id Network entity identifier.
     * @returns The gate's handle, or null when no live gate has that ID.
     */
    static getById(id: number): Gate | null;

    /**
     * Looks a gate up by the level's own identity for it, which survives a restart and is the same on every machine. A gate the level places is always found, in any virtual world.
     * @param guid The gate's level EntityGuid as hex, with or without an `0x` prefix, as `gate.guid` prints it.
     * @param virtualWorld Optional virtual world to look in; omitted looks in the global one, where every body starts.
     * @returns The gate's handle, or null when the level places no such gate.
     */
    static find(guid: string, virtualWorld?: number): Gate | null;

    /**
     * Finds the gate nearest a point. Gate positions come from the level's own data, so unlike a door this needs no round trip to anybody's client.
     * @param position World-space point to measure from.
     * @param radius How far to look, in metres. Castle architecture is visible from a long way off, so this is tens of metres rather than a couple.
     * @param virtualWorld Optional virtual world to look in; omitted looks in the global one.
     * @returns The nearest gate within the radius, or null when there is none.
     */
    static nearest(position: Vector3 | Partial<Vector3>, radius: number, virtualWorld?: number): Gate | null;
  }

  interface Gate extends Entity {}

  /** */
  interface SiegeResult {
    /**
     * Whether the engine did it.
     */
    accepted: boolean;

    /**
     * Why it did not, as a phrase that reads after the engine's name: `is not loaded`, `cannot fire: the target is further than the engine can throw`. Empty when it did.
     */
    reason: string;

    /**
     * How long until the step it started is over: the whole load for `load`, until the projectile is let go for `fire`. 0 when nothing happened.
     */
    seconds: number;
  }

  /**
   * Replicated handle for a trebuchet or a cannon.
   */
  class SiegeEngine {
    /**
     * Creates a script wrapper for an existing siege engine with this ID; use SiegeEngine.spawn() to build one.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * What the engine is: `trebuchet` or `cannon`. The game ships no catapult and no ballista.
     */
    readonly kind: string;

    /**
     * Where the engine is in its cycle: `idle` (unloaded), `drawing` (a trebuchet's arm being winched down), `loading` (the crew laying the projectile), `ready` or `firing`. Named `cycle` because every entity already has a `state`, which is its state bag.
     */
    readonly cycle: string;

    /**
     * Whether the engine is ready to fire.
     */
    readonly loaded: boolean;

    /**
     * How fast the cycle runs, from 0.1 to 20; 1 is the game's own pace, which takes a trebuchet's crew nearly two minutes to load and lets the stone go 3.5 seconds into the shot. Changing it mid-step carries on from where the engine has got to.
     */
    speed: number;

    /**
     * Health taken from anything at the point of impact; a player has 100. A quarter of it reaches the edge of `damageRadius`. 0 leaves hits to `siegeImpact`.
     */
    damage: number;

    /**
     * How far from the point of impact anything is hurt, in metres, up to 100.
     */
    damageRadius: number;

    /**
     * Whether players may work the engine. A player within 2 m of an empty one gets the game's own action hint on the use key to take its place, and becomes its `operator`; near a trebuchet with a `crewRequired`, a free winch is offered too. Each ask raises `siegeEngineUse` first, which may refuse it. Off by default.
     */
    usable: boolean;

    /**
     * How far a shot goes along the operator's facing when they fire before laying the engine anywhere, in metres; kept between `minRange` and `maxRange`. 150 for a trebuchet and 120 for a cannon to start with.
     */
    range: number;

    /**
     * The closest the engine can be aimed, in metres along the ground.
     */
    readonly minRange: number;

    /**
     * The furthest the engine can be aimed, in metres along the ground.
     */
    readonly maxRange: number;

    /**
     * How long `load` takes at the engine's speed, in seconds.
     */
    readonly loadDuration: number;

    /**
     * What the engine can take before it is wrecked; 0, the default, means nothing harms it. Setting it heals the engine to the new maximum and repairs a wreck. A blast within `damageRadius` of an engine -- any engine's, its own included -- takes its share of that shot's `damage` off it, raising `siegeEngineDamage`.
     */
    maxHealth: number;

    /**
     * What is left of `maxHealth`. Setting it to 0 wrecks the engine and raising it from 0 repairs one; ignored while `maxHealth` is 0.
     */
    health: number;

    /**
     * Whether the engine's health ran out. A wrecked cannon shows the game's broken gun and a wrecked trebuchet leans and settles; nothing loads, aims or fires it, and its operator and crew are let go, until `repair` or a new `maxHealth`.
     */
    readonly wrecked: boolean;

    /**
     * Shots left to load; each `load` takes one, and an engine with none refuses to load. -1, the default, never runs out; any negative value means the same.
     */
    ammo: number;

    /**
     * How many of the engine's winches must be manned for its arm to be winched down; 0, the default, winds it by itself. With fewer hands than this the winding holds where it is, and goes on when they come back. Players near a usable engine that needs them are offered its winches, and each one there plays the game's own pull. Kept to `crewPlaces`.
     */
    crewRequired: number;

    /**
     * How many winches the engine has: 2 on a trebuchet, 0 on a cannon.
     */
    readonly crewPlaces: number;

    /**
     * The player in the engine's place, or null. They lay it where they look while they hold the block key, load it with the torch key, fire it with the attack key and step away with the use key; walking off lets go of it too. `setOperator` puts somebody there.
     */
    readonly operator: Player | null;

    /**
     * Who mans each winch, in place order, with null for an empty one.
     */
    readonly crew: (Player | null)[];

    /**
     * Formats this engine handle for logging and debugging.
     * @returns The engine ID, its kind and where it is in its cycle.
     */
    toString(): string;

    /**
     * Starts the reload cycle of an idle engine. `siegeEngineReady` fires when it can shoot.
     * @returns What happened, and the phrase to explain it with when nothing did.
     */
    load(): SiegeResult;

    /**
     * Turns a loaded engine to face the target and shoots at it. A trebuchet's arm lets the stone go at one angle, so its speed is solved for the distance; a cannon fires at one speed, so its elevation is. The stone is let go partway through the shot (`seconds`), then every client nearby draws it along the same arc. It comes down where the world stops it, which the attacker is asked to report when near enough, else the player nearest the target, or on the target when nobody can.
     * @param target Where the projectile should come down.
     * @param attacker The player credited with whatever it hits, in `playerDamage`, `npcDamage` and `siegeImpact`.
     * @returns What happened, and the phrase to explain it with when nothing did.
     */
    fire(target: Vector3 | Partial<Vector3>, attacker?: Player | number | null): SiegeResult;

    /**
     * Turns the engine to face a point without shooting. `fire` turns it anyway; this is for showing where it will shoot.
     * @param target The point to turn towards.
     * @returns What happened, and the phrase to explain it with when nothing did.
     */
    aim(target: Vector3 | Partial<Vector3>): SiegeResult;

    /**
     * Whether the engine can reach a point from where it stands.
     * @param target The point to test.
     * @returns Empty when it can; otherwise why not, as a phrase.
     */
    canHit(target: Vector3 | Partial<Vector3>): string;

    /**
     * Shows or hides the operator's aim guide: a translucent red beam along the shot's path -- out of a trebuchet's sling and along its arc, or straight from a cannon's muzzle -- to where it would come down, with a ball there. Off by default, which leaves laying the engine to the eye; the Fire hint says how far the shot goes either way.
     * @param enabled Whether the operator sees the beam.
     */
    setAimGuide(enabled: boolean): void;

    /**
     * Whether the operator sees the aim guide; see `setAimGuide`.
     * @returns True when the beam is shown.
     */
    getAimGuide(): boolean;

    /**
     * Makes the engine whole again: `health` back to `maxHealth`, and a wreck idle and unloaded.
     */
    repair(): void;

    /**
     * Puts a player in the engine's place without asking them, or takes whoever is there out of it. A player holds one place at a time, so they leave any other engine or winch first. Raises no `siegeEngineUse`.
     * @param player Who takes the engine's place, or null to empty it.
     * @returns False for a player who is no longer connected.
     */
    setOperator(player: Player | null): boolean;

    /**
     * Removes the engine on every client after emitting `siegeEngineDestroy`. A projectile already in the air still lands.
     */
    destroy(): void;

    /**
     * Builds and replicates a siege engine, unloaded. The trebuchet is the game's own model and plays its own winch, load and fire animations; the cannon is the game's static gun.
     * @param kind `trebuchet` or `cannon`.
     * @param position World-space position of the engine's base.
     * @param rotation Optional initial orientation: a Quaternion, or a Vector3 of Euler angles in degrees. `fire` and `aim` turn it to face their target.
     * @param virtualWorld Optional virtual world the engine belongs to; omitted puts it in the global one.
     * @returns The new engine's handle.
     */
    static spawn(kind: string, position: Vector3 | Partial<Vector3>, rotation?: Vector3 | Quaternion, virtualWorld?: number): SiegeEngine;

    /**
     * Lists the siege engines the server has.
     * @param virtualWorld Optional virtual world to list; omitted lists every one.
     * @returns One handle per engine, in no particular order.
     */
    static all(virtualWorld?: number): SiegeEngine[];

    /**
     * Looks an engine up by its network entity ID.
     * @param id Network entity identifier.
     * @returns The engine's handle, or null when no live engine has that ID.
     */
    static getById(id: number): SiegeEngine | null;

    /**
     * Removes siege engines, emitting `siegeEngineDestroy` for each one.
     * @param virtualWorld Optional virtual world to clear; omitted clears every one.
     * @returns How many were removed.
     */
    static destroyAll(virtualWorld?: number): number;
  }

  interface SiegeEngine extends Entity {}

  /** */
  interface SiegeLadderKind {
    /**
     * Its name: `siege`, `tall`, `short` or `low`.
     */
    kind: string;

    /**
     * The mesh it stands as, a path from the shared prop catalog.
     */
    model: string;

    /**
     * How far up its rungs go, in metres.
     */
    height: number;

    /**
     * How far it leans towards the wall standing, in degrees about its own X; it leans towards its forward.
     */
    lean: number;
  }

  /**
   * Replicated handle for a siege ladder leaning on a wall.
   */
  class SiegeLadder {
    /**
     * Creates a script wrapper for an existing siege ladder with this ID; use SiegeLadder.spawn() to place one.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * Which of the game's ladders it is: `siege` (the Suchdol siege ladder, 9.75 m), `tall` (5.5 m), `short` (3.25 m) or `low` (2 m).
     */
    readonly kind: string;

    /**
     * `standing` against its wall, `pushed` (a defender's swing, then its fall), `down`, `raising` or `broken`. Named `pose` because every entity already has a `state`, which is its state bag.
     */
    readonly pose: string;

    /**
     * How far up its rungs go, in metres: its top rests this high above its foot on the wall's top. Pick the kind whose height meets the walk.
     */
    readonly height: number;

    /**
     * Whether the game's own climb is offered on it: while it stands and nobody has started to push it.
     */
    readonly climbable: boolean;

    /**
     * Whether players are offered the push at its top and the raise at the foot of a fallen one, each raising `siegeLadderUse` first. On by default. The climb is the game's and is offered whenever the ladder stands.
     */
    usable: boolean;

    /**
     * Who last pushed it, or null.
     */
    readonly pusher: Player | null;

    /**
     * Formats this ladder handle for logging and debugging.
     * @returns The ladder ID, its kind and its pose.
     */
    toString(): string;

    /**
     * Pushes a standing ladder off its wall. It stands through the swing, falls away from the wall as the blade meets it -- throwing off anyone climbing it -- and lies where it lands, about 4.5 seconds later.
     * @param player Who pushes it: they play the game's own halberd push, and are credited with it in `siegeLadderFall`.
     * @returns What happened, and the phrase to explain it with when nothing did.
     */
    push(player?: Player | number | null): SiegeResult;

    /**
     * Lays a fallen ladder back against its wall, over the lift's 5.7 seconds.
     * @param player Who raises it: they play the game's own ladder lift.
     * @returns What happened, and the phrase to explain it with when nothing did.
     */
    raise(player?: Player | number | null): SiegeResult;

    /**
     * Breaks the ladder: it lies in pieces where it stands, anyone on it falls, and nothing raises it until `repair`.
     */
    break(): void;

    /**
     * Puts a fallen or broken ladder back against its wall at once.
     */
    repair(): void;

    /**
     * Removes the ladder on every client after emitting `siegeLadderDestroy`.
     */
    destroy(): void;

    /**
     * Places one of the game's own ladders leaning on a wall, the way the Suchdol and Nebakov sieges stand theirs: the game's climb, and the game's step off over the palisade at its top, which wants the top a little over the walk.
     * @param kind `siege`, `tall`, `short` or `low`.
     * @param position Where its foot stands, on the ground below the wall.
     * @param rotation Its facing: forward is the way to the wall it leans on. A Quaternion, or a Vector3 of Euler angles in degrees.
     * @param virtualWorld Optional virtual world the ladder belongs to; omitted puts it in the global one.
     * @returns The new ladder's handle.
     */
    static spawn(kind: string, position: Vector3 | Partial<Vector3>, rotation?: Vector3 | Quaternion, virtualWorld?: number): SiegeLadder;

    /**
     * What one kind of ladder is: its mesh, its climb and its lean -- what a placement preview needs to show it as it will stand.
     * @param kind `siege`, `tall`, `short` or `low`.
     * @returns The kind, or null for a name that is not one.
     */
    static describe(kind: string): SiegeLadderKind | null;

    /**
     * Lists the siege ladders the server has.
     * @param virtualWorld Optional virtual world to list; omitted lists every one.
     * @returns One handle per ladder, in no particular order.
     */
    static all(virtualWorld?: number): SiegeLadder[];

    /**
     * Looks a ladder up by its network entity ID.
     * @param id Network entity identifier.
     * @returns The ladder's handle, or null when no live ladder has that ID.
     */
    static getById(id: number): SiegeLadder | null;

    /**
     * Removes siege ladders, emitting `siegeLadderDestroy` for each one.
     * @param virtualWorld Optional virtual world to clear; omitted clears every one.
     * @returns How many were removed.
     */
    static destroyAll(virtualWorld?: number): number;
  }

  interface SiegeLadder extends Entity {}

  /**
   * Replicated handle for a pile of hurling stones on a battlement.
   */
  class StonePile {
    /**
     * Creates a script wrapper for an existing stone pile with this ID; use StonePile.spawn() to place one.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * Stones left to throw; each throw takes one, and a pile with none offers nothing. Players see at most nine in the heap. -1, the default, never runs out; any negative value means the same.
     */
    stones: number;

    /**
     * Whether players are offered the game's own prompt at the pile. On by default.
     */
    usable: boolean;

    /**
     * Health taken from anyone at the point a stone comes down, up to 1000; a quarter of it reaches the edge of `damageRadius`. 60 to start with; a player has 100.
     */
    damage: number;

    /**
     * How far from where a stone comes down anyone is hurt, in metres, up to 10. 1.5 to start with.
     */
    damageRadius: number;

    /**
     * Formats this pile handle for logging and debugging.
     * @returns The pile ID and the stones it has left.
     */
    toString(): string;

    /**
     * Removes the pile on every client after emitting `stonePileDestroy`. A stone already thrown still lands.
     */
    destroy(): void;

    /**
     * Places the game's own battlement pile of hurling stones, the Nebakov and Suchdol defenders' heap. Its prompt, the lift and the heave over the wall are the game's stone throwing; each stone is a real rigid body that falls where it falls.
     * @param position Where the thrower stands on the walk, behind the parapet.
     * @param rotation Its facing: forward is the way the stones go, over the wall. A Quaternion, or a Vector3 of Euler angles in degrees.
     * @param virtualWorld Optional virtual world the pile belongs to; omitted puts it in the global one.
     * @returns The new pile's handle.
     */
    static spawn(position: Vector3 | Partial<Vector3>, rotation?: Vector3 | Quaternion, virtualWorld?: number): StonePile;

    /**
     * Lists the stone piles the server has.
     * @param virtualWorld Optional virtual world to list; omitted lists every one.
     * @returns One handle per pile, in no particular order.
     */
    static all(virtualWorld?: number): StonePile[];

    /**
     * Looks a pile up by its network entity ID.
     * @param id Network entity identifier.
     * @returns The pile's handle, or null when no live pile has that ID.
     */
    static getById(id: number): StonePile | null;

    /**
     * Removes stone piles, emitting `stonePileDestroy` for each one.
     * @param virtualWorld Optional virtual world to clear; omitted clears every one.
     * @returns How many were removed.
     */
    static destroyAll(virtualWorld?: number): number;
  }

  interface StonePile extends Entity {}

  /** */
  interface AreaDefinition {
    /**
     * The area's identity, unique among script areas. It may not be sixteen hex digits, which is how a level area's GUID reads.
     */
    id: string;

    /**
     * A readable name; the id when omitted.
     */
    name?: string | undefined;

    /**
     * CryEngine's own area shapes: `AreaBox`, `AreaShape` (a closed polygon with a height) and `AreaSphere`.
     */
    type: 'box' | 'shape' | 'sphere';

    /**
     * Where the shape is placed from; the origin when omitted.
     */
    position?: Vector3 | undefined;

    /**
     * The shape's orientation: a Quaternion or {x, y, z, w}, or Euler angles in degrees.
     */
    rotation?: Quaternion | Vector3 | undefined;

    /**
     * box: the corner nearest negative infinity, relative to `position` before rotation.
     */
    min?: Vector3 | undefined;

    /**
     * box: the opposite corner. Every component must be above `min`'s.
     */
    max?: Vector3 | undefined;

    /**
     * shape: 3 to 256 corners relative to `position` before rotation, closed back to the first. As in the engine, the floor is the lowest corner.
     */
    points?: Vector3[] | undefined;

    /**
     * shape: how far above its lowest corner the area reaches; 0, the default, for no ceiling.
     */
    height?: number | undefined;

    /**
     * sphere: its radius around `position`.
     */
    radius?: number | undefined;

    /**
     * Free-form labels, found again with `Area.withLabel`.
     */
    labels?: string[] | undefined;

    /**
     * Anything JSON can hold, kept on the area for scripts.
     */
    metadata?: Record<string, unknown> | undefined;

    /**
     * The one virtual world it belongs to; every world when omitted.
     */
    virtualWorld?: number | undefined;

    /**
     * Invisible boundary collision from both sides; false when omitted. Polygons need a simple outline and positive height. Boxes and polygons get side walls only unless `collisionFloor` or `collisionCeiling` asks for more; spheres use a tessellated shell.
     */
    collision?: boolean | undefined;

    /**
     * box and shape: also close the bottom of the boundary with a plate across the whole interior; false when omitted.
     */
    collisionFloor?: boolean | undefined;

    /**
     * box and shape: also close the top of the boundary with a plate across the whole interior; false when omitted.
     */
    collisionCeiling?: boolean | undefined;

    /**
     * The boundary's thickness in metres, centred on the outline: 0.2 when omitted, from 0.05 to 2.
     */
    collisionThickness?: number | undefined;

    /**
     * Whether it starts enabled; true when omitted.
     */
    enabled?: boolean | undefined;
  }

  /** */
  interface AreaDoor {
    /**
     * The door's level EntityGuid as hex, which `Door.find` takes.
     */
    guid: string;

    /**
     * The entity name the level gives the door.
     */
    name: string;

    /**
     * Where the level places the door.
     */
    position: Vector3;

    /**
     * The `crime_doorKind` the level gives the link: the way in, an inner door, or a storage room's.
     */
    link: 'entrance' | 'basic' | 'storage';
  }

  /**
   * Handle for one area: one of the level's own gameplay areas, or one a script or the World Builder drew. The server tests who is inside itself, by the game's own rule for that kind of area.
   */
  class Area {
    /**
     * Wraps an area handle the server already holds; use Area.create, Area.getById or the lookups to get one.
     * @param handle Internal area handle.
     */
    constructor(handle: number);

    /**
     * False once a script area has been destroyed; every other read then returns its empty value.
     */
    readonly exists: boolean;

    /**
     * The stable identity to store an area under. A level area's is its EntityGuid as sixteen lowercase hex digits, the same on every machine and every boot, as `door.guid` prints a door's; a script area's is the id it was created with.
     */
    readonly id: string;

    /**
     * Whether the level placed it or a script built it.
     */
    readonly kind: 'level' | 'script';

    /**
     * A level area's entity class -- a `TriggerArea` prism, an `AreaUnion` of them, or a `SmartAreaShape` -- or a script area's shape.
     */
    readonly type: 'TriggerArea' | 'AreaUnion' | 'SmartAreaShape' | 'box' | 'shape' | 'sphere';

    /**
     * The level's entity name, or the script area's name. Only a script area's can be changed.
     */
    name: string;

    /**
     * The level's own `Label` values -- `private`, `personal`, `interior`, `prohibited`, `castle` and the rest the game's AI reads -- or a script area's labels. Only a script area's can be changed.
     */
    labels: string[];

    /**
     * Whatever a script keeps on a script area: ownership, a type, rules. Stored as JSON, so read it, change it and assign it back. Always empty on a level area.
     */
    metadata: Record<string, unknown>;

    /**
     * Invisible boundary walls block both directions while enabled, leaving the interior empty. Authored areas only; invalid collision shapes throw.
     */
    collision: boolean;

    /**
     * Whether a box or polygon boundary also closes its bottom with a plate across the whole interior. False by default, for side walls only. A sphere is always one closed shell. Authored areas only.
     */
    collisionFloor: boolean;

    /**
     * Whether a box or polygon boundary also closes its top with a plate across the whole interior. False by default, for side walls only. A sphere is always one closed shell. Authored areas only.
     */
    collisionCeiling: boolean;

    /**
     * How thick the boundary is, in metres, centred on the drawn outline: 0.2 by default, from 0.05 to 2. Throws when the shape is too small for it while collision is on. Authored areas only.
     */
    collisionThickness: number;

    /**
     * Whether crossings are raised and the lookups find it. Switching it off raises `areaExit` for everyone inside on the next tick; switching it on, `areaEnter`. Works on level areas too.
     */
    enabled: boolean;

    /**
     * The one virtual world a script area belongs to, or null for all of them. A body in another world still crosses it, with `matchingVirtualWorld` false. A level area is in every world.
     */
    virtualWorld: number | null;

    /**
     * A script area's origin, which its shape is placed from; assignment moves it. A level area's is the centre of its bounds.
     */
    position: Vector3;

    /**
     * A script area's orientation; assignment turns it. A level area's shape is already in world space, so its rotation is the identity.
     */
    rotation: Quaternion;

    /**
     * How far above its lowest point a prism reaches, in metres; 0 for a shape with no ceiling. A TriggerArea's comes from the level's baked prism.
     */
    readonly height: number;

    /**
     * For a SmartAreaShape bound to one of the game's map locations, that location's English name, e.g. a settlement. Empty otherwise.
     */
    readonly location: string;

    /**
     * The world box around the area. An area with no ceiling reaches far above and below.
     */
    readonly bounds: { min: Vector3; max: Vector3 };

    /**
     * For an AreaUnion, the TriggerAreas it joins; it contains whatever one of them does. Empty otherwise.
     */
    readonly members: Area[];

    /**
     * The doors the level itself links to this area as its `crime_door`s, which is how the game knows a house's doors. Only native links: nothing is inferred from where a door stands.
     */
    readonly doors: AreaDoor[];

    /**
     * Every player inside as of the last tick, in any world.
     */
    readonly players: Player[];

    /**
     * Every body inside as of the last tick: players, horses, carts and server NPCs.
     */
    readonly occupants: (Player | Horse | Cart | Npc)[];

    /**
     * Formats this area for logging and debugging.
     * @returns Its id, kind, type and name.
     */
    toString(): string;

    /**
     * Tests a position the way the game tests its own areas, whether or not the area is enabled and in whatever world.
     * @param position The world position to test.
     * @returns True when the position is inside.
     */
    contains(position: Vector3): boolean;

    /**
     * Tests where a body stands right now, and in which world: an area bound to another virtual world does not contain it. This is containment only -- whether the game considers the player to be trespassing is decided by its own crime system on the player's client.
     * @param player The body to test; a player who has not reported a pose yet is never inside.
     * @returns True when the body is inside.
     */
    containsPlayer(player: Player | Horse | Cart | Npc): boolean;

    /**
     * Moves, turns or reshapes a script area. Bodies standing where it now is, or was, get their `areaEnter` and `areaExit` on the next tick. A level area cannot be reshaped.
     * @param definition The shape fields to change; any left out keep their value. `id`, `labels`, `metadata` and the rest are ignored here.
     * @returns False for a level area, a gone one, or a shape the engine could not build.
     */
    setShape(definition: Partial<AreaDefinition>): boolean;

    /**
     * Removes a script area. Every body still inside gets its `areaExit` first, while the handle still resolves.
     * @returns False for a level area or one already gone.
     */
    destroy(): boolean;

    /**
     * The definition `Area.create` takes to build this area again, which is what a script saves: the server keeps nothing past a restart. It is the format the World Builder exports too.
     * @returns Null for a level area, which the level always builds.
     */
    toJSON(): AreaDefinition | null;

    /**
     * Lists available authored herbs whose source positions are inside this area's current geometry. Works without nearby players. Picked and script-removed plants are excluded. Ignores area.enabled; an area in another world or a destroyed area returns an empty array.
     * @param virtualWorld One virtual-world bucket; defaults to the area's world, or 0 for an area in every world. Pass player.virtualWorld for a player's bucket. 0xffffffff selects only the global bucket. Invalid IDs throw.
     */
    getHerbs(virtualWorld?: number): GatheringHerb[];

    /**
     * Removes authored herbs inside this area's current geometry for one world, including plants already waiting to regrow. Gives no items or experience and raises no harvest events. Plants stay removed until an explicit respawn or server restart, even if the area's geometry, enabled state or lifetime changes.
     * @param virtualWorld One virtual-world bucket; defaults to the area's world, or 0 for an area in every world. Pass player.virtualWorld for a player's bucket. 0xffffffff selects only the global bucket. Invalid IDs throw.
     * @returns Number of sources newly marked for removal; repeated removal returns 0. Includes picked plants whose regrowth was disabled.
     */
    removeHerbs(virtualWorld?: number): number;

    /**
     * Restores picked and script-removed herbs inside this area's current geometry in one world. Restores vegetation for connected players and future arrivals. Ignores area.enabled; an area in another world or a destroyed area changes nothing.
     * @param virtualWorld One virtual-world bucket; defaults to the area's world, or 0 for an area in every world. Pass player.virtualWorld for a player's bucket. 0xffffffff selects only the global bucket. Invalid IDs throw.
     * @returns Number of active harvested or removed sources restored.
     */
    respawnHerbs(virtualWorld?: number): number;

    /**
     * Builds a script area. Bodies already standing in it get `areaEnter` on the next tick. Kept in memory only: save `area.toJSON()` wherever the resource keeps its data, and create it again on boot.
     * @param definition The area to build, in the format `area.toJSON()` and the World Builder export write.
     * @returns The new area. Throws for an id already taken or a shape the engine could not build.
     */
    static create(definition: AreaDefinition): Area;

    /**
     * Looks an area up by its stable identity.
     * @param id A script area's id, or a level area's GUID in hex as `area.definition.id` prints it.
     * @returns The area, or null when there is none.
     */
    static getById(id: string): Area | null;

    /**
     * Every enabled area containing a position, level and script alike.
     * @param position The world position to test.
     * @param virtualWorld Optional world; when given, a script area bound to another world is left out.
     * @returns The areas, in no particular order.
     */
    static getAt(position: Vector3, virtualWorld?: number): Area[];

    /**
     * Every enabled area the body stands in right now, in its own world.
     * @param player The body whose position and world to test.
     * @returns The areas; empty for a player who has not reported a pose yet.
     */
    static getAtPlayer(player: Player | Horse | Cart | Npc): Area[];

    /**
     * Every area whose bounds come within a radius of a position, enabled or not.
     * @param position Where to look from.
     * @param radius How far, in metres, from the area's bounds.
     * @param virtualWorld Optional world; when given, a script area bound to another world is left out.
     * @returns The areas, nearest first.
     */
    static nearby(position: Vector3, radius: number, virtualWorld?: number): Area[];

    /**
     * Every area carrying a label, such as `private` or `castle`.
     * @param label The label, compared without regard to case.
     * @returns The areas, level ones first.
     */
    static withLabel(label: string): Area[];

    /**
     * Every area the server knows.
     * @param kind Optional: only the level's areas, or only script ones.
     * @returns The areas, level ones first.
     */
    static all(kind?: 'level' | 'script'): Area[];
  }

  /**
   * A named world export loaded from mod.world_resources. Server-owned, with live access to its surviving props, effects, level edits, doors, smart objects, items, areas and patrol routes. Obtain handles through all or find; runtime loading, reloading and unloading are not exposed.
   */
  class WorldResource {
    private constructor();

    /**
     * Unique case-sensitive resource name: the filename without .world.json, or the explicit config name. Empty after shutdown.
     */
    readonly name: string;

    /**
     * Resolved absolute source file path, in UTF-8. Empty after shutdown.
     */
    readonly path: string;

    /**
     * The exported game level, checked against the server level at startup. Empty after shutdown.
     */
    readonly level: string;

    /**
     * Whether this export is registered. Remains true even if scripts destroy all of its contents.
     */
    readonly loaded: boolean;

    /**
     * A fresh array of surviving props in export order. Changing the array does not change membership; the handles support normal Prop operations.
     */
    readonly props: Prop[];

    /**
     * A fresh array of surviving point lights in export order, excluding destroyed lights.
     */
    readonly lights: Light[];

    /**
     * A fresh array of surviving effects in export order, excluding destroyed effects.
     */
    readonly effects: Vfx[];

    /**
     * A fresh array of surviving level edits in export order, excluding restored edits.
     */
    readonly levelEdits: LevelEdit[];

    /**
     * A fresh array of the surviving doors the export hangs, in export order, excluding destroyed ones. A door the export takes out of the level is a level edit, in `levelEdits`; `Door.removed` lists those.
     */
    readonly doors: Door[];

    /**
     * A fresh array of the surviving seats, beds, tables and stations the export places, in export order, excluding destroyed ones. A level station the export takes out is a level edit, in `levelEdits`.
     */
    readonly smartObjects: SmartObject[];

    /**
     * A fresh array of the stacks the export lays on the ground that nobody has picked up yet, in export order. Each one's `name` is the name the export gave it.
     */
    readonly items: GroundItem[];

    /**
     * A fresh array of surviving exported areas in export order, excluding destroyed areas.
     */
    readonly areas: Area[];

    /**
     * Surviving exported patrol definitions, in export order.
     */
    readonly patrolRoutes: PatrolRoute[];

    /**
     * True after every configured export has loaded successfully, even when the config list is empty. Check this before subscribing to worldResourcesReady when a script may start later.
     */
    static readonly ready: boolean;

    /**
     * Lists config-loaded world exports in configuration order. Empty before successful startup and after shutdown.
     * @returns A fresh array of resource handles.
     */
    static all(): WorldResource[];

    /**
     * Finds a config-loaded export by name. Throws for a non-string argument.
     * @param name Exact, case-sensitive resource name.
     * @returns The resource, or null when absent or startup has not completed.
     */
    static find(name: string): WorldResource | null;
  }

  /**
   * Container handle for a spawned chest, a level chest, or virtual stock without a world entity.
   */
  class Stash {
    /**
     * Creates a script wrapper for an existing container with this ID; use Stash.spawn() for a chest, Stash.createVirtual() for stock without a chest, or Stash.find() for one the level places.
     * @param id Network entity identifier.
     */
    constructor(id: number);

    /**
     * The server-minted identity of a scripted chest or virtual stash, from 1. Distinct from the network entity id. Virtual stashes use it for an inventory GUID without creating a chest. 0 for a level chest.
     */
    readonly stashId: number;

    /**
     * The EntityGuid every client finds this container under, as sixteen lowercase hex digits: the level's own for a container the level places, the one minted when a script spawned it otherwise. Empty for virtual stock; `Stash.find` takes physical container GUIDs back.
     */
    readonly guid: string;

    /**
     * The level's own EntityGuid for a container the level places, as sixteen lowercase hex digits; empty for a scripted chest or virtual stock. The same on every machine, so it is the identity to store a container under; `Stash.find` takes it back.
     */
    readonly levelGuid: string;

    /**
     * The entity name the level gives the container, or `spawned_stash_<stashId>` for a scripted chest; empty for virtual stock.
     */
    readonly name: string;

    /**
     * How many rows the server is holding. A container linked to another in the level opens that one's contents, and reads them here too.
     */
    readonly itemCount: number;

    /**
     * Whether a player holds its view. Physical chests animate their lid by it; virtual stashes have no lid.
     */
    readonly isOpen: boolean;

    /**
     * Whether this stash has stock without a chest entity. Open it with open(player); its position and rotation do not govern access, and guid is empty.
     */
    readonly isVirtual: boolean;

    /**
     * The network ID of the player who has its contents open, or 0. While it is held, every other player's open is refused before their transfer screen appears.
     */
    readonly holderId: number;

    /**
     * Whether the container is locked. Assignment reaches every client that has it streamed in. A locked container offers only its key and its lockpick; a script locking one marks it `lockedByServer`, which the generated home or shop key does not open. Unlocking it by any route the server accepted clears both.
     */
    locked: boolean;

    /**
     * Whether the lock was set by a script rather than by the level. The generated home or shop key does not open a container locked from here.
     */
    readonly lockedByServer: boolean;

    /**
     * Whether the container offers the lockpick while locked. Starts as the level authored it; assignment reaches every client, and a pick on one that cannot be is refused.
     */
    lockpickable: boolean;

    /**
     * How hard the lockpicking minigame is, from 0 to 1, or null for what the level gave it. The game's prompt names it in fifths: below 0.2 very easy, then easy, average, hard, and from 0.8 very hard. Assignment reaches every client and applies from the next pick; null gives the level's back.
     */
    lockDifficulty: number | null;

    /**
     * Which lock the lockpicking minigame shows, or null for the level's. Assignment reaches every client.
     */
    lockFanciness: 'common' | 'fancy' | null;

    /**
     * The model a scripted container was spawned with, or empty for the default chest and for one the level places.
     */
    readonly model: string;

    /**
     * The item class GUID of the key that unlocks the container, as `player.giveItem` takes it. Reads the level's own key until a script assigns one; empty when it has none. Assigning a class makes every client ask for that class instead, so a player holding it is offered "unlock and open"; assigning an empty string restores the level's. An unknown class is refused and logged. The server does not see inventories: which key a player holds is their client's to say, and `stashInteract` is where a script checks its own record.
     */
    keyItem: string;

    /**
     * The horizontal unit direction a player has to face the container along to be offered it: the game's own use check only offers a chest to a body standing behind this axis and looking along it. Stand at `position - useDirection * distance` to face it.
     */
    readonly useDirection: Vector3;

    /**
     * Whether the level builds this container locked, which is the state it has at boot. False for one a script spawned.
     */
    readonly startsLocked: boolean;

    /**
     * Formats this container handle for logging and debugging.
     * @returns The stash ID, its level GUID, how much is in it, who has it open and whether it is locked.
     */
    toString(): string;

    /**
     * Destroys this scripted chest or virtual stash and forgets what was in it. Anything inside goes with it. Does nothing to a container the level places.
     */
    destroy(): void;

    /**
     * Opens a virtual stash from anywhere in its virtual world. Calls stashInteract before granting exclusive access. Returns false for a physical stash, a locked or busy stash, an unavailable player, or a script refusal. A true result requests the screen; the client releases access if it cannot open.
     * @param player Player whose native transfer screen should open.
     */
    open(player: Player): boolean;

    /**
     * Reads what the container holds. A linked child reads its master's.
     * @returns A copy of its rows, or null once the container is gone.
     */
    getInventory(): { revision: number; items: InventoryRow[] } | null;

    /**
     * Puts items in the container, as `Inventory.add` gives them to a player.
     */
    addItem(request: { item: string; amount: number; metadata?: Record<string, unknown>; revision?: number }): InventoryResult;

    /**
     * Takes exact units out of named rows, every one of them or none.
     */
    removeItem(request: { units: InventoryUnit[]; revision?: number }): InventoryResult;

    /**
     * Replaces everything in the container, which is how saved contents come back. At most 128 rows. An empty list empties it.
     */
    setInventory(request: { items: { id?: string; item: string; amount: number; metadata?: Record<string, unknown> }[]; revision?: number }): InventoryResult;

    /**
     * Creates empty server-owned stock without spawning a chest on any client. Supports the same inventory methods and events as physical stashes. Use open(player) to show the native transfer screen, and destroy() to discard it. Contents last until destroyed or the server restarts.
     * @param virtualWorld World whose players may open it, as a uint32 number; defaults to the global world. Invalid values throw before creating stock.
     */
    static createVirtual(virtualWorld?: number): Stash;

    /**
     * Spawns and replicates an empty container. Fill it with `addItem` or `setInventory`, or let players put things in it.
     * @param position Optional world-space spawn position; omitted components default to zero.
     * @param rotation Optional initial orientation: a Quaternion, or a Vector3 of Euler angles in degrees.
     * @param virtualWorld Optional virtual world the container belongs to; omitted puts it in the global one.
     * @param options `model` builds it as any .cgf, .cga or .cdf -- a cabinet such as `objects/manmade/common_furniture/cabinets/cabinet_fancy_d.cgf` -- instead of the default chest. Throws on any other path. A mesh without the game's open and close clips stays shut while it is looted.
     * @returns The newly spawned container handle.
     */
    static spawn(position?: Vector3 | Partial<Vector3>, rotation?: Vector3 | Quaternion, virtualWorld?: number, options?: { model?: string }): Stash;

    /**
     * Lists every container the server currently has: every one the level places, which exist from boot in the global world, and every scripted chest or virtual stash. In another virtual world a level container is built the first time anything there reaches it, so this lists only those.
     * @returns One handle per live container, in no particular order.
     */
    static all(): Stash[];

    /**
     * Looks a container up by its network entity ID.
     * @param id Network entity identifier.
     * @returns The container's handle, or null when no live container has that ID.
     */
    static getById(id: number): Stash | null;

    /**
     * Looks a container up by its EntityGuid: one the level places, by the level's own identity for it, which survives a restart and is the same on every machine, and is found in any virtual world; or one a script spawned, in the virtual world it was spawned in, for as long as it exists.
     * @param guid The container's EntityGuid as hex, with or without an `0x` prefix, as `stash.guid` prints it.
     * @param virtualWorld Optional virtual world to look in; omitted looks in the global one, where every body starts.
     * @returns The container's handle, or null when no container has that GUID there.
     */
    static find(guid: string, virtualWorld?: number): Stash | null;

    /**
     * Finds the physical container nearest a point, level or spawned. Virtual stock is excluded. The server knows where every container the level places stands, so this answers at once, without asking a client.
     * @param position The point to measure from, usually a player's position.
     * @param maxDistance How far to look, in metres; omitted looks 5 m, about the reach the game gives a chest.
     * @param virtualWorld Optional virtual world to look in; omitted looks in the global one.
     * @returns The nearest container's handle, or null when none is that close.
     */
    static nearest(position: Vector3 | Partial<Vector3>, maxDistance?: number, virtualWorld?: number): Stash | null;

    /**
     * Destroys scripted chests and virtual stashes, including everything inside. The level's own stay.
     * @param virtualWorld Optional virtual world to clear; omitted clears every one of them.
     * @returns How many containers were removed.
     */
    static destroyAll(virtualWorld?: number): number;
  }

  interface Stash extends Entity {}

  /**
   * The server's own clock and weather, which every client follows, and the three questions it can ask a client's engine about the level itself.
   */
  const World: {
    /**
     * The configured level loaded by every client in this server session.
     */
    readonly level: string;

    /**
     * Whole days the clock has run, from the level's own midnight. Starts at 0 and only grows.
     */
    readonly day: number;

    /**
     * Hour within the current day, from 0 up to but not including 24, with the minutes as the fraction. 13.5 is half past one.
     */
    readonly hour: number;

    /**
     * How many game seconds pass per real second. 15 is the game's own pace, 0 stops the clock, and the server drops it to 0 by itself at the clock's ceiling.
     */
    readonly timeScale: number;

    /**
     * The time-of-day preset the sky is blending towards, which is where it settles. Set it with `setWeather`.
     */
    readonly weather: string;

    /**
     * The preset the sky is blending from; the same as `weather` once it has settled.
     */
    readonly previousWeather: string;

    /**
     * Whether a blend is still running. `setWeather` is refused while it is: a half-finished blend has no single preset to leave from.
     */
    readonly weatherBlending: boolean;

    /**
     * Game seconds left of the blend, and 0 once the sky has settled. `setWeather` is refused until then.
     */
    readonly weatherRemaining: number;

    /**
     * How hard it is raining, from 0 to 1.
     */
    readonly rainIntensity: number;

    /**
     * How much of the rain is drawn, from 0 to 1. Thins the downpour without stopping it.
     */
    readonly rainAmount: number;

    /**
     * The wind every client is blowing, in metres per second, world space. It bends the trees, drags the cloth and slants the rain, and it is the one part of the weather the game itself never touches. Set it with `setWind`.
     */
    readonly wind: Vector3;

    /**
     * How hard the wind is blowing, whichever way: the length of `wind`, from 0 to 50.
     */
    readonly windSpeed: number;

    /**
     * How wet the ground has become, from 0 to 3. Derived from how long it has been raining, so it is read-only, and it dries far slower than it soaks.
     */
    readonly wetness: number;

    /**
     * Puddle coverage, from 0 to 1. Derived like `wetness`, and only starts filling after the rain has run a while.
     */
    readonly puddles: number;

    /**
     * Points of interest in the configured level only, from the compiled game catalog. Positions are ground-marker clusters checked against the level navmesh; a teleport still needs the destination to stream on the client.
     */
    pointsOfInterest(): { name: string; group: string; level: string; position: { x: number; y: number; z: number } }[];

    /**
     * Winds the clock forward to an absolute day and hour, which is how a saved clock is restored.
     * @param day Absolute day to wind forward to, as the `day` property counts them. Must be whole.
     * @param hour Hour of that day, from 0 up to but not including 24.
     * @returns True when the clock moved; false when that moment is already past, or the day or hour is out of range.
     */
    setTime(day: number, hour: number): boolean;

    /**
     * Winds the clock forward to the next time it is this hour, rolling into tomorrow when it has passed today. The clock never runs backwards: clients cannot be wound back with it.
     * @param hour Hour to wind forward to, from 0 up to but not including 24.
     * @returns True when the clock moved; false when the hour is out of range.
     */
    setHour(hour: number): boolean;

    /**
     * Sets how fast the day passes for everyone.
     * @param scale Game seconds per real second, from 0 to 200. 0 freezes the clock where it stands.
     * @returns True when the scale was taken; false when it is out of range.
     */
    setTimeScale(scale: number): boolean;

    /**
     * Blends the sky to another of the game's time-of-day presets, and raises `worldWeatherChange`. Only a client can tell whether a preset exists, so a name the game does not know leaves every sky where it is.
     * @param preset Name of one of the game's own time-of-day presets, such as `cloudless_sunny`.
     * @param seconds Game seconds to blend over, up to 21600. Omitted, the sky changes at once.
     * @returns True when the blend started; false while an earlier one is still running, or when the name or duration is out of range.
     */
    setWeather(preset: string, seconds?: number): boolean;

    /**
     * Sets the rain, which is separate from the sky preset: a preset can be overcast without a drop falling. Starting or stopping it restarts the phase the ground soaks and dries over.
     * @param intensity How hard it rains, from 0 to 1. 0 stops it.
     * @param amount How much of the rain is drawn, from 0 to 1; defaults to all of it.
     * @returns True when the rain was taken; false when either value is out of range.
     */
    setRain(intensity: number, amount?: number): boolean;

    /**
     * Sets the wind for everyone. There is nothing to blend against -- the engine takes a vector and holds it -- so a gust is a script ramping this itself. It also drives a physical wind area, which is why the length is capped.
     * @param wind Wind velocity in metres per second, world space; omitted components are zero. Its length may not exceed 50.
     * @returns True when the wind was taken; false when a component is not finite or it blows harder than 50 m/s.
     */
    setWind(wind: Vector3 | Partial<Vector3>): boolean;

    /**
     * Traces a segment through the world and reports the first thing it meets.
     * 
     * The server has no world of its own, so this asks that player's client and waits for it to answer. Three things follow: the answer is a round trip late; it covers only what that machine has streamed in, so a point far from the player reads as empty world; and it is a client's word, which is fine for a prompt and is not fine for anything a player gains by lying about. The promise always settles -- on the answer, on a five-second timeout, or when the player leaves -- and `answered` says which.
     * @param player Whose client is asked. Their machine is the one that answers, so pick a player near the point in question.
     * @param from Where the ray starts, in world-space metres.
     * @param to Where it ends. Up to 4096 metres away; a ray of no length is refused.
     * @param options `mode` picks which of the game's own three traces to run: `cover` asks whether the world is in the way -- static geometry, props and doors block, and bodies cannot, which is what a line of sight wants; `anything` asks what is under the ray, bodies included, which is what a pick wants; `ground` sees only surfaces a player could stand on. It defaults to `cover`, and a name that is none of the three is rejected rather than guessed at.
     * @returns The trace, once that client has run it.
     */
    raycast(player: Player, from: Vector3, to: Vector3, options?: { mode?: "cover" | "anything" | "ground" }): Promise<WorldTraceResult>;

    /**
     * Traces a segment and reports everything solid along it, nearest first.
     * 
     * The server has no world of its own, so this asks that player's client and waits for it to answer. Three things follow: the answer is a round trip late; it covers only what that machine has streamed in, so a point far from the player reads as empty world; and it is a client's word, which is fine for a prompt and is not fine for anything a player gains by lying about. The promise always settles -- on the answer, on a five-second timeout, or when the player leaves -- and `answered` says which.
     * @param player Whose client is asked. Their machine is the one that answers, so pick a player near the point in question.
     * @param from Where the ray starts, in world-space metres.
     * @param to Where it ends. Up to 4096 metres away; a ray of no length is refused.
     * @param options `mode` is as `World.raycast` documents it. `maxHits` is how many things along the ray to report, up to 8 and 8 by default; each is a separate solid hit, found by re-tracing past the one before, so a window does not hide the wall it is set in.
     * @returns The trace, once that client has run it.
     */
    raycastAll(player: Player, from: Vector3, to: Vector3, options?: { mode?: "cover" | "anything" | "ground"; maxHits?: number }): Promise<WorldTraceResult>;

    /**
     * What is underfoot at a point: its height in `hit.position.z`, the slope it landed on, and what the surface is made of.
     * 
     * A trace rather than a terrain height, so it stands on whatever is actually there -- a bridge, a floor, a castle roof -- and not on the heightmap underneath it. There is no bare-number form here, because a promise has to be able to say that nothing answered.
     * 
     * The server has no world of its own, so this asks that player's client and waits for it to answer. Three things follow: the answer is a round trip late; it covers only what that machine has streamed in, so a point far from the player reads as empty world; and it is a client's word, which is fine for a prompt and is not fine for anything a player gains by lying about. The promise always settles -- on the answer, on a five-second timeout, or when the player leaves -- and `answered` says which.
     * @param player Whose client is asked. Their machine is the one that answers, so pick a player near the point in question.
     * @param position The point to look under, in world-space metres. Its own z is where the probe is centred.
     * @param options How far above the point the probe starts and how far below it reaches, in metres. 5 and 200 by default -- the 5 above is what lets a point already slightly underground still resolve -- and up to 512 each.
     * @returns The probe, once that client has run it.
     */
    resolveGround(player: Player, position: Vector3, options?: { up?: number; down?: number }): Promise<WorldTraceResult>;

    /**
     * The server's own replicated entities near a point, nearest first -- players, horses, dogs, props, dropped items, doors, gates and stashes, each as its own handle.
     * 
     * This needs no client: the server already knows where its replicas are, so unlike `entitiesInRadius` it is immediate and authoritative. It sees only what the server replicates, which is the other half -- the level's own entities are what `entitiesInRadius` is for.
     * @param position World-space point to measure from.
     * @param radius How far to look, in metres.
     * @param virtualWorld Optional virtual world to look in; omitted looks in the global one, where every body starts.
     * @returns The handles, nearest first.
     */
    replicasInRadius(position: Vector3, radius: number, virtualWorld?: number): Entity[];

    /**
     * Every entity that client's engine has inside a sphere, nearest first.
     * 
     * It reads the engine's own spatial grid, so it costs what the sphere covers rather than what the level holds. This reports the level's entities -- doors, props, bodies -- and not the server's replicas; correlate them by `guid`.
     * 
     * The server has no world of its own, so this asks that player's client and waits for it to answer. Three things follow: the answer is a round trip late; it covers only what that machine has streamed in, so a point far from the player reads as empty world; and it is a client's word, which is fine for a prompt and is not fine for anything a player gains by lying about. The promise always settles -- on the answer, on a five-second timeout, or when the player leaves -- and `answered` says which.
     * @param player Whose client is asked. Their machine is the one that answers, so pick a player near the point in question.
     * @param centre The centre of the sphere, in world-space metres.
     * @param radius Its radius in metres, up to 256.
     * @param options `class` reports only entities of that engine class -- `AnimDoor`, `NPC_NAI`, `GeomEntity` -- and a class the engine does not know matches nothing rather than everything; omitted, every class is reported. `max` is how many to report, nearest first, up to 64. `physicalOnly` skips entities the engine has built no physics for, which is the filter the game's own proximity query applies, and is off by default.
     * @returns The entities, once that client has looked.
     */
    entitiesInRadius(player: Player, centre: Vector3, radius: number, options?: { class?: string; max?: number; physicalOnly?: boolean }): Promise<WorldRadiusResult>;
  };

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
     * The object the level placed that was hit, named the way `LevelEdit.apply` finds it again on every machine, with where the level put it and how it turned and sized it. A tree, a rock or a wall is one; so is a door or any other entity the level gives a GUID. Null for terrain, for static geometry the engine records no source for, and for anything the session spawned -- props, players, NPCs. Spread it into a definition to take the object out or swap it: `LevelEdit.apply({ ...hit.levelObject, model: stump })`.
     */
    levelObject: WorldLevelObject | null;
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
  }

  /**
   * What a trace came back with, and whether it came back at all.
   */
  interface WorldTraceResult {
    /**
     * Whether the client ran the trace. False when it has no world loaded, is in another level, did not answer in time, or left.
     */
    answered: boolean;

    /**
     * Why it did not answer. Empty when it did.
     */
    reason: string;

    /**
     * The nearest thing the ray met, or null when it met nothing and whenever `answered` is false.
     */
    hit: WorldRayHit | null;

    /**
     * Everything it met, nearest first. One entry at most unless `maxHits` asked for more; empty whenever `answered` is false.
     */
    hits: WorldRayHit[];
  }

  /**
   * What a sphere came back with, and whether it came back at all.
   */
  interface WorldRadiusResult {
    /**
     * Whether the client ran the query. False when it has no world loaded, is in another level, did not answer in time, or left.
     */
    answered: boolean;

    /**
     * Why it did not answer. Empty when it did.
     */
    reason: string;

    /**
     * The entities inside the sphere, nearest first. Empty whenever `answered` is false.
     */
    entities: WorldNearbyEntity[];
  }

  /**
   * A walk across the mesh.
   */
  interface NavigationPath {
    /**
     * The corners to walk through, first the point on the mesh nearest `from`. Straight lines between them stay on the mesh.
     */
    points: Vector3[];

    /**
     * Whether it reaches `to`. False when `to` cannot be reached, and the path then ends at the nearest point the mesh allows.
     */
    complete: boolean;

    /**
     * Its length in metres.
     */
    length: number;
  }

  /**
   * How far a straight walk across the mesh gets.
   */
  interface NavigationRay {
    /**
     * Whether the edge of the mesh -- a wall, a drop, a locked door -- stops it before `to`.
     */
    hit: boolean;

    /**
     * Where it stops: the edge, or the point on the mesh under `to`.
     */
    point: Vector3;
  }

  /**
   * Queries the level's navigation mesh on the server. The mesh includes walkable floors, bridges and stairs.
   * 
   * Copy the game's `Data/Levels/<level>/recast.pak` to the server's `files/<level>/` directory. The server searches `files/` recursively at startup. An optional `mod.navmesh` in `server.json` takes priority and can name a game install, level folder or pak file. The game data must be supplied separately. Without a mesh, `ready` is false and geometry queries return null or false. Points are world-space metres, Z up. Every query takes an optional options object. `searchRadius` and `searchHeight` limit how far a point may be from the mesh, across the ground and up or down: 2 metres by default, up to 64, and 4 by default, up to 128. Keep `searchHeight` under a storey's height, or a point on one floor snaps to the floor above. `doors` picks which doors a query walks through: `unlocked`, the default, is how an NPC treats a door -- open or shut, it goes through unless the door is locked; `open` only through doors that stand open; `any` ignores locks; `none` treats every doorway as a wall. Door states are read in the global world, except validatePatrol with an actor, which uses that actor's world. A field given with the wrong type or out of range throws.
   */
  const Navigation: {
    /**
     * Whether a mesh loaded from `mod.navmesh` or the server's `files/` directory. False when no usable copy is found; the server log gives the reason.
     */
    readonly ready: boolean;

    /**
     * Available overlay names, sorted. Overlays contain replacement navigation data for quest states. Enabling one replaces the covered part of the mesh. Empty without a mesh.
     */
    readonly overlays: string[];

    /**
     * Enabled overlays in activation order. Initially empty; the level starts with its base mesh.
     */
    readonly activeOverlays: string[];

    /**
     * Checks all legs, including the closing leg of a loop and reverse legs of ping-pong. Uses the patrol runner's search budget. A single waypoint is checked against itself. Missing mesh reports available false; partial paths are incomplete. This does not start or change an actor's order.
     * @param route A registered route or its id.
     * @param options Optional actor chooses door policy in its virtual world: unlocked doors for humanoids, no doorways for horses and animals.
     */
    validatePatrol(route: PatrolRoute | string, options?: { actor?: Npc | Horse }): { available: boolean; complete: boolean; legs: { fromIndex: number; toIndex: number; complete: boolean; path: NavigationPath | null }[] };

    /**
     * Snaps a point to the nearest walkable surface.
     * @param point The point to snap; omitted components default to zero.
     * @param options How far to look, and which doors count.
     * @returns The nearest point within the search limits, or null when none is found or no mesh is loaded.
     */
    closestPoint(point: Vector3 | Partial<Vector3>, options?: { searchRadius?: number; searchHeight?: number; doors?: "unlocked" | "open" | "any" | "none" }): Vector3 | null;

    /**
     * Returns the height of the nearest walkable surface, including upper floors, bridges and stairs.
     * @param point The point to measure; omitted components default to zero. Its z estimates the floor height and selects the storey.
     * @param options How far to look.
     * @returns The height in metres, or null when none is found or no mesh is loaded.
     */
    floorAt(point: Vector3 | Partial<Vector3>, options?: { searchRadius?: number; searchHeight?: number; doors?: "unlocked" | "open" | "any" | "none" }): number | null;

    /**
     * Plans a path around obstacles on the mesh. Plan when a destination changes; avoid recalculating every NPC's path each tick.
     * @param from Where the walk starts; omitted components default to zero.
     * @param to Where it should end; omitted components default to zero.
     * @param options How far to look for each end, and which doors to walk through.
     * @returns The path, or null when an endpoint cannot be found, the query fails or no mesh is loaded. A partial path has `complete: false`.
     */
    findPath(from: Vector3 | Partial<Vector3>, to: Vector3 | Partial<Vector3>, options?: { searchRadius?: number; searchHeight?: number; doors?: "unlocked" | "open" | "any" | "none" }): NavigationPath | null;

    /**
     * Checks reachability by calling `findPath`.
     * @param from Where the walk starts; omitted components default to zero.
     * @param to Where it should end; omitted components default to zero.
     * @param options How far to look for each end, and which doors to walk through.
     * @returns True when a complete path exists; false otherwise, including when no mesh is loaded.
     */
    canReach(from: Vector3 | Partial<Vector3>, to: Vector3 | Partial<Vector3>, options?: { searchRadius?: number; searchHeight?: number; doors?: "unlocked" | "open" | "any" | "none" }): boolean;

    /**
     * Checks a straight line across the mesh and reports where it stops.
     * @param from Where the walk starts; omitted components default to zero.
     * @param to Where it heads; only its x and y are used, the walk follows the surface.
     * @param options How far to look for `from`, and which doors let the walk through.
     * @returns The result, or null when `from` is off the mesh, the query fails or no mesh is loaded.
     */
    raycast(from: Vector3 | Partial<Vector3>, to: Vector3 | Partial<Vector3>, options?: { searchRadius?: number; searchHeight?: number; doors?: "unlocked" | "open" | "any" | "none" }): NavigationRay | null;

    /**
     * Returns a random walkable point reachable from `center` and no further from it across the ground than `radius`. A point drawn outside the circle is drawn again, a bounded number of times. Without a centre, samples the whole level, choosing each tile with equal probability.
     * @param center The point to scatter around; it has to be on the mesh, and omitted components default to zero. Omitted entirely, the point can be anywhere on the level's mesh; pass `undefined` for it and for `radius` to give options.
     * @param radius How far from `center`, in metres, up to 512. Required with a centre.
     * @param options How far to look for `center`, and which doors the point may lie behind.
     * @returns The point, or null when `center` is off the mesh, no draw lands within `radius`, the query fails or no mesh is loaded.
     */
    randomPoint(center?: Vector3 | Partial<Vector3>, radius?: number, options?: { searchRadius?: number; searchHeight?: number; doors?: "unlocked" | "open" | "any" | "none" }): Vector3 | null;

    /**
     * Enables an overlay. Where overlays overlap, the last enabled takes precedence. Subsequent queries use the updated mesh.
     * @param name An entry of `overlays`.
     * @returns False for a name the level does not ship, one already enabled, one whose tiles cannot be read or loaded (the server log says why), or without a mesh.
     */
    enableOverlay(name: string): boolean;

    /**
     * Disables an overlay, restoring the previous enabled overlay or the base mesh in that area.
     * @param name An entry of `activeOverlays`.
     * @returns False for a name that is not enabled, or when part of the area could not be restored (the server log says why); the overlay is disabled either way.
     */
    disableOverlay(name: string): boolean;
  };

  /**
   * Immutable custom effect, with an existing native buff supplying its icon.
   */
  interface CustomBuffDefinition {
    /**
     * Display name; 1 to 256 UTF-8 bytes.
     */
    name: string;

    /**
     * Optional description, up to 4096 UTF-8 bytes.
     */
    description?: string;

    /**
     * Existing game buff name or GUID; only presentation is borrowed.
     */
    visual: string;

    /**
     * Positive real seconds up to seven days, or -1 for permanent; default -1.
     */
    duration?: number;

    /**
     * Server lifecycle callback interval in seconds; zero disables, otherwise 0.1 to 86400. Default zero.
     */
    tickInterval?: number;

    /**
     * Default false. When true, each application has its own native timer and modifiers.
     */
    stackable?: boolean;

    /**
     * Integer 1 to 16; default 1.
     */
    maxStacks?: number;

    /**
     * Default true. At the stack limit, replaces the oldest instance with a full-duration instance; false ignores applications at the limit.
     */
    refresh?: boolean;

    /**
     * Optional native modifiers. A number adds points. Buffs.modifiers lists supported names. healthRegeneration adds health per second. Effective stat changes do not change earned XP.
     */
    modifiers?: Record<string, number | { operation: "add" | "multiply" | "set"; value: number }>;
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
   * The game's status-effect table, and which of it this server decides.
   */
  const Buffs: {
    /**
     * Supported custom modifier names: progression tracks, charisma, maxStamina (stamina alias), maxHealth, carryCapacity, runSpeed, armorRating, bodyArmor, hungerDrain, energyDrain, staminaRegeneration and healthRegeneration. Native query bounds and contexts still apply.
     */
    readonly modifiers: string[];

    /**
     * The kinds of effect a server can take over, which is what `claim` accepts: the ones carrying state nobody can work out for themselves -- potions, poison, injury, drunkenness, illness, unconsciousness. The game has other kinds, and they read back in `BuffInfo.class`, but they either shadow something that already replicates or are the game's own plumbing, so there is no decision in them to take.
     */
    readonly classes: string[];

    /**
     * The families the game groups effects into, which is what `player.clearBuffs` takes: `poison`, `bleed`, `alcohol_drunk`, `unconscious` and nineteen more. These are the game's own words, and its own AI reasons in them.
     */
    readonly tags: string[];

    /**
     * Registers a custom native effect and publishes it to connected and future clients. An identical registration is idempotent; an existing id with another definition throws. Returns a GUID accepted by the existing player buff methods, like Items.register.
     * @param id Unique resource-qualified identifier, such as my_resource:blessing.
     * @param definition Immutable definition; unknown keys, unsupported modifiers and invalid values throw.
     * @returns Custom buff GUID.
     */
    register(id: string, definition: CustomBuffDefinition): string;

    /**
     * Looks a status effect up, so a resource can check a name is real before handing it to a player and can see what the effect is before applying it.
     * @param query An effect's name, as the game's buff tables spell it, or its GUID.
     * @returns What the tables say about it, or null for a name they do not carry.
     */
    find(query: string): BuffInfo | null;

    /**
     * Takes over whole kinds of effect. Every client then turns down an effect of that kind the game tried to give its own player, and raises `playerBuffBlocked` instead -- which is where the resource decides what really happens, usually `player.addBuff` with its own rule applied. A claim nothing listens to does not move the gameplay here, it removes it.
     * 
     * Two things to plan around. The refusal covers the game's follow-on effects too: claim `alcohol` and the six `alcoholism_level*` steps stop arriving along with the drink, so the resource owns the whole progression. And an effect has no strength -- the handler can block it, swap it for another, or let it through, but it cannot make the drink half as strong.
     * 
     * Replaces the previous claim rather than adding to it, and reaches every connected client at once.
     * @param classNames The kinds of effect to decide, from `Buffs.classes`. `[]` hands them all back. Throws for anything else.
     * @returns The kinds now claimed.
     */
    claim(classNames: string[]): string[];
  };

  /**
   * The game's skills, stats and perks, and the rules this server puts on how players move along them.
   */
  const Progression: {
    /**
     * Whether players' own games may earn XP. See `setNativeXp`.
     */
    readonly nativeXp: boolean;

    /**
     * The perks this server blocks.
     */
    readonly blockedPerks: string[];

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

    /**
     * Switches off XP from play. Every gain the game produces is dropped on the player's own client, and only `player.addXp` and `player.setLevel` move a track -- for servers where progression comes from jobs, trainers or quests a resource runs.
     * @param enabled False to stop players' own games from earning XP at all.
     * @returns Nothing.
     */
    setNativeXp(enabled: boolean): void;

    /**
     * Scales the XP players earn by playing. Applied before the player's own perk multipliers, which still work as they do in single player. `player.addXp` is not scaled.
     * @param track A track name, or `*` for every track.
     * @param rate What a gain is multiplied by before it is granted: 1 is the game's own pace, 0.5 half, 0 none at all.
     * @returns Nothing.
     */
    setXpRate(track: string, rate: number): void;

    /**
     * Limits how fast a track can be ground. The budget holds a minute's worth and refills steadily, so a burst after a quiet spell goes through and a loop -- two players sparring for sword XP, a lock picked over and over -- runs dry. `player.addXp` is not limited.
     * @param track A track name, or `*` for every track.
     * @param xpPerMinute How much XP a player may earn on the track per minute, before multipliers. 0 is unlimited.
     * @returns Nothing.
     */
    setXpBudget(track: string, xpPerMinute: number): void;

    /**
     * Stops a track at a level. Gains close to it are granted exactly what is left, so no multiplier carries the track past it. `player.setLevel` and `player.addXp` can still go beyond: the cap rules over what is earned.
     * @param track A track name, or `*` for every track.
     * @param level The highest level XP from play may reach, at most the game's own 30.
     * @returns Nothing.
     */
    setLevelCap(track: string, level: number): void;

    /**
     * Takes perks out of the game: every player who owns one gives it up, the perk screen shows it as blocked and refuses to learn it, the game's own grants of it are turned down or taken back, and `player.restoreProgression` leaves it out. A point spent on it is not given back; `player.respecPerks` returns every point. `player.addPerk` still grants one, as the script's own decision.
     * @param perks Perk names or GUIDs.
     * @returns Nothing. Throws for an unknown perk.
     */
    blockPerks(perks: string[]): void;

    /**
     * Lifts `blockPerks`.
     * @param perks Perk names or GUIDs.
     * @returns Nothing. Throws for an unknown perk.
     */
    unblockPerks(perks: string[]): void;
  };

  /**
   * The game's own character-component catalog: every face, hairstyle, beard and skin a player's body can be given.
   * 
   * Everyone on a fresh server is Henry, because a body with nothing chosen is the one the game hands out. This is where the alternatives come from; `player.setAppearance` is what puts one on somebody.
   */
  const Appearances: {
    /**
     * Every option the game ships for one part. The male tree has 212 faces, 262 hairstyles, 51 beards and 47 skins; the female tree has 61, 71, none and 41.
     * 
     * The hairstyle count is large because the game ships each style once per colour rather than colouring one. Group by `option.group` to get the styles back.
     * 
     * The 51 beards are the whole catalog, not what any one body can wear: a beard is modelled per face and only exists for the faces it was modelled for. `Appearances.beards` is the list to show somebody.
     * @param part Which part to list.
     * @param gender Which half of the catalog to read; male by default.
     * @returns The options, in the catalog's own order.
     */
    options(part: "body" | "head" | "hair" | "beard", gender?: "male" | "female"): AppearanceOption[];

    /**
     * The beards that face can actually grow.
     * 
     * A beard is modelled against one head's mesh and stored under that head's own folder, so asking for one the artists never made for the face wearing it does nothing at all -- no error, no beard, a body that comes back clean-shaven. The owning client refuses such a pair outright, so filter a chooser through this rather than through `options("beard")`.
     * 
     * The 164 generic faces carry 24 each, Henry's face carries the 15 the game's own barber offers, and 14 faces carry none.
     * @param head The face to ask about -- a name from `Appearances.options("head")`. Omitted or empty means the face a body nobody has chosen one for wears.
     * @param gender Which half of the catalog the head came from; male by default.
     * @returns The wearable beards, in the catalog's own order. Empty for a female body, for a face with none, and for a name the catalog does not carry.
     */
    beards(head?: string, gender?: "male" | "female"): AppearanceOption[];

    /**
     * Looks one component name up, so a resource can check a name is real before handing it to a player.
     * @param part Which part the name is for.
     * @param name The component name to look up.
     * @param gender Which half of the catalog to look in; male by default.
     * @returns What the catalog says about it, or null for a name it does not carry.
     */
    find(part: "body" | "head" | "hair" | "beard", name: string, gender?: "male" | "female"): AppearanceOption | null;

    /**
     * Picks one option for every part, which is the cheapest way to stop a server full of Henrys. Hand the result straight to `player.setAppearance`.
     * 
     * Uniform over the catalog rather than over what looks good together: the game's own faces and hairstyles are authored for particular NPCs, so a random body is recognisably random. The beard is drawn after the face and only from what that face can wear, so the result is always one a client will accept.
     * @param gender Which half of the catalog to draw from; male by default.
     * @returns A complete appearance. `beard` is empty for a female one, which the catalog has none of, and for the fourteen male faces no beard was modelled for.
     */
    random(gender?: "male" | "female"): Appearance;
  };

  /**
   * One animation the game plays on a human body: a Mannequin fragment and the tags that pick its variant, as a row of the game's own `actor_anim_action` table.
   */
  interface AnimationInfo {
    /**
     * The fragment's name, which is what `playAnimation` takes.
     */
    fragment: string;

    /**
     * The tags that select this variant, `+`-separated, which is what `playAnimation`'s `tags` option takes. Empty for the fragment's default.
     */
    tags: string;

    /**
     * How long one pass lasts, in seconds.
     */
    duration: number;

    /**
     * Whether it ends by itself. One that does not is a loop and plays until something replaces it, `loop` or not.
     */
    oneShot: boolean;

    /**
     * Whether it takes the whole body. A full-body animation stops the legs, so a player playing one is held still unless `lockMovement` says otherwise.
     */
    fullBody: boolean;

    /**
     * What the right hand is animated holding, as the game names the kind (`bucket`, `axe_tool`), or empty for nothing. Match it against `AnimationPropInfo.hand` to find a prop that fits.
     */
    rightHand: string;

    /**
     * The same for the left hand.
     */
    leftHand: string;

    /**
     * Whether the game has it for a man's body.
     */
    male: boolean;

    /**
     * Whether the game has it for a woman's body.
     */
    female: boolean;

    /**
     * The in/loop/out sequence this fragment is a part of, shared by every fragment in it (`BakerMill` for `BakerMillIn`, `BakerMillLoop` and `BakerMillOut`), or empty when it is not part of one. The game does not record sequences; this is read from the fragment names, so treat it as a strong hint rather than a guarantee.
     */
    sequence: string;

    /**
     * Which part of its `sequence` this is: the way in, the middle part the body stays in, the way out, or a variation played in place of the middle. `loop` is the middle whether or not it ends by itself -- `oneShot` says that, and `playAnimation`'s `loop` repeats it. Empty when `sequence` is.
     */
    phase: "in" | "loop" | "out" | "variation" | "";

    /**
     * The fragment a second actor plays against this one, for the game's two-person animations (`TiedUpOut_Master` for `TiedUpOut_Slave`), or empty. Each actor plays its own fragment; read from the fragment names, like `sequence`.
     */
    partner: string;

    /**
     * Whether, with these tags, the fragment also animates a second character the game binds to the body -- a corpse carried, a horse groomed, a smith's workpiece. `playAnimation` binds none, so only this body moves.
     */
    slaveBody: boolean;
  }

  /**
   * A model the game puts in a body's hand: one of its tools, from the item tables.
   */
  interface AnimationPropInfo {
    /**
     * The item's name, which is what a prop's `item` takes.
     */
    name: string;

    /**
     * The model it is drawn with.
     */
    model: string;

    /**
     * The kind of thing it is (`bucket`, `axe_tool`), which is what `AnimationInfo.rightHand` and `leftHand` name.
     */
    hand: string;
  }

  /**
   * Every animation the game plays on a human body, and the props it gives one to hold. `player.playAnimation` and `npc.playAnimation` play them.
   */
  const Animations: {
    /**
     * Lists the animations the game plays on people -- over six thousand, from waves and bows to chopping wood, drinking and every tavern pose. These are the game's own combinations, so each one is known to play; `playAnimation` also takes fragments and tags this does not list, and plays whatever the body's animation database makes of them.
     * @param search Only rows whose fragment or tags contain this, ignoring case. Everything when left out.
     * @returns One row per fragment and tag combination, sorted by fragment.
     */
    list(search?: string): AnimationInfo[];

    /**
     * Lists the tools and carried objects the game puts in people's hands, by item name. Pick one whose `hand` matches the animation's `rightHand` or `leftHand` and it sits in the hand the way the animation was made for.
     * @returns Every prop, sorted by name.
     */
    props(): AnimationPropInfo[];
  };

  /**
   * Node.js timer handle returned by setTimeout and setInterval.
   */
  interface Timeout {
    /**
     * Keeps the event loop alive while this timer is pending.
     * @returns This timer.
     */
    ref(): Timeout;

    /**
     * Lets the event loop exit while this timer is still pending.
     * @returns This timer.
     */
    unref(): Timeout;

    /**
     * Whether this timer keeps the event loop alive.
     */
    hasRef(): boolean;

    /**
     * Restarts this timer's countdown from now, with its original delay.
     * @returns This timer.
     */
    refresh(): Timeout;
  }

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
     * Broadcasts a named event to every connected client's shared Events handlers.
     * @param eventName Client shared-event name.
     * @param payload Optional string payload sent verbatim; other values are JSON-serialized.
     */
    emitAllClients(eventName: string, payload?: unknown): void;

    /**
     * Registers a persistent server handler for events originating from clients; this namespace is isolated from native events.
     * @param eventName Case-sensitive event name.
     * @param handler Resource-owned callback invoked with the emitted arguments.
     * @returns Function that removes this exact subscription.
     */
    onClient(eventName: string, handler: EventHandler): Unsubscribe;

    /**
     * Registers a one-shot server handler for a client-originated event.
     * @param eventName Case-sensitive event name.
     * @param handler Resource-owned callback invoked with the emitted arguments.
     */
    onceClient(eventName: string, handler: EventHandler): void;

    /**
     * Removes a matching client-originated event handler owned by the calling resource.
     * @param eventName Case-sensitive event name.
     * @param handler Resource-owned callback invoked with the emitted arguments.
     */
    offClient(eventName: string, handler: EventHandler): void;
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
   * Server-side delivery of structured chat messages to connected clients.
   */
  const Chat: {
    /**
     * Broadcasts a structured chat message to all clients.
     * @param text Message body broadcast to every connected client.
     * @param options Optional author label and color: a Color or a packed 0xRRGGBBAA integer.
     */
    sendToAll(text: string, options?: { author?: string; color?: number | Color }): void;

    /**
     * Sends a structured chat message to one player's owning connection.
     * @param player Player-owned entity identifying the destination connection.
     * @param text Message body sent to the player.
     * @param options Optional author label and color: a Color or a packed 0xRRGGBBAA integer.
     */
    sendToPlayer(player: Entity, text: string, options?: { author?: string; color?: number | Color }): void;

    /**
     * Enables or disables the built-in player-to-player chat relay.
     * @param enabled True to relay chat automatically; false to answer playerChat yourself.
     */
    setDefaultRelay(enabled: boolean): void;

    /**
     * Checks whether the built-in chat relay is enabled.
     * @returns True when chat is relayed automatically.
     */
    isDefaultRelay(): boolean;
  };

  /**
   * Server-side proximity voice rules: how far voice carries, and who may talk to or hear whom.
   */
  const Voice: {
    /**
     * Sets how far voice carries for talkers with no override of their own. Connected clients are told, so their playback fades out at the same distance.
     * @param range Audibility radius in world units; values <= 0 restore the default of 25.
     */
    setRange(range: number): void;

    /**
     * Reads the server-wide proximity range.
     * @returns Radius in world units.
     */
    getRange(): number;

    /**
     * Overrides how far one player's voice carries, whichever voice tier they chose.
     * @param player Player whose voice carries the given distance.
     * @param range Audibility radius in world units; values <= 0 return them to the radius of their voice tier.
     */
    setPlayerRange(player: Entity, range: number): void;

    /**
     * Reads how far a player's voice carries, with the server-wide default already resolved.
     * @param player Player to query.
     * @returns Radius in world units.
     */
    getPlayerRange(player: Entity): number;

    /**
     * Sets how far a voice tier carries. Players switch tier themselves; whisper starts at 3, shout at 25, and normal carries the server-wide range. Set a tier to 0 to make it carry the server-wide range, which takes it away. Connected clients are told.
     * @param tier Voice tier: 0 whisper, 1 normal, 2 shout.
     * @param range Audibility radius in world units; values <= 0 make the tier carry the server-wide range.
     */
    setTierRange(tier: number, range: number): void;

    /**
     * Reads how far a voice tier carries, with the server-wide range already resolved.
     * @param tier Voice tier: 0 whisper, 1 normal, 2 shout.
     * @returns Radius in world units.
     */
    getTierRange(tier: number): number;

    /**
     * Puts a player on a voice tier, as if they had switched to it. Their own indicator follows, and the playerVoiceTierChange event is not raised.
     * @param player Player to move.
     * @param tier Voice tier: 0 whisper, 1 normal, 2 shout.
     */
    setPlayerTier(player: Entity, tier: number): void;

    /**
     * Reads the voice tier a player is on. A player switching tier raises the playerVoiceTierChange event with the player and the new tier.
     * @param player Player to query.
     * @returns 0 whisper, 1 normal, 2 shout.
     */
    getPlayerTier(player: Entity): number;

    /**
     * Server-wide mute: a muted player's voice reaches nobody.
     * @param player Player to mute or unmute.
     * @param muted True to stop their voice reaching anyone.
     */
    setPlayerMuted(player: Entity, muted: boolean): void;

    /**
     * Checks the server-wide mute flag.
     * @param player Player to query.
     * @returns True when the player's voice reaches nobody.
     */
    isPlayerMuted(player: Entity): boolean;

    /**
     * Server-wide deafen: a deaf player receives nobody.
     * @param player Player to deafen or undeafen.
     * @param deaf True to stop them receiving anyone's voice.
     */
    setPlayerDeaf(player: Entity, deaf: boolean): void;

    /**
     * Checks the server-wide deafen flag.
     * @param player Player to query.
     * @returns True when the player receives nobody's voice.
     */
    isPlayerDeaf(player: Entity): boolean;

    /**
     * One-way mute between two players, enforced by the server rather than the client.
     * @param listener Player who stops hearing the target.
     * @param target Player the listener stops hearing.
     * @param muted True to mute, false to restore.
     */
    setLocalMute(listener: Entity, target: Entity, muted: boolean): void;

    /**
     * Checks a one-way mute.
     * @param listener Player doing the muting.
     * @param target Player being muted.
     * @returns True when the listener does not receive the target.
     */
    isLocallyMuted(listener: Entity, target: Entity): boolean;

    /**
     * Checks whether a player left voice chat enabled in their own client settings. A preference, not a permission: use setPlayerMuted or setPlayerDeaf to enforce anything.
     * @param player Player to query.
     * @returns True unless the player turned voice chat off.
     */
    isPlayerVoiceEnabled(player: Entity): boolean;

    /**
     * Checks whether a player is speaking right now. Tracks speech rather than the push-to-talk key: silence is dropped before it reaches the server, and the state clears shortly after the last frame. The same signal raises the playerVoiceStart and playerVoiceStop events.
     * @param player Player to query.
     * @returns True while the player's voice is reaching the server.
     */
    isPlayerTalking(player: Entity): boolean;
  };

  /**
   * A player asking to join, handed to `playerConnecting`. Every identifier is reported by the player's own client and is not verified by the server.
   */
  interface PendingConnection {
    /**
     * The name the player asked to join under.
     */
    readonly nickname: string;

    /**
     * Steam identifier the client reported, or an empty string when it had none.
     */
    readonly steamId: string;

    /**
     * Epic Games account identifier the client reported, or an empty string when it had none.
     */
    readonly epicId: string;

    /**
     * Discord identifier the client reported, or an empty string when it had none.
     */
    readonly discordId: string;

    /**
     * Framework hardware identifier the client reported, or an empty string when it had none.
     */
    readonly hardwareId: string;

    /**
     * The string the client was launched with, as the `ticket` of its launch link, or an empty string. The framework passes it through untouched; checking it -- against a one-time join ticket your own launcher or website issued, typically -- is the script's job.
     */
    readonly ticket: string;

    /**
     * The remote address the connection comes from, without its port.
     */
    readonly ip: string;

    /**
     * Turns the player away before they receive anything. Takes effect on the next server tick, whatever other handlers are still doing. Does nothing once the connection is decided or the player has left.
     * @param reason Shown to the player as written, up to 512 bytes. Omitted, they read that the server refused the connection.
     */
    reject(reason?: string): void;

    /**
     * Shows the waiting player a line of status and restarts the admission timeout, so a queue that keeps updating its players is never timed out. Does nothing once the connection is decided or the player has left.
     * @param message One line for the player's connecting screen, up to 256 bytes: 'Checking the whitelist', 'You are 4th in the queue'.
     */
    update(message: string): void;

    /**
     * Checks whether this connection is still waiting on a decision.
     * @returns False once it was let in or turned away, or the player gave up and left; a queue drops such entries.
     */
    isPending(): boolean;
  }

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

    /**
     * Moves this entity into another virtual world.
     * @param world Virtual-world identifier used to partition replication and visibility.
     */
    setVirtualWorld(world: number): void;

    /**
     * Restricts replication of this entity to one owning connection while preserving normal range and visibility checks.
     * @param player Player-owned entity whose connection should exclusively receive this entity, or null to clear the restriction.
     */
    setVisibleTo(player: Entity | null): void;
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

    /**
     * Writes one key of this entity's state and replicates it to whoever the scope names. Throws when a limit is reached.
     * @param key Key to write; at most 64 UTF-8 bytes.
     * @param value Value to store. Booleans, numbers and strings travel as themselves; anything else is serialized as JSON. At most 4096 bytes.
     * @param options Who the key reaches: every client that can see the entity (the default), only its owning client, or nobody -- server-side storage that never goes on the wire.
     * @returns True when the value changed, false when it was already stored.
     */
    set(key: string, value: any, options?: { scope?: 'broadcast' | 'owner' | 'server' }): boolean;

    /**
     * Removes one key from this entity's state, telling every client that held it.
     * @param key Key to drop.
     * @returns True when the key was set and has been removed.
     */
    remove(key: string): boolean;
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
     * Authenticated Steam identifier, or an empty string when unavailable.
     */
    readonly steamId: string;

    /**
     * Epic Games account identifier the client reported, not verified by the server, or an empty string when unavailable.
     */
    readonly epicId: string;

    /**
     * Authenticated Discord identifier, or an empty string when unavailable.
     */
    readonly discordId: string;

    /**
     * Framework hardware identifier, or an empty string when unavailable.
     */
    readonly hardwareId: string;

    /**
     * Current round-trip latency in milliseconds, or -1 when unavailable.
     */
    readonly ping: number;

    /**
     * Current remote network address, or an empty string when unavailable.
     */
    readonly ip: string;

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

    /**
     * Disconnects this player from the server.
     * @param reason Optional reason shown to the disconnected player; omitting it uses the generic kicked reason.
     */
    kick(reason?: string): void;

    /**
     * Emits a named script event to this player's client connection.
     * @param eventName Client event name.
     * @param payloadJson Optional JSON payload forwarded verbatim to the owning client.
     */
    emit(eventName: string, payloadJson?: string): void;

    /**
     * Returns this player's current remote network address.
     * @returns Address string, or an empty string when the player or peer is unavailable.
     */
    getIP(): string;

    /**
     * Shows or hides the name over this player's head for every other player. The health bar has its own switch, and each player can still hide all nametags locally.
     * @param visible True to show this player's nametag to everyone, false to hide it.
     */
    setNametagVisible(visible: boolean): void;

    /**
     * Shows or hides the health bar under this player's nametag, leaving the name itself alone.
     * @param visible True to show the health bar under this player's name, false to hide it.
     */
    setNametagHealthVisible(visible: boolean): void;

    /**
     * Overrides the text drawn on this player's nametag.
     * @param text Text to show instead of the player's name, cut to 64 bytes; empty or omitted restores the name.
     */
    setNametagText(text?: string): void;

    /**
     * Tints the text on this player's nametag.
     * @param color Packed 0xAARRGGBB color.
     */
    setNametagColor(color: number): void;
  }

  interface BasePlayer extends Entity {}

  /**
   * Calls a function once after a delay.
   * @param handler Function to call.
   * @param milliseconds Delay before the call.
   * @param args Arguments passed to the handler.
   * @returns Handle to pass to the matching clear function.
   */
  function setTimeout(handler: (...args: any[]) => void, milliseconds?: number, ...args: unknown[]): Timeout;

  /**
   * Calls a function repeatedly, waiting the delay between calls.
   * @param handler Function to call.
   * @param milliseconds Delay before the call.
   * @param args Arguments passed to the handler.
   * @returns Handle to pass to the matching clear function.
   */
  function setInterval(handler: (...args: any[]) => void, milliseconds?: number, ...args: unknown[]): Timeout;

  /**
   * Cancels a pending setTimeout.
   * @param handle Handle returned when the timer was scheduled; anything else is ignored.
   */
  function clearTimeout(handle?: Timeout): void;

  /**
   * Cancels a setInterval.
   * @param handle Handle returned when the timer was scheduled; anything else is ignored.
   */
  function clearInterval(handle?: Timeout): void;

  /**
   * Queues a function on the microtask queue.
   * @param callback Function to run once the current task completes.
   */
  function queueMicrotask(callback: () => void): void;
}

export {};
