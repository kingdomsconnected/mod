#include "shared/game/inventory_property_classes.h"
#include "shared/game/item_classes.h"
#include "shared/game/mod_item_tables.h"
#include "shared/xml/mini_xml.h"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

// The item classes a streamed KCD2 mod adds: read from its table patch by the catalog generators'
// rules, held by the session registry, and answered by the lookups every inventory operation uses.

namespace {
    void Require(bool value, const char *message) {
        if (!value) {
            throw std::runtime_error(message);
        }
    }

    constexpr const char *kTable = R"(<?xml version="1.0" encoding="us-ascii"?>
<!-- a mod's patch -->
<database xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" name="barbora" xsi:noNamespaceSchemaLocation="item.xsd">
	<ItemClasses>
		<Hood MaxQuality="4" UiSound="ui_inv_item_clothes" Clothing="Hood11_m12_m01" UIName="ui_nm_hood11_m01" Price="1099" Id="06EA1F05-277B-4096-BE3E-00B32F9D872B" Name="Hood11_m12_C_DarkKnight" />
		<Armor MaxQuality="4" Clothing="Spurs01_m04_m01" Price="8000" Id="81912e06-3a19-438f-ba3b-01ed492b9d97" Name="Spurs01_m04_C_DarkKnight" />
		<Armor MaxQuality="4" Clothing="Spurs01_m01" Price="10" Id="81912e06-3a19-438f-ba3b-01ed492b9d9a" Name="belt_stock_component" />
		<Armor MaxQuality="4" Clothing="ModBelt01" Price="10" Id="81912e06-3a19-438f-ba3b-01ed492b9d98" Name="belt_dark" />
		<Armor MaxQuality="4" Clothing="ModCoat01" Price="10" Id="81912e06-3a19-438f-ba3b-01ed492b9d99" Name="coat_dark" />
		<MeleeWeapon IsBreakable="true" MaxQuality="2" Price="40" UIName="ui_nm_axe &amp; co" Id="1d1fc948-1d92-4c93-8f66-6e9e7b53ab15" Name="axe_dark">
			<Phase index="0" />
		</MeleeWeapon>
		<Food IsDivisible="true" Price="3" Id="c0de0001-6466-4728-b551-6f645d40a76d" Name="bread_dark" />
		<Ammo Id="c0de0002-ee38-4d67-9a93-ac1ec288f89c" Name="arrow_dark" Price="12" />
		<Document IsQuestItem="true" Id="c0de0003-77d1-4686-801e-871b49440d7d" Name="letter_dark" />
		<ItemAlias SourceItemId="1d1fc948-1d92-4c93-8f66-6e9e7b53ab15" IsQuestItem="false" Id="c0de0004-859e-4c3d-b812-16ead6f38da8" Name="axe_dark_alias" />
		<ItemAlias SourceItemId="dfea5d01-b25c-414a-9ab4-6911a5f82118" IsQuestItem="false" Id="c0de0005-3a19-438f-ba3b-01ed492b9d93" Name="arrow_crude_alias" />
		<Hood Id="not-a-guid" Name="broken" />
		<Hood Id="81912e06-3a19-438f-ba3b-01ed492b9d94" />
		<Planet Id="81912e06-3a19-438f-ba3b-01ed492b9d95" Name="unknown_kind" />
		<Armor MaxQuality="9" Id="81912e06-3a19-438f-ba3b-01ed492b9d96" Name="bad_quality" />
	</ItemClasses>
</database>)";

    constexpr const char *kComponents = R"(<database name="barbora">
	<CharacterComponents>
		<Component Name="Clothing">
			<DerivedComponents>
				<Clothing Name="Belt" ArmorType="Belt">
					<DerivedComponents>
						<Clothing Name="ModBelt01" />
					</DerivedComponents>
				</Clothing>
				<Clothing Name="Coat" ArmorType="Coat">
					<DerivedComponents>
						<Clothing Name="ModCoat01" />
					</DerivedComponents>
				</Clothing>
				<Clothing Name="Spurs">
					<DerivedComponents>
						<Clothing Name="Spurs01_m04">
							<DerivedComponents>
								<Clothing Name="Spurs01_m04_m01" />
							</DerivedComponents>
						</Clothing>
					</DerivedComponents>
				</Clothing>
			</DerivedComponents>
		</Component>
	</CharacterComponents>
