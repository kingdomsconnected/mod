# Dice events and scoreboard names

These APIs run on the server. The sample gamemode uses each player's nickname
for their scoreboard label:

```ts
const match = Dice.start(first.id, second.id, {
    targetScore: 4000,
    firstName: first.nickname,
    secondName: second.nickname,
});

Dice.setNames(match, "First player", "Second player");
```

Names follow the seats passed to `Dice.start`. Each client puts its own name
in the local column and the other name in the opponent column. Empty names
restore the native labels on the next scoreboard refresh. Names accept up to
64 UTF-8 bytes without control characters. `Dice.setNames` returns `false`
when the match has ended.

## Turns, rolls, and moves

```ts
Events.on("diceTurnStart", (event) => {
    console.log(`Match ${event.match}: player ${event.player}, turn ${event.turn}`);
});

Events.on("diceRoll", (event) => {
    console.log(`Roll ${event.roll}: ${event.faces.join(", ")} (${event.reason})`);
});

Events.on("diceMove", (event) => {
    console.log(`${event.reason}: ${event.points} points, selected mask ${event.heldMask}`);
});

Events.on("diceTurnEnd", (event) => {
    console.log(`Turn ended: ${event.reason}; banked total ${event.totalScore}`);
});
```

A round is one player's turn. `turn` starts at 1 and increases whenever play
passes to the other player. `roll` starts at 1 within each turn; it is 0 in
`diceTurnStart`. `seat` is 0 for the first player and 1 for the second.

`diceRoll` includes six pip values from 1 through 6, in die-index order.
`rolledMask` identifies the dice rolled this time: bit 0 is die 0, through
bit 5 for die 5. Other dice were already scored. `heldMask` uses the same
indexing for the selection accepted in `diceMove`.

The order is turn start, roll, then accepted moves and any resulting rolls.
A bust emits roll, turn end, then the next turn start. Passing emits move,
turn end, and the next turn start; winning ends the match instead. Rejected
or duplicate moves emit nothing. A match stopped early also ends its open
turn before `diceMatchEnd`.

These are notifications of accepted server decisions. The server may deal
several consecutive busts together, so events can arrive before the native
animations finish. They do not report animation completion. Calling
`Dice.stop` from a handler is supported; notifications already queued for
accepted decisions retain their order.

Use `diceMatchEnd` to settle a wager. `points` in `diceMove` is the score of
that selection, while `points` in a passed or won `diceTurnEnd` is the whole
turn's banked score. `turnScore` preserves the unbanked turn score before a
pass or bust. `totalScore` is that seat's banked total.

## Playing against an NPC

The second seat can be one of the server's NPCs. Pass its id where the second
player's would go:

```ts
const keeper = Npc.create({ soul: "townsman", position: seatPosition });
const match = Dice.start(player.id, keeper.id, { secondName: "Innkeeper" });
```

The NPC is held to what a player is: alive, human, in the player's virtual
world and within a few metres of them at the table, and in no other match.
It takes the second seat only. `Dice.start` throws an Error saying which of
these failed.

The server deals the NPC's throws and checks its moves by the same rules.
What it does with a throw is the script's decision. `diceRoll` reports the
throw with `player` set to the NPC's id; when its `reason` is `rolled`, the
NPC has dice on the table and a move to make:

```ts
Events.on("diceRoll", (event) => {
    if (event.player !== keeper.id || event.reason !== "rolled") {
        return;
    }
    // Set aside every one and five just thrown, then bank from 300 up.
    let held = 0;
    let points = 0;
    for (let die = 0; die < 6; die++) {
        const face = event.faces[die];
        if ((event.rolledMask >> die & 1) !== 0 && (face === 1 || face === 5)) {
            held |= 1 << die;
            points += face === 1 ? 100 : 50;
        }
    }
    const pass = held === 0 || event.turnScore + points >= 300;
    Dice.move(event.match, held, pass);
});
```

`Dice.move(match, heldMask, pass)` plays the NPC's move. `heldMask` names the
dice to set aside, among those in the roll's `rolledMask`; every one of them
must count towards a scoring combination, as the table requires of a player.
With `pass` false the held dice are scored, which needs at least one, and the
rest are thrown. With `pass` true the turn is banked, after scoring the held
dice if there are any. It returns `false`, and changes nothing, when the move
is not legal, it is not the NPC's turn, or the match is over or has no NPC.
An accepted move raises `diceMove` and whatever follows, as a player's does.

The example above only reads ones and fives. A throw can also score with
three of a kind or a straight and show neither; `held` is then empty and the
example banks what the turn already has. A fuller opponent would read those
combinations too.

A move may be made from inside the `diceRoll` handler or later, from a timer.
The player's table plays each move at its own pace whenever it arrives, so a
delay is a matter of feel and not of correctness. The NPC forfeits like a
player if no move is accepted for three minutes: `diceMatchEnd` then reports
`timedOut` with the player as the winner. An NPC that dies or is removed
during a match leaves it: the reason is `left` and the player wins.

`diceMatchStart` and `diceMatchEnd` pass the `Npc` as `second`. A script
cannot move a seat a player holds.

The player's game seats the NPC's body opposite and animates its throws, as
it does another player's. While the match lasts, that client leaves the body
to the table: it does not follow orders or play scripted animations there.
Give the NPC no route during a match, and stand it at the table beforehand.
The table's own opponent commentary is voiced on an NPC; it stays silent on
a player's body.

## What nearby players see

Observers see the seated players, dice throws, and table props without
entering the minigame themselves. One participant reports both native dice
sets and the cup. The server validates the match and table before relaying
their transforms and visibility. This presentation does not decide scores.
Props disappear when the report expires, the match ends, or its player
leaves the observer's streamed world.

An NPC opponent is seated only on the machine of the player it plays
against. Bystanders see that player, the dice and the cup, and the NPC
wherever the server last had it. A script can play a seated animation on the
NPC for them; the player at the table does not see it.

Both client and server must use the updated protocol version, 3.0.0.
