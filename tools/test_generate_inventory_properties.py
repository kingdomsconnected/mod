#!/usr/bin/env python3
"""Fixtures for nested clothing inheritance and alias property resolution."""
import io
import unittest
import zipfile

from generate_inventory_properties import extract, extract_repair_kits, extract_sharpenable


def table(items, components='<Clothing Name="base" ArmorType="cloth"><DerivedComponents><Clothing Name="derived" /></DerivedComponents></Clothing>'):
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, 'w') as archive:
        archive.writestr('Libs/Tables/item/item.xml', f'<database><ItemClasses>{items}</ItemClasses></database>')
        archive.writestr('Libs/Tables/item/armor_type.xml', '<database><armor_types><armor_type Name="cloth"/><armor_type Name="pouch" CanHealthChange="false"/></armor_types></database>')
        archive.writestr('Libs/Tables/Character/CharacterComponent.xml', f'<database><CharacterComponents>{components}</CharacterComponents></database>')
        archive.writestr('Libs/Tables/item/weapon_class.xml', '<database><WeaponClasss><MeleeWeaponClass id="1" sharpening_zone_count="5"/><MeleeWeaponClass id="3" sharpening_zone_count="3"/><MeleeWeaponClass id="5" sharpening_zone_count="0"/></WeaponClasss></database>')
    buffer.seek(0)
    return zipfile.ZipFile(buffer)


class PropertyCatalogTests(unittest.TestCase):
    def test_repair_kits_use_authored_type_and_inherit_through_aliases(self):
        rows = '<Ointment Id="kit" Type="1"/><Ointment Id="perfume" Type="2"/><Ointment Id="bandage" Type="0"/><ItemAlias Id="copy" SourceItemId="kit" IsQuestItem="false"/>'
        with table(rows) as archive:
            self.assertEqual(extract_repair_kits(archive), ['copy', 'kit'])

    def test_sharpenable_weapons_need_zones_and_a_real_sub_class(self):
        rows = '<MeleeWeapon Id="sword" Class="1" SubClass="11"/><MeleeWeapon Id="mace" Class="5"/><MeleeWeapon Id="stub" Class="1" SubClass="6"/><MeleeWeapon Id="training" Class="1" SubClass="5"/><MeleeWeapon Id="plain" Class="3"/><ItemAlias Id="copy" SourceItemId="sword" IsQuestItem="false"/>'
        with table(rows) as archive:
            self.assertEqual(extract_sharpenable(archive), ['copy', 'plain', 'sword'])

    def test_armor_inherits_clothing_type_and_native_max_quality(self):
        with table('<Armor Id="armor" Clothing="derived"/>') as archive:
            self.assertEqual(extract(archive), [('armor', 3, True, True, False, False, False)])

    def test_clothing_override_disables_quality_even_with_authored_maximum(self):
        with table('<QuickSlotContainer Id="pouch" Clothing="derived" MaxQuality="4"/>', '<Clothing Name="base" ArmorType="cloth"><DerivedComponents><Clothing Name="derived" ArmorType="pouch"/></DerivedComponents></Clothing>') as archive:
            self.assertEqual(extract(archive), [('pouch', 1, False, True, False, False, False)])

    def test_alias_chain_keeps_weapon_constraints(self):
        with table('<ItemAlias Id="a" SourceItemId="b" IsQuestItem="false"/><ItemAlias Id="b" SourceItemId="sword" IsQuestItem="false"/><MeleeWeapon Id="sword" MaxQuality="4" IsBreakable="true"/>') as archive:
            self.assertEqual([row[1:] for row in extract(archive)], [(4, True, True, True, False, False)] * 3)

    def test_future_alias_property_override_cannot_be_silently_ignored(self):
        with table('<ItemAlias Id="a" SourceItemId="sword" IsQuestItem="false" MaxQuality="1"/><MeleeWeapon Id="sword" MaxQuality="4" IsBreakable="true"/>') as archive:
            with self.assertRaisesRegex(ValueError, 'property override'):
                extract(archive)

    def test_alias_cycle_is_not_silently_assigned_a_default(self):
        with table('<ItemAlias Id="a" SourceItemId="b"/><ItemAlias Id="b" SourceItemId="a"/>') as archive:
            with self.assertRaisesRegex(ValueError, 'cycle'):
                extract(archive)

    def test_unknown_category_and_component_fail_generation(self):
        for row in ['<FutureItem Id="a"/>', '<Armor Id="a" Clothing="not-in-table"/>']:
            with table(row) as archive:
                with self.assertRaises(ValueError):
                    extract(archive)

    def test_unknown_armor_type_cannot_be_assumed_mutable(self):
        with table('<Armor Id="a" Clothing="unknown"/>', '<Clothing Name="unknown" ArmorType="missing"/>') as archive:
            with self.assertRaisesRegex(ValueError, 'Unknown ArmorType'):
                extract(archive)

    def test_quest_item_is_read_from_its_own_row(self):
        with table('<CraftingMaterial Id="coin" IsQuestItem="true"/><CraftingMaterial Id="iron"/>') as archive:
            self.assertEqual([(row[0], row[5]) for row in extract(archive)], [('coin', True), ('iron', False)])

    def test_alias_answers_quest_item_for_itself(self):
        rows = '<ItemAlias Id="questCopy" SourceItemId="sword" IsQuestItem="true"/><ItemAlias Id="plainCopy" SourceItemId="relic" IsQuestItem="false"/><MeleeWeapon Id="sword" IsBreakable="true"/><MeleeWeapon Id="relic" IsBreakable="true" IsQuestItem="true"/>'
        with table(rows) as archive:
            self.assertEqual({row[0]: row[5] for row in extract(archive)}, {'plainCopy': False, 'questCopy': True, 'relic': True, 'sword': False})

    def test_alias_without_quest_item_is_not_given_a_default(self):
        with table('<ItemAlias Id="a" SourceItemId="sword"/><MeleeWeapon Id="sword" IsBreakable="true"/>') as archive:
            with self.assertRaisesRegex(ValueError, 'without IsQuestItem'):
                extract(archive)

    def test_healthless_classes_differ_from_consumables(self):
        with table('<Ammo Id="arrow" MaxQuality="4"/><Herb Id="sage"/>') as archive:
            self.assertEqual(extract(archive), [('arrow', 1, False, True, False, False, True), ('sage', 1, True, False, False, False, False)])

    def test_native_stackability_uses_class_and_authored_divisibility(self):
        rows = '<MeleeWeapon Id="torch" IsBreakable="false"/><Ammo Id="arrow"/><Herb Id="sage" IsDivisible="true"/><Food Id="bottle"/><Die Id="die"/><CraftingMaterial Id="metal" IsDivisible="false"/>'
        with table(rows) as archive:
            self.assertEqual({row[0]: row[6] for row in extract(archive)}, {'torch': False, 'arrow': True, 'sage': True, 'bottle': False, 'die': False, 'metal': False})

    def test_alias_inherits_native_stackability(self):
        with table('<Ammo Id="arrow"/><ItemAlias Id="copy" SourceItemId="arrow" IsQuestItem="false"/>') as archive:
            self.assertTrue(all(row[6] for row in extract(archive)))

    def test_unknown_divisibility_is_refused(self):
        for rows in ['<Herb Id="sage" IsDivisible="yes"/>', '<Ammo Id="arrow"/><ItemAlias Id="copy" SourceItemId="arrow" IsQuestItem="false" IsDivisible="true"/>']:
            with table(rows) as archive:
                with self.assertRaises(ValueError):
                    extract(archive)


if __name__ == '__main__':
    unittest.main()
