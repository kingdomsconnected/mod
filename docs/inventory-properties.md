# Inventory item properties

An inventory row's `metadata` holds every property the server keeps:

| Key             | Meaning                                                                    |
| --------------- | -------------------------------------------------------------------------- |
| `quality`       | 1 up to the class's maximum quality.                                       |
| `health`        | Absolute item health, inside the interval the quality allows.              |
| `condition`     | Health as a fraction of that interval, 0 to 1.                             |
| `poison`        | Missiles only: the poison buff GUID coating it.                            |
| `poisonCharges` | Missiles only: uses left of that poison.                                   |
| `custom`        | Registered custom types only: optional name, description and private data. |
| `onEquipBuffs`  | Persistent on-equip buff GUIDs (see below).                                |

A default is left out, so two equal items encode equal and a grant merges
into the row that already holds that kind of item. Provenance (owner,
stolen-from) and equipment placement are the game's, not the inventory's,
and have no key; a request naming them is refused as `invalidMetadata`.

New items get an explicit, deterministic policy: quality 1 and full
condition. Absolute health follows the native class and quality rules. This
is an intentional server policy. It is not the game's default creation
behavior: omitted native quality/condition can produce randomized values.

Every write goes through the same normalizer inside the server inventory:
`add`, `setProperties`, `set` (a restore) and the client's wear and repair
reports.
Stored metadata is therefore always canonical. Client values never establish
the class limits or fill missing properties.

`tools/generate_inventory_properties.py` generates constraints for all 5,604
shipped item classes into `code/shared/generated/inventory_property_classes.cpp`.
It resolves item aliases and clothing inheritance from `CharacterComponent`
to `armor_type.CanHealthChange`. The application never reads these tables
from an installed game at runtime. Unknown categories, missing component
references, cyclic aliases and unsupported alias property overrides fail
generation. The 51 shipped aliases override only display, quest and weight
fields, which do not replace the source class health/quality constraints.
The generator also lists the repair-kit classes -- Ointments whose authored
`Type` is 1, directly or through an alias -- that the server's repair check
admits.

## Validation

Quality, health and condition are concrete in stored metadata. Unknown
classes, out-of-range quality, immutable-class wear, and contradictory
health/condition are rejected. Float comparison permits only the documented
small tolerance needed for float32 conversion. Health alone is interpreted
within the requested quality's interval, with quality 1 when omitted; the
server derives condition before native construction.

Poison definitions and charge limits must match the compiled missile catalog;
arbitrary GUIDs and classes without a native poison runtime are refused.

## Wear

The client reports health and quality changes for items the server holds.
The server accepts a report only as a loss -- lower quality, lower health within a
quality, fewer poison charges -- and records it as a `wear` change. Anything
else outside an admitted native repair (a sharpened blade, for example) is
written back to the server's properties on the next projection. Food and herb
decay stays frozen.

## Repair kits

Repair kits raise equipment health and lose durability according to the
game's skill and perk calculations. One repair can spend several kits.
The client records each unit's inventory row and its starting and final
properties before any repaired stacks merge.

On the next update, it reports all combines in the selection as
one `InventoryConsume` of kind `repair`: every distinct equipment unit,
whether it is worn, and every distinct kit with its starting and final
properties, an exhausted kit as `spent`. Projection holds until the answer's
revision arrives, and the repaired stacks' own health changes are never
reported as wear. A repair the client cannot describe, or one the server
refuses, is projected back: the kits reappear and the equipment returns to its
condition. A selection may report at most 64 equipment units and 16 distinct
kits; a larger one is projected back. A surviving kit reused across several
selected items is captured once, with its total durability loss across the
selection.

