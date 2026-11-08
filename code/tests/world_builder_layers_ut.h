#pragma once

#include "features/prop/prop_editor_layers.h"

MODULE(world_builder_layers, {
    IT("protects Default identity while allowing visibility and lock changes", {
        KCDC::Features::Prop::EditorLayers layers;
        std::string error;
        EQUALS(layers.Layers().size(), std::size_t(1));
        EQUALS(layers.LayerOf("world:7"), std::string("default"));
        EQUALS(layers.Rename("default", "New name", error), false);
        EQUALS(layers.Delete("default", true, error), false);
        EQUALS(layers.SetVisible("default", false, error), true);
        EQUALS(layers.Visible("world:7"), false);
        EQUALS(layers.Editable("world:7"), false);
        EQUALS(layers.SetVisible("default", true, error), true);
        EQUALS(layers.SetLocked("default", true, error), true);
        EQUALS(layers.Visible("world:7"), true);
        EQUALS(layers.Editable("world:7"), false);
    });

    IT("keeps custom visibility and locks independent of Default", {
        KCDC::Features::Prop::EditorLayers layers;
        std::string error;
        const auto custom = layers.Create("Buildings", error);
        EQUALS(custom.empty(), false);
        EQUALS(layers.Assign({"world:7", "prop:10", "effect:3", "area:market", "patrol:guard"}, custom, error), true);
        EQUALS(layers.SetVisible("default", false, error), true);
        EQUALS(layers.SetLocked("default", true, error), true);
        EQUALS(layers.Visible("world:7"), true);
        EQUALS(layers.Editable("prop:10"), true);
        EQUALS(layers.Visible("world:8"), false);
        EQUALS(layers.SetLocked(custom, true, error), true);
        EQUALS(layers.Visible("area:market"), true);
        EQUALS(layers.Editable("patrol:guard"), false);
        EQUALS(layers.SetLocked(custom, false, error), true);
        EQUALS(layers.SetVisible(custom, false, error), true);
        EQUALS(layers.Editable("effect:3"), false);
    });

    IT("requires delete confirmation and returns surviving members to Default", {
        KCDC::Features::Prop::EditorLayers layers;
        std::string error;
        const auto custom = layers.Create("Buildings", error);
        EQUALS(layers.SetActive(custom, error), true);
        EQUALS(layers.Assign({"prop:10"}, custom, error), true);
        const auto before = layers.ToJson();
        EQUALS(layers.Delete(custom, false, error), false);
        EQUALS(layers.ToJson() == before, true);
        EQUALS(layers.Delete(custom, true, error), true);
        EQUALS(layers.LayerOf("prop:10"), std::string("default"));
        EQUALS(layers.Active(), std::string("default"));
        EQUALS(layers.Members().empty(), true);
        EQUALS(layers.Create("Buildings", error) != custom, true);
    });

    IT("roundtrips definitions and deterministic membership without reusing deleted IDs", {
        KCDC::Features::Prop::EditorLayers layers;
        std::string error;
        const auto first   = layers.Create("Buildings", error);
        const auto deleted = layers.Create("Deleted", error);
        EQUALS(layers.Delete(deleted, true, error), true);
        EQUALS(layers.SetActive(first, error), true);
        EQUALS(layers.SetLocked(first, true, error), true);
        EQUALS(layers.Assign({"area:square", "prop:10", "world:7"}, first, error), true);
        KCDC::Features::Prop::EditorLayers restored;
        EQUALS(KCDC::Features::Prop::EditorLayers::FromJson(layers.ToJson(), restored, error), true);
        EQUALS(restored.ToJson() == layers.ToJson(), true);
        EQUALS(restored.Create("Later", error), std::string("layer-3"));
        EQUALS(KCDC::Features::Prop::EditorLayers::FromJson(layers.ToJson(false), restored, error), true);
        EQUALS(restored.Members().empty(), true);
        EQUALS(restored.Active(), first);
        EQUALS(restored.Find(first)->locked, true);
    });

    IT("rejects malformed assignments atomically and leaves revision unchanged for noops", {
        KCDC::Features::Prop::EditorLayers layers;
        std::string error;
        const auto custom = layers.Create("Buildings", error);
        EQUALS(layers.Assign({"prop:10"}, custom, error), true);
        const auto before   = layers.ToJson();
        const auto revision = layers.Revision();
        EQUALS(layers.Assign({"prop:11", "prop:bogus"}, custom, error), false);
        EQUALS(layers.Assign({"prop:11", "prop:11"}, custom, error), false);
        EQUALS(layers.Assign({"prop:11"}, "missing", error), false);
        EQUALS(layers.Assign({"prop:18446744073709551616"}, custom, error), false);
        EQUALS(layers.Assign({"prop:01"}, custom, error), false);
        EQUALS(layers.Assign({"patrol:"}, custom, error), false);
        EQUALS(layers.ToJson() == before, true);
        EQUALS(layers.Assign({"prop:10"}, custom, error), true);
        EQUALS(layers.SetVisible(custom, true, error), true);
        EQUALS(layers.Rename(custom, "Buildings", error), true);
        EQUALS(layers.Revision(), revision);
        EQUALS(layers.Assign({"prop:10"}, "default", error), true);
        EQUALS(layers.Members().empty(), true);
    });

    IT("releases extinct membership capacity without mutating shared history snapshots", {
        KCDC::Features::Prop::EditorLayers layers;
        std::string error;
        const auto custom = layers.Create("Buildings", error);
        std::vector<std::string> keys;
        keys.reserve(KCDC::Features::Prop::EditorLayers::kMaxMembers);
        for (std::size_t i = 1; i <= KCDC::Features::Prop::EditorLayers::kMaxMembers; ++i) {
            keys.push_back("prop:" + std::to_string(i));
        }
        EQUALS(layers.Assign(keys, custom, error), true);
        const auto snapshot = layers;
        EQUALS(layers.Assign({"effect:1"}, custom, error), false);
        const auto revision = layers.Revision();
        layers.Forget("absent");
        EQUALS(layers.Revision(), revision);
        layers.Forget("prop:1");
        EQUALS(layers.Members().size(), KCDC::Features::Prop::EditorLayers::kMaxMembers - 1);
        EQUALS(layers.Assign({"effect:1"}, custom, error), true);
        EQUALS(snapshot.LayerOf("prop:1"), custom);
        EQUALS(snapshot.Members().contains("effect:1"), false);
        EQUALS(layers.LayerOf("prop:1"), std::string("default"));
    });

    IT("rejects invalid layer names and missing layers", {
        KCDC::Features::Prop::EditorLayers layers;
        std::string error;
        EQUALS(layers.Create("", error).empty(), true);
        EQUALS(layers.Create(" ", error).empty(), true);
        EQUALS(layers.Create("Default", error).empty(), true);
        EQUALS(layers.Create("Name\n", error).empty(), true);
        EQUALS(layers.Create(std::string(129, 'a'), error).empty(), true);
        EQUALS(layers.SetActive("missing", error), false);
        EQUALS(layers.SetVisible("missing", false, error), false);
        EQUALS(layers.SetLocked("missing", true, error), false);
        EQUALS(layers.Layers().size(), std::size_t(1));
    });

    IT("preserves full runtime identifiers and never recycles IDs across undo branches", {
        KCDC::Features::Prop::EditorLayers layers;
        std::string error;
        const auto initial = layers;
        const auto created = layers.Create("First", error);
        EQUALS(layers.Assign({"prop:18446744073709551615", "effect:4294967296", "world:4294967297"}, created, error), true);
        EQUALS(layers.LayerOf("prop:18446744073709551615"), created);
        const auto revision = layers.Revision();
        layers.Restore(initial);
        EQUALS(layers.Members().empty(), true);
        EQUALS(layers.Revision() > revision, true);
        EQUALS(layers.Create("Second", error), std::string("layer-2"));
    });

    IT("shares large memberships across visibility lock and active layer history snapshots", {
        KCDC::Features::Prop::EditorLayers layers;
        std::string error;
        const auto custom = layers.Create("Buildings", error);
        std::vector<std::string> keys;
        for (std::size_t index = 1; index <= 4096; ++index) {
            keys.push_back("world:" + std::to_string(index));
        }
        EQUALS(layers.Assign(keys, custom, error), true);
        const auto *members = &layers.Members();
        std::vector<KCDC::Features::Prop::EditorLayers> history;
        for (std::size_t step = 0; step < 256; ++step) {
            history.push_back(layers);
            EQUALS(layers.SetVisible(custom, step % 2 == 0, error), true);
            EQUALS(layers.SetLocked(custom, step % 3 == 0, error), true);
            EQUALS(layers.SetActive(step % 2 == 0 ? custom : "default", error), true);
            EQUALS(layers.Rename(custom, "Buildings " + std::to_string(step), error), true);
        }
        // The full history retains one membership allocation, not 256 copies.
        for (const auto &snapshot : history) {
            EQUALS(&snapshot.Members() == members, true);
            EQUALS(snapshot.Members().size(), keys.size());
        }
        EQUALS(&layers.Members() == members, true);
        EQUALS(history.front().Find(custom)->name, std::string("Buildings"));
        EQUALS(history.front().Find(custom)->visible, true);
        EQUALS(history.front().Find(custom)->locked, false);
    });

    IT("detaches changed membership while preserving copied and restored history values", {
        KCDC::Features::Prop::EditorLayers layers;
        std::string error;
        const auto first  = layers.Create("First", error);
        const auto second = layers.Create("Second", error);
        EQUALS(layers.Assign({"prop:10", "effect:11"}, first, error), true);
        const auto before = layers;
        const auto saved  = before.ToJson();
        EQUALS(layers.Assign({"prop:10", "world:12"}, second, error), true);
        EQUALS(&layers.Members() != &before.Members(), true);
        EQUALS(before.ToJson() == saved, true);
        EQUALS(layers.LayerOf("prop:10"), second);
        EQUALS(before.LayerOf("prop:10"), first);
        const auto changed  = layers;
        const auto revision = layers.Revision();
        layers.Restore(before);
        EQUALS(&layers.Members() == &before.Members(), true);
        EQUALS(layers.Revision() > revision, true);
        EQUALS(layers.ToJson() == saved, true);
        EQUALS(layers.Assign({"effect:11"}, "default", error), true);
        EQUALS(layers.LayerOf("effect:11"), std::string("default"));
        EQUALS(before.LayerOf("effect:11"), first);
        layers.Restore(changed);
        EQUALS(layers.Delete(second, true, error), true);
        EQUALS(layers.LayerOf("prop:10"), std::string("default"));
        EQUALS(layers.LayerOf("world:12"), std::string("default"));
        EQUALS(layers.LayerOf("effect:11"), first);
        EQUALS(changed.LayerOf("prop:10"), second);
        EQUALS(changed.LayerOf("world:12"), second);
        EQUALS(before.ToJson() == saved, true);
    });

    IT("keeps shared membership for failed and no-op edits and isolates parsed replacement", {
        KCDC::Features::Prop::EditorLayers layers;
        std::string error;
        const auto populated = layers.Create("Populated", error);
        const auto empty     = layers.Create("Empty", error);
        EQUALS(layers.Assign({"prop:10"}, populated, error), true);
        const auto snapshot = layers;
        const auto *members = &snapshot.Members();
        EQUALS(layers.Assign({"prop:10"}, populated, error), true);
        EQUALS(layers.Assign({"prop:11"}, "default", error), true);
        EQUALS(layers.Assign({"prop:11", "invalid"}, populated, error), false);
        EQUALS(layers.Assign({"prop:11"}, "missing", error), false);
        EQUALS(layers.Delete(populated, false, error), false);
        EQUALS(layers.Delete(empty, true, error), true);
        EQUALS(&layers.Members() == members, true);
        auto replacement       = layers.ToJson();
        replacement["members"] = nlohmann::json::array({{{"key", "effect:20"}, {"layer", populated}}});
        EQUALS(KCDC::Features::Prop::EditorLayers::FromJson(replacement, layers, error), true);
        EQUALS(layers.LayerOf("effect:20"), populated);
        EQUALS(layers.LayerOf("prop:10"), std::string("default"));
        EQUALS(snapshot.LayerOf("prop:10"), populated);
        EQUALS(snapshot.LayerOf("effect:20"), std::string("default"));
        EQUALS(snapshot.Find(empty) != nullptr, true);
    });

    IT("rejects malformed saved layers without partially replacing the current model", {
        KCDC::Features::Prop::EditorLayers layers;
        std::string error;
        const auto custom = layers.Create("Buildings", error);
        EQUALS(layers.Assign({"prop:10"}, custom, error), true);
        const auto valid    = layers.ToJson();
        const auto revision = layers.Revision();
        auto invalid        = valid;
        invalid["members"].push_back(invalid["members"][0]);
        EQUALS(KCDC::Features::Prop::EditorLayers::FromJson(invalid, layers, error), false);
        invalid                        = valid;
        invalid["members"][0]["layer"] = "layer-77";
        EQUALS(KCDC::Features::Prop::EditorLayers::FromJson(invalid, layers, error), false);
        invalid                      = valid;
        invalid["layers"][0]["name"] = "Renamed";
        EQUALS(KCDC::Features::Prop::EditorLayers::FromJson(invalid, layers, error), false);
        invalid = valid;
        invalid["layers"].erase(0);
        EQUALS(KCDC::Features::Prop::EditorLayers::FromJson(invalid, layers, error), false);
        invalid = valid;
        invalid["layers"].push_back(invalid["layers"][1]);
        EQUALS(KCDC::Features::Prop::EditorLayers::FromJson(invalid, layers, error), false);
        invalid           = valid;
        invalid["active"] = "unknown";
        EQUALS(KCDC::Features::Prop::EditorLayers::FromJson(invalid, layers, error), false);
        for (const auto &nextId : {nlohmann::json(-1), nlohmann::json(0), nlohmann::json(1), nlohmann::json(2.5), nlohmann::json("3")}) {
            invalid           = valid;
            invalid["nextId"] = nextId;
            EQUALS(KCDC::Features::Prop::EditorLayers::FromJson(invalid, layers, error), false);
        }
        EQUALS(layers.ToJson() == valid, true);
        EQUALS(layers.Revision(), revision);
    });
});
