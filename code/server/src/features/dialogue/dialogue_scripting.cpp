#include "features/dialogue/dialogue_scripting.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"
#include "features/dialogue/dialogue_service.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/option_field.h"

#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <string>
#include <vector>

namespace KCDC::Scripting {
    namespace {
        /**
         * Reads a page out of a script object, or reports why it could not.
         *
         * Every option needs an `id`, because that is what comes back when the
         * player picks it -- an index would break the moment a page is built
         * with a different set of rows.
         */
        bool ReadPage(v8::Isolate *isolate, v8::Local<v8::Value> value, Features::Dialogue::Page &page, std::string &error) {
            if (value.IsEmpty() || !value->IsObject()) {
                error = "a page must be an object";
                return false;
            }
            v8::Local<v8::Object> object = value.As<v8::Object>();

            const v8::Local<v8::Value> line = OptionField(isolate, object, "line");
            if (!line.IsEmpty() && line->IsString()) {
                page.line = v8pp::from_v8<std::string>(isolate, line);
            }
            const v8::Local<v8::Value> onRight = OptionField(isolate, object, "onRight");
            if (!onRight.IsEmpty() && onRight->IsBoolean()) {
                page.onRight = v8pp::from_v8<bool>(isolate, onRight);
            }

            const v8::Local<v8::Value> options = OptionField(isolate, object, "options");
            if (options.IsEmpty() || !options->IsArray()) {
                error = "a page needs an options array";
                return false;
            }
            v8::Local<v8::Array> array     = options.As<v8::Array>();
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            const std::uint32_t count      = array->Length();
            if (count == 0) {
                error = "a page needs at least one option";
                return false;
            }
            if (count > Shared::RPC::kMaxDialogueOptions) {
                error = "a page carries at most " + std::to_string(Shared::RPC::kMaxDialogueOptions) + " options";
                return false;
            }
            for (std::uint32_t index = 0; index < count; ++index) {
                v8::Local<v8::Value> entry;
                if (!array->Get(context, index).ToLocal(&entry) || !entry->IsObject()) {
                    error = "every option must be an object";
                    return false;
                }
                v8::Local<v8::Object> row = entry.As<v8::Object>();
                Features::Dialogue::Option option;
                const v8::Local<v8::Value> id   = OptionField(isolate, row, "id");
                const v8::Local<v8::Value> text = OptionField(isolate, row, "text");
                if (id.IsEmpty() || !id->IsString() || text.IsEmpty() || !text->IsString()) {
                    error = "every option needs a string id and a string text";
                    return false;
                }
                option.id                          = v8pp::from_v8<std::string>(isolate, id);
                option.text                        = v8pp::from_v8<std::string>(isolate, text);
                const v8::Local<v8::Value> enabled = OptionField(isolate, row, "enabled");
                if (!enabled.IsEmpty() && enabled->IsBoolean()) {
                    option.enabled = v8pp::from_v8<bool>(isolate, enabled);
                }
                if (option.id.empty() || option.text.empty()) {
                    error = "an option's id and text must not be empty";
                    return false;
                }
                page.options.push_back(std::move(option));
            }
            return true;
        }

