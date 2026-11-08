# Character inputs for server-computed results

The server holds no soul. When it computes a result the game would compute
from the player's character -- today, a brewing outcome -- it assembles the
inputs in memory at that moment, from what it already holds for the
connected player, and evaluates them with the game's own modifier rules.
Nothing is stored and nothing is configured by a script.

## The snapshot

`KCDC::Features::Player::SoulSnapshot`
(`code/server/src/features/player/soul_profile.h`) is built by
`CraftingService::Evaluate`:

- **Skill levels** from the progression digest the server last accepted
  (`Progression::Accepted`). Levels are the owner's, checked by the
  progression module against the orders it sent. No digest yet, no
  evaluation (`progressionUnavailable`).
- **Perks** from the same digest: each owned bit of the perk set, mapped
  through `g_Perks` to the perk's GUID -- the keys `g_SoulPerkAbilities`
  and `g_PerkBuffRules` use.
- **Effects** from the buffs the owner last reported (`Buffs::Report`),
  permanent ones only (catalog duration below zero). A timed buff would
  need its remaining time simulated, which the server does not do. A buff
  that is an owned perk's own buff is already a source through the perk
  and is not counted twice; a definition reported twice counts once.
- **Health** from the replicated soul state. A player at zero does not
  brew (`soulActorNotAlive`).
- **Clock**: the server's real time and the replicated world time, for
  time-conditioned rules such as Black Arts' daily window.

## Evaluation

`code/server/src/features/player/soul_evaluator.h` resolves the ordered
modifier terms of one native query from the snapshot's perks and effects,
using the generated buff rule catalog, and `EvaluateResolvedSoulScalar`
applies them in native order.

- Only `Cpp:Constant` and `Cpp:BlackArtsApprentice` implementations are
  evaluated. A contributing source with any other implementation, or one
  the catalog cannot describe, is refused rather than ignored.
- A contributing timed effect is refused (`activeEffectSimulationRequired`).
- Two distinct values on one operation are refused where native order
  between them is unknown (`nativeModifierTieOrderRequired`).
- The **alchemy scope** (`SoulEvaluationScope::AlchemyOutcome`) answers
  only the brewing queries -- the alchemy skill, the yield (73) and score
  (74) adjustments, the entry bonus (184) -- and skips any source whose
  authored parameters name none of them. The buffs a live player carries
  are many, and resource-only effects such as Enthusiast's minigame
  exhaustion have no bearing on the outcome. The general scope still
  refuses those it cannot simulate.

`AlchemyCharacterEvaluator::Evaluate`
(`code/server/src/features/crafting/alchemy_character_evaluator.h`) turns
the snapshot into the brewing inputs: effective and relative alchemy skill,
boiling tolerance from the relative skill, allowed mistakes (plus Trial and
Error), herb quality rules, failure XP, the entry bonus, abilities 19, 30,
63 and 92 from owned perks, and the yield and score term chains, which are
applied to each actual base when a result is computed. XP multipliers are
not an input: the server orders base XP and the owner's engine applies its
own.

## Tests

`alchemy_character_evaluator_ut.cpp` pins the native defaults, perk
abilities, the yield chain, the Black Arts window, refusal of a timed
contributing buff, the scope rules, and that the owner binds the server's
`character` object. `soul_evaluator_ut.cpp` covers the scalar arithmetic,
the daily window and the timed-step preparation.
