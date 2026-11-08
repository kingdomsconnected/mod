"""Verify native defaults survive sparse tables and authored rules override them."""
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('soul_rules_generator', ROOT / 'tools/generate_soul_rule_catalog.py')
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)
PERK = '11111111-1111-1111-1111-111111111111'


def archive(parameters='', abilities=''):
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, 'w') as pak:
        pak.writestr('Libs/Tables/rpg/rpg_param.xml', f'<root><rpg_params>{parameters}</rpg_params></root>')
        pak.writestr('Libs/Tables/rpg/perk_soul_ability.xml', f'<root><perk_soul_abilitys>{abilities}</perk_soul_abilitys></root>')
    buffer.seek(0)
    return zipfile.ZipFile(buffer)


class SoulRuleCatalogTests(unittest.TestCase):
    def test_sparse_table_keeps_native_constructor_defaults(self):
        with archive() as pak:
            values = dict(GENERATOR.extract(pak)['parameters'])
        self.assertEqual(values['SkillCap'], 30)
        self.assertEqual(values['SkillXPLevelBase'], 20)
        self.assertEqual(values['SkillXPLevelDiff'], 30)
        self.assertEqual(values['AlchemyXPOverride'], 1)
        self.assertEqual(values['AlchemyXPPerFailedBrewing'], 5)
        self.assertEqual(values['AlchemyRecipeStepsTolerance'], 2)
        self.assertEqual(values['AlchemyToleranceBase'], .2)
        self.assertEqual(values['AlchemyToleranceMaxLevel'], .7)
        self.assertEqual(values['BlackArtsApprenticeBuffStartTime'], 0)
        self.assertEqual(values['BlackArtsApprenticeBuffEndTime'], 4.5)
        self.assertEqual(values['SurvivalXpRewardPerDriedItem'], 15)
        self.assertEqual(values['SurvivalXpRewardPerSmokedItem'], 15)
        self.assertEqual(values['SurvivalXpRewardPerCookedItem'], 10)

    def test_authored_values_override_without_overwriting_absent_defaults(self):
        with archive('<row rpg_param_key="AlchemyRecipeStepsTolerance" rpg_param_value="4"/><row rpg_param_key="AlchemyDryHerbQuality" rpg_param_value="0.3"/>') as pak:
            values = dict(GENERATOR.extract(pak)['parameters'])
        self.assertEqual(values['AlchemyRecipeStepsTolerance'], 4)
        self.assertEqual(values['AlchemyDryHerbQuality'], .3)
        self.assertEqual(values['AlchemyTrialEndErrorPerkTolerance'], 1)
        self.assertEqual(values['AlchemyFreshHerbQuality'], 1)

    def test_black_arts_window_uses_authored_override_when_present(self):
        with archive('<row rpg_param_key="BlackArtsApprenticeBuffStartTime" rpg_param_value="22"/><row rpg_param_key="BlackArtsApprenticeBuffEndTime" rpg_param_value="3"/>') as pak:
            values = dict(GENERATOR.extract(pak)['parameters'])
        self.assertEqual(values['BlackArtsApprenticeBuffStartTime'], 22)
        self.assertEqual(values['BlackArtsApprenticeBuffEndTime'], 3)

    def test_invalid_numeric_rules_fail_generation(self):
        for name, value in [('SkillCap', '1.5'), ('AlchemyToleranceBase', '1.1'), ('AlchemyDryHerbQuality', '-1'), ('AlchemyFreshHerbQuality', 'nan'), ('AlchemyXPOverride', 'inf')]:
            with self.subTest(name=name), archive(f'<row rpg_param_key="{name}" rpg_param_value="{value}"/>') as pak:
                with self.assertRaises(ValueError):
                    GENERATOR.extract(pak)

    def test_duplicate_ability_edge_cannot_grant_twice(self):
        edge = f'<row perk_id="{PERK}" soul_ability_id="63"/>'
        with archive(abilities=edge + edge) as pak:
            with self.assertRaisesRegex(ValueError, 'Duplicate perk ability'):
                GENERATOR.extract(pak)

    def test_rule_and_ability_changes_are_digest_bound(self):
        with archive() as pak:
            baseline = GENERATOR.extract(pak)
        with archive(abilities=f'<row perk_id="{PERK}" soul_ability_id="63"/>') as pak:
            changed = GENERATOR.extract(pak)
        self.assertNotEqual(baseline['digest'], changed['digest'])
        self.assertEqual(changed['abilities'], [(PERK, 63)])

    def test_cpp_generation_is_deterministic(self):
        with archive() as pak:
            data = GENERATOR.extract(pak)
        with tempfile.TemporaryDirectory(dir=ROOT) as temporary:
            first, second = Path(temporary) / 'one.cpp.pending', Path(temporary) / 'two.cpp.pending'
            GENERATOR.generate(data, first)
            GENERATOR.generate(data, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertIn('{"AlchemyXPOverride", 1.0F}', first.read_text())


if __name__ == '__main__':
    unittest.main()