        void JS_Open(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            if (info.Length() < 2 || !info[0]->IsNumber()) {
                isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "Dialogue.open takes a player id and a page")));
                return;
            }
            const std::uint64_t playerId = v8pp::from_v8<std::uint64_t>(isolate, info[0]);
            Features::Dialogue::Page page;
            std::string error;
            if (!ReadPage(isolate, info[1], page, error)) {
                isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, error)));
                return;
            }
            const std::uint32_t session = Features::Dialogue::DialogueService::Get().Open(playerId, page);
            info.GetReturnValue().Set(v8pp::to_v8(isolate, session));
        }

        void JS_Update(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            if (info.Length() < 2 || !info[0]->IsNumber()) {
                isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "Dialogue.update takes a session id and a page")));
                return;
            }
            const std::uint32_t session = v8pp::from_v8<std::uint32_t>(isolate, info[0]);
            Features::Dialogue::Page page;
            std::string error;
            if (!ReadPage(isolate, info[1], page, error)) {
                isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, error)));
                return;
            }
            info.GetReturnValue().Set(v8pp::to_v8(isolate, Features::Dialogue::DialogueService::Get().Update(session, page)));
        }

        bool Close(std::uint32_t sessionId) {
            return Features::Dialogue::DialogueService::Get().Close(sessionId, Shared::RPC::DialogueCloseReason::Completed);
        }

        std::uint32_t SessionOf(std::uint64_t playerId) {
            return Features::Dialogue::DialogueService::Get().SessionOf(playerId);
        }
    } // namespace

    void RegisterDialogueBindings(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        // Declared types, not inline shapes: the contract only carries named
        // ones, and a page is the same object at `open` and at `update`.
        auto &option = ServerCatalog().data_type("DialogueOption", "One row of a page.");
        option.add_property("id", "string", "What comes back on `dialogueChoice` when this row is picked. An id, never an index, so a handler stays correct when the page is rebuilt with different rows.");
        option.add_property("text", "string", "The line the player reads. Server-authored text reaches the game's own list as written; it is not a localization key.");
        option.add_property("enabled", v8pp::metadata::type{"boolean | undefined", {}, true}, "Draws the row greyed and unpickable when false. A rendering hint only -- the server re-checks the choice when it arrives, so nothing a row costs may rely on this.");

        auto &page = ServerCatalog().data_type("DialoguePage", "A line and the options under it.");
        page.add_property("line", v8pp::metadata::type{"string | undefined", {}, true}, "Shown above the options. Omit it for a list with no preamble.");
        page.add_property("onRight", v8pp::metadata::type{"boolean | undefined", {}, true}, "Draws the list on the right of the screen instead of the left.");
        page.add_property("options", "DialogueOption[]", "The rows, at least one and at most eight. A page with none is refused.");

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("dialogueChoice", "[session: number, player: Player, optionId: string]",
            "Dispatched when a player picks an option. The option's own id comes back, never its index, so a handler stays correct when the page is rebuilt with different rows. "
            "Anything the option costs is checked here, not when the page was built: `enabled` on the wire is a rendering hint and the server is what decides.");
        events.add_property("dialogueClosed", "[session: number, player: Player, reason: number]", "Dispatched when a conversation ends, whoever ended it. 0 completed, 1 the player cancelled, 2 another conversation replaced it, 3 it was interrupted -- a disconnect, most often.");

        v8pp::module dialogue(isolate, ServerCatalog(), "Dialogue",
            "Conversations the server drives on a player, in the game's own dialogue list.\n\n"
            "A conversation is attached to nothing. It runs on a player and what it is about -- a smith, a notice board, a chest, a timer -- is this gamemode's own business, held in its own closures. "
            "That is deliberate and it has a cost: nothing here watches distance, line of sight or whether anyone died, because none of that is knowable without the relationship it refuses to hold. Close the conversation yourself when the fiction says it should end.");

        dialogue.function("open", &JS_Open,
            v8pp::metadata::docs("number",
                {
                    v8pp::metadata::param("player", "number", false, "NetworkID of the player the conversation runs on."),
                    v8pp::metadata::param("page", "DialoguePage", false, "The first page. `options` is required and carries at most 8 rows; each needs an `id` and a `text`, and may set `enabled`. `line` and `onRight` are optional."),
                },
                "Opens a conversation and shows its first page. A player already in one has it closed with reason 2 first, because the game has a single choice list and stacking them would lie about which is live.",
                "The session id, or 0 when the page was unusable or the player is gone."));

        dialogue.function("update", &JS_Update,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("session", "number", false, "The session to advance."),
                    v8pp::metadata::param("page", "DialoguePage", false, "The page to show instead."),
                },
                "Replaces the page a live conversation is showing. Each page carries a generation the client echoes, so an answer still in flight against the page this replaces is dropped rather than misapplied.", "False when that session has already ended."));

        dialogue.function("close", &Close, v8pp::metadata::docs("boolean", {v8pp::metadata::param("session", "number", false, "The session to end.")}, "Ends a conversation and takes the list off that player's screen.", "False when the session had already ended."));

        dialogue.function("sessionOf", &SessionOf, v8pp::metadata::docs("number", {v8pp::metadata::param("player", "number", false, "NetworkID of the player to ask about.")}, "The conversation that player is in.", "The session id, or 0 when they are not in one."));

        global->Set(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "Dialogue"), dialogue.new_instance()).Check();
    }

    void Events::DialogueChoice(std::uint32_t sessionId, std::uint64_t playerNetworkId, const std::string &optionId) {
        EmitReservedEvent("dialogueChoice", [sessionId, playerNetworkId, optionId](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(v8pp::to_v8(isolate, sessionId));
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(v8pp::to_v8(isolate, optionId));
            return args;
        });
    }

    void Events::DialogueClosed(std::uint32_t sessionId, std::uint64_t playerNetworkId, std::uint8_t reason) {
        EmitReservedEvent("dialogueClosed", [sessionId, playerNetworkId, reason](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(v8pp::to_v8(isolate, sessionId));
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(v8pp::to_v8(isolate, static_cast<std::uint32_t>(reason)));
            return args;
        });
    }
} // namespace KCDC::Scripting
