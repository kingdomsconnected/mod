# Screen effects

Client-local, full-screen post effects that a client resource puts on one
player's picture, for things like intoxication, injury, poison, dreams or a
place that should feel wrong. They are exposed as the screen-effect verbs of
the `Hud` global. The C++ lives in
`code/client/src/features/ui/ui_hud_screen_effect_stack.{h,cpp}` (who asked for
what, and how it adds up) and `ui_hud_screen_effects.{h,cpp}` (getting that
onto the screen).

They are presentation only. Nothing is replicated, nobody else sees them, and
nothing reads gameplay state back from them. Whatever decides that a player is
drunk or hurt keeps that state itself, usually on the server, and sends the
client a level to show. Weather and time of day are shared world state and
stay with `World`.

## Supported effects

The API provides `colorGrade`, `blur`, `ghosting` and `screenBlood`. Vignette
is not offered. Script strengths map to each effect's supported range, and a
strength of zero switches the effect off.

## Composition policy

- The order is deterministic. Effects fold in creation (handle) order and
  every fold is commutative, so the result does not depend on which resource
  asked first.
- Brightness, contrast and saturation **multiply**, starting from the game's
  own tint as read off `C_UIOverlay`.
- Cyan, magenta, yellow, darkness and hue **add**, also starting from the
  game's tint.
- Blur, ghosting and blood take the **strongest** request.
- A fading effect is weighted, so it moves its parameters towards their
  neutral value rather than switching off.
- Writes are forced and happen only when a composed value changes: once for a
  steady effect, and every frame of a fade.
- `ApplyUserColor` is detoured. When the game re-publishes its tint, the
  composition goes straight back on top, so nothing is re-asserted every frame
  and the game's tint is never hidden.
- Removing the last effect on a channel restores what was there before:
  - Colour channels are re-published by calling the game's own
    `ApplyUserColor(!hidden)`.
  - The other channels get back the value captured before the first effect.
- Ownership. An effect belongs to the resource that created it, and only that
  resource can update or remove it. Effects are dropped when the resource
  stops, when the session ends (the feature sweep's `Reset`) and on
  `NativeContextChanged`.
- The limit is 64 effects across all resources. Fades can be up to 60 s and
  durations up to 24 h.

## API

```ts
Hud.screenEffects(): ScreenEffect[]           // names, params, ranges, defaults, neutral values
Hud.addScreenEffect(effect, params?, { fadeInMs?, fadeOutMs?, durationMs? }): number   // 0 = budget full
Hud.updateScreenEffect(handle, params, { fadeMs? }): boolean
Hud.removeScreenEffect(handle, { fadeOutMs? }): boolean   // default: the fade-out given at creation
Hud.removeAllScreenEffects(): number                      // the calling resource's only
Hud.isScreenEffectActive(handle): boolean
```

Worked examples, intoxication and injury, are in
`resources/kcdc-gamemode/src/client/screen.ts`. `/screenfx` drives them in
game: `drunk [0-1]`, `injured [0-1]`, `hit`, `grey [0-2]`,
`raw <blur|ghosting|screenBlood> [0-1]`, `clear` and `list`.

```ts
// Intoxication: one level, eased in.
const grade = Hud.addScreenEffect("colorGrade", { saturation: 0.7, contrast: 0.9, yellow: 0.05 }, { fadeInMs: 2000, fadeOutMs: 3000 });
const blur  = Hud.addScreenEffect("blur", { amount: 0.4 }, { fadeInMs: 2000, fadeOutMs: 3000 });
Hud.updateScreenEffect(blur, { amount: 0.6 }, { fadeMs: 2000 });   // another drink

// Injury: blood at the edges, plus one flash per hit.
Hud.addScreenEffect("screenBlood", { amount: 0.6 }, { fadeInMs: 300, fadeOutMs: 1500 });
Hud.addScreenEffect("screenBlood", { amount: 1 }, { fadeInMs: 80, durationMs: 250, fadeOutMs: 900 });
```

## Limits

- A cinematic or reset that calls `ResetPostEffects` clears the screen until
  the effect next changes or the game re-publishes its tint.
  `isScreenEffectActive` still reports the effect as active.
- While a TrackView sequence animates the same parameters, it wins in steady
  state: its unforced per-frame writes pull the value back towards its own.
  An effect's fade frames override it for that frame.
- Ghosting is measured per frame, so it trails less at low frame rates.
