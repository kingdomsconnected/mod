#include "features/player/player_appearance_scripting.h"

#include "shared/features/player/player_appearance_bindings.h"
#include "shared/game/appearance_catalog.h"
#include "shared/scripting/catalog.h"

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace KCDC::Scripting {
    namespace {
        /** Male by default: what an unset appearance carries, and what Henry is. */
        Shared::Game::AppearanceGender GenderFromArg(v8::Isolate *isolate, const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            if (info.Length() <= index || !info[index]->IsString()) {
                return Shared::Game::AppearanceGender::Male;
            }
            return v8pp::from_v8<std::string>(isolate, info[index]) == "female" ? Shared::Game::AppearanceGender::Female : Shared::Game::AppearanceGender::Male;
        }

        std::vector<const Shared::Game::AppearanceOption *> OptionsFor(Shared::Game::AppearancePart part, Shared::Game::AppearanceGender gender) {
            std::vector<const Shared::Game::AppearanceOption *> matches;
            for (const Shared::Game::AppearanceOption &option : Shared::Game::g_AppearanceOptions) {
                if (option.part == part && option.gender == gender) {
                    matches.push_back(&option);
                }
            }
            return matches;
        }

        /** The beards one head can actually wear, as catalog options. */
        std::vector<const Shared::Game::AppearanceOption *> BeardsFor(Shared::Game::AppearanceGender gender, std::string_view head) {
            std::vector<const Shared::Game::AppearanceOption *> matches;
            for (const std::string_view name : Shared::Game::AppearanceBeardsForHead(gender, head)) {
                if (const Shared::Game::AppearanceOption *const option = Shared::Game::FindAppearanceOption(Shared::Game::AppearancePart::Beard, gender, name)) {
                    matches.push_back(option);
                }
            }
            return matches;
        }

        void ReturnOptions(const v8::FunctionCallbackInfo<v8::Value> &info, const std::vector<const Shared::Game::AppearanceOption *> &matches) {
            v8::Isolate *isolate           = info.GetIsolate();
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> out       = v8::Array::New(isolate, static_cast<int>(matches.size()));
            for (std::size_t i = 0; i < matches.size(); ++i) {
                out->Set(context, static_cast<std::uint32_t>(i), AppearanceOptionToV8(isolate, *matches[i])).Check();
            }
            info.GetReturnValue().Set(out);
        }
    } // namespace

    // JS: Appearances.options(part, gender?) -> AppearanceOption[]
    void Appearance::JS_Options(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 1 || !info[0]->IsString()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Appearances.options: expected (part, gender?)"));
            return;
        }

        const Shared::Game::AppearancePart part = Detail::FindAppearancePart(v8pp::from_v8<std::string>(isolate, info[0]));
        if (part == Shared::Game::AppearancePart::Count) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Appearances.options: part must be one of \"body\", \"head\", \"hair\" or \"beard\""));
            return;
        }

        ReturnOptions(info, OptionsFor(part, GenderFromArg(isolate, info, 1)));
    }

    // JS: Appearances.beards(head?, gender?) -> AppearanceOption[]
    void Appearance::JS_Beards(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() >= 1 && !info[0]->IsString() && !info[0]->IsUndefined() && !info[0]->IsNull()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Appearances.beards: expected (head?, gender?)"));
            return;
        }

        const std::string head = info.Length() >= 1 && info[0]->IsString() ? v8pp::from_v8<std::string>(isolate, info[0]) : std::string();
        ReturnOptions(info, BeardsFor(GenderFromArg(isolate, info, 1), head));
    }

    // JS: Appearances.find(part, name, gender?) -> AppearanceOption | null
    void Appearance::JS_Find(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 2 || !info[0]->IsString() || !info[1]->IsString()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Appearances.find: expected (part, name, gender?)"));
            return;
        }

        const Shared::Game::AppearancePart part = Detail::FindAppearancePart(v8pp::from_v8<std::string>(isolate, info[0]));
        if (part == Shared::Game::AppearancePart::Count) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Appearances.find: part must be one of \"body\", \"head\", \"hair\" or \"beard\""));
            return;
        }

        const std::string name                          = v8pp::from_v8<std::string>(isolate, info[1]);
        const Shared::Game::AppearanceOption *const hit = Shared::Game::FindAppearanceOption(part, GenderFromArg(isolate, info, 2), name);
        if (hit == nullptr) {
            info.GetReturnValue().SetNull();
            return;
        }
        info.GetReturnValue().Set(AppearanceOptionToV8(isolate, *hit));
    }

    // JS: Appearances.random(gender?) -> Appearance
    void Appearance::JS_Random(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        Shared::Game::CharacterAppearance appearance;
        appearance.gender = GenderFromArg(isolate, info, 0);

        // One generator for the server's lifetime: seeding per call from a
        // clock hands consecutive callers the same body.
        static std::mt19937 generator {std::random_device {}()};

        for (std::size_t i = 0; i < Shared::Game::CharacterAppearance::kPartCount; ++i) {
            const auto part = static_cast<Shared::Game::AppearancePart>(i);

            // Beards come last in part order, so the face is already rolled
            // and only what was modelled for it can be drawn.
            const std::vector<const Shared::Game::AppearanceOption *> choices = part == Shared::Game::AppearancePart::Beard ? BeardsFor(appearance.gender, appearance.Part(Shared::Game::AppearancePart::Head)) : OptionsFor(part, appearance.gender);

            // The female tree has no beards, and 14 male faces wear none.
            if (choices.empty()) {
                continue;
            }
            std::uniform_int_distribution<std::size_t> pick(0, choices.size() - 1);
            appearance.Part(part) = std::string(choices[pick(generator)]->name);
        }
        info.GetReturnValue().Set(AppearanceToV8(isolate, appearance));
    }

    void Appearance::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        // Also registered by the Player handles; neither side owns it.
        RegisterAppearanceDataTypes(ServerCatalog());

        v8pp::module appearance(isolate, ServerCatalog(), "Appearances",
            "The game's own character-component catalog: every face, hairstyle, beard and skin a player's body can be given.\n\nEveryone on a fresh server is Henry, because a body with nothing chosen is the one the game hands out. This is where the alternatives come from; `player.setAppearance` is what puts one on somebody.");

        appearance.function("options", &Appearance::JS_Options,
            v8pp::metadata::docs("AppearanceOption[]",
                {
                    v8pp::metadata::param("part", "\"body\" | \"head\" | \"hair\" | \"beard\"", false, "Which part to list."),
                    v8pp::metadata::param("gender", "\"male\" | \"female\"", true, "Which half of the catalog to read; male by default."),
                },
                "Every option the game ships for one part. The male tree has 212 faces, 262 hairstyles, 51 beards and 47 skins; the female tree has 61, 71, none and 41.\n\nThe hairstyle count is large because the game ships each style once per colour rather than colouring one. Group by `option.group` to get the styles back.\n\nThe 51 beards are the whole catalog, not what any one body can wear: a beard is modelled per face and only exists for the faces it was modelled for. `Appearances.beards` is the list to show somebody.",
                "The options, in the catalog's own order."));

        appearance.function("beards", &Appearance::JS_Beards,
            v8pp::metadata::docs("AppearanceOption[]",
                {
                    v8pp::metadata::param("head", "string", true, "The face to ask about -- a name from `Appearances.options(\"head\")`. Omitted or empty means the face a body nobody has chosen one for wears."),
                    v8pp::metadata::param("gender", "\"male\" | \"female\"", true, "Which half of the catalog the head came from; male by default."),
                },
                "The beards that face can actually grow.\n\nA beard is modelled against one head's mesh and stored under that head's own folder, so asking for one the artists never made for the face wearing it does nothing at all -- no error, no beard, a body that comes back clean-shaven. The owning client refuses such a pair outright, so filter a chooser through this rather than through `options(\"beard\")`.\n\nThe 164 generic faces carry 24 each, Henry's face carries the 15 the game's own barber offers, and 14 faces carry none.",
                "The wearable beards, in the catalog's own order. Empty for a female body, for a face with none, and for a name the catalog does not carry."));

        appearance.function("find", &Appearance::JS_Find,
            v8pp::metadata::docs("AppearanceOption | null",
                {
                    v8pp::metadata::param("part", "\"body\" | \"head\" | \"hair\" | \"beard\"", false, "Which part the name is for."),
                    v8pp::metadata::param("name", "string", false, "The component name to look up."),
                    v8pp::metadata::param("gender", "\"male\" | \"female\"", true, "Which half of the catalog to look in; male by default."),
                },
                "Looks one component name up, so a resource can check a name is real before handing it to a player.", "What the catalog says about it, or null for a name it does not carry."));

        appearance.function("random", &Appearance::JS_Random,
            v8pp::metadata::docs("Appearance", {v8pp::metadata::param("gender", "\"male\" | \"female\"", true, "Which half of the catalog to draw from; male by default.")},
                "Picks one option for every part, which is the cheapest way to stop a server full of Henrys. Hand the result straight to `player.setAppearance`.\n\nUniform over the catalog rather than over what looks good together: the game's own faces and hairstyles are authored for particular NPCs, so a random body is recognisably random. The beard is drawn after the face and only from what that face can wear, so the result is always one a client will accept.",
                "A complete appearance. `beard` is empty for a female one, which the catalog has none of, and for the fourteen male faces no beard was modelled for."));

        appearance.publish(global);
    }
} // namespace KCDC::Scripting
