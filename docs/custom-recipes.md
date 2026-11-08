# Custom recipes

A server can add alchemy and smithing recipes in two ways:

- a server script registers them with `Crafting.registerRecipe`;
- a streamed KCD2 mod ships recipe table patches, as it would for the
  single-player game.

The server brews and forges both kinds by the same rules as the game's own
recipes (see [authoritative-alchemy.md](authoritative-alchemy.md) and
[authoritative-smithing.md](authoritative-smithing.md)), and every client puts
them in the game's own recipe book and smithy. `Crafting.recipes` lists them
with a `source` of `"script"` or `"mod"`; the game's own are `"game"`.

## Registering a recipe from a script

`Crafting.registerRecipe(definition)` runs on the server. Call it when the
resource starts; a player who joins later still gets every recipe.

```ts
const CHAMOMILE = "7259b9bc-dfae-487e-a8bb-c1f500894e0c";
const SWORD = "efa237c7-3905-4813-b9c3-a32b449c17ad";
const IRON = "92aa6120-028e-48ee-8ed1-1c5f91afaa26";

Crafting.registerRecipe({
    kind: "alchemy",
    id: 9100,
    name: "Chamomile tonic",
    usage: "Calms the nerves before a duel.",
    icon: "potion_hair_o_dog",
    base: "Water",
    baseXP: 10,
    ingredients: [{ item: CHAMOMILE, amount: 1 }],
    products: [{ item: "7a1e8447-4449-473b-aac9-63f7a324fa0b", threshold: 1, maxYield: 2 }],
    steps: [
        { id: 1, type: "Trivial", condition: `PotionBase() AND IngredientCount('${CHAMOMILE}') >= 1` },
        { id: 2, type: "Easy", text: "Grind the chamomile.", condition: `IsMilled('${CHAMOMILE}', 1)` },
    ],
});

Crafting.registerRecipe({
    kind: "smithing",
    id: "guild_sword",
    name: "Guild sword",
    description: "The sword every guild member forges once.",
    icon: "shortswordCommon",
    category: "ShortSword",
    workpiece: "sword",
    ingredients: [{ item: IRON, amount: 2 }],
    products: [{ item: SWORD, threshold: 0 }],
});
```

The fields are those of a row of the game's `AlchemyRecipe.xml` or
`BlacksmithRecipes.xml`; the generated `CraftRecipeDefinition` type lists them
all. What the server checks:

- **Ids.** An alchemy id is a whole number from 1 to 2147483647, a smithing id
  a string of letters, digits, `_`, `-` and `.` of at most 128 characters.
  Neither may be one the game, a streamed mod or another script already uses,
  including the game's own test recipes (alchemy 1001 and 1002, smithing
  `basic_axe`, `basic_horseshoe` and `basic_longsword`).
- **Items.** Ingredients and products are item class GUIDs of the game or of a
  streamed mod, and the server must know each one when the recipe is
  registered. Each ingredient appears once, with an amount of at least one.
  Custom items from `Items.register` cannot be used.
- **Alchemy.** `base` is `Water`, `Wine`, `Oil` or `Spiritus`. A recipe has
  1 to 4 products, one per grade (`threshold` 1 to 4), each making 1 to 64
  potions (`maxYield`); a product cannot be a quest item. It has 1 to 28 steps with distinct ids from 1 to 28;
  a step's id is its bit in the knowledge mask of `Crafting.knowledge`. The
  first step listed identifies the recipe: a brew that fails it is not this
  recipe. When two recipes would identify the same brew, the one with the lower
  id wins, as it does in the game. A step's `type` is `Trivial`, `Easy` or
  `Hard` (0, 12 or 24 XP).
- **Alchemy conditions** use the game's own syntax, terms joined by ` AND `:
  `PotionBase()`, `IngredientCount('<item>') == n` or `>= n`,
  `IsMilled('<item>', 0 | 1)`, `WeakBoilingTime('<item>', time)`,
  `StrongBoilingTime('<item>', time)`, `IsBaseDistilled(0 | 1)` and
  `IsResultMilled(0 | 1)`. Item GUIDs are in lower case, and a condition is at
  most 1024 characters. Anything else is refused. The game's recipes are the best examples.
