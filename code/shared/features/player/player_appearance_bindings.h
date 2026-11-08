#pragma once

#include "shared/features/player/player_appearance.h"
#include "shared/game/appearance_catalog.h"

#include <v8.h>
#include <v8pp/convert.hpp>
#include <v8pp/metadata.hpp>

#include <cstddef>
#include <string>

// The scripted shape of an appearance, bound once for both sides: the
// server's Player reads it back and asks for a new one, the client's only
// reads. Written twice, the two would drift over what a missing part means.
namespace KCDC::Scripting {
    inline void RegisterAppearanceDataTypes(v8pp::metadata::registry &catalog) {
        auto &appearance = catalog.data_type("Appearance",
            "How a player's body looks: four of the game's own character-component names, and the gender whose catalog they come from.\n\nA part left out, or set to an empty string, means \"leave that one alone\" -- the body keeps whatever it would have had. An appearance with every part empty is the default body, which is what everyone wears until something chooses otherwise.");
        appearance.add_property("gender", "\"male\" | \"female\"",
            "Which half of the game's character-component tree the four names come from. Not one of the four: a body's gender comes from its soul, and a name from one tree means nothing under the other, so this travels with them and is applied first.");
        appearance.add_property("body", "string", "The skin: complexion and build. One of `Appearances.options(\"body\", gender)`, or empty.");
        appearance.add_property("head", "string", "The face. One of `Appearances.options(\"head\", gender)`, or empty.");
        appearance.add_property("hair", "string", "The hairstyle, including its colour -- the game ships each style recoloured rather than colouring one. One of `Appearances.options(\"hair\", gender)`, or empty.");
        appearance.add_property("beard", "string",
            "The beard. Male only -- the female tree has none -- and one of `Appearances.beards(head)`, or empty.\n\nNot every beard goes with every face: they are modelled per head, and a body asked for a pair that was never modelled refuses the whole appearance rather than quietly losing the beard.");

        auto &option = catalog.data_type("AppearanceOption", "One entry of the game's character-component tree: something a body can be given for one part.");
        option.add_property("name", "string", "What to put in the part. This is the game's own node name.");
        option.add_property("part", "\"body\" | \"head\" | \"hair\" | \"beard\"", "Which part it fills.");
        option.add_property("gender", "\"male\" | \"female\"", "Which tree it came out of. An option is only valid on a body of the same gender.");
        option.add_property("group", "string | null",
            "The option this one is a variation of, or null when it stands on its own. The game ships a hairstyle once per colour, all derived from the same style node, so grouping by this is what turns 262 hairstyles into a list somebody can read.");
    }

    namespace Detail {
        inline const char *AppearancePartName(Shared::Game::AppearancePart part) {
            switch (part) {
            case Shared::Game::AppearancePart::Body: return "body";
            case Shared::Game::AppearancePart::Head: return "head";
            case Shared::Game::AppearancePart::Hair: return "hair";
            case Shared::Game::AppearancePart::Beard: return "beard";
            default: return "";
            }
        }

        /** `Count` for a name the game has no slot for. */
        inline Shared::Game::AppearancePart FindAppearancePart(const std::string &name) {
            for (std::size_t i = 0; i < static_cast<std::size_t>(Shared::Game::AppearancePart::Count); ++i) {
                const auto part = static_cast<Shared::Game::AppearancePart>(i);
                if (name == AppearancePartName(part)) {
                    return part;
                }
            }
            return Shared::Game::AppearancePart::Count;
        }

        inline const char *AppearanceGenderName(Shared::Game::AppearanceGender gender) {
            return gender == Shared::Game::AppearanceGender::Female ? "female" : "male";
        }
    } // namespace Detail

    inline v8::Local<v8::Object> AppearanceToV8(v8::Isolate *isolate, const Shared::Game::CharacterAppearance &appearance) {
        v8::EscapableHandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Object> out      = v8::Object::New(isolate);

        out->Set(context, v8pp::to_v8(isolate, "gender"), v8pp::to_v8(isolate, std::string(Detail::AppearanceGenderName(appearance.gender)))).Check();
        for (std::size_t i = 0; i < Shared::Game::CharacterAppearance::kPartCount; ++i) {
            const auto part = static_cast<Shared::Game::AppearancePart>(i);
            out->Set(context, v8pp::to_v8(isolate, Detail::AppearancePartName(part)), v8pp::to_v8(isolate, appearance.Part(part))).Check();
        }
        return handleScope.Escape(out);
    }

    /**
     * An `Appearance` object as the struct that travels. Absent keys are
     * left alone, so `{hair: "..."}` means only the hair; false for a value
     * of the wrong type.
     */
    inline bool AppearanceFromV8(v8::Isolate *isolate, v8::Local<v8::Value> value, Shared::Game::CharacterAppearance &out) {
        if (value.IsEmpty() || !value->IsObject()) {
            return false;
        }
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Object> source   = value.As<v8::Object>();

        const auto read = [&](const char *key, std::string &into) {
            v8::Local<v8::Value> field;
            if (!source->Get(context, v8pp::to_v8(isolate, key)).ToLocal(&field) || field->IsUndefined() || field->IsNull()) {
                return true;
            }
            if (!field->IsString()) {
                return false;
            }
            into = v8pp::from_v8<std::string>(isolate, field);
            return true;
        };

        std::string gender;
        if (!read("gender", gender)) {
            return false;
        }
        if (!gender.empty() && gender != "male" && gender != "female") {
            return false;
        }
        out.gender = gender == "female" ? Shared::Game::AppearanceGender::Female : Shared::Game::AppearanceGender::Male;

        for (std::size_t i = 0; i < Shared::Game::CharacterAppearance::kPartCount; ++i) {
            const auto part = static_cast<Shared::Game::AppearancePart>(i);
            if (!read(Detail::AppearancePartName(part), out.Part(part))) {
                return false;
            }
        }
        return true;
    }

    inline v8::Local<v8::Object> AppearanceOptionToV8(v8::Isolate *isolate, const Shared::Game::AppearanceOption &option) {
        v8::EscapableHandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Object> out      = v8::Object::New(isolate);

        out->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, std::string(option.name))).Check();
        out->Set(context, v8pp::to_v8(isolate, "part"), v8pp::to_v8(isolate, std::string(Detail::AppearancePartName(option.part)))).Check();
        out->Set(context, v8pp::to_v8(isolate, "gender"), v8pp::to_v8(isolate, std::string(Detail::AppearanceGenderName(option.gender)))).Check();
        out->Set(context, v8pp::to_v8(isolate, "group"), option.group.empty() ? v8::Local<v8::Value>(v8::Null(isolate)) : v8pp::to_v8(isolate, std::string(option.group))).Check();
        return handleScope.Escape(out);
    }

} // namespace KCDC::Scripting
