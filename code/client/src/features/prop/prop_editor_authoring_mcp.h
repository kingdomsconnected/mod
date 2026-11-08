#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace KCDC::Features::Prop {
    class MapEditor;
    struct EditorSnapshot;

    /** Render-thread MCP adapter for authored data and catalog operations. Native work uses MapEditor's queue. */
    class AuthoringMcp final {
      public:
        static nlohmann::json Tools();
        static bool Execute(MapEditor &editor, const EditorSnapshot &snapshot, const std::string &name, const nlohmann::json &arguments, nlohmann::json &result);
    };
}
