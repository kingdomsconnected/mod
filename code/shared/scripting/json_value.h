#pragma once

#include <v8.h>
#include <v8pp/convert.hpp>

#include <optional>
#include <string>

namespace KCDC::Scripting {
    // JSON text the server wrote itself, as a script value; `fallback` should it ever fail to parse.
    inline v8::Local<v8::Value> ParseJsonValue(v8::Isolate *isolate, const std::string &json, v8::Local<v8::Value> fallback) {
        v8::Local<v8::Value> parsed;
        return v8::JSON::Parse(isolate->GetCurrentContext(), v8pp::to_v8(isolate, json)).ToLocal(&parsed) ? parsed : fallback;
    }

    // A script value as JSON text, through its own `toJSON` where it has one -- a Vector3 or a Quaternion comes out as plain components. None when JSON cannot hold it.
    inline std::optional<std::string> StringifyValue(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        v8::Local<v8::String> text;
        if (value.IsEmpty() || !v8::JSON::Stringify(isolate->GetCurrentContext(), value).ToLocal(&text)) {
            return std::nullopt;
        }
        return v8pp::from_v8<std::string>(isolate, text);
    }
} // namespace KCDC::Scripting