The server requires kits whose authored Ointment `Type` is 1 (repair kit),
directly or through an alias, equipment whose health can change, starting
properties equal to the rows', and a health gain on the equipment paid by a
durability loss or a spent kit. Quality, poison and on-equip effects must stay
as they were: an effect a repair perk adds is taken back off by the next
projection. All units are validated against the starting inventory before any
output is credited, so several out of one row are admitted without spending an
output twice. A kit that survives joins the row holding that kind at its new
health, or a new row. A worn equipment copy keeps its row and the untouched
copies move to a matching or new row; a loose one moves out itself, so the
equipment record keeps naming the copy on the body. The server admits the
owner's result rather than recomputing the skill-based cost. Everything
commits as one `repair` change or not at all.

The equipment field is now an array, with `equipped` on each entry. The server
still accepts the older single-equipment object, but batch repairs require the
updated client and server together.

The projection follows these splits by health: a stack whose health no longer
matches the row it held moves to the row of its kind that it does match,
unless it carries reported wear the rows have not caught up with.

Scripts see wear and repairs as `wear` and `repair` changes on
`playerInventoryChanged`; see [Inventory scripting API](inventory-scripting-api.md#events).

## Grindstones

The sharpening minigame raises a weapon's health as the wheel turns, and can
lower it at a bad angle. The generator lists supported melee weapons,
aliases included, as `g_SharpenableClasses`. Training weapons and stubs
are excluded.

While the game holds `E_UIState::MinigameSharpening`, the client neither
reports the weapon's changes as wear nor projects, so the wheel's progress is
not written back. Once it closes, a stack of one unit whose health alone rose
is reported as one `InventoryConsume` of kind `sharpen`, with the unit's
projected and final properties and whether it is worn; anything else is wear,
or projected back.

A sharpening costs no item, so where the player stands is its proof: the
server admits one only from a player who can act, within 3 m of a placed
`Grindstone` of the server's level (the minigame seats them about 1.4 m from
it). The crafting generator lists those from the level data as
`g_SharpeningStations`. The weapon must be a sharpenable class and only its
health may rise, from properties equal to its row's; the change commits through
the repair path as one `sharpen` change, or is projected back.

## Tests

`inventory_property_policy_ut.cpp` checks native formula fixtures, all shipped
classes, rejection without partial output, alias-equivalent defaults and the
keys that are not properties. Poison cases cover supported definitions, excess
charges, invented IDs and classes without a supported poison runtime.
`tools/test_generate_inventory_properties.py` checks clothing inheritance,
healthless armor types, alias chains/cycles and invalid source references,
repair-kit detection through the authored type and aliases, and sharpenable
weapons by class zones and sub class.
`inventory_model_ut.cpp` checks that a repair commits the kits' cost and the
equipment's gain in one revision, spends several kits from one row, keeps the
worn copy's row, and refuses atomically, capacity included.
`inventory_repair_ut.h`, in `KCDCTests`, exercises selection capture through
message parsing and server admission: shared and exhausted kits, several
equipment units from one row, worn-copy identity in either selection order,
atomic rejection, bounds, and the older single-item message. It also checks
that merging repaired equipment or a surviving kit before the next update
does not lose the captured result or spend an extra kit. Its sharpening cases
refuse a class the grindstone does not take, a stub included, any change but a
health gain, and stale starting properties, and parse only a whole message.

## Persistent on-equip item effects

`onEquipBuffs` is an optional array of canonical GUID strings, separate from
`poison` and its consumable charges. It describes the complete persistent
native on-equip definition set; an empty set is left out.
The wire bounds the set at 32 unique definitions; current gameplay admission
supports Experimental Powder on ammunition and its inherited aliases:
`11fd0a23-9871-48e1-8eeb-e5dfac149f9c`. Other requested effect/class combinations
are refused until their native semantics are implemented.

Projection preserves the exact effect metadata. The game merges stacks whose
health differs by up to 0.01; for items the server holds, the merge hook
allows it only between stacks with exactly equal properties, so clean and
enhanced ammunition never collapse into one. World, vendor and crafting adapters are separate
integrations. This item representation does not claim server combat/effect
simulation or an implemented firing adapter.
