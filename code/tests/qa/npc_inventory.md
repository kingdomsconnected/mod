# NPC inventory QA

Build `KCDCServer` and `KCDCClient` from the Framework root with
`builds\build.bat <target> 64 Debug`. For the client, append
`-DKCDC_AGENT_TESTING=ON` to `FW_CMAKE_ARGS` for the build. Run
`code\tests\qa\start_npc_inventory.ps1` from the mod checkout. It creates
independent runtimes under `builds\build-64`, starts a localhost server and
client, installs the fixture only in that runtime, and records the launch.
Check the client's `[Agent testing]` title contains the intent and `LootQA`.
The game PID and `Agent testing graphics` log must match that run.

Open chat normally and enter these commands on flat, open ground:

1. `/lootqa human`: take the padded coif first, then take the helmet and
   another item without closing the screen. The helmet should be removed
   from the corpse's head when its coif goes, but remain available to loot.
   Close and reopen the same body, loot one sword, split the sage stack,
   then take all.
   There are two swords at 50% condition, seven sage and 23 money units,
   plus the bandit's clothes. Close and reopen: taken items stay taken.
   Depositing an item back, where the native screen allows it, preserves
   its condition and amount and does not equip it on the corpse.
2. `/lootqa animal`, then `/lootqa status`: note the already prepared stock.
   Harvest the hare using the native hold interaction. Cancel once, retry,
   then compare status before taking anything: the same rows and quantities
   remain, with `harvested=true`. Skin/liver have a 50% spawn chance; meat is
   guaranteed. Perks do not roll extra stock. Take some meat and reopen:
   taken items stay taken.
3. `/lootqa native`: test a bandit's native default inventory census.
   `/lootqa empty` tests an empty corpse; `/lootqa blocked` must refuse loot.
4. On a fresh human, `/lootqa remove 10`, then open its loot screen.
   Removal must close it safely. Repeat with `revive 10`, `move 10`,
   `lock 10`, and `restore 10`. Stale screens must never duplicate items.
   `restore` deliberately restores the stock captured when scheduled.
5. Walk away and return, or reconnect: looted stock and harvested state
   must survive. A second client at the same body should be refused while
   the first owns its screen/action, then succeed after it closes or leaves.
   Use matching binaries, its own runtime and a distinct nickname.

`/lootqa status` also writes the complete stock to `server.stdout.log`.
Every fixture NPC and player inventory change is logged with `[lootqa]`.
`/lootqa reset` removes fixture bodies and keeps already collected items.
Perk/level commands affect only this test server's session state.
Close the game and stop the recorded server PID when finished. The CMake
option remains cached: explicitly rebuild with `KCDC_AGENT_TESTING=OFF`
before producing a normal client.

For synchronized butchering, start the script with
`-Nickname HarvesterQA -ObserverNickname ObserverQA`. It creates separate
client runtimes and records both PIDs in `session.json`. After both clients
are ready, tile their recorded game PIDs with `tools\tile_agent_clients.ps1`.

1. HarvesterQA: `/lootqa animal`. ObserverQA: `/lootqa visit` to stand next
   to that same animal. Both should initially offer Butcher.
2. HarvesterQA: start butchering. ObserverQA must not be able to butcher or
   loot that body while the action is held. The knife must stay in the
   harvester's hand locally and on ObserverQA throughout the animation.
3. Cancel if the native action permits it. The observer's prompt should
   become usable again, with unchanged prepared stock. The animation stops
   and the temporary knife disappears on both clients. Retry on the same body.
4. Complete butchering and close the loot screen. ObserverQA should now see
   Loot and the same skinned model. Both clients loot the prepared parts;
   neither may generate another yield.
   Neither player's inventory gains a knife. Repeat with a weapon equipped:
   after closing loot, drawing and holstering that weapon must still work.
5. Reconnect ObserverQA and use `/lootqa visit` again. The body should still
   offer Loot and retain exactly the remaining items. Repeat with a fresh
   animal and `/lootqa lock 2` before starting to test grant revocation.
6. After completion, `/lootqa revive`: both models must return to their
   original appearance. Kill it again with `/lootqa kill`, butcher again
   and confirm only the remaining stock is available.

Automated model/protocol/harvest regressions are part of `KCDCTests`, run
through `builds\build.bat RunKCDCTests 64 Debug`. This fixture exercises the
native UI, animation and projection paths that require the running game.
