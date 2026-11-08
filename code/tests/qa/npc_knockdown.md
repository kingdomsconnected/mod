# NPC knockdown QA

An NPC attacking a player could finish with a lethal sync attack once the
player's health ran low. The player's life is the server's, so the death
never came: the body lay limp in the dying half of the sync, its combat
action outranked the ragdoll and the stand-up, and attack input still
worked from the floor. NPCs no longer weigh lethal variants against the
local player.

Build `KCDCServer` and `KCDCClient` from the Framework root with
`builds\build.bat <target> 64`, the client with `-DKCDC_AGENT_TESTING=ON` in
`FW_CMAKE_ARGS`. Run `code\tests\qa\start_npc_knockdown.ps1` from the mod
checkout. It starts a localhost server and a `KnockQA` client in their own
runtimes under `builds\build-64`. Set `KNOCKQA_HEALTH` in the environment
before launching to hold the tester at that health; it defaults to 100.

Eight seconds after the tester is ready the fixture equips a sword on them,
spawns a sword bandit four metres ahead and, three seconds later, orders it
to attack. After every blow the tester's health is set back to the held
value with `setStat`, which leaves the knockdown's own buffs alone.
`/knockqa spawn` starts over; `/knockqa hold` stops the bandit.

1. With `KNOCKQA_HEALTH=25`, fight the bandit for a few minutes, blocking and
   attacking. It must never play a finisher on you. Any knockdown it lands
   ends with you standing up again on your own within a few seconds.
2. With the default health, fight until a combo knocks you down: you get up
   by yourself, and cannot attack until you do.

Every blow, buff and the tester's state every two seconds go to
`server.stdout.log` with `[knockqa]`.
