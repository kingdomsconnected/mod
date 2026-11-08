#include "quest_scripting.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"

#include "features/quest/quest_verbs.h"

#include "shared/features/player/player_entity.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/quest_options.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>
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
        Quest *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<Quest>::unwrap_object(info.GetIsolate(), info.This());
        }

        /**
         * The connection a player network id belongs to.
         *
         * A quest given to one player is streamed to one connection, so the
         * avatar's owning peer is what has to be named. An id that resolves to
         * nothing leaves the quest world-wide rather than silently going to
         * nobody, because a resource that meant "everyone" and a resource that
         * mistyped an id both read the same here.
         */
        std::optional<MafiaNet::PeerGuid> OwnerOf(std::uint64_t playerNetworkId) {
            if (playerNetworkId == 0) {
                return std::nullopt;
            }

            auto *replication = Framework::CoreModules::GetReplication();
            if (replication == nullptr) {
                return std::nullopt;
            }

            auto *player = replication->GetEntity<Shared::Entities::HumanEntity>(playerNetworkId);
            if (player == nullptr) {
                return std::nullopt;
            }

            return player->ownerGUID;
        }

        // JS: quest.remove()
        void JS_Remove(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *self = Self(info)) {
                KCDC::Features::Quest::Quests::Remove(self->ResolveQuest());
            }
        }

        // JS: quest.setProgress(progress, announce?)
        void JS_SetProgress(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            auto *self           = Self(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Quest.setProgress: expected (progress, announce?)"));
                return;
            }

            const bool announce = info.Length() < 2 || !info[1]->IsBoolean() || v8pp::from_v8<bool>(isolate, info[1]);
            KCDC::Features::Quest::Quests::SetProgress(self->ResolveQuest(), QuestOptionDetail::ProgressFromName(v8pp::from_v8<std::string>(isolate, info[0])), announce);
        }

        // JS: quest.setObjective(index, objective, announce?)
        void JS_SetObjective(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            auto *self           = Self(info);
            if (self == nullptr || info.Length() < 2 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Quest.setObjective: expected (index, objective, announce?)"));
                return;
            }

            const std::size_t index = static_cast<std::size_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0));
            const bool announce     = info.Length() < 3 || !info[2]->IsBoolean() || v8pp::from_v8<bool>(isolate, info[2]);

            KCDC::Features::Quest::Quests::SetObjective(self->ResolveQuest(), index, ParseQuestObjective(isolate, info[1]), announce);
        }

        // JS: quest.setObjectives(objectives, announce?)
        void JS_SetObjectives(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            auto *self           = Self(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsArray()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Quest.setObjectives: expected (objectives, announce?)"));
                return;
            }

            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> list      = info[0].As<v8::Array>();
            const std::uint32_t length     = std::min<std::uint32_t>(list->Length(), static_cast<std::uint32_t>(Shared::Entities::QuestEntity::kMaxObjectives));

            std::vector<Shared::Entities::QuestObjective> drafts;
            drafts.reserve(length);
            for (std::uint32_t entry = 0; entry < length; ++entry) {
                v8::Local<v8::Value> value;
                if (list->Get(context, entry).ToLocal(&value)) {
                    drafts.push_back(ParseQuestObjective(isolate, value));
                }
            }

            const bool announce = info.Length() < 2 || !info[1]->IsBoolean() || v8pp::from_v8<bool>(isolate, info[1]);
            KCDC::Features::Quest::Quests::SetObjectives(self->ResolveQuest(), drafts, announce);
        }

        /** Shared by `track` and `untrack`: an optional player argument, then the verb. */
        void TrackWith(const v8::FunctionCallbackInfo<v8::Value> &info, bool tracked, const char *usage) {
            v8::Isolate *isolate = info.GetIsolate();
            auto *self           = Self(info);
            if (self == nullptr || (info.Length() >= 1 && !info[0]->IsNumber() && !info[0]->IsUndefined())) {
                isolate->ThrowError(v8pp::to_v8(isolate, usage));
                return;
            }

            std::optional<std::uint64_t> player;
            if (info.Length() >= 1 && info[0]->IsNumber()) {
                player = static_cast<std::uint64_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0));
            }

            info.GetReturnValue().Set(static_cast<std::uint32_t>(KCDC::Features::Quest::Quests::Track(self->ResolveQuest(), tracked, player)));
        }

        // JS: quest.track(player?) -> number
        void JS_Track(const v8::FunctionCallbackInfo<v8::Value> &info) {
            TrackWith(info, true, "Quest.track: expected (player?)");
        }

        // JS: quest.untrack(player?) -> number
        void JS_Untrack(const v8::FunctionCallbackInfo<v8::Value> &info) {
            TrackWith(info, false, "Quest.untrack: expected (player?)");
        }

        // JS: quest.objectives() -> QuestObjective[]
        void JS_Objectives(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> result    = v8::Array::New(isolate);

            auto *self  = Self(info);
            auto *quest = self != nullptr ? self->ResolveQuest() : nullptr;
            if (quest == nullptr) {
                info.GetReturnValue().Set(result);
                return;
            }

            for (std::uint32_t index = 0; index < quest->objectiveCount; ++index) {
                v8::Local<v8::Object> entry = v8::Object::New(isolate);
                entry->Set(context, v8pp::to_v8(isolate, "text"), v8pp::to_v8(isolate, quest->objectiveText[index])).Check();
                entry->Set(context, v8pp::to_v8(isolate, "progress"), v8pp::to_v8(isolate, std::string(QuestOptionDetail::ProgressName(quest->objectiveProgress[index])))).Check();
                entry->Set(context, v8pp::to_v8(isolate, "optional"), v8pp::to_v8(isolate, quest->objectiveOptional[index])).Check();
                if (quest->objectiveHasPosition[index]) {
                    entry->Set(context, v8pp::to_v8(isolate, "position"), v8pp::to_v8(isolate, quest->objectivePosition[index])).Check();
                }
                result->Set(context, index, entry).Check();
            }

            info.GetReturnValue().Set(result);
        }

        // JS: Quest.give(key, title, options?, virtualWorld?) -> Quest
        void JS_Give(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 2 || !info[0]->IsString() || !info[1]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Quest.give: expected (key, title, options?, virtualWorld?)"));
                return;
            }

            const std::string questKey = v8pp::from_v8<std::string>(isolate, info[0]);
            const std::string title    = v8pp::from_v8<std::string>(isolate, info[1]);
            const QuestOptions options = ParseQuestOptions(info, 2);

            const std::uint32_t virtualWorld = info.Length() >= 4 && info[3]->IsNumber() ? static_cast<std::uint32_t>(v8pp::from_v8<double>(isolate, info[3])) : MafiaNet::VIRTUAL_WORLD_GLOBAL;

            auto *quest = KCDC::Features::Quest::Quests::Give(questKey, title, options.description, options.questType, options.objectives, OwnerOf(options.playerNetworkId), virtualWorld);
            if (quest == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Quest.give: refused \"" + questKey + "\"; the key is malformed or already taken in that world"));
                return;
            }

            // Given and announced are two separate things, so a resource can
            // seed a journal on join without a toast for every entry.
            if (options.announce) {
                KCDC::Features::Quest::Quests::Announce(quest, Shared::RPC::QuestAnnounceKind::Started);
            }

            if (options.track) {
                KCDC::Features::Quest::Quests::Track(quest, true);
            }

            info.GetReturnValue().Set(WrapQuest(isolate, quest->GetNetworkID()));
        }

        // JS: Quest.all(virtualWorld?) -> Quest[]
        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> quests    = v8::Array::New(isolate);

            std::uint32_t index = 0;
            for (auto *quest : KCDC::Features::Quest::Quests::Collect(WorldArg(info, 0))) {
                quests->Set(context, index++, WrapQuest(isolate, quest->GetNetworkID())).Check();
            }
            info.GetReturnValue().Set(quests);
        }

        // JS: Quest.find(key, virtualWorld?) -> Quest | null
        void JS_Find(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Quest.find: expected (key, virtualWorld?)"));
                return;
            }

            const std::uint64_t playerNetworkId = info.Length() >= 3 && info[2]->IsNumber() ? static_cast<std::uint64_t>(info[2]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0)) : 0;
            auto *quest                         = KCDC::Features::Quest::Quests::Find(v8pp::from_v8<std::string>(isolate, info[0]), WorldArg(info, 1), OwnerOf(playerNetworkId));
            if (quest == nullptr) {
                info.GetReturnValue().SetNull();
                return;
            }

            info.GetReturnValue().Set(WrapQuest(isolate, quest->GetNetworkID()));
        }

        // JS: Quest.getById(id) -> Quest | null
        void JS_GetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Quest.getById: expected (id)"));
                return;
            }

            const std::uint64_t networkId = static_cast<std::uint64_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0));
            info.GetReturnValue().Set(WrapQuest(isolate, networkId));
        }

        // JS: Quest.removeAll(virtualWorld?) -> number
        void JS_RemoveAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(static_cast<std::uint32_t>(KCDC::Features::Quest::Quests::RemoveAll(WorldArg(info, 0))));
        }
    } // namespace

    Shared::Entities::QuestEntity *Quest::ResolveQuest() const {
        return dynamic_cast<Shared::Entities::QuestEntity *>(Resolve());
    }

    std::string Quest::GetKey() const {
        auto *quest = ResolveQuest();
        return quest != nullptr ? quest->questKey : std::string();
    }

    std::string Quest::GetTitle() const {
        auto *quest = ResolveQuest();
        return quest != nullptr ? quest->title : std::string();
    }

    void Quest::SetTitle(const std::string &title) {
        if (auto *quest = ResolveQuest()) {
            KCDC::Features::Quest::Quests::SetText(quest, title, quest->description);
        }
    }

    std::string Quest::GetDescription() const {
        auto *quest = ResolveQuest();
        return quest != nullptr ? quest->description : std::string();
    }

    void Quest::SetDescription(const std::string &description) {
        if (auto *quest = ResolveQuest()) {
            KCDC::Features::Quest::Quests::SetText(quest, quest->title, description);
        }
    }

    std::string Quest::GetType() const {
        auto *quest = ResolveQuest();
        return quest != nullptr ? std::string(QuestOptionDetail::TypeName(quest->questType)) : std::string();
    }

    std::string Quest::GetProgress() const {
        auto *quest = ResolveQuest();
        return quest != nullptr ? std::string(QuestOptionDetail::ProgressName(quest->progress)) : std::string();
    }

    void Quest::SetProgress(const std::string &progress) {
        KCDC::Features::Quest::Quests::SetProgress(ResolveQuest(), QuestOptionDetail::ProgressFromName(progress), true);
    }

    double Quest::GetPlayerId() const {
        const std::optional<MafiaNet::PeerGuid> owner = KCDC::Features::Quest::Quests::OwnerOf(ResolveQuest());
        auto *replication                             = Framework::CoreModules::GetReplication();
        if (!owner.has_value() || replication == nullptr) {
            return 0.0;
        }

        auto *human = replication->GetViewerAs<Shared::Entities::HumanEntity>(*owner);
        return human != nullptr ? static_cast<double>(human->GetNetworkID()) : 0.0;
    }

    double Quest::GetObjectiveCount() const {
        auto *quest = ResolveQuest();
        return quest != nullptr ? static_cast<double>(quest->objectiveCount) : 0.0;
    }

    std::string Quest::ToString() const {
        std::ostringstream ss;
        ss << "Quest{ id: " << GetId() << ", key: " << GetKey() << ", progress: " << GetProgress() << ", objectives: " << GetObjectiveCount() << " }";
        return ss.str();
    }

    v8pp::class_<Quest> &Quest::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (!_class) {
            // v8pp's inherit<Entity> needs the framework Entity registered first.
            Framework::Scripting::Builtins::Entity::GetClass(isolate);

            _class = std::make_unique<v8pp::class_<Quest>>(isolate, ServerCatalog(), "Quest", "Replicated journal quest handle.");
            _class->auto_wrap_objects(true);
            _class->inherit<Framework::Scripting::Builtins::Entity>()
                .ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Creates a script wrapper for an existing quest with this ID; use Quest.give() to write one."))
                .function("toString", &Quest::ToString, v8pp::metadata::docs("string", {}, "Formats this quest handle for logging and debugging.", "The quest ID, its key, its state and how many objectives it carries."));

            _class->prototype_function("remove", &JS_Remove, v8pp::metadata::docs("void", {}, "Takes this quest out of every journal it was written into."));
            _class->prototype_function("setProgress", &JS_SetProgress,
                v8pp::metadata::docs("void",
                    {
                        v8pp::metadata::param("progress", "'active' | 'done' | 'failed'", false, "Where the quest now stands."),
                        v8pp::metadata::param("announce", "boolean", true, "Whether to raise the game's quest-updated toast; defaults to true. Pass false for a correction the player should not be told about."),
                    },
                    "Moves the quest on. There is no way back to unstarted: a quest nobody should see any more is removed."));
            _class->prototype_function("setObjective", &JS_SetObjective,
                v8pp::metadata::docs("void",
                    {
                        v8pp::metadata::param("index", "number", false, "Which objective to rewrite, counting from zero. An index past the end is ignored."),
                        v8pp::metadata::param("objective", kQuestObjectiveType, false, "The line, as text or as an object carrying its state."),
                        v8pp::metadata::param("announce", "boolean", true, "Whether to raise the quest-updated toast; defaults to true."),
                    },
                    "Rewrites one line of the quest in place."));
            _class->prototype_function("setObjectives", &JS_SetObjectives,
                v8pp::metadata::docs("void",
                    {
                        v8pp::metadata::param("objectives", "(" + std::string(kQuestObjectiveType) + ")[]", false, "The whole list, at most eight entries; anything past that is dropped."),
                        v8pp::metadata::param("announce", "boolean", true, "Whether to raise the quest-updated toast; defaults to true."),
                    },
                    "Replaces the quest's objective list."));
            _class->prototype_function("objectives", &JS_Objectives,
                v8pp::metadata::docs("{ text: string; progress: 'active' | 'done' | 'failed' | 'none'; optional: boolean; position?: Vector3 }[]", {}, "Reads the quest's objectives back.",
                    "One entry per objective, in journal order. `position` is present only on an objective that has one."));
            _class->prototype_function("track", &JS_Track,
                v8pp::metadata::docs("number", {v8pp::metadata::param("player", "number", true, "Network id of the player who should follow the quest. Omitted, everyone who has the quest follows it: its one owner, or every player in its world.")},
                    "Makes the game follow this quest, exactly as the journal's track button does: it is listed in the HUD tracker, and every active objective that has a `position` is drawn as the game's own quest marker on the map and the compass. "
                    "It is an instruction, not a lock -- the player can untrack it from the journal -- and the outcome arrives as `questTrackingChanged` like any other change. A quest given a moment ago can be tracked at once; the client waits for it to arrive.",
                    "How many clients were told. Zero when the player does not have this quest."));
            _class->prototype_function("untrack", &JS_Untrack,
                v8pp::metadata::docs("number", {v8pp::metadata::param("player", "number", true, "Network id of the player who should stop following the quest. Omitted, everyone who has the quest stops.")},
                    "Makes the game stop following this quest, which takes its markers off the map and the compass. The outcome arrives as `questTrackingChanged`.", "How many clients were told."));

            _class->property("key", &Quest::GetKey, v8pp::metadata::property_docs("string", "The key the quest is filed under, and the name the game knows the quest node by. Read-only: it is the quest's identity."));
            _class->property("title", &Quest::GetTitle, &Quest::SetTitle, v8pp::metadata::property_docs("string", "The line the journal row and the quest toast show. Assignment is silent; it does not raise a toast."));
            _class->property("description", &Quest::GetDescription, &Quest::SetDescription, v8pp::metadata::property_docs("string", "The body of the journal's diary page for this quest. Assignment is silent."));
            _class->property("type", &Quest::GetType, v8pp::metadata::property_docs("string", "Which section of the journal the quest files under. Read-only: it is decided when the quest is given."));
            _class->property("progress", &Quest::GetProgress, &Quest::SetProgress, v8pp::metadata::property_docs("string", "`active`, `done` or `failed`. Assignment announces the change; `setProgress` can do it quietly."));
            _class->property("player", &Quest::GetPlayerId, v8pp::metadata::property_docs("number", "Network id of the one player this quest was written for, or 0 when it went to everyone in the world. Read-only: who a quest belongs to is decided when it is given."));
            _class->property("objectiveCount", &Quest::GetObjectiveCount, v8pp::metadata::property_docs("number", "How many objectives the quest carries, up to eight."));
        }
        return *_class;
    }

    void Quest::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    void Quest::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("questTrackingChanged", "[quest: Quest, player: Player, tracked: boolean]",
            "Dispatched when a player starts or stops following a quest in the game's journal, whichever way it happened: the track button, the game auto-tracking a quest that turns active, or the untrack that follows finishing or failing one. "
            "The client reports it -- following a quest is decided there and cannot be refused here -- so a handler reacts rather than vetoes. It fires only on an actual change, and only for a quest that player was given.");

        v8pp::class_<Quest> &cls = GetClass(isolate);

        cls.static_function("give", &JS_Give,
            v8pp::metadata::docs("Quest",
                {
                    v8pp::metadata::param("key", "string", false, "What the quest is filed under: up to 64 letters, digits, underscores or dashes, unique within its virtual world."),
                    v8pp::metadata::param("title", "string", false, "The line the journal row shows."),
                    v8pp::metadata::param("options", kQuestOptionsType, true,
                        "`description` is the diary page, `type` the journal section, `objectives` the lines under it -- each with an optional world `position` the game marks on the map and compass while the quest is followed -- `player` the network id of the one player it belongs to, `announce` whether to raise the toast, and `track` whether its recipients start following it."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world the quest belongs to; omitted puts it in the global one."),
                },
                "Writes a quest into the game's own journal. It is a replicated entity, so a player who joins late, reloads or walks away still finds it in their log -- which is what a quest needs and what a one-shot notification cannot do. "
                "What the player sees is the game's quest UI: its journal row, its diary page, its objective tracker and its quest-updated toast, all reading a quest node the client builds with the game's own constructor.",
                "The newly written quest handle."));

        cls.static_function("all", &JS_All,
            v8pp::metadata::docs("Quest[]", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to list; omitted lists every one of them.")}, "Lists every replicated quest the server currently has.",
                "One handle per live quest, in no particular order."));
        cls.static_function("find", &JS_Find,
            v8pp::metadata::docs("Quest | null",
                {
                    v8pp::metadata::param("key", "string", false, "The key the quest was given under."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to search; omitted searches every one of them."),
                    v8pp::metadata::param("player", "number", true, "Network id of the player whose copy to find. Keys are unique per recipient, so this is how one player's errand is told from another's."),
                },
                "Looks a quest up by its key.", "The quest's handle, or null when no live quest has that key."));
        cls.static_function("getById", &JS_GetById,
            v8pp::metadata::docs("Quest | null", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Looks a replicated quest up by its network entity ID.", "The quest's handle, or null when no live quest has that ID."));
        cls.static_function("removeAll", &JS_RemoveAll,
            v8pp::metadata::docs("number", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to clear; omitted clears every one of them.")}, "Takes quests out of every journal they were written into.", "How many quests were removed."));
        cls.publish(global);
    }

    void Events::QuestTrackingChanged(std::uint64_t questNetworkId, std::uint64_t playerNetworkId, bool tracked) {
        EmitReservedEvent("questTrackingChanged", [questNetworkId, playerNetworkId, tracked](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapQuest(isolate, questNetworkId));
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(v8pp::to_v8(isolate, tracked));
            return args;
        });
    }
} // namespace KCDC::Scripting
