#include "door_scripting.h"

#include "core/scripting/script_context.h"

#include "shared/scripting/catalog.h"
#include "shared/scripting/level_guid.h"

#include <v8pp/convert.hpp>

#include <vector>

namespace KCDC::Scripting {
    void RegisterDoorMetadata() {
        auto &door = ClientCatalog().data_type("DoorUse", "The door a player's use is about, as this machine sees it when they use it.");
        door.add_property("id", "number", "The server's door entity, as server-side `door.id`; 0 when the server has not sent this door yet.");
        door.add_property("guid", "string", "The door's EntityGuid, as sixteen lowercase hex digits: the same value as server-side `door.guid`.");
        door.add_property("name", "string", "The entity name the level gives the door.");
        door.add_property("open", "boolean", "Whether the door stands open on this machine.");
        door.add_property("locked", "boolean", "Whether the door is locked on this machine.");

        auto &events = ClientCatalog().data_type("EventMap");
        events.add_property("doorInteract", "[door: DoorUse, action: \"open\" | \"close\" | \"unlock\" | \"lockpick\", keySide: boolean]",
            "Dispatched on this machine when the local player uses a door, before anything happens: the door has not moved, the body has not started, and the server has not been told. `action` is `open`, `close`, `unlock` or `lockpick`, as server-side `doorInteract` has it; a key turned in the same use as the push arrives as `unlock` and then `open`, and refusing the `unlock` refuses the use without asking about the `open`. `keySide` says whether the player stands on the side with the keyhole.\n\n"
            "Return false to refuse it: the use does nothing, with no round trip and nothing to put back. Every handler runs whatever an earlier one returned, and an async handler cannot refuse. A door the server made unusable (`door.usable` there) offers no prompt and never gets here.\n\n"
            "It is this machine's own rule. The server's `doorInteract` still decides what it is told, so a refusal that has to hold against a modified client belongs there as well.");
    }

    bool Events::DoorInteract(const DoorUse &door, const char *action, bool keySide) {
        const std::string named = action != nullptr ? action : "";
        return EmitReservedEventSync("doorInteract", [&door, &named, keySide](v8::Isolate *isolate) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> use      = v8::Object::New(isolate);
            use->Set(context, v8pp::to_v8(isolate, "id"), v8pp::to_v8(isolate, static_cast<double>(door.networkId))).Check();
            use->Set(context, v8pp::to_v8(isolate, "guid"), v8pp::to_v8(isolate, FormatLevelGuid(door.guid))).Check();
            use->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, door.name)).Check();
            use->Set(context, v8pp::to_v8(isolate, "open"), v8pp::to_v8(isolate, door.open)).Check();
            use->Set(context, v8pp::to_v8(isolate, "locked"), v8pp::to_v8(isolate, door.locked)).Check();
            return std::vector<v8::Local<v8::Value>> {use, v8pp::to_v8(isolate, named), v8pp::to_v8(isolate, keySide)};
        });
    }
} // namespace KCDC::Scripting
