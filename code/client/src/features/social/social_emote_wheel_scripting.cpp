#include <utils/safe_win32.h>

#include "social_emote_wheel_scripting.h"

#include "core/application.h"
#include "features/social/social_emote_wheel.h"

#include "shared/game/emote_catalog.h"
#include "shared/scripting/catalog.h"

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace KCDC::Scripting {
    namespace {
        KCDC::Features::Social::EmoteWheelService *Wheel() {
            return KCDC::Core::gApplication != nullptr ? &KCDC::Core::gApplication->GetEmoteWheelService() : nullptr;
        }

        /** A whole, non-negative number no larger than `limit`, or false. */
        bool ReadIndex(v8::Isolate *isolate, v8::Local<v8::Value> value, double limit, double &out) {
            if (!value->IsNumber()) {
                return false;
            }
            const double number = v8pp::from_v8<double>(isolate, value);
            if (!std::isfinite(number) || number < 0.0 || number > limit || std::floor(number) != number) {
                return false;
            }
            out = number;
            return true;
        }

        bool ReadEmoteId(v8::Isolate *isolate, v8::Local<v8::Value> value, std::uint16_t &out) {
            double number = 0.0;
            if (!ReadIndex(isolate, value, static_cast<double>(std::numeric_limits<std::uint16_t>::max()), number) || Shared::Game::FindEmote(static_cast<std::uint16_t>(number)) == nullptr) {
                return false;
            }
            out = static_cast<std::uint16_t>(number);
            return true;
        }

        // JS: EmoteWheel.getSlotCount() -> number
        void JS_GetSlotCount(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(static_cast<double>(KCDC::Features::Social::EmoteWheelService::kSlotCount));
        }

        // JS: EmoteWheel.getSlots() -> number[]
        void JS_GetSlots(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            KCDC::Features::Social::EmoteWheelService *wheel = Wheel();
            v8::Local<v8::Array> result                      = v8::Array::New(isolate);
            if (wheel != nullptr) {
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                for (std::size_t slot = 0; slot < wheel->GetSlots().size(); ++slot) {
                    result->Set(context, static_cast<std::uint32_t>(slot), v8pp::to_v8(isolate, static_cast<double>(wheel->GetSlots()[slot]))).Check();
                }
            }
            info.GetReturnValue().Set(result);
        }

        // JS: EmoteWheel.setSlot(slot, emoteId) -> boolean
        void JS_SetSlot(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            KCDC::Features::Social::EmoteWheelService *wheel = Wheel();
            double slot                                      = 0.0;
            std::uint16_t emoteId                            = 0;
            if (wheel == nullptr || info.Length() < 2 || !ReadIndex(isolate, info[0], static_cast<double>(KCDC::Features::Social::EmoteWheelService::kSlotCount - 1), slot) || !ReadEmoteId(isolate, info[1], emoteId)) {
                info.GetReturnValue().Set(false);
                return;
            }
            info.GetReturnValue().Set(wheel->SetSlot(static_cast<std::size_t>(slot), emoteId));
        }

        // JS: EmoteWheel.setSlots(emoteIds) -> boolean
        void JS_SetSlots(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            KCDC::Features::Social::EmoteWheelService *wheel = Wheel();
            if (wheel == nullptr || info.Length() < 1 || !info[0]->IsArray() || info[0].As<v8::Array>()->Length() != KCDC::Features::Social::EmoteWheelService::kSlotCount) {
                info.GetReturnValue().Set(false);
                return;
            }

            // Every id is read before any slot is written, so a bad one leaves
            // the wheel as it was rather than half rearranged.
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> list      = info[0].As<v8::Array>();
            std::array<std::uint16_t, KCDC::Features::Social::EmoteWheelService::kSlotCount> layout {};
            for (std::size_t slot = 0; slot < layout.size(); ++slot) {
                v8::Local<v8::Value> value;
                if (!list->Get(context, static_cast<std::uint32_t>(slot)).ToLocal(&value) || !ReadEmoteId(isolate, value, layout[slot])) {
                    info.GetReturnValue().Set(false);
                    return;
                }
            }
            for (std::size_t slot = 0; slot < layout.size(); ++slot) {
                wheel->SetSlot(slot, layout[slot]);
            }
            info.GetReturnValue().Set(true);
        }

        // JS: EmoteWheel.reset()
        void JS_Reset(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (KCDC::Features::Social::EmoteWheelService *wheel = Wheel()) {
                wheel->ResetSlots();
            }
        }

        // JS: EmoteWheel.getCatalog() -> EmoteCatalogEntry[]
        void JS_GetCatalog(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> result    = v8::Array::New(isolate, static_cast<int>(Shared::Game::g_Emotes.size()));
            for (std::size_t index = 0; index < Shared::Game::g_Emotes.size(); ++index) {
                const Shared::Game::EmoteEntry &entry = Shared::Game::g_Emotes[index];
                v8::Local<v8::Object> row             = v8::Object::New(isolate);
                row->Set(context, v8pp::to_v8(isolate, "id"), v8pp::to_v8(isolate, static_cast<double>(entry.id))).Check();
                row->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, entry.name)).Check();
                row->Set(context, v8pp::to_v8(isolate, "duration"), v8pp::to_v8(isolate, static_cast<double>(entry.duration))).Check();
                row->Set(context, v8pp::to_v8(isolate, "fullBody"), v8pp::to_v8(isolate, entry.fullBody)).Check();
                result->Set(context, static_cast<std::uint32_t>(index), row).Check();
            }
            info.GetReturnValue().Set(result);
        }
    } // namespace

    void RegisterEmoteWheel(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &entry = ClientCatalog().data_type("EmoteCatalogEntry", "One gesture of the shipped emote catalog, which is every gesture a wheel slot can choose.");
        entry.add_property("id", "number", "What a slot is set to, and what goes on the wire when the gesture plays.");
        entry.add_property("name", "string", "The label the wheel draws for it.");
        entry.add_property("duration", "number", "How long the gesture plays, in seconds.");
        entry.add_property("fullBody", "boolean", "Whether it takes the whole body. A full-body gesture holds the player still while it plays; an upper-body one plays over a walk.");

        v8pp::module wheel(isolate, ClientCatalog(), "EmoteWheel",
            "Which gesture each slot of this client's emote wheel chooses.\n\n"
            "The wheel has a fixed number of slots, numbered clockwise from the one straight up, and each chooses one row of the shipped catalog. They start out as the stock wheel -- wave, big wave, nod, respectful nod, bow, cheer -- and stay that way until a script changes them. A change is drawn on the next frame, including on a wheel held open.\n\n"
            "Client-only and local: this arranges the wheel on this machine and nothing else. Every client can already play every gesture in the catalog, so a slot changes what is within reach, not what is allowed. The layout lasts for the session, and the next one starts from the default.");

        wheel.function("getSlotCount", &JS_GetSlotCount, v8pp::metadata::docs("number", {}, "How many slots the wheel has.", "The slot count, which the catalog growing does not change."));

        wheel.function("getSlots", &JS_GetSlots, v8pp::metadata::docs("number[]", {}, "The catalog id each slot chooses, in slot order.", "One id per slot."));

        wheel.function("setSlot", &JS_SetSlot,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("slot", "number", false, "Zero-based, clockwise from straight up."), v8pp::metadata::param("emoteId", "number", false, "An `id` from `getCatalog`.")},
                "Points one slot at a catalog gesture. The same gesture may sit in more than one slot.", "False, and nothing changes, when the slot is out of range or the id is not in the catalog."));

        wheel.function("setSlots", &JS_SetSlots,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("emoteIds", "number[]", false, "One catalog id per slot, in slot order; exactly `getSlotCount()` of them.")}, "Lays out the whole wheel at once.",
                "False, and nothing changes, when the list is the wrong length or any id is not in the catalog."));

        wheel.function("reset", &JS_Reset, v8pp::metadata::docs("void", {}, "Puts every slot back to the stock wheel: wave, big wave, nod, respectful nod, bow, cheer."));

        wheel.function("getCatalog", &JS_GetCatalog, v8pp::metadata::docs("EmoteCatalogEntry[]", {}, "Every gesture a slot can choose.", "The catalog, in its own order."));

        wheel.publish(global);
    }
} // namespace KCDC::Scripting
