# Door veto QA

A server `doorInteract` handler returning `false` did not stop a player
opening a door. The player's own client moves the door as soon as it is
used and reports afterwards, so a server refusal can only arrive a round
trip later, as the door swinging back. A door can now be refused before it
moves: server scripts set `door.usable = false`, and client scripts refuse
in their own `doorInteract`.

Build `KCDCServer`, `KCDCClient` and `KCDCLauncher` from the KCDC root with
`builds\build.bat <target> 64`, the client with `-DKCDC_AGENT_TESTING=ON` in
`KCDC_CMAKE_ARGS`. Run `code\tests\qa\start_door_veto.ps1` from the KCDC
root. It starts a localhost server and a `DoorQA` client in their own
runtimes under `builds\build-64`; `-ObserverNickname` adds a second client
that watches.

`/doorqa goto` teleports the tester a metre off the hinge of the nearest
shut, unlocked level door, and the next one each time it is run again;
`/doorqa goto <n> -x|y|-y` lands on another side of door `<n>` when the
first lands inside a wall.

1. `/doorqa server` (the default). Open a door: it starts to swing and then
   swings back shut. The server log shows the `open` refused.
2. `/doorqa client`. Open a door: nothing moves, the body does not start the
   push, and the server log shows no `doorInteract` for it. The client log
   shows the use refused.
3. `/doorqa bar`. Every door in the world loses its prompt. `/doorqa unbar`
   brings them back.
4. `/doorqa allow`. Doors open and close as usual, and an observer sees them
   move.

The server prints every use, its verdict and the door's state 100 ms and
2.5 s later to `server.stdout.log` with `[doorqa]`; the client prints each
use it is asked about to its own `logs\KCDC-<pid>.log` with `[doorqa]`.
