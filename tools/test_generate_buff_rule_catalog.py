"""Verify compiled buff inputs without assigning unsupported native semantics."""
from __future__ import annotations

import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
import zipfile


MOD = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("buff_catalog_generator", MOD / "tools/generate_buff_rule_catalog.py")
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)

BUFF = "11111111-1111-1111-1111-111111111111"
SECOND_BUFF = "22222222-2222-2222-2222-222222222222"
PERK = "33333333-3333-3333-3333-333333333333"
ITEM = "44444444-4444-4444-4444-444444444444"
ALIAS = "55555555-5555-5555-5555-555555555555"


def archive(overrides=None):
    rows = {
        "Libs/Tables/rpg/buff.xml": f'<root><buffs><row buff_id="{BUFF}" implementation="BasicTimed" /></buffs></root>',
        "Libs/Tables/item/item.xml": f'<root><ItemClasses><Food Id="{ITEM}" BuffDefinitionId="{BUFF}" /><ItemAlias Id="{ALIAS}" SourceItemId="{ITEM}" /></ItemClasses></root>',
        "Libs/Tables/rpg/perk_buff.xml": f'<root><perk_buffs><row perk_id="{PERK}" buff_id="{BUFF}" /></perk_buffs></root>',
        "Libs/Tables/rpg/perk_buff_override.xml": '<root><perk_buff_overrides /></root>',
    }
    rows.update(overrides or {})
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w") as pak:
        for path, text in rows.items():
            pak.writestr(path, text)
    output.seek(0)
    return zipfile.ZipFile(output)


class BuffCatalogTests(unittest.TestCase):
    def test_alias_inherits_actual_item_buff(self):
        with archive() as pak:
            data = GENERATOR.extract(pak)
        self.assertEqual(data["itemBuffs"], [(ITEM, BUFF), (ALIAS, BUFF)])
        self.assertEqual(data["perkBuffs"], [(PERK, BUFF)])

    def test_missing_and_authored_empty_parameters_stay_distinct(self):
        with archive() as pak:
            missing = GENERATOR.extract(pak)
        with archive({"Libs/Tables/rpg/buff.xml": f'<root><buffs><row buff_id="{BUFF}" implementation="BasicTimed" buff_params="" /></buffs></root>'}) as pak:
            empty = GENERATOR.extract(pak)
        self.assertFalse(missing["buffs"][0][1][2])
        self.assertTrue(empty["buffs"][0][1][2])
        self.assertNotEqual(missing["digest"], empty["digest"])

    def test_authored_parameter_order_and_typo_are_preserved(self):
        parameters = "strenght+4; alchemy*1.5, alchemy%0.5"
        with archive({"Libs/Tables/rpg/buff.xml": f'<root><buffs><row buff_id="{BUFF}" implementation="BasicTimed" buff_params="{parameters}" /></buffs></root>'}) as pak:
            data = GENERATOR.extract(pak)
        self.assertEqual(data["buffs"][0][1][1], parameters)

    def test_duplicate_cross_table_buff_is_refused(self):
        with archive({"Libs/Tables/rpg/buff__dlc.xml": f'<root><buffs><row buff_id="{BUFF}" implementation="Other" /></buffs></root>'}) as pak:
            with self.assertRaisesRegex(ValueError, "Duplicate buff"):
                GENERATOR.extract(pak)

    def test_unknown_item_buff_is_refused(self):
        with archive({"Libs/Tables/item/item.xml": f'<root><ItemClasses><Food Id="{ITEM}" BuffDefinitionId="{SECOND_BUFF}" /></ItemClasses></root>'}) as pak:
            with self.assertRaisesRegex(ValueError, "Unknown item buff"):
                GENERATOR.extract(pak)

    def test_unknown_perk_effect_is_refused(self):
        with archive({"Libs/Tables/rpg/perk_buff.xml": f'<root><perk_buffs><row perk_id="{PERK}" buff_id="{SECOND_BUFF}" /></perk_buffs></root>'}) as pak:
            with self.assertRaisesRegex(ValueError, "Unknown perk buff"):
                GENERATOR.extract(pak)

    def test_unknown_remap_target_is_refused(self):
        with archive({"Libs/Tables/rpg/perk_buff_override.xml": f'<root><perk_buff_overrides><row perk_id="{PERK}" source_buff_id="{BUFF}" target_buff_id="{SECOND_BUFF}" /></perk_buff_overrides></root>'}) as pak:
            with self.assertRaisesRegex(ValueError, "Unknown buff remap"):
                GENERATOR.extract(pak)

    def test_alias_cycle_is_refused(self):
        xml = f'<root><ItemClasses><ItemAlias Id="{ITEM}" SourceItemId="{ALIAS}" /><ItemAlias Id="{ALIAS}" SourceItemId="{ITEM}" /></ItemClasses></root>'
        with archive({"Libs/Tables/item/item.xml": xml}) as pak:
            with self.assertRaisesRegex(ValueError, "ItemAlias cycle"):
                GENERATOR.extract(pak)

    def test_unproven_alias_override_is_refused(self):
        xml = f'<root><ItemClasses><Food Id="{ITEM}" BuffDefinitionId="{BUFF}" /><ItemAlias Id="{ALIAS}" SourceItemId="{ITEM}" BuffDefinitionId="{BUFF}" /></ItemClasses></root>'
        with archive({"Libs/Tables/item/item.xml": xml}) as pak:
            with self.assertRaisesRegex(ValueError, "Unproven ItemAlias"):
                GENERATOR.extract(pak)

    def test_generated_cpp_is_deterministic_and_escapes_authored_strings(self):
        xml = f'<root><buffs><row buff_id="{BUFF}" implementation="BasicTimed" buff_params="quoted&quot;\\parameter" /></buffs></root>'
        with archive({"Libs/Tables/rpg/buff.xml": xml}) as pak:
            data = GENERATOR.extract(pak)
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent) as folder:
            first, second = Path(folder) / "first.pending", Path(folder) / "second.pending"
            GENERATOR.generate(data, first)
            GENERATOR.generate(data, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertIn("\\\\parameter", first.read_text())
            self.assertIn('\\"', first.read_text())


if __name__ == "__main__":
    unittest.main()