</database>)";

    KCDC::Shared::Game::ModClothingTypes Clothing() {
        KCDC::Shared::Game::ModClothingTypes clothing;
        std::string error;
        Require(KCDC::Shared::Game::ParseModClothingTypes(kComponents, clothing, error), "the component patch parses");
        return clothing;
    }

    void XmlTests() {
        KCDC::Shared::Xml::Element root;
        std::string error;
        Require(KCDC::Shared::Xml::Parse(kTable, root, error), "the table parses");
        Require(root.tag == "database" && root.Attribute("name") == "barbora", "the root and its attributes are read");
        const KCDC::Shared::Xml::Element *classes = root.Child("ItemClasses");
        Require(classes != nullptr && classes->children.size() == 15, "every direct child of ItemClasses is an element");
        Require(classes->children[5].children.size() == 1 && classes->children[5].Attribute("UIName") == "ui_nm_axe & co", "nested elements and entities are handled");
        Require(!KCDC::Shared::Xml::Parse("<a><b></a>", root, error) && !error.empty(), "a mismatched end tag is refused");
        Require(KCDC::Shared::Xml::Parse("\xEF\xBB\xBF<a x='1'/>", root, error) && root.Attribute("x") == "1", "a byte order mark and single quotes are fine");

        const auto nested = [](std::size_t depth) {
            std::string text;
            for (std::size_t i = 0; i < depth; ++i) {
                text += "<a>";
            }
            for (std::size_t i = 0; i < depth; ++i) {
                text += "</a>";
            }
            return text;
        };
        Require(KCDC::Shared::Xml::Parse(nested(256), root, error), "elements nest as deep as the limit");
        Require(!KCDC::Shared::Xml::Parse(nested(200000), root, error) && error.find("nested too deeply") != std::string::npos, "a document nested past the limit is refused rather than run off the stack");
    }

    void DeriveTests() {
        const KCDC::Shared::Game::ModClothingTypes clothing = Clothing();
        Require(clothing.size() == 7 && clothing.at("ModBelt01").armorType == "Belt" && clothing.at("ModCoat01").armorType == "Coat", "components inherit the armor type down the tree");
        Require(clothing.at("Spurs01_m04_m01").armorType.empty() && clothing.at("Spurs01_m04_m01").parent == "Spurs01_m04", "a patch that repeats the game's tree by name alone records the parent");
        Require(KCDC::Shared::Game::IsImmutableModClothing(clothing, "Spurs01_m04_m01") && KCDC::Shared::Game::IsImmutableModClothing(clothing, "Spurs01_m01") && !KCDC::Shared::Game::IsImmutableModClothing(clothing, "ModCoat01") && !KCDC::Shared::Game::IsImmutableModClothing(clothing, "Coat12_m02_m01"), "immutability resolves through the patch's parents and the game's table");

        std::vector<KCDC::Shared::Game::ModItemClass> items;
        std::string error;
        Require(KCDC::Shared::Game::ParseModItemTable(kTable, clothing, items, error), "the patch is read");
        Require(items.size() == 11, "the eleven well-formed classes are kept and the four broken ones dropped");

        const auto &spurs = items[1];
        Require(spurs.maximumQuality == 1 && !spurs.canHealthChange && spurs.equippable, "a mod component under the game's spurs never changes condition, whatever MaxQuality says");
        const auto &stockBelt = items[2];
        Require(stockBelt.maximumQuality == 1 && !stockBelt.canHealthChange, "a shipped immutable component is known from the compiled table");
        const auto &belt = items[3];
        Require(belt.maximumQuality == 1 && !belt.canHealthChange, "a mod-defined component under an immutable armor type is immutable too");
        const auto &coat = items[4];
        Require(coat.maximumQuality == 4 && coat.canHealthChange, "a mod-defined component under a repairable type keeps its quality range");
        items.erase(items.begin() + 1, items.begin() + 5);

        const auto &hood = items[0];
        Require(hood.name == "Hood11_m12_C_DarkKnight" && hood.guid == "06ea1f05-277b-4096-be3e-00b32f9d872b" && hood.category == "Hood" && hood.price == 1099 && hood.nameKey == "ui_nm_hood11_m01", "a hood keeps the catalog fields, its GUID lower-cased");
        Require(hood.maximumQuality == 4 && hood.canHealthChange && hood.equippable && !hood.breakableWeapon && !hood.questItem && !hood.nativeStackable, "armor is repairable gear of the authored quality");

        const auto &axe = items[1];
        Require(axe.maximumQuality == 2 && axe.breakableWeapon && axe.equippable && axe.canHealthChange, "a weapon reads MaxQuality and IsBreakable");
        const auto &bread = items[2];
        Require(bread.maximumQuality == 1 && bread.canHealthChange && !bread.equippable && bread.nativeStackable, "food is quality 1, mutable, and stacks when divisible");
        const auto &arrow = items[3];
        Require(arrow.maximumQuality == 1 && !arrow.canHealthChange && arrow.equippable && arrow.nativeStackable, "ammo is immutable, equippable and stacks");
        const auto &letter = items[4];
        Require(letter.questItem && !letter.equippable && !letter.nativeStackable, "a quest document is a quest item");
        const auto &alias = items[5];
        Require(alias.aliasOf == "1d1fc948-1d92-4c93-8f66-6e9e7b53ab15" && !alias.equippable, "an alias records what it names, and copies it only once the registry resolves it");
        const auto &crudeAlias = items[6];
        Require(crudeAlias.aliasOf == "dfea5d01-b25c-414a-9ab4-6911a5f82118", "an alias of a shipped class records the shipped GUID");

        std::vector<KCDC::Shared::Game::ModItemClass> empty;
        Require(!KCDC::Shared::Game::ParseModItemTable("<database/>", clothing, empty, error) && error == "no <ItemClasses> element", "a file without ItemClasses is refused");
        Require(!KCDC::Shared::Game::ParseModItemTable("<database><ItemClasses>", clothing, empty, error), "broken XML is refused");
    }

    void RegistryTests() {
        auto &registry = KCDC::Shared::Game::ModItemRegistry::Get();
        registry.Reset();
        std::vector<KCDC::Shared::Game::ModItemClass> items;
        std::string error;
        KCDC::Shared::Game::ParseModItemTable(kTable, Clothing(), items, error);
        items.erase(items.begin() + 1, items.begin() + 5);
        const std::uint64_t before = registry.Revision();
        registry.Replace("dark-knight", items);
        Require(registry.Size() == 7 && registry.Revision() > before, "the source's classes are held");

        const auto *hood = KCDC::Shared::Game::FindItemClass("06ea1f05-277b-4096-be3e-00b32f9d872b");
        Require(hood != nullptr && hood->name == "Hood11_m12_C_DarkKnight" && hood->category == "Hood", "the catalog lookup answers a mod's class by GUID");
        Require(KCDC::Shared::Game::FindItemClass(KCDC::Shared::Game::ItemClassIdFromGuid("06ea1f05-277b-4096-be3e-00b32f9d872b")) == hood, "and by its wire id");
        Require(KCDC::Shared::Game::FindItemClassByName("Hood11_m12_C_DarkKnight") == hood, "and by name");
        Require(KCDC::Shared::Game::FindItemClassByName("arrow_crude") != nullptr && KCDC::Shared::Game::FindItemClassByName("arrow_crude")->guid == "dfea5d01-b25c-414a-9ab4-6911a5f82118", "a shipped name still resolves to the shipped class");
        const auto *properties = KCDC::Shared::Game::FindInventoryPropertyClass("06ea1f05-277b-4096-be3e-00b32f9d872b");
        Require(properties != nullptr && properties->maximumQuality == 4 && properties->equippable, "the property policy sees the mod's class");
        const auto *alias = KCDC::Shared::Game::FindInventoryPropertyClass("c0de0004-859e-4c3d-b812-16ead6f38da8");
        Require(alias != nullptr && alias->maximumQuality == 2 && alias->breakableWeapon && alias->equippable && !alias->questItem, "an alias of a sibling copies the sibling's constraints");
        const auto *crudeAlias = KCDC::Shared::Game::FindInventoryPropertyClass("c0de0005-3a19-438f-ba3b-01ed492b9d93");
        Require(crudeAlias != nullptr && crudeAlias->equippable && crudeAlias->nativeStackable && !crudeAlias->canHealthChange, "an alias of a shipped class copies the shipped constraints");

        std::size_t counted = 0;
        KCDC::Shared::Game::ForEachItemClass([&](const KCDC::Shared::Game::ItemClassEntry &entry) { counted += entry.name == "axe_dark" ? 1 : 0; });
        Require(counted == 1, "the enumeration includes the mod's classes once");

        // A contested GUID belongs to the source whose name sorts first, whichever came first; the
        // other source's copy waits, and answers once the first is gone.
        std::vector<KCDC::Shared::Game::ModItemClass> rival = {items[0]};
        rival[0].name                                       = "impostor";
        registry.Replace("zz-other", rival);
        Require(KCDC::Shared::Game::FindItemClass("06ea1f05-277b-4096-be3e-00b32f9d872b") == hood, "a source sorting later neither takes a contested GUID nor moves the entry");
        registry.Replace("aa-other", rival);
        Require(KCDC::Shared::Game::FindItemClass("06ea1f05-277b-4096-be3e-00b32f9d872b") == hood && hood->name == "impostor", "a source sorting earlier takes it, whatever the order they came in, rewriting the entry callers hold");
        registry.Remove("aa-other");
        Require(KCDC::Shared::Game::FindItemClass("06ea1f05-277b-4096-be3e-00b32f9d872b") == hood && hood->name == "Hood11_m12_C_DarkKnight", "and gives it back when it goes");
        const auto *axe = KCDC::Shared::Game::FindItemClass("1d1fc948-1d92-4c93-8f66-6e9e7b53ab15");
        registry.Remove("zz-other");
        Require(KCDC::Shared::Game::FindItemClass("1d1fc948-1d92-4c93-8f66-6e9e7b53ab15") == axe, "an entry no change touched keeps its address");

        // A changed pak arrives while the one it replaces is still mounted: both hold the same
        // GUIDs, and the newer one's must answer once the older one is closed.
        registry.Replace("dark-knight new.pak", items);
        registry.Remove("dark-knight");
        Require(KCDC::Shared::Game::FindItemClass("06ea1f05-277b-4096-be3e-00b32f9d872b") != nullptr && registry.Size() == 7, "a replacement's classes answer once the source it shadowed is gone");
        registry.Remove("dark-knight new.pak");
        Require(KCDC::Shared::Game::FindItemClass("06ea1f05-277b-4096-be3e-00b32f9d872b") == nullptr && KCDC::Shared::Game::FindItemClass("1d1fc948-1d92-4c93-8f66-6e9e7b53ab15") == nullptr && registry.Size() == 0, "a retired source's classes are gone");
        Require(hood->name == "Hood11_m12_C_DarkKnight" && axe->name == "axe_dark", "what a caller still holds stays readable");
        registry.Replace("dark-knight", items);
        Require(KCDC::Shared::Game::FindItemClass("06ea1f05-277b-4096-be3e-00b32f9d872b") == hood, "a class that comes back answers from the same entry");
        registry.Remove("dark-knight");

        // An alias may name a class later in its file or in another resource.
        KCDC::Shared::Game::ModItemClass forward = items[5];
        forward.guid                             = "2bd9d159-859e-4c3d-b812-16ead6f38da8";
        forward.name                             = "forward_alias";
        forward.aliasOf                          = "3bd9d159-859e-4c3d-b812-16ead6f38da8";
        KCDC::Shared::Game::ModItemClass target  = items[1];
        target.guid                              = "3bd9d159-859e-4c3d-b812-16ead6f38da8";
        target.name                              = "forward_target";
        registry.Replace("b-alias", {forward});
        Require(KCDC::Shared::Game::FindItemClass("2bd9d159-859e-4c3d-b812-16ead6f38da8") == nullptr, "an alias of a class nobody has is not in the catalog");
        registry.Replace("a-target", {target});
        const auto *forwarded = KCDC::Shared::Game::FindInventoryPropertyClass("2bd9d159-859e-4c3d-b812-16ead6f38da8");
        Require(forwarded != nullptr && forwarded->maximumQuality == 2 && forwarded->breakableWeapon, "it joins, with the constraints of the class another source brought");
        registry.Remove("b-alias");
        registry.Replace("a-target", {forward, target});
        Require(KCDC::Shared::Game::FindInventoryPropertyClass("2bd9d159-859e-4c3d-b812-16ead6f38da8") != nullptr, "the same alias ahead of its target in one file resolves too");
        registry.Reset();

        // The client keys a pak by `<resource> <pak>`: `mod` must stay ahead of `mod-extra`, as the
        // server's bare resource names have it.
        KCDC::Shared::Game::ModItemClass first  = items[0];
        KCDC::Shared::Game::ModItemClass second = items[0];
        first.name                              = "from_mod";
        second.name                             = "from_mod_extra";
        registry.Replace("mod-extra c:/cache/b.pak", {second});
        registry.Replace("mod c:/cache/a.pak", {first});
        Require(KCDC::Shared::Game::FindItemClass("06ea1f05-277b-4096-be3e-00b32f9d872b")->name == "from_mod", "a resource whose name prefixes another's keeps a contested GUID on the client too");
        registry.Reset();

        // A mod's row for a class the game ships leaves the shipped entry alone.
        KCDC::Shared::Game::ModItemClass shipped = items[3];
        shipped.guid                             = "dfea5d01-b25c-414a-9ab4-6911a5f82118";
        shipped.name                             = "arrow_crude_patched";
        registry.Replace("balance", {shipped});
        std::size_t listed = 0;
        KCDC::Shared::Game::ForEachItemClass([&](const KCDC::Shared::Game::ItemClassEntry &entry) { listed += entry.guid == "dfea5d01-b25c-414a-9ab4-6911a5f82118" ? 1 : 0; });
        Require(KCDC::Shared::Game::FindItemClass("dfea5d01-b25c-414a-9ab4-6911a5f82118") == KCDC::Shared::Game::FindShippedItemClass("dfea5d01-b25c-414a-9ab4-6911a5f82118") && listed == 1 && registry.Size() == 0, "a shipped GUID keeps its compiled entry and is listed once");
        registry.Reset();
        Require(registry.Size() == 0 && KCDC::Shared::Game::FindItemClassByName("impostor") == nullptr, "reset empties it");
    }
} // namespace

int RunModItemTableSuite() {
    try {
        XmlTests();
        DeriveTests();
        RegistryTests();
    }
    catch (const std::exception &failure) {
        std::cerr << "FAILED: " << failure.what() << '\n';
        return 1;
    }
    std::cout << "mod item table tests passed\n";
    return 0;
}
