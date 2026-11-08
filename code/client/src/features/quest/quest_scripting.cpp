#include "quest_scripting.h"

#include "core/scripting/script_context.h"

#include "shared/scripting/catalog.h"

#include <v8pp/convert.hpp>

#include <vector>

namespace KCDC::Scripting {
    void RegisterQuestMetadata() {
        auto &events = ClientCatalog().data_type("EventMap");
        events.add_property("questTrackingChanged", "[questKey: string, tracked: boolean]",
            "Dispatched when this player starts or stops following a server quest in the game's journal, whichever way it happened: the track button, the game auto-tracking a quest that turns active, or the untrack that follows finishing or failing one. "
            "Local to this machine and raised before the server is told; the server raises its own `questTrackingChanged` when the report arrives. Repeats are filtered, so it only fires on an actual change. Handler promises are not awaited.");
    }

    void Events::QuestTrackingChanged(const std::string &questKey, bool tracked) {
        EmitReservedEvent("questTrackingChanged", [questKey, tracked](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {v8pp::to_v8(isolate, questKey), v8pp::to_v8(isolate, tracked)};
        });
    }
} // namespace KCDC::Scripting