- **Smithing.** `category` is the smithy tab (`ShortSword`, `LongSword`,
  `Axe` or `Other`) and `workpiece` the blank on the anvil (`sword`,
  `longsword`, `axe` or `horseshoe`). A product is a melee weapon, armor that
  never changes condition, or a misc item, and not a quest item.
  `threshold` is the minimal workpiece quality, 0 to 100, and `minSkill` the
  craftsmanship it takes (alchemy recipes have no skill requirement).
- **Texts.** `name`, `usage`, `description` and a step's `text` are shown as
  written.
- **Icon.** `icon` names one of the game's item icons, such as
  `potion_hair_o_dog` or `shortswordCommon`; `iconImage` brings one of the
  recipe's own instead. Give one of them.

A malformed definition, an unknown item or a taken id throws, with the reason
in the message. A recipe stays for the rest of the server's life: registering
the same definition again does nothing, and a different one under the same id
throws. To change a recipe, give it a new id or restart the server.

## Pictures

A recipe can bring its own pictures, each a PNG as base64 text:

| Field          | Where the game shows it                                        | Fitted onto                        |
| -------------- | -------------------------------------------------------------- | ---------------------------------- |
| `iconImage`    | The recipe's icon in the recipe lists                          | 64×64, the size of every item icon |
| `productImage` | Alchemy only: the recipe book's picture beside the description | 256×256                            |
| `recipeImage`  | Alchemy only: the recipe book's recipe picture                 | 1024×1024                          |

```ts
const fs = require("fs");
const png = (file: string) => fs.readFileSync(file).toString("base64");

Crafting.registerRecipe({
    kind: "alchemy",
    id: 9100,
    // ... the fields above ...
    iconImage: png("art/tonic_icon.png"),
    productImage: png("art/tonic_product.png"),
    recipeImage: png("art/tonic_recipe.png"),
});
```

Each picture is scaled to fit its square with its proportions kept and
centred on a transparent background, so a square picture whose subject
reaches the edges looks like the game's own. A PNG may be up to 4096×4096
pixels, and a recipe's pictures take at most 256 KiB of PNG together. A
picture a player's game cannot decode shows the game's placeholder there.

An alchemy recipe can show the book pictures of one of the game's own recipes
instead: `bookPicturesFrom` takes that recipe's id, which `Crafting.recipes`
lists as `key.id`, and leaves `productImage` and `recipeImage` out.

```ts
Crafting.registerRecipe({ kind: "alchemy", id: 9101, /* ... */ bookPicturesFrom: 1 }); // Hair o' the Dog's pictures
```

## Recipes from a streamed mod

A resource that ships a KCD2 mod (see [asset-streaming.md](asset-streaming.md))
can carry recipe table patches in its replace lane:

```
resources/<resource>/replace/libs/tables/minigame/alchemyrecipe__<id>.xml
resources/<resource>/replace/libs/tables/minigame/blacksmithrecipes__<id>.xml
```

The rows are the game's own format. The server reads them when the resource
starts and drops them when it stops; clients read the same files through the
game's own table loader at the next level load. A row with the id of one of the
game's recipes replaces that recipe, as it does in single player. When two
resources add a recipe with the same id, the one whose name sorts first keeps
it, as for item classes. A row the server cannot read, or one that names an item
the catalog does not know, is left out with a warning in the server log, and
the server will not brew or forge it.

## What players see

- **Smithy.** A recipe without a `perk` is listed for every smith; one with a
  perk only for a smith who has it (`player.addPerk` teaches it). Pressing
  Craft asks the server, which checks skill, materials and the perk.
- **Recipe book.** The alchemy table's book lists a recipe once the player
  knows one of its steps. Brewing a recipe successfully teaches all of it;
  `Crafting.setKnowledge(player, 9100, mask)` teaches steps directly.
- **Brewing** needs no knowledge: the right ingredients, handled the right way,
  make the recipe's potion whether or not the book shows it.

## Limitations

- Build alchemy recipes from herbs and the game's other alchemy ingredients.
  The alchemy table may not let a player put other items on it.
- A herb a streamed mod adds always counts as fresh when the potion's quality
  is worked out.
- A melee weapon a streamed mod adds gets the forged condition of an ordinary
  weapon, whatever its weapon class.
- A script's recipes are fixed for the server's life (see above).
- A streamed mod's recipe cannot bring its own book pictures, since the replace
  lane does not take files under `libs/`; its alchemy recipes show the game's
  placeholder in the book unless they replace a game recipe of the same id.
