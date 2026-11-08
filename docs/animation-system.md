# Scripted animations

## Usage

Server scripts make a player or an NPC play any of the game's own
animations, optionally holding props, once or on a loop:

```ts
player.playAnimation("CrimeSurrender", { tags: "r_axe", loop: true, props: { item: "battle_gate_axe" } });
npc.playAnimation("GateAttack", { tags: "r_axe", loop: true, props: [{ item: "battle_gate_axe" }] });
player.playAnimation("", { tags: "r_bucket", props: { item: "waterBucket" } }); // carry while walking
player.stopAnimation();
player.stopAnimation({ standUp: true }); // out of a sitting or lying stance the game's way

Events.on("playerAnimationEnd", (player, fragment, reason) => { /* finished | interrupted | refused | stopped | replaced */ });
Events.on("npcAnimationEnd", (npc, fragment, reason) => {});

Animations.list("bucket"); // AnimationInfo[]: fragment, tags, duration, oneShot, fullBody, rightHand, leftHand, sequence, phase, partner, slaveBody
Animations.props();        // AnimationPropInfo[]: name, model, hand
```

A prop is `{ item }` (a row of `Animations.props()`, which brings its model
and in-hand offset) or `{ model: "objects/...cgf" }`, on the right hand
unless `hand: "left"` or `joint: "<skeleton joint>"`; `position` and
`rotation` offset it in the joint's frame. `AnimationInfo.rightHand` names
the kind of object an animation was authored holding; a prop whose `hand`
matches it sits the way the animation expects.

Three fields describe how a row relates to others. `sequence` and `phase`
group the in/loop/out parts of one activity (`BakerMillIn`, `BakerMillLoop`,
`BakerMillOut`, plus `variation`s played in place of the middle); a script
plays them in that order itself. `partner` names what a second actor plays
in the game's two-person animations (`TiedUpOut_Master` against
`TiedUpOut_Slave`). The game records neither: both are read from the
fragment names by `tools/generate_animation_catalog.py`. `slaveBody` is the
game's own data -- the fragment's scopes in the body's controller definition
include `SlaveChar` for those tags -- and marks animations that also move a
character bound to the body: corpses, horses, a smith's workpiece. Nothing binds
one yet, so only the body playing it moves.

`npc.playAnimation(id: number)` still plays a row of the emote catalog once.

`stopAnimation`, a new `playAnimation` and `teleport` cut a running
animation at once. `standUp` follows a request whose tags held the body in a
sitting or lying `Stance` tag (`sittingNoTable`, `lyingGround`) out with the
game's own transition, `StandUp` or `GetUp`; without it the body pops upright.

Each request's end is announced once, as `playerAnimationEnd` or
`npcAnimationEnd`: `finished`, `interrupted` (it played and something cut it),
`refused` (it never started), `stopped` or `replaced`. A loop ends only the
last two ways; a request with no fragment raises nothing.

The gamemode's `/anim` command exercises all of it: `/anim list <search>`
then `/anim next` to step through results, `/anim play`, `/anim hold`,
`/anim npc`, `/anim stop [standup]`, `/anim npcstop [standup]`, and it tells
the caller when each animation it started ended and how.

## Model

The request is held state, not an event: `AnimationPlayback` on
`HumanEntity::animation` (a server-only field, relayed to observers) and on
`NpcEntity::animation` (server-authored block, pushed to the simulator with
`ForceState`). A player's owner is sent no fields, so it is told through the
`PlayerAnimation` RPC. `revision` distinguishes a new request from the same
one read again. A client that first sees a body mid-request applies its tags
and props and plays it only if it loops, so a one-shot is not replayed for
someone streaming in later.

`lockMovement` defaults to the table's `isfullbody` and holds the local
player's body through the Animation MCM slot, as full-body emotes do, from
the fragment's `Enter` to its end.

A stand-up is a request of its own: the stance exit's fragment with the
stance tag, `lockMovement` and `endsWithFragment`, which takes the tags off at
the fragment's completion point and is ignored by a body that first sees it
mid-way. The exit comes from `g_AnimationStanceExits` (below).

