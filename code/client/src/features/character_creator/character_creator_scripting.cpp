#include <utils/safe_win32.h>

#include "character_creator_scripting.h"

#include "character_creator_service.h"

#include "core/scripting/script_context.h"

#include "shared/features/player/player_appearance_bindings.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/option_field.h"

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <optional>
#include <string>
#include <vector>

namespace KCDC::Scripting {
    namespace {
        void ThrowType(v8::Isolate *isolate, const char *message) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, message)));
        }

        // JS: CharacterCreator.open(options?) -> boolean
        void JS_Open(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            KCDC::Features::CharacterCreator::CharacterCreatorService::OpenRequest request;
            if (const v8::Local<v8::Value> genders = OptionField(isolate, info, 0, "genders"); !genders.IsEmpty()) {
                const std::string value = genders->IsString() ? v8pp::from_v8<std::string>(isolate, genders) : std::string();
                if (value == "male") {
                    request.genders = KCDC::Features::CharacterCreator::CharacterCreatorService::kMale;
                }
                else if (value == "female") {
                    request.genders = KCDC::Features::CharacterCreator::CharacterCreatorService::kFemale;
                }
                else if (value != "both") {
                    ThrowType(isolate, "CharacterCreator.open: genders must be \"male\", \"female\" or \"both\"");
                    return;
                }
            }

            if (const v8::Local<v8::Value> title = OptionField(isolate, info, 0, "title"); !title.IsEmpty()) {
                if (!title->IsString()) {
                    ThrowType(isolate, "CharacterCreator.open: title must be a string");
                    return;
                }
                request.title = v8pp::from_v8<std::string>(isolate, title);
                if (request.title.size() > KCDC::Features::CharacterCreator::CharacterCreatorService::kMaxTitle) {
                    isolate->ThrowException(v8::Exception::RangeError(v8pp::to_v8(isolate, "CharacterCreator.open: title is longer than 64 bytes")));
                    return;
                }
            }

            // Where the stage starts: what was asked for, else the first
            // offered gender with every part left to the figure -- which, on
            // the man, is the player's own look.
            if (const v8::Local<v8::Value> appearance = OptionField(isolate, info, 0, "appearance"); !appearance.IsEmpty()) {
                if (!AppearanceFromV8(isolate, appearance, request.appearance) || !request.appearance.IsSane() || !KCDC::Features::CharacterCreator::CharacterCreatorService::Offers(request.genders, request.appearance.gender)) {
                    ThrowType(isolate, "CharacterCreator.open: appearance must name parts from the game's catalog, for a gender the creator offers");
                    return;
                }
            }
            else {
                request.appearance.gender = KCDC::Features::CharacterCreator::CharacterCreatorService::Offers(request.genders, Shared::Game::AppearanceGender::Male) ? Shared::Game::AppearanceGender::Male : Shared::Game::AppearanceGender::Female;
            }

            KCDC::Features::CharacterCreator::CharacterCreatorService::Get().Open(request);
            info.GetReturnValue().Set(true);
        }

        // JS: CharacterCreator.close() -> boolean
        void JS_Close(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(KCDC::Features::CharacterCreator::CharacterCreatorService::Get().Close());
        }

        // JS: CharacterCreator.isOpen() -> boolean
        void JS_IsOpen(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(KCDC::Features::CharacterCreator::CharacterCreatorService::Get().IsOpen());
        }
    } // namespace

    namespace Events {
        void CharacterCreatorClosed(const Shared::Game::CharacterAppearance *accepted, const char *reason) {
            const std::optional<Shared::Game::CharacterAppearance> look = accepted != nullptr ? std::optional<Shared::Game::CharacterAppearance>(*accepted) : std::nullopt;
            const std::string named                                     = reason;
            EmitReservedEvent("characterCreatorClosed", [look, named](v8::Isolate *isolate) {
                v8::Local<v8::Value> appearance = look ? v8::Local<v8::Value>(AppearanceToV8(isolate, *look)) : v8::Local<v8::Value>(v8::Null(isolate));
                return std::vector<v8::Local<v8::Value>> {appearance, v8pp::to_v8(isolate, named)};
            });
        }
    } // namespace Events

    void RegisterCharacterCreator(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ClientCatalog().data_type("EventMap");
        events.add_property("characterCreatorClosed", "[appearance: Appearance | null, reason: \"accepted\" | \"cancelled\" | \"closed\" | \"replaced\" | \"unavailable\" | \"interrupted\"]",
            "Dispatched once for every creator, on this machine only, whatever ended it. `appearance` is the look the player accepted, and null for every other ending. `reason` is `accepted`, `cancelled` for the player backing out, `closed` for `CharacterCreator.close`, `replaced` for another `CharacterCreator.open`, `unavailable` when the stage could not be put up, and `interrupted` when the session or the body went away under it. "
            "Nothing is put on the body: send the look wherever it should go -- `Events.emitServer`, then `player.setAppearance` on the server -- or open the next creator straight from the handler.");

        auto &options = ClientCatalog().data_type("CharacterCreatorOptions", "What `CharacterCreator.open` shows.");
        options.add_property("appearance", v8pp::metadata::type{"Appearance | undefined", {}, true}, "The look the stage starts on. Left out, it starts on the first gender offered with every part left to the figure -- the player's own look on the man, the game's default on the woman.");
        options.add_property("genders", v8pp::metadata::type{"\"male\" | \"female\" | \"both\" | undefined", {}, true}, "Which bodies the player may choose between; `both` (the default) lets them switch.");
        options.add_property("title", v8pp::metadata::type{"string | undefined", {}, true}, "The panel's heading, at most 64 bytes. Defaults to `Character`.");

        v8pp::module creator(isolate, ClientCatalog(), "CharacterCreator",
            "The game's own character stage, for the local player to choose how they look.\n\n"
            "Opens the pause screen on its character page, hides the page's own panels and stands a panel of tabs -- body, face, hair, beard -- beside a figure the player dresses and turns. The keys are listed on the panel, named as the game names them.\n\n"
            "Client-only and local: it changes nothing anywhere. `characterCreatorClosed` reports what the player accepted, and the resource decides what that means.");

        creator.function("open", &JS_Open,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("options", "CharacterCreatorOptions", true, "What to show.")}, "Opens a creator, ending one already open as `replaced`. The stage goes up on the next ticks; one that cannot ends as `unavailable`.",
                "True. Malformed options throw."));
        creator.function("close", &JS_Close, v8pp::metadata::docs("boolean", {}, "Ends the open creator as `closed`.", "False when none was open."));
        creator.function("isOpen", &JS_IsOpen, v8pp::metadata::docs("boolean", {}, "Whether a creator is open or going up.", "True from `open` until its `characterCreatorClosed`."));

        creator.publish(global);
    }
} // namespace KCDC::Scripting
