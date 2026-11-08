#include "vendor_scripting.h"

#include "core/scripting/script_context.h"

#include "shared/scripting/catalog.h"

#include <v8pp/convert.hpp>

#include <string>
#include <vector>

namespace KCDC::Scripting {
    void RegisterVendorMetadata() {
        auto &events = ClientCatalog().data_type("EventMap");
        events.add_property("vendorOpened", "[session: number, npc: number | null]",
            "Dispatched when the game's trade screen actually comes up on this machine for a server vendor -- not when the server asked for it, which can take a few frames or fail. `npc` is the network ID of the NPC keeping the shop, or null when nobody does. "
            "Every `vendorOpened` is followed by exactly one `vendorClosed`.");
        events.add_property("vendorClosed", "[session: number, reason: \"player\" | \"server\" | \"replaced\" | \"levelChange\" | \"sessionOver\"]",
            "Dispatched when a trade screen that `vendorOpened` announced goes away: the player closed it, the server closed it, the server opened another vendor over it, the level changed under it, or the connection ended. "
            "A screen that never came up raises neither event. The server raises its own `vendorClosed` for the session.");
    }

    void Events::VendorOpened(std::uint32_t sessionId, std::uint64_t npcNetworkId) {
        EmitReservedEvent("vendorOpened", [sessionId, npcNetworkId](v8::Isolate *isolate) {
            v8::Local<v8::Value> npc = npcNetworkId != 0 ? v8pp::to_v8(isolate, static_cast<double>(npcNetworkId)) : v8::Null(isolate).As<v8::Value>();
            return std::vector<v8::Local<v8::Value>> {v8pp::to_v8(isolate, sessionId), npc};
        });
    }

    void Events::VendorClosed(std::uint32_t sessionId, const char *reason) {
        EmitReservedEvent("vendorClosed", [sessionId, reason](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {v8pp::to_v8(isolate, sessionId), v8pp::to_v8(isolate, std::string(reason))};
        });
    }
} // namespace KCDC::Scripting
