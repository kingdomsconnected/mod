# Revived NPC hits QA

A revived NPC used to be impossible to hurt, and its blows raised no events:
clients never learned the NPC's new `spawnEpoch`, so the server refused every
combat report about it. This checks both halves, with the NPC run by either
client.

Build `KCDCServer` and `KCDCClient` from the Framework root with
`builds\build.bat <target> 64`, the client with `-DKCDC_AGENT_TESTING=ON` in
`FW_CMAKE_ARGS`. Run `code\tests\qa\start_revive_hits.ps1` from the mod
checkout. It starts a localhost server, a `ReviveQA` client and a
`ReviveObserver` client in their own runtimes under `builds\build-64`. Pass
`-ObserverNickname ''` for one client. Tile the recorded game PIDs with
`tools\tile_agent_clients.ps1`.

Every line the fixture prints goes to all players' chat and to
`server.stdout.log` with `[reviveqa]`. Each is tagged with the NPC's life.

The NPC fights only the player it was ordered to attack, which is whoever ran
the command. Another player can neither lock on to it nor hurt it; that is
the mod's combat-pair rule, not this bug. Play every step in one window.

1. On flat open ground: `/reviveqa spawn`. A bandit with a sword appears 5 m
   ahead and attacks you. Let it land blows: each prints `playerHit` and
   `playerDamage`, and the fixture then heals you. Hit it: each blow prints
   `npcDamage`.
2. `/reviveqa kill`: it falls on both clients and prints `npcDeath`.
3. `/reviveqa revive`: the reported repro. It revives the NPC and orders the
   attack again in one go. It prints `npcRevive`, and on **both** clients
   the corpse is replaced by a standing, armed body. Each client's log shows
   `Despawned NPC` and `Spawned NPC` for it.
4. Repeat step 1 against the revived body. `life=2` must print the same
   `playerHit`, `playerDamage` and `npcDamage` lines as `life=1`. Before the
   fix it printed none of them.
5. Kill it with your sword this time: `npcDeath by <you>` must print.
   Revive it again and check `life=3` the same way.
6. In the other window: `/reviveqa attack`. The NPC turns on that player.
   `/reviveqa status` shows which client runs it; repeat step 4 from there,
   so both a local and a remote simulator are covered.

Stop the recorded server PID and close both games when finished.
