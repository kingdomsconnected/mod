#include "prop_editor_scripting.h"

#include "core/application.h"

#include "features/prop/prop_editor.h"

#include "shared/scripting/catalog.h"

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

namespace KCDC::Scripting {
    namespace {
        // JS: MapEditor.open() -> "opened" | "alreadyOpen" | "disabled"
        void JS_Open(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (Core::gApplication == nullptr || !Core::gApplication->IsMapEditorEnabled()) {
                info.GetReturnValue().Set(v8pp::to_v8(isolate, "disabled"));
                return;
            }

            KCDC::Features::Prop::MapEditor &editor = Core::gApplication->GetMapEditor();
            if (editor.IsOpen()) {
                info.GetReturnValue().Set(v8pp::to_v8(isolate, "alreadyOpen"));
                return;
            }

            editor.SetOpen(true);
            info.GetReturnValue().Set(v8pp::to_v8(isolate, "opened"));
        }

        // JS: MapEditor.close() -> void
        void JS_Close(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (Core::gApplication != nullptr) {
                Core::gApplication->GetMapEditor().SetOpen(false);
            }
        }

        // JS: MapEditor.isOpen() -> boolean
        void JS_IsOpen(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(Core::gApplication != nullptr && Core::gApplication->GetMapEditor().IsOpen());
        }

        // JS: MapEditor.isEnabled() -> boolean
        void JS_IsEnabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(Core::gApplication != nullptr && Core::gApplication->IsMapEditorEnabled());
        }
    } // namespace

    void RegisterMapEditor(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        v8pp::module editor(isolate, ClientCatalog(), "MapEditor",
            "The world builder: a free camera, the game's mesh catalog as an asset library, a placement brush, a gizmo that moves anything, and maps saved to and loaded from files.\n\n"
            "F7 or a client resource opens it. Multiplayer requires the server to grant this player access with `player.setWorldBuilderEnabled(true)`; single-player and offline editing are always allowed. "
            "While it is open it holds the one free camera, so a `NoClip` flight in progress ends and `NoClip.enable` refuses. The player can still close it from its own window.");

        editor.function("open", &JS_Open, v8pp::metadata::docs("\"opened\" | \"alreadyOpen\" | \"disabled\"", {}, "Opens the editor.", "`opened` when it opened. `alreadyOpen` when it was open already. `disabled` when this multiplayer connection has not been granted access."));

        editor.function("close", &JS_Close, v8pp::metadata::docs("void", {}, "Closes the editor and hands the camera back. Doing nothing when it is closed is not an error.", "Nothing."));

        editor.function("isOpen", &JS_IsOpen, v8pp::metadata::docs("boolean", {}, "Whether the editor is open at this client.", "True while it is open."));

        editor.function("isEnabled", &JS_IsEnabled, v8pp::metadata::docs("boolean", {}, "Whether this client may open World Builder.", "True offline or when the server has granted this player access."));

        editor.publish(global);
    }
} // namespace KCDC::Scripting
