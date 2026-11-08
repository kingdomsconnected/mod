#include "streamed_asset_scripting.h"

#include "core/application.h"

#include "shared/scripting/catalog.h"

#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <cctype>
#include <string>

namespace KCDC::Scripting {
    void Assets::JS_Has(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 1 || !info[0]->IsString()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Assets.has: expected (path)"));
            return;
        }

        std::string path;
        for (const char c : v8pp::from_v8<std::string>(isolate, info[0])) {
            path.push_back(c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        info.GetReturnValue().Set(KCDC::Core::gApplication->GetStreamedAssetService().HasEntry(path));
    }

    void Assets::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        v8pp::module assets(isolate, ClientCatalog(), "Assets",
            "What the server streams, seen from this machine. Models, materials, textures, sounds and particle libraries a server streams load by path like the game's own -- `objects/kcdc/<resource>/chair.cgf` works wherever a model path does -- so nothing here is needed to use them. "
            "Clips play through the server's `playAnimation(\"\", { clip })`.");

        assets.function("has", &Assets::JS_Has,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("path", "string", false, "Path inside the game's data, as a server streams it -- `objects/kcdc/<resource>/chair.cgf`."),
                },
                "Whether the session's streamed assets carry a file at this path. Only new assets count; a replaced stock file is the game's own path.",
                "True when a mounted stream-lane pak carries it."));

        assets.publish(global);
    }
} // namespace KCDC::Scripting
