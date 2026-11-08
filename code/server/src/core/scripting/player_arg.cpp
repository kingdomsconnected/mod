#include "player_arg.h"

#include "features/player/player_roster.h"

#include <scripting/builtins/entity.h>
#include <scripting/v8pp_types.h>

#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>

namespace KCDC::Scripting {
    std::uint64_t PlayerArg(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        std::uint64_t networkId = 0;
        if (value.IsEmpty() || value->IsNullOrUndefined()) {
            return 0;
        }
        if (value->IsNumber()) {
            networkId = static_cast<std::uint64_t>(v8pp::from_v8<double>(isolate, value));
        }
        else if (auto *entity = v8pp::class_<Framework::Scripting::Builtins::Entity>::unwrap_object(isolate, value)) {
            networkId = entity->GetId();
        }
        return KCDC::Features::Player::Players::Find(networkId) != nullptr ? networkId : 0;
    }
} // namespace KCDC::Scripting
