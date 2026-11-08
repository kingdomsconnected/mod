#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace KCDC::Features::Prop {
    class MapEditor;
    struct EditorSnapshot;

    class ProjectMcp final {
      public:
        static nlohmann::json Tools();
        static bool Execute(MapEditor &editor, const EditorSnapshot &snapshot, const std::string &name, const nlohmann::json &arguments, nlohmann::json &result);
    };
} // namespace KCDC::Features::Prop
