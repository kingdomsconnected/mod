import unittest
import xml.etree.ElementTree as ET
from generate_cook_pot_catalog import collect, food_items, pot_consumables
from io import BytesIO
from zipfile import ZipFile


class CatalogTests(unittest.TestCase):
    def test_poison_catalog_uses_ingested_effect_and_native_nutrition(self):
        data = BytesIO()
        with ZipFile(data, 'w') as archive:
            archive.writestr('Libs/Tables/item/item.xml', '<ItemClasses><Food Id="meal" NutritionBenefit="15"/><Poison Id="poison" BuffDefinitionId="ingested" WeaponBuffDefinitionId="weapon"/><Food Id="potion" BuffDefinitionId="other"/></ItemClasses>')
        with ZipFile(data) as archive:
            self.assertEqual(pot_consumables(archive, ['meal']), ([15], [('poison', 'ingested')]))

    def fixture(self):
        return list(ET.fromstring('''<Entities>
          <Entity EntityClass="FireplaceSmartObject" EntityId="1" EntityGuid="01020304-0506-0708">
            <EntityLinks>
              <Link Name="kettleTrigger['kettle']" TargetId="2" TargetGuid="00000000-0000-0000"/>
              <Link Name="proceduralObject['cauldron']" TargetId="3"/>
              <Link Name="proceduralObject['fire']" TargetId="4"/>
              <Link Name="proceduralObject['steam']" TargetId="5"/>
            </EntityLinks>
          </Entity>
          <Entity EntityClass="KettleActionTrigger" EntityId="2" EntityGuid="02020304-0506-0708" Pos="10,20,30"/>
          <Entity EntityClass="ProceduralObject" EntityId="3" EntityGuid="03020304-0506-0708"/>
          <Entity EntityClass="RuntimePrefab" EntityId="4" EntityGuid="04020304-0506-0708"/>
          <Entity EntityClass="RuntimePrefab" EntityId="5" EntityGuid="05020304-0506-0708"/>
        </Entities>'''))

    def test_runtime_guid_encoding_and_id_fallback(self):
        row, = collect(self.fixture())
        self.assertEqual(row[0], 0x0708050601020304)
        self.assertEqual(row[3:], ((10, 20, 30), 'goulash', False, True, 0x0708050604020304, 0x0708050605020304))

    def test_authored_food_home_and_empty(self):
        entities = self.fixture()
        props = ET.SubElement(entities[0], 'Properties')
        ET.SubElement(props, 'Fireplace', esFireplaceCauldronType='soup', bFullOnInit='0')
        ET.SubElement(props, 'Script', Misc='isHomeFireplace:true')
        row, = collect(entities)
        self.assertEqual(row[4:7], ('soup', True, False))

    def test_fire_only_camp_is_not_a_pot(self):
        entities = self.fixture()
        entities[0].remove(entities[0].find('EntityLinks'))
        self.assertEqual(collect(entities), [])

    def test_broken_links_are_not_silently_omitted(self):
        with self.assertRaises(ValueError):
            collect(self.fixture()[:1])

    def test_food_items_follow_the_smart_object_script(self):
        # Synthetic script with distinct IDs to check named and fallback order.
        script = """function FireplaceSmartObject:GetEatItemClassId()
    if meal == 'lentil' then
        return '00000000-0000-0000-0000-000000000002'
    elseif meal == 'goulash' then
        return '00000000-0000-0000-0000-000000000001'
    else
        return '00000000-0000-0000-0000-000000000003'
    end
end
"""
        self.assertEqual(food_items(script), ['00000000-0000-0000-0000-000000000001', '00000000-0000-0000-0000-000000000002', '00000000-0000-0000-0000-000000000003'])


if __name__ == '__main__':
    unittest.main()
