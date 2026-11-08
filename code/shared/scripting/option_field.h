#pragma once

#include <v8.h>
#include <v8pp/convert.hpp>

#include <cmath>
#include <string>

namespace KCDC::Scripting {
    // One field of an options object: an empty handle when the object is empty, the field is missing, or it holds undefined or null, so every caller tests `IsEmpty()` alone for "not given". Every verb that takes an options bag reads it through this.
    inline v8::Local<v8::Value> OptionField(v8::Isolate *isolate, v8::Local<v8::Object> options, const char *key) {
        v8::Local<v8::Value> value;
        if (options.IsEmpty() || !options->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, key)).ToLocal(&value) || value->IsNullOrUndefined()) {
            return v8::Local<v8::Value>();
        }
        return value;
    }

    // The same for the options object passed as argument `index`, which may be absent or not an object at all.
    inline v8::Local<v8::Value> OptionField(v8::Isolate *isolate, const v8::FunctionCallbackInfo<v8::Value> &info, int index, const char *key) {
        return info.Length() > index && info[index]->IsObject() ? OptionField(isolate, info[index].As<v8::Object>(), key) : v8::Local<v8::Value>();
    }

    // Writes a finite number field of an options object into `out` and says whether it did; `out` keeps its value otherwise.
    inline bool FloatOption(v8::Isolate *isolate, v8::Local<v8::Object> options, const char *key, float &out) {
        const v8::Local<v8::Value> value = OptionField(isolate, options, key);
        if (value.IsEmpty() || !value->IsNumber()) {
            return false;
        }
        const double number = v8pp::from_v8<double>(isolate, value);
        if (!std::isfinite(number)) {
            return false;
        }
        out = static_cast<float>(number);
        return true;
    }

    // A finite number from the options argument, or `fallback`.
    inline double NumberOption(v8::Isolate *isolate, const v8::FunctionCallbackInfo<v8::Value> &info, int index, const char *key, double fallback) {
        const v8::Local<v8::Value> value = OptionField(isolate, info, index, key);
        if (value.IsEmpty() || !value->IsNumber()) {
            return fallback;
        }
        const double number = v8pp::from_v8<double>(isolate, value);
        return std::isfinite(number) ? number : fallback;
    }

    // A boolean from the options argument, or `fallback`.
    inline bool BoolOption(v8::Isolate *isolate, const v8::FunctionCallbackInfo<v8::Value> &info, int index, const char *key, bool fallback) {
        const v8::Local<v8::Value> value = OptionField(isolate, info, index, key);
        return value.IsEmpty() || !value->IsBoolean() ? fallback : value->BooleanValue(isolate);
    }

    // A string from the options argument, or empty.
    inline std::string StringOption(v8::Isolate *isolate, const v8::FunctionCallbackInfo<v8::Value> &info, int index, const char *key) {
        const v8::Local<v8::Value> value = OptionField(isolate, info, index, key);
        return value.IsEmpty() || !value->IsString() ? std::string {} : v8pp::from_v8<std::string>(isolate, value);
    }
} // namespace KCDC::Scripting
