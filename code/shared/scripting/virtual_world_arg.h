#pragma once

#include <mafianet/virtual_world.h>

#include <v8.h>
#include <v8pp/convert.hpp>

#include <cmath>
#include <limits>
#include <optional>

namespace KCDC::Scripting {
    // A script number as a world id, or none for one no world has: negative, fractional, NaN or past 32 bits. Casting those straight is undefined.
    inline std::optional<MafiaNet::VirtualWorldId> VirtualWorldFromNumber(double number) {
        if (!std::isfinite(number) || number < 0.0 || number > static_cast<double>(std::numeric_limits<MafiaNet::VirtualWorldId>::max()) || std::floor(number) != number) {
            return std::nullopt;
        }
        return static_cast<MafiaNet::VirtualWorldId>(number);
    }

    // The virtual world argument `index` names, or none -- which every lookup taking one reads as "every world".
    inline std::optional<MafiaNet::VirtualWorldId> VirtualWorldArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
        if (info.Length() <= index || !info[index]->IsNumber()) {
            return std::nullopt;
        }
        return VirtualWorldFromNumber(v8pp::from_v8<double>(info.GetIsolate(), info[index]));
    }
} // namespace KCDC::Scripting
