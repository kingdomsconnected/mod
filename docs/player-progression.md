# Player progression: skills, stats and perks

Tested in game with a server and two clients.
Every flow below was exercised: server XP, levels, perks and points; XP
earned in play (jumping); a script veto; native XP switched off; a rate; a
cap; learning a perk from the game's own perk screen; a blocked learn;
respec; saving a player's progression and restoring it onto another; and a
late joiner seeing everyone's levels and perks.

## Decision

The server authorizes the input and the owner's engine computes the result.

Native XP effects only advance, so every earned gain is withheld on the
owner and asked for, then returns as an authorized order. Explicit absolute
assignments can now lower the typed native records and notify the native
property system; see [custom buffs and live progression editing](custom-buffs.md). The curve, the perk XP
multipliers, the perk-point grants and the level-up popup all stay native.

## Flows

**The player earns XP.** The `Apply` detour sees a gain for the local soul,
zeroes it, records `{track, requestedXp, scaled, cause name}` and returns
true. Every 250 ms the gains go to the server as one `XpRequest`. The server
asks `playerXpGaining`, applies the rate, the per-minute budget and the cap,
and sends a sequenced `ProgressionOrder`. The owner builds the effect on an
event from `C_EventFactory::CreateAlchemyStartedEvent` (the event the Lua
`AddSkillXP` binding uses) with the scaled or unscaled creator, and runs
`ProcessEvent` under an exemption flag.

**The player learns a perk.** The client sends a `LearnPerkRequest` before
applying the perk. The server checks the tree level, parent, exclusivity,
available points, block list and `playerPerkLearning`, then orders the learn
or answers `PerkRefused`. An accepted order refreshes the screen and plays
the game's own perk-learned sound.

**The game adds a perk.** Codex entries, recipes and autolearned combat moves
reach `AddPerk` without a learn. They go through, except blocked perks and
perk-screen perks, which only a learn may add.

**A script blocks a perk.** `Progression.blockPerks` takes the perk out of
the game. Every player who owns it gets a `RemovePerk` order straight away,
and a blocked perk that shows up later in a digest without a server order,
such as one of a new character's starting perks, is ordered away as soon as
the server sees it. `restoreProgression` leaves blocked perks out of the
restored set. The point spent on a removed perk is not refunded;
`respecPerks` refunds every point. `player.addPerk` still grants a blocked
perk, because that is the script's own decision.

The perk screen shows a perk on the server's block list as blocked.

**The server grants.** `addXp`, `setLevel`, `addPerk`, `removePerk`,
`addPerkPoints`, `respecPerks` and `restoreProgression` are orders. They run
through `CreateSkillXPEffect`, `AdvanceTo*Level`, `AddPerk`, `RemovePerk`,
`Respec`, and a direct bank write.

A perk order that follows an XP or level order waits until the local perk
list has applied the preceding change. The client rebuilds its digest when
an order, perk change or point-bank change occurs.

## Checking the digest

The owner publishes a `ProgressionDigest` on change, carrying the last order
it has applied. The server adds up what those orders could have moved:

- an XP order counts its amount times 4 when scaled, exactly when not;
- a level order counts the total XP of its level.

A track past that, or a perk-screen perk nobody ordered, raises
`playerProgressionRejected`. The digest is adopted anyway, because the owner
owns its soul and no level can be taken back.

## Replication

| Peer      | Gets                                       | Channel                                          |
| --------- | ------------------------------------------ | ------------------------------------------------ |
| Owner     | orders, refusals, the policy               | RPC                                              |
| Server    | the digest                                 | RPC                                              |
| Observers | levels, main level, perk bitset (945 bits) | `HumanEntity::progression`, server-authored only |

Remote puppets are never given perks. That would build the 255 buff perks a
second time on top of the stats `HumanSoulState` already carries. The
`HasPerk` detour instead answers for a remote puppet's soul from the
replicated bitset, through a roster published whole for worker threads.
The bitset carries what `C_Soul::HasPerk` finds: the perks in the soul's list
and the ones a metaperk among them holds (`Player` holds `Player - companion -
horse`, which makes the player's horse immortal), since `HasPerk` walks into
those too.

Adding fields to `HumanEntity` is a netcode change (MAJOR).

## Files

- `tools/generate_progression_catalog.py` generates `code/shared/generated/progression_catalog.cpp`
- `code/shared/game/progression_catalog.h`: tracks, curve, perks, exclusivity
- `code/shared/features/player/player_progression.h`: payloads, `ProgressionView`
- `code/shared/features/player/player_progression_bindings.h`: the shared scripting shapes and the `Progression` catalog functions
- `code/client/src/sdk/wh/rpgmodule/`: `c_skill_xp_effect.h`, `c_stat_xp_effect.h`, `c_perk.h`, `c_perk_list.h`, `c_perk_learning_state.h`, `s_alchemy_started_data.h`, and the split `c_soul_state_storage.h`
- `code/client/src/features/player/player_progression_{service,hooks,scripting}.*`
- `code/server/src/features/player/player_progression_{module,scripting}.*`

## Known gaps

- The server trusts the perk points a digest reports. A client that inflates
  them can learn perks it did not earn.
- A replayed gain carries no query reason. The engine scales it by
  `XPMultiplier` under reason 0, whatever reason the withheld gain had.
- A gain the game makes in the frame between a new body and the service's
  next tick is not withheld. It lands natively and trips the digest bound.
- The perk screen's confirm also learns on a second actor the UI resolves.
  The gate covers only the local player's soul, so that actor learns natively.
- The effect of replicated perks on combat has not been verified.
