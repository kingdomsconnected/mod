#include "prop_editor_world_objects.h"

#include "shared/features/door/door_definition.h"
#include "shared/features/ground_item/ground_item_definition.h"
#include "shared/features/smart_object/smart_object_entity.h"
#include "shared/game/inventory_property_classes.h"
#include "shared/game/item_classes.h"
#include "shared/game/prop_models.h"
#include "shared/game/smart_object_blueprints.h"

#include <array>
#include <cstdio>

namespace KCDC::Features::Prop {
    namespace {
        struct KindNames {
            PlacedKind kind;
            const char *name;
            const char *prefix;
            const char *array;
            const char *modelKey; // the field the shared definition names its model by
        };

        constexpr std::array<KindNames, 3> kWorldKinds {{
            {PlacedKind::Door, "door", "door:", "doors", "model"},
            {PlacedKind::SmartObject, "smartObject", "smart:", "smartObjects", "blueprint"},
            {PlacedKind::Item, "item", "item:", "items", "item"},
        }};

        const KindNames *NamesOf(PlacedKind kind) {
            for (const KindNames &names : kWorldKinds) {
                if (names.kind == kind) {
                    return &names;
                }
            }
            return nullptr;
        }

        nlohmann::json Entry(PlacedKind kind, const std::string &model, const nlohmann::json &settings, const SDK::Vec3 &position, const SDK::Quat &rotation) {
            nlohmann::json entry           = settings.is_object() ? settings : nlohmann::json::object();
            entry[NamesOf(kind)->modelKey] = model;
            entry["position"]              = nlohmann::json::array({position.x, position.y, position.z});
            entry["rotation"]              = nlohmann::json::array({rotation.x, rotation.y, rotation.z, rotation.w});
            return entry;
        }

        /** The definition's canonical entry, with the fields a record keeps elsewhere taken out. */
        nlohmann::json SettingsOf(nlohmann::json entry, PlacedKind kind) {
            entry.erase(NamesOf(kind)->modelKey);
            entry.erase("position");
            entry.erase("rotation");
            return entry;
        }

        /** The `interactive` folder a blueprint sits in. */
        const char *InteractiveFolder(Shared::Game::SmartObjectKind kind) {
            switch (kind) {
            case Shared::Game::SmartObjectKind::Seat: return "seats";
            case Shared::Game::SmartObjectKind::Bed: return "beds";
            case Shared::Game::SmartObjectKind::Dice: return "games";
            default: return "stations";
            }
        }

        /** Reads one entry through the shared definition, canonical model and settings out. */
        bool Read(PlacedKind kind, const nlohmann::json &entry, std::string &model, nlohmann::json &settings, glm::vec3 &position, glm::quat &rotation, std::string &error) {
            switch (kind) {
            case PlacedKind::Door: {
                Shared::Game::DoorDefinition door;
                if (!Shared::Game::DoorDefinitionFromJson(entry, door, error)) {
                    return false;
                }
                door.model = Shared::Game::g_DoorModels[Shared::Game::ResolveDoorModel(door.model)].path;
                model      = door.model;
                position   = door.position;
                rotation   = door.rotation;
                settings   = SettingsOf(Shared::Game::DoorDefinitionToJson(door), kind);
                return true;
            }
            case PlacedKind::SmartObject: {
                Shared::Game::SmartObjectDefinition object;
                if (!Shared::Game::SmartObjectDefinitionFromJson(entry, object, error)) {
                    return false;
                }
                model    = object.blueprint;
                position = object.position;
                rotation = object.rotation;
                settings = SettingsOf(Shared::Game::SmartObjectDefinitionToJson(object), kind);
                return true;
            }
            case PlacedKind::Item: {
                Shared::Game::ItemPlacementDefinition item;
                if (!Shared::Game::ItemPlacementDefinitionFromJson(entry, item, error)) {
                    return false;
                }
                // A shipped class by its GUID, which no rename or same-named class changes.
                if (const Shared::Game::ItemClassEntry *shipped = Shared::Game::FindItemClass(item.item); shipped != nullptr) {
                    item.item = std::string(shipped->guid);
                }
                else if (const Shared::Game::ItemClassEntry *named = Shared::Game::FindItemClassByName(item.item); named != nullptr) {
                    item.item = std::string(named->guid);
                }
                model    = item.item;
                position = item.position;
                rotation = item.rotation;
                settings = SettingsOf(Shared::Game::ItemPlacementDefinitionToJson(item), kind);
                return true;
            }
            case PlacedKind::Prop: break;
            }
            error = "a prop has no settings";
            return false;
        }
    } // namespace

    const char *PlacedKindName(PlacedKind kind) {
        const KindNames *names = NamesOf(kind);
        return names != nullptr ? names->name : "prop";
    }

    std::optional<PlacedKind> PlacedKindFromName(std::string_view name) {
        if (name == "prop") {
            return PlacedKind::Prop;
        }
        for (const KindNames &names : kWorldKinds) {
            if (name == names.name) {
                return names.kind;
            }
        }
        return std::nullopt;
    }

    const char *WorldObjectArray(PlacedKind kind) {
        const KindNames *names = NamesOf(kind);
        return names != nullptr ? names->array : "objects";
    }

    std::string WorldObjectKey(PlacedKind kind, std::string_view model) {
        const KindNames *names = NamesOf(kind);
        return names != nullptr ? names->prefix + std::string(model) : std::string(model);
    }

    PlacedKind SplitWorldObjectKey(std::string_view key, std::string &model) {
        for (const KindNames &names : kWorldKinds) {
            const std::string_view prefix(names.prefix);
            if (key.substr(0, prefix.size()) == prefix) {
                model = std::string(key.substr(prefix.size()));
                return names.kind;
            }
        }
        model = std::string(key);
        return PlacedKind::Prop;
    }