### How an end is announced

The client whose body counts -- a player's owner, an NPC's simulator --
sends `AnimationEnded { entity, revision, reason }` from the edge that ended
the action (below). The server accepts it only from that peer, only for the
request still current and only once (`animationEndAnnounced` on the entity),
and announces the ends it causes itself: `stopped` and `replaced` after the
new request is published, so a handler that plays again replaces it; an NPC
nobody simulates plays nothing, so its one-shot ends `refused` where it is
asked for; and a change of simulator ends a one-shot still playing
`interrupted`, because the new simulator does not replay what it did not see
start. An owner whose body is rebuilt mid one-shot reports `interrupted` too.

## The face

```ts
player.setFacialExpression("FE_DialogueSpeaking", { tags: "happy" }); // held mood
npc.setFacialExpression("ADLG_FA_Smile");                              // one gesture
player.setFacialExpression("");                                        // the game's own face
player.setTalking(true, "neutral"); // "happy" | "drunk" | "chew"
player.setTalking(false);
```

The gamemode's `/face` command exercises it on the caller or the nearest
NPC (`/face npc ...`).

State: `FaceState` on `HumanEntity::face` (server-only relay, owner told
through `PlayerFace`) and `NpcEntity::face` (server block, `ForceState`).
It is kept apart from `AnimationPlayback`, so a face change never
restarts the body's animation.

- **Expression.** A fragment on the `FacialExpression` scope, queued as the
  same generic `TAction` at priority 5 with no queue timeout, so it waits out
  whatever else holds the scope. It takes priority over the game's default
  and speaking expressions, which return when it ends.
  `expressionRevision` replays a gesture asked for twice.
  - Mood tags (`happy`, `angry`) are global `Mood` tags, stated and restored
    through the same `TagOverlay` as the body's global tags. They can
    colour the body's idle too.
  - No end events. The expression is played even for a body first seen
    mid-request.
- **Talking.** A looping generic clip from `Animations/humans/facials`,
  which every human `.chrparams` loads: `fa_cin_talk_neutral_01`,
  `fa_cin_talk_happy_01`, `fa_cin_talk_drunk`, `fa_chew_loop`. It is
  started on layer 12 at weight 0.75, using
  `wh_actor_LipSyncStartTransitionTime` and
  `wh_actor_LipSyncStopTransitionTime` for blending.
  - The clip restarts only on an empty layer. The game's lip-sync for a
    spoken line takes priority.
  - Stopping only stops the layer while ours is the only clip in it, so a
    line of the game's own lip-sync blending in is never cut.

Not yet verified in game: the talk clips resolving and blending on a remote
body, mood tags on the `FE_*` fragments, and a gesture's return to the
game's default face.

## Catalog

`g_AnimationStanceExits` maps each global `Stance` tag the table authors a
way out of to that transition's fragment: the rows starting sitting (3) or
lying (2) and ending standing (1) that carry the tag, grouped by tag, taking
the most common fragment -- `StandUp` for the sitting tags, `GetUp` for the
lying ones. `beggarKneel`, `kneelingChest` and the like have no authored
exit. The `Stance` group comes from `kcd_male_tags.xml` and
`wh_female_tags.xml` in Animations.pak.


`tools/generate_animation_catalog.py <game dir>` writes
`code/shared/generated/animation_catalog.cpp` (server only): every
`actor_anim_action` row of the male and female human classes, folded per
fragment/tag pair, and every `item.xml` item with a `ManipulationType` and a
`.cgf` model, with its manipulation's default `ItemTransformation`. That
transform is applied as the socket's offset from the hand joint.

## Verified in game with two clients

Bow and surrender loop on the owner and its observer; hand-tag carry pose;
bucket, broom and axe props swapping in turn and cleared by stop; an NPC
swinging an axe on a loop. Not yet exercised: `lockMovement` under player
input, streaming a looping body in, NPC simulator handover.

Not yet exercised live: the immediate cut on stop, replace and
teleport; the end events; `standUp`; gapless loops through `SetFragment`.
