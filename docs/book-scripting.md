# Books from a client resource

`Book` opens a book a resource composed in the player's hands. It is the
game's own reading: the same book in hand, camera, page-turn animation,
prompts and pagination as a book read from the inventory. The player
turns pages and puts it down with the game's own keys.

```js
const seal = Book.image('art/seal.dds');

const id = Book.open({
    style: 'book',          // 'book' (red cover) | 'plainBook' | 'letter'
    visual: 6,              // 1 plain .. 7 embellished
    pages: [
        '<title>The Ride to Rattay</title>',
        `<heading>Of the road</heading><paragraph><inc>W</inc>e left at dawn, and the mist lay on the river.</paragraph><img src='${seal}' width='300' height='300' align='center'/>`,
    ],
});

if (id === null) {
    Hud.showNotification(`No book: ${Book.getLastError()}`);
}

Events.on('bookClosed', (bookId, reason) => {
    if (bookId === id) {
        Hud.showNotification(`Closed (${reason})`);
    }
});
```

## Pages and markup

Each string is one page, and a page break follows every page but the last.
`<newpage/>` inside a page breaks it further. The text uses the movie's
own markup, the same that the game's books are written in:

| Tag                                                    | Effect                                                   |
| ------------------------------------------------------ | -------------------------------------------------------- |
| `<title>`, `<subtitle>`                                | Centred title lines                                      |
| `<heading>`                                            | Section heading in the book's red                        |
| `<paragraph>`                                          | A paragraph                                              |
| `<br/>`                                                | Line break                                               |
| `<accent>`                                             | Red emphasis                                             |
| `<inc>X</inc>`                                         | A decorated initial (`Libs/UI/Textures/Books/Initials/`) |
| `<poem>`, `<lpoem>`, `<note>`                          | Verse and margin notes                                   |
| `<img src='...' width='W' height='H' align='center'/>` | An inline picture                                        |

The player's own reading skill never scrambles a script's book.
`legibility` (0 to 1, default 1) decides that instead. Below 1, the movie
swaps letters for look-alike glyphs, which is the same effect a vanilla
book shows a player with low reading skill:

- It works in tenths. A character class stays readable while
  `legibility^0.25` is above its threshold `(k/10)^0.25`, so 0.31 and 0.39
  look the same.
- It is deterministic: the same text at the same level always reads the
  same way.
- Markup is never touched.

`Book.setLegibility(value)` changes it while the book is in hand, for
example to sharpen the text as a quest progresses. The movie applies it
while laying the text out, so the book is laid out again, which can return
it to its first page. It returns false before `bookOpened`.

`type` picks the layout by the game's `document_class` id: 1 book (the
default), 2 recipe, 3 skill book, 4 map, 5 letter (the default for the
`letter` style), 6 plan. The skill-book and map layouts place pictures by
page instead of inline, through `images: [{ page, image }]`, where `image`
names one of the game's own pictures under `Libs/UI/Textures/Books/Skills`
or `Maps`, without the `_ui.dds` suffix.

## Pictures a resource ships

`Book.image(path)` takes a `.dds` the calling resource ships and returns
the value for an `<img src>`. The file is repackaged into the mod's own
archive, under the folder the book movie loads inline pictures from, and
mounted. `width` and `height` are required on the tag: the movie sizes the
picture to them, so any texture resolution works. A changed file gets a
new name, so the old texture is never shown stale.

## Custom paper and cover

`texture` replaces the book's diffuse texture with a picture from
`Book.image`:

```js
Book.open({ style: 'plainBook', texture: Book.image('art/my_book_diff.dds'), pages });
```

The book model maps its paper and its cover from **one atlas**, so this
re-skins both together. Paint over the vanilla atlas to keep the layout:

| Style       | Template (in `IPL_Objects-part3.pak`)                                            | Diffuse colour multiplying it |
| ----------- | -------------------------------------------------------------------------------- | ----------------------------- |
| `book`      | `Objects/manmade/task_specific_props/read_and_write/books/book_alchemy_diff.dds` | 0.78                          |
| `plainBook` | the same atlas                                                                   | 1.0, shown as painted         |
| `letter`    | `Objects/manmade/task_specific_props/read_and_write/scrolls/scroll_diff.dds`     | 0.72                          |

The texture change belongs to this book and goes away with it. The material
queues the swap, so it can land a frame after the book opens.

The text itself is not part of the texture: it is the GeneralBook movie
rendered into the opacity slot (where the vanilla material has its
`book_fake_text` placeholder), so a custom texture never covers it.

## Lifecycle

- `Book.open` returns the book's id, or `null` when the game will not open
  one now. That happens when there is no body yet, a book is already open,
  or the player is somewhere they cannot read (on horseback, in combat).
  `Book.getLastError()` says which. Malformed options throw.
- `bookOpened [bookId]` fires once the book is in hand and showing.
- `bookClosed [bookId, reason]` fires when it leaves the hands:
  - `player`: the player's own exit;
  - `script`: `Book.close()`;
  - `failed`: the book never reached the hands;
  - `sessionOver`: the session ended.
- `Book.getOpenBook()` returns the open book's id, or `null`.
- A tap of Escape (the game's `back` key) closes the book, and so does
  holding it. This applies to every book the game shows, vanilla ones
  included. The reading minigame is given the forge builder's exit as the
  game loads its config: its action map includes `minigame_exit` without
  `open_pause_menu`, and its close hint fires on release.

## What it does not do

A book is local to the machine that opened it, and its pages never cross
the network. Other players see the reader holding a book and reading it,
the way the game's NPCs read: the NPCs' own book, not this one's cover or
pages. Reading it grants
nothing: no XP, no read marker, no quest progress. A server that wants a
player to read something sends the pages in its own event.
