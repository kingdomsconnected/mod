#include "blip_scripting.h"

#include "core/scripting/wrap.h"

#include "features/blip/blip_verbs.h"

#include "shared/game/blip_icon_catalog.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/option_field.h"

#include <glm/glm.hpp>
#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace KCDC::Scripting {

    namespace {
        /** The virtual world an argument names, or none for every one of them. */
        std::optional<MafiaNet::VirtualWorldId> WorldArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            if (info.Length() <= index || !info[index]->IsNumber()) {
                return std::nullopt;
            }
            return static_cast<MafiaNet::VirtualWorldId>(v8pp::from_v8<double>(info.GetIsolate(), info[index]));
        }

        /** The handle a prototype function was called on, or nullptr. */
        Blip *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<Blip>::unwrap_object(info.GetIsolate(), info.This());
        }

        /** What a live blip is drawn from now, for a setter to change one part of. */
        KCDC::Features::Blip::BlipLook LookOf(const Shared::Entities::BlipEntity &blip) {
            KCDC::Features::Blip::BlipLook look;
            look.icon    = blip.icon;
            look.state   = blip.state;
            look.label   = blip.label;
            look.compass = blip.compass;
            look.map     = blip.map;
            return look;
        }

        // JS: blip.remove()
        void JS_Remove(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *self = Self(info)) {
                KCDC::Features::Blip::Blips::Destroy(self->ResolveBlip());
            }
        }

        // JS: Blip.create({ position, type?, markState?, label?, compass?, map?, player?, virtualWorld? }) -> Blip
        void JS_Create(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsObject()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Blip.create: expected ({ position, type?, markState?, label?, compass?, map?, player?, virtualWorld? })"));
                return;
            }
            v8::Local<v8::Object> options = info[0].As<v8::Object>();

            const v8::Local<v8::Value> position = OptionField(isolate, options, "position");
            if (position.IsEmpty() || !position->IsObject()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Blip.create: position is required and must be a Vector3"));
                return;
            }

            KCDC::Features::Blip::BlipLook look;
            const v8::Local<v8::Value> type = OptionField(isolate, options, "type");
            if (!type.IsEmpty() && type->IsString()) {
                const std::string name             = v8pp::from_v8<std::string>(isolate, type);
                const Shared::Game::BlipIcon *icon = Shared::Game::FindBlipIcon(name);
                if (icon == nullptr) {
                    isolate->ThrowError(v8pp::to_v8(isolate, "Blip.create: \"" + name + "\" is not one of the game's compass mark types; Blip.types() lists them"));
                    return;
                }
                look.icon = icon->type;
            }

            const v8::Local<v8::Value> state = OptionField(isolate, options, "markState");
            if (!state.IsEmpty() && state->IsNumber()) {
                look.state = static_cast<std::int32_t>(v8pp::from_v8<double>(isolate, state));
            }
            const v8::Local<v8::Value> label = OptionField(isolate, options, "label");
            if (!label.IsEmpty() && label->IsString()) {
                look.label = v8pp::from_v8<std::string>(isolate, label);
            }
            const v8::Local<v8::Value> compass = OptionField(isolate, options, "compass");
            if (!compass.IsEmpty() && compass->IsBoolean()) {
                look.compass = v8pp::from_v8<bool>(isolate, compass);
            }
            const v8::Local<v8::Value> map = OptionField(isolate, options, "map");
            if (!map.IsEmpty() && map->IsBoolean()) {
                look.map = v8pp::from_v8<bool>(isolate, map);
            }

            std::uint32_t virtualWorld               = MafiaNet::VIRTUAL_WORLD_GLOBAL;
            const v8::Local<v8::Value> worldArgument = OptionField(isolate, options, "virtualWorld");
            if (!worldArgument.IsEmpty() && worldArgument->IsNumber()) {
                virtualWorld = static_cast<std::uint32_t>(v8pp::from_v8<double>(isolate, worldArgument));
            }

            MafiaNet::PeerGuid viewer         = MafiaNet::UNASSIGNED_PEER_GUID;
            const v8::Local<v8::Value> player = OptionField(isolate, options, "player");
            if (!player.IsEmpty() && !player->IsNullOrUndefined()) {
                auto *handle = v8pp::class_<Framework::Scripting::Builtins::Entity>::unwrap_object(isolate, player);
                auto *entity = handle != nullptr ? handle->GetHandle() : nullptr;
                if (entity == nullptr || entity->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID) {
                    isolate->ThrowError(v8pp::to_v8(isolate, "Blip.create: player must be a connected Player"));
                    return;
                }
                viewer = entity->ownerGUID;
            }

            auto *blip = KCDC::Features::Blip::Blips::Place(v8pp::convert<glm::vec3>::from_v8(isolate, position), look, virtualWorld, viewer);
            if (blip == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Blip.create: failed to create entity"));
                return;
            }

            info.GetReturnValue().Set(WrapBlip(isolate, blip->GetNetworkID()));
        }

        // JS: Blip.all(virtualWorld?) -> Blip[]
        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> blips     = v8::Array::New(isolate);

            std::uint32_t index = 0;
            for (auto *blip : KCDC::Features::Blip::Blips::Collect(WorldArg(info, 0))) {
                blips->Set(context, index++, WrapBlip(isolate, blip->GetNetworkID())).Check();
            }
            info.GetReturnValue().Set(blips);
        }

        // JS: Blip.getById(id) -> Blip | null
        void JS_GetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Blip.getById: expected (id)"));
                return;
            }

            const std::uint64_t networkId = static_cast<std::uint64_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0));
            info.GetReturnValue().Set(WrapBlip(isolate, networkId));
        }

        // JS: Blip.removeAll(virtualWorld?) -> number
        void JS_RemoveAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(static_cast<std::uint32_t>(KCDC::Features::Blip::Blips::DestroyAll(WorldArg(info, 0))));
        }

        // JS: Blip.types() -> string[]
        void JS_Types(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> names     = v8::Array::New(isolate);

            std::uint32_t index = 0;
            for (const Shared::Game::BlipIcon &icon : Shared::Game::kBlipIcons) {
                names->Set(context, index++, v8pp::to_v8(isolate, std::string(icon.name))).Check();
            }
            info.GetReturnValue().Set(names);
        }
    } // namespace

    Shared::Entities::BlipEntity *Blip::ResolveBlip() const {
        return dynamic_cast<Shared::Entities::BlipEntity *>(Resolve());
    }

    std::string Blip::GetType() const {
        const auto *blip                   = ResolveBlip();
        const Shared::Game::BlipIcon *icon = blip != nullptr ? Shared::Game::BlipIconOf(blip->icon) : nullptr;
        return icon != nullptr ? std::string(icon->name) : std::string();
    }

    void Blip::SetType(std::string type) {
        // An unknown name leaves the blip as it was; Blip.create is the path that throws.
        auto *blip                         = ResolveBlip();
        const Shared::Game::BlipIcon *icon = Shared::Game::FindBlipIcon(type);
        if (blip != nullptr && icon != nullptr) {
            KCDC::Features::Blip::BlipLook look = LookOf(*blip);
            look.icon                           = icon->type;
            KCDC::Features::Blip::Blips::SetLook(blip, look);
        }
    }

    double Blip::GetMarkState() const {
        const auto *blip = ResolveBlip();
        return blip != nullptr ? static_cast<double>(blip->state) : 0.0;
    }

    void Blip::SetMarkState(double state) {
        if (auto *blip = ResolveBlip()) {
            KCDC::Features::Blip::BlipLook look = LookOf(*blip);
            look.state                          = static_cast<std::int32_t>(state);
            KCDC::Features::Blip::Blips::SetLook(blip, look);
        }
    }

    std::string Blip::GetLabel() const {
        const auto *blip = ResolveBlip();
        return blip != nullptr ? blip->label : std::string();
    }

    void Blip::SetLabel(std::string label) {
        if (auto *blip = ResolveBlip()) {
            KCDC::Features::Blip::BlipLook look = LookOf(*blip);
            look.label                          = std::move(label);
            KCDC::Features::Blip::Blips::SetLook(blip, look);
        }
    }

    bool Blip::GetCompass() const {
        const auto *blip = ResolveBlip();
        return blip != nullptr && blip->compass;
    }

    void Blip::SetCompass(bool compass) {
        if (auto *blip = ResolveBlip()) {
            KCDC::Features::Blip::BlipLook look = LookOf(*blip);
            look.compass                        = compass;
            KCDC::Features::Blip::Blips::SetLook(blip, look);
        }
    }

    bool Blip::GetMap() const {
        const auto *blip = ResolveBlip();
        return blip != nullptr && blip->map;
    }

    void Blip::SetMap(bool map) {
        if (auto *blip = ResolveBlip()) {
            KCDC::Features::Blip::BlipLook look = LookOf(*blip);
            look.map                            = map;
            KCDC::Features::Blip::Blips::SetLook(blip, look);
        }
    }

    std::string Blip::ToString() const {
        std::ostringstream ss;
        ss << "Blip{ id: " << GetId() << ", type: " << GetType() << ", label: " << GetLabel() << " }";
        return ss.str();
    }

    v8pp::class_<Blip> &Blip::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (!_class) {
            // v8pp's inherit<Entity> needs the framework Entity registered first.
            Framework::Scripting::Builtins::Entity::GetClass(isolate);

            _class = std::make_unique<v8pp::class_<Blip>>(isolate, ServerCatalog(), "Blip", "Replicated compass and map-screen blip handle.");
            _class->auto_wrap_objects(true);
            _class->inherit<Framework::Scripting::Builtins::Entity>()
                .ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Creates a script wrapper for an existing blip with this ID; use Blip.create() to place one."))
                .function("toString", &Blip::ToString, v8pp::metadata::docs("string", {}, "Formats this blip handle for logging and debugging.", "The blip ID, its type and its label."));

            _class->prototype_function("remove", &JS_Remove, v8pp::metadata::docs("void", {}, "Takes this blip off every compass and map screen that shows it."));

            _class->property("type", &Blip::GetType, &Blip::SetType,
                v8pp::metadata::property_docs("string", "Which of the game's own compass icons the blip draws as, by the game's own name for it -- `Blip.types()` lists them. Assigning a name the game does not know leaves the blip as it was."));
            _class->property("markState", &Blip::GetMarkState, &Blip::SetMarkState,
                v8pp::metadata::property_docs("number",
                    "The mark's state, forwarded to the compass and map movies as-is -- what the client Blip calls `state`, renamed here because every entity's `state` is its StateBag. What a given value draws is undocumented; 0 is what a fresh game mark carries."));
            _class->property("label", &Blip::GetLabel, &Blip::SetLabel,
                v8pp::metadata::property_docs("string", "The blip's name on the map screen, shown verbatim in place of the name the game gives its icon type; empty keeps that name. Cut to 64 bytes. The compass has no text, so it never shows this."));
            _class->property("compass", &Blip::GetCompass, &Blip::SetCompass, v8pp::metadata::property_docs("boolean", "Whether the blip is drawn on players' HUD compasses."));
            _class->property("map", &Blip::GetMap, &Blip::SetMap,
                v8pp::metadata::property_docs("boolean",
                    "Whether the blip is drawn on players' map screens, for an icon the map has art for: `Main`, `Side`, `Micro`, `Checkpoint`, `Dlcs`, `Bailiff`, `FistFight` and `Racing` have none and stay on the compass. The map legend's switch for the blip's icon type hides it like one of the game's own."));
        }
        return *_class;
    }

    void Blip::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    void Blip::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        v8pp::class_<Blip> &cls = GetClass(isolate);

        cls.static_function("create", &JS_Create,
            v8pp::metadata::docs("Blip",
                {
                    v8pp::metadata::param("options", "{ position: Vector3; type?: string; markState?: number; label?: string; compass?: boolean; map?: boolean; player?: Player; virtualWorld?: number }", false,
                        "Where the blip goes, which icon it draws with -- one of `Blip.types()`, `GeneralPoi` by default -- its mark state, its name on the map screen, and whether it shows on the compass and the map screen, both by default. "
                        "`player` makes it private to that player from the start; `virtualWorld` puts a public one in that world, the global one by default."),
                },
                "Puts a blip on the compass and map screen of every player in its virtual world, or of the one player it is private to. It is a replicated entity that is never culled by distance -- a blip is for finding something far away -- so it reaches players however far "
                "they are and whenever they join, and assigning `position` moves it for all of them. `setVisibleTo` works on it like on any entity.",
                "The new blip handle."));
        cls.static_function("all", &JS_All,
            v8pp::metadata::docs("Blip[]", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to list; omitted lists every one of them.")}, "Lists every blip the server currently has.", "One handle per live blip, in no particular order."));
        cls.static_function("getById", &JS_GetById, v8pp::metadata::docs("Blip | null", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Looks a blip up by its network entity ID.", "The blip's handle, or null when no live blip has that ID."));
        cls.static_function("removeAll", &JS_RemoveAll, v8pp::metadata::docs("number", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to clear; omitted clears every one of them.")}, "Removes blips.", "How many blips were removed."));
        cls.static_function("types", &JS_Types, v8pp::metadata::docs("string[]", {}, "Every icon a blip can draw with, by the game's own name for it, misspellings included.", "The names, in the game's own order."));
        cls.publish(global);
    }
} // namespace KCDC::Scripting
