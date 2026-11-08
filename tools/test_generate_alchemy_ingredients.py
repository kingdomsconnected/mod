import io
import unittest
import zipfile

from generate_alchemy_ingredients import extract, extract_crushable


def catalog(body, extra=None):
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, 'w') as archive:
        archive.writestr('Libs/Tables/item/item.xml', '<database><ItemClasses>' + body + '</ItemClasses></database>')
        if extra is not None:
            archive.writestr('Libs/Tables/item/item__dlc.xml', '<database><ItemClasses>' + extra + '</ItemClasses></database>')
    buffer.seek(0)
    return zipfile.ZipFile(buffer)


class AlchemyIngredientCatalogTests(unittest.TestCase):
    def test_crushable_catalog_uses_exact_native_descriptor_guids(self):
        first = '0513eeae-05a8-4ca7-8719-443cb0d906d5'
        second = '55be7c8b-7ef1-4e45-820d-d04a2497f016'
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, 'w') as archive:
            archive.writestr('Libs/Tables/minigame/AlchemyCrushableSpecialIngredient.xml', '<database><AlchemyCrushableSpecialIngredients><AlchemyCrushableSpecialIngredient ItemId="' + second + '"/><AlchemyCrushableSpecialIngredient ItemId="' + first + '"/></AlchemyCrushableSpecialIngredients></database>')
        buffer.seek(0)
        with zipfile.ZipFile(buffer) as archive:
            self.assertEqual(extract_crushable(archive), [first, second])

    def test_crushable_duplicate_or_unproved_identifier_refused(self):
        for body in ('<AlchemyCrushableSpecialIngredient ItemId="fake"/>', '<AlchemyCrushableSpecialIngredient ItemId="0513eeae-05a8-4ca7-8719-443cb0d906d5"/>' * 2):
            buffer = io.BytesIO()
            with zipfile.ZipFile(buffer, 'w') as archive:
                archive.writestr('Libs/Tables/minigame/AlchemyCrushableSpecialIngredient.xml', '<database><AlchemyCrushableSpecialIngredients>' + body + '</AlchemyCrushableSpecialIngredients></database>')
            buffer.seek(0)
            with zipfile.ZipFile(buffer) as archive, self.assertRaises(ValueError):
                extract_crushable(archive)

    def test_category_decay_and_aliases(self):
        with catalog('<Herb Id="fresh" DecayTime="72" DriedItemId="dry"/><Herb Id="dry" DecayTime="0"/>'
                     '<Herb Id="ginger" DecayTime="0"/><Food Id="food" DecayTime="72"/>'
                     '<ItemAlias Id="alias" SourceItemId="fresh"/>') as archive:
            self.assertEqual(extract(archive), [('alias', True), ('dry', False), ('fresh', True), ('ginger', False)])

    def test_decay_override_is_unsupported(self):
        with catalog('<Herb Id="fresh" DecayTime="72"/><ItemAlias Id="alias" SourceItemId="fresh" DecayTime="0"/>') as archive:
            with self.assertRaisesRegex(ValueError, 'override'):
                extract(archive)

    def test_cycle_and_duplicates_fail(self):
        with catalog('<ItemAlias Id="a" SourceItemId="b"/><ItemAlias Id="b" SourceItemId="a"/>') as archive:
            with self.assertRaisesRegex(ValueError, 'cycle'):
                extract(archive)
        with catalog('<Herb Id="a" DecayTime="72"/>', '<Herb Id="a" DecayTime="0"/>') as archive:
            with self.assertRaisesRegex(ValueError, 'Duplicate'):
                extract(archive)

    def test_invalid_or_unrecorded_decay_fails(self):
        for value in ('-1', 'nan', 'inf'):
            with self.subTest(value=value), catalog('<Herb Id="a" DecayTime="' + value + '"/>') as archive:
                with self.assertRaisesRegex(ValueError, 'Invalid'):
                    extract(archive)
        with catalog('<Herb Id="a"/>') as archive:
            with self.assertRaises(KeyError):
                extract(archive)


if __name__ == '__main__':
    unittest.main()
