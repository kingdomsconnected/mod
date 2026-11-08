#include "world_scripting.h"

#include "core/scripting/script_context.h"

#include "shared/scripting/catalog.h"

#include <v8pp/convert.hpp>

#include <cstring>
#include <vector>

namespace KCDC::Scripting {
    void RegisterWorldMetadata() {
        auto &events = ClientCatalog().data_type("EventMap");
        events.add_property("poiDiscovered", "[poiId: LocationId]",
            "Dispatched when a point of interest becomes discovered at this machine: silently while the server's `reveal_map` is on, as the game's own discovery when it is off, and for each one `WorldMap.discover` marks. Handler promises are not awaited. A position merely made known emits nothing.");
        events.add_property("locationDiscovered", "[locationId: LocationId]",
            "Dispatched when a location -- a town, a district -- becomes discovered at this machine through the game's own discovery, which only happens while the server's `reveal_map` is off. `WorldMap.discover` does not raise it. Handler promises are not awaited.");

        auto &locationId = ClientCatalog().data_type("LocationId", "Native location or POI GUID fields.");
        locationId.add_property("data1", "number", "First GUID field, an unsigned 32-bit integer.");
        locationId.add_property("data2", "number", "Second GUID field, an unsigned 16-bit integer.");
        locationId.add_property("data3", "number", "Third GUID field, an unsigned 16-bit integer.");
        locationId.add_property("data4", "Uint8Array", "The final eight GUID bytes, in native order.");
    }

    namespace {
        void EmitLocationId(const char *eventName, std::uint32_t data1, std::uint16_t data2, std::uint16_t data3, const std::uint8_t (&data4)[8]) {
            EmitReservedEvent(eventName, [data1, data2, data3, &data4](v8::Isolate *isolate) {
                const auto context = isolate->GetCurrentContext();
                auto id            = v8::Object::New(isolate);
                id->Set(context, v8pp::to_v8(isolate, "data1"), v8::Integer::NewFromUnsigned(isolate, data1)).Check();
                id->Set(context, v8pp::to_v8(isolate, "data2"), v8::Integer::NewFromUnsigned(isolate, data2)).Check();
                id->Set(context, v8pp::to_v8(isolate, "data3"), v8::Integer::NewFromUnsigned(isolate, data3)).Check();
                auto tail = v8::ArrayBuffer::New(isolate, sizeof(data4));
                std::memcpy(tail->Data(), data4, sizeof(data4));
                id->Set(context, v8pp::to_v8(isolate, "data4"), v8::Uint8Array::New(tail, 0, sizeof(data4))).Check();
                return std::vector<v8::Local<v8::Value>> {id};
            });
        }
    } // namespace

    void Events::POIDiscovered(std::uint32_t data1, std::uint16_t data2, std::uint16_t data3, const std::uint8_t (&data4)[8]) {
        EmitLocationId("poiDiscovered", data1, data2, data3, data4);
    }

    void Events::LocationDiscovered(std::uint32_t data1, std::uint16_t data2, std::uint16_t data3, const std::uint8_t (&data4)[8]) {
        EmitLocationId("locationDiscovered", data1, data2, data3, data4);
    }
} // namespace KCDC::Scripting
