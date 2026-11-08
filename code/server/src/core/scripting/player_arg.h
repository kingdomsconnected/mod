#pragma once

#include <v8.h>

#include <cstdint>

namespace KCDC::Scripting {
    // The connected player an argument names -- a Player handle or its id -- or 0 for anything else, null and undefined included.
    std::uint64_t PlayerArg(v8::Isolate *isolate, v8::Local<v8::Value> value);
} // namespace KCDC::Scripting
