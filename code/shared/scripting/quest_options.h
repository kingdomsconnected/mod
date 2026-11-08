#pragma once

#include "shared/features/quest/quest_entity.h"
#include "shared/features/quest/quest_progress.h"
#include "shared/scripting/option_field.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <scripting/v8pp_types.h>
#include <string>
#include <v8.h>
#include <v8pp/convert.hpp>
#include <vector>

namespace KCDC::Scripting {
    /** The options bag the `Quest` verbs take. Absent or mistyped fields keep their defaults, like the rest of the API's option bags. */
    struct QuestOptions {
        std::string description;
        Shared::Entities::QuestType questType = Shared::Entities::QuestType::Side;
        std::vector<Shared::Entities::QuestObjective> objectives;

        /** Network id of the player this quest belongs to, or zero for everyone in the world. */
        std::uint64_t playerNetworkId = 0;

        /** Whether a change should raise the game's own quest-updated toast. */
        bool announce = true;

        /** Whether whoever receives the quest starts following it, which is what shows its markers. */
        bool track = false;
    };

    namespace QuestOptionDetail {
        /** The journal section a name means. Anything unrecognised is a side quest, which is the harmless answer. */
        inline Shared::Entities::QuestType TypeFromName(const std::string &name) {
            if (name == "main") {
                return Shared::Entities::QuestType::Main;
            }
            if (name == "activity") {
                return Shared::Entities::QuestType::Activity;
            }
            if (name == "event") {
                return Shared::Entities::QuestType::Event;
            }
            if (name == "micro") {
                return Shared::Entities::QuestType::Micro;
            }
            if (name == "racing") {
                return Shared::Entities::QuestType::Racing;
            }
            return Shared::Entities::QuestType::Side;
        }

        inline const char *TypeName(std::uint8_t questType) {
            switch (static_cast<Shared::Entities::QuestType>(questType)) {
            case Shared::Entities::QuestType::Main: return "main";
            case Shared::Entities::QuestType::Activity: return "activity";
            case Shared::Entities::QuestType::Event: return "event";
            case Shared::Entities::QuestType::Micro: return "micro";
            case Shared::Entities::QuestType::Racing: return "racing";
            default: return "side";
            }
        }

        inline Shared::Entities::QuestProgress ProgressFromName(const std::string &name) {
            if (name == "done") {
                return Shared::Entities::QuestProgress::Done;
            }
            if (name == "failed") {
                return Shared::Entities::QuestProgress::Failed;
            }
            return Shared::Entities::QuestProgress::Active;
        }

        inline const char *ProgressName(std::uint8_t progress) {
            switch (static_cast<Shared::Entities::QuestProgress>(progress)) {
            case Shared::Entities::QuestProgress::Done: return "done";
            case Shared::Entities::QuestProgress::Failed: return "failed";
            case Shared::Entities::QuestProgress::None: return "none";
            default: return "active";
            }
        }
    } // namespace QuestOptionDetail

    /** Reads one objective out of a JS object; a bare string is the text with everything else defaulted. */
    inline Shared::Entities::QuestObjective ParseQuestObjective(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        Shared::Entities::QuestObjective objective;

        if (value->IsString()) {
            objective.text = v8pp::from_v8<std::string>(isolate, value);
            return objective;
        }

        if (!value->IsObject()) {
            return objective;
        }

        v8::Local<v8::Object> entry = value.As<v8::Object>();

        const v8::Local<v8::Value> text = OptionField(isolate, entry, "text");
        if (!text.IsEmpty() && text->IsString()) {
            objective.text = v8pp::from_v8<std::string>(isolate, text);
        }

        const v8::Local<v8::Value> progress = OptionField(isolate, entry, "progress");
        if (!progress.IsEmpty() && progress->IsString()) {
            objective.progress = QuestOptionDetail::ProgressFromName(v8pp::from_v8<std::string>(isolate, progress));
        }

        const v8::Local<v8::Value> optional = OptionField(isolate, entry, "optional");
        if (!optional.IsEmpty() && optional->IsBoolean()) {
            objective.optional = v8pp::from_v8<bool>(isolate, optional);
        }

        // A position that does not read as three finite numbers is no marker
        // rather than a marker at the origin.
        const v8::Local<v8::Value> position = OptionField(isolate, entry, "position");
        if (!position.IsEmpty() && position->IsObject()) {
            const glm::vec3 parsed = v8pp::convert<glm::vec3>::from_v8(isolate, position);
            if (std::isfinite(parsed.x) && std::isfinite(parsed.y) && std::isfinite(parsed.z)) {
                objective.hasPosition = true;
                objective.position    = parsed;
            }
        }

        return objective;
    }

    /** Reads the options object at `index`. Anything past the objective ceiling is dropped rather than refused. */
    inline QuestOptions ParseQuestOptions(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
        QuestOptions parsed;
        v8::Isolate *isolate = info.GetIsolate();
        if (info.Length() <= index || !info[index]->IsObject()) {
            return parsed;
        }

        v8::Local<v8::Object> options  = info[index].As<v8::Object>();
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        const v8::Local<v8::Value> description = OptionField(isolate, options, "description");
        if (!description.IsEmpty() && description->IsString()) {
            parsed.description = v8pp::from_v8<std::string>(isolate, description);
        }

        const v8::Local<v8::Value> questType = OptionField(isolate, options, "type");
        if (!questType.IsEmpty() && questType->IsString()) {
            parsed.questType = QuestOptionDetail::TypeFromName(v8pp::from_v8<std::string>(isolate, questType));
        }

        const v8::Local<v8::Value> player = OptionField(isolate, options, "player");
        if (!player.IsEmpty() && player->IsNumber()) {
            parsed.playerNetworkId = static_cast<std::uint64_t>(player->IntegerValue(context).FromMaybe(0));
        }

        const v8::Local<v8::Value> announce = OptionField(isolate, options, "announce");
        if (!announce.IsEmpty() && announce->IsBoolean()) {
            parsed.announce = v8pp::from_v8<bool>(isolate, announce);
        }

        const v8::Local<v8::Value> track = OptionField(isolate, options, "track");
        if (!track.IsEmpty() && track->IsBoolean()) {
            parsed.track = v8pp::from_v8<bool>(isolate, track);
        }

        const v8::Local<v8::Value> objectives = OptionField(isolate, options, "objectives");
        if (!objectives.IsEmpty() && objectives->IsArray()) {
            v8::Local<v8::Array> list  = objectives.As<v8::Array>();
            const std::uint32_t length = std::min<std::uint32_t>(list->Length(), static_cast<std::uint32_t>(Shared::Entities::QuestEntity::kMaxObjectives));

            for (std::uint32_t entry = 0; entry < length; ++entry) {
                v8::Local<v8::Value> value;
                if (list->Get(context, entry).ToLocal(&value)) {
                    parsed.objectives.push_back(ParseQuestObjective(isolate, value));
                }
            }
        }

        return parsed;
    }

    /** The TypeScript the bags are documented as, so every binding spells them identically. */
    inline constexpr const char *kQuestObjectiveType = "string | { text: string; progress?: 'active' | 'done' | 'failed'; optional?: boolean; position?: Vector3 }";
    inline constexpr const char *kQuestOptionsType =
        "{ description?: string; type?: 'main' | 'side' | 'activity' | 'event' | 'micro' | 'racing'; objectives?: (string | { text: string; progress?: 'active' | 'done' | 'failed'; optional?: boolean; position?: Vector3 })[]; player?: number; announce?: boolean; track?: boolean }";
} // namespace KCDC::Scripting