    bool NormalizeWorldObject(PlacedKind kind, std::string &model, nlohmann::json &settings, std::string &error) {
        if (NamesOf(kind) == nullptr) {
            error = "a prop has no settings";
            return false;
        }
        if (!settings.is_null() && !settings.is_object()) {
            error = "settings must be an object";
            return false;
        }
        glm::vec3 position {0.0F};
        glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};
        std::string canonical;
        nlohmann::json clean;
        if (!Read(kind, Entry(kind, model, settings, SDK::Vec3 {}, SDK::Quat {0.0F, 0.0F, 0.0F, 1.0F}), canonical, clean, position, rotation, error)) {
            return false;
        }
        model    = std::move(canonical);
        settings = std::move(clean);
        return true;
    }

    bool WorldObjectStandsBare(const nlohmann::json &settings) {
        const auto bare = settings.is_object() ? settings.find("bare") : settings.end();
        return bare != settings.end() && bare->is_boolean() && bare->get<bool>();
    }

    bool WorldObjectTakesBare(PlacedKind kind, const std::string &model) {
        const Shared::Game::SmartObjectBlueprint *blueprint = kind == PlacedKind::SmartObject ? Shared::Game::FindSmartObjectBlueprint(model) : nullptr;
        return blueprint != nullptr && Shared::Game::SmartObjectStandsBare(*blueprint);
    }

    nlohmann::json WorldObjectToJson(PlacedKind kind, const std::string &model, const nlohmann::json &settings, const SDK::Vec3 &position, const SDK::Quat &rotation) {
        return NamesOf(kind) != nullptr ? Entry(kind, model, settings, position, rotation) : nlohmann::json::object();
    }

    bool WorldObjectFromJson(PlacedKind kind, const nlohmann::json &entry, std::string &model, nlohmann::json &settings, SDK::Vec3 &position, SDK::Quat &rotation, std::string &error) {
        if (NamesOf(kind) == nullptr) {
            error = "a prop is not a world object";
            return false;
        }
        glm::vec3 at {0.0F};
        glm::quat turn {1.0F, 0.0F, 0.0F, 0.0F};
        if (!Read(kind, entry, model, settings, at, turn, error)) {
            return false;
        }
        position = SDK::Vec3 {at.x, at.y, at.z};
        rotation = SDK::Quat {turn.x, turn.y, turn.z, turn.w};
        return true;
    }

    std::string WorldObjectName(PlacedKind kind, const std::string &model) {
        switch (kind) {
        case PlacedKind::Door:
        case PlacedKind::SmartObject: {
            std::string_view stem(model);
            if (const std::size_t slash = stem.find_last_of("/\\"); slash != std::string_view::npos) {
                stem.remove_prefix(slash + 1);
            }
            if (const std::size_t dot = stem.find_last_of('.'); kind == PlacedKind::Door && dot != std::string_view::npos) {
                stem = stem.substr(0, dot);
            }
            return std::string(stem);
        }
        case PlacedKind::Item: {
            const Shared::Game::ItemClassEntry *item = Shared::Game::FindItemClass(model);
            return item != nullptr ? std::string(item->name) : model;
        }
        case PlacedKind::Prop: break;
        }
        return model;
    }

    const std::vector<WorldObjectRow> &WorldObjectRows() {
        static const std::vector<WorldObjectRow> rows = [] {
            std::vector<WorldObjectRow> built;
            built.reserve(Shared::Game::g_DoorModels.size() + Shared::Game::g_SmartObjectBlueprints.size() + Shared::Game::g_ItemClasses.size());
            for (const Shared::Game::DoorModel &door : Shared::Game::g_DoorModels) {
                char detail[32];
                std::snprintf(detail, sizeof(detail), "door, %u in levels", static_cast<unsigned>(door.placements));
                built.push_back(WorldObjectRow {PlacedKind::Door, door.path, WorldObjectName(PlacedKind::Door, door.path), {"doors"}, detail});
            }
            for (const Shared::Game::SmartObjectBlueprint &blueprint : Shared::Game::g_SmartObjectBlueprints) {
                const std::string name(blueprint.name);
                built.push_back(WorldObjectRow {PlacedKind::SmartObject, name, WorldObjectName(PlacedKind::SmartObject, name), {"interactive", InteractiveFolder(blueprint.kind)}, Shared::Game::SmartObjectKindName(blueprint.kind)});
            }
            for (const Shared::Game::ItemClassEntry &item : Shared::Game::g_ItemClasses) {
                // An alias names another class, and a quest item never lies on the ground.
                const Shared::Game::InventoryPropertyClass *properties = Shared::Game::FindInventoryPropertyClass(item.guid);
                if (item.category == "ItemAlias" || (properties != nullptr && properties->questItem)) {
                    continue;
                }
                built.push_back(WorldObjectRow {PlacedKind::Item, std::string(item.guid), std::string(item.name), {"items", std::string(item.category)}, "item"});
            }
            return built;
        }();
        return rows;
    }

    const WorldObjectRow *WorldObjectAtLibraryRow(int index) {
        if (index < 0 || static_cast<std::size_t>(index) < Shared::Game::g_PropModels.size()) {
            return nullptr;
        }
        const std::size_t row = static_cast<std::size_t>(index) - Shared::Game::g_PropModels.size();
        return row < WorldObjectRows().size() ? &WorldObjectRows()[row] : nullptr;
    }
} // namespace KCDC::Features::Prop
