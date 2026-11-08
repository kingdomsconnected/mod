# Visor voice muffling

Closing an equipped helmet visor muffles that player's proximity voice for
other players. Opening it or removing the helmet restores normal voice.
Whisper, normal and shout keep their existing distance ranges.

Closed-visor speech uses two cascaded low-pass filters at 1 kHz with a 4.5 dB
reduction, blended over 100 ms. Other players also see the helmet's visor in
the same open or closed state. This does not muffle all sounds heard by the
wearer or replay the hand animation used to raise and lower the visor.

Use matching clients and server from this change. The player format adds
a visor flag, so `VERSION` moves to 2.0.0 under the repository's major-version
rule for wire changes. KCDC currently disables build-token version checking,
so this version bump does not enforce compatibility. Do not mix these clients
or servers with older builds. This change does not publish a release.

## Scripting

Server and client scripts can read the same state as `player.visorClosed`,
a readonly boolean on `Player`. It is `true` while the player wears a helmet
with a visor and has the visor closed. It is `false` when the visor is open,
when the player wears no helmet or a helmet without a visor, and once the
player's body is gone.

The player's own game reports the state. Their client checks it every frame
and sends each change, so the server and other clients see it shortly after
it happens. On that player's own client, `LocalPlayer.visorClosed` reads the
local value directly. No event fires when the visor moves, so read the
property when you need it. Scripts cannot open or close a visor.

```js
// Server script
setInterval(() => {
    for (const player of Player.all()) {
        if (player.visorClosed) {
            console.log(`${player.nickname} has their visor down`);
        }
    }
}, 1000);
```

```js
// Client script
const visorDown = LocalPlayer !== null && LocalPlayer.visorClosed;
```

The property reads the existing replicated flag, so it adds no network
change.

## Implementation

The owner samples the native visor state and checks that a visor is actually
equipped. The resulting boolean travels through `HumanEntity::SerializeFields`
with reliable deltas and the full initial state sent to new listeners.

Remote bodies compare that same boolean with their native model state after
applying equipment and appearance. A mismatch calls `C_Actor::OpenVisor` or
`CloseVisor`, which also invalidates the worn helmet's visuals. Checking the
native state each frame covers newly spawned bodies and native actions that
change the visor, without repeatedly invalidating an unchanged helmet. No
additional replicated field or RPC is needed.

The receiving client associates it with the voice speaker's owning peer. Its
existing FMOD DSP filters decoded PCM before volume and spatial panning. The
audio callback reads only an atomic target; filter history belongs to the
mixer thread and resets when a slot changes speakers. Normal voice passes
through exactly after the opening fade finishes.

This effect currently applies to the KCDC FMOD sink. The Framework fallback
audio device, used when FMOD setup fails, continues to play unfiltered voice.

## Validation

Build from the Framework root using the pinned revision:

```powershell
.\builds\build.bat KCDCClient 64 Debug
.\builds\build.bat KCDCServer 64 Debug
.\builds\build.bat RunKCDCTests 64 Debug
```

The automated tests cover unfiltered output, frequency response, smooth
transitions and reversals, independent speakers, slot reuse, bounded output,
silence, and visor replication to existing and late listeners.

The Debug client builds with agent testing enabled and disabled, the Debug
server builds, and all 301 tests in 47 modules pass.
Two local clients also exercised the real FMOD path with a quiet synthetic
signal substituted for microphone PCM. Native open/close calls ran on the
game thread; traces confirmed that the receiving voice channel enabled and
disabled the effect while continuing to render nonzero audio. Removing a
closed helmet cleared the effect. An open-face helmet stayed unfiltered even
with the actor's closed flag set. A fresh listener joined while the visor was
closed and received the muffled state with nonzero audio output.

A subsequent two-client listening pass used microphone input. The tester
confirmed that the final 1 kHz / 4.5 dB setting works.

The equipped items were `BascinetVisor01_m01_C3` and
`BascinetOpen01_m01_C2`, each with `CoifLarge01_m01_D2` underneath. The padded
coif is required: equipping the helmet alone is rejected by the game.

For gameplay validation, use two matching clients near each other:

1. Speak with no helmet and with an open visor. Both should sound normal.
2. Close the visor while speaking. The listener should hear a smooth change
   to muffled speech, with direction and distance attenuation still working.
3. Open the visor mid-sentence, then remove a closed helmet. Both should
   restore normal voice. A helmet without a visor should stay unfiltered.
4. Join another listener while the speaker's visor is already closed.
5. Have an open-visor and closed-visor player speak together. Their effects
   should remain independent. Check whisper and shout as well as normal voice.
6. Disconnect a muffled speaker and let another player use the voice slots.
   No muffling or sound from the previous speaker should carry over.

Further helmet variants, simultaneous live talkers, distance/tier checks,
and detailed animation timing were not covered by the recorded gameplay
checks. The tests cover filter independence and slot reuse; those cases
still benefit from broader multiplayer QA.

### Visual state synchronization

The follow-up visual change reuses the same replicated flag. The developer
confirmed that it works in the two-client gameplay test. The Debug client
with agent testing enabled and disabled and the Debug server build successfully. All
301 tests in 47 modules pass, including visor deltas and late-listener state.

Visual checks across helmet replacement, remote body recreation, and joining
while already closed remain useful follow-up coverage. The native state is
reconciled every frame, but those lifecycle cases were not separately
confirmed in this visual test.
