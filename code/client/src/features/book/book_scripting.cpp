#include <utils/safe_win32.h>

#include "book_scripting.h"

#include "book_service.h"

#include "core/application.h"
#include "core/scripting/script_context.h"
#include "core/ui/native_ui/native_ui_assets.h"
#include "core/ui/native_ui/native_ui_module.h"

#include "shared/scripting/catalog.h"

#include <logging/logger.h>
#include <scripting/resource/resource_manager.h>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace KCDC::Scripting {
    namespace {
        void ThrowError(v8::Isolate *isolate, const std::string &message) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, message)));
        }

        /** `object[key]`, or an empty handle when it is absent or undefined. */
        v8::Local<v8::Value> Field(v8::Isolate *isolate, v8::Local<v8::Object> object, const char *key) {
            v8::Local<v8::Value> value;
            if (!object->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, key)).ToLocal(&value) || value->IsUndefined()) {
                return {};
            }
            return value;
        }

        /** A whole number in [low, high] at `object[key]`, `fallback` when absent; false when present and wrong. */
        bool ReadRange(v8::Isolate *isolate, v8::Local<v8::Object> object, const char *key, int low, int high, int fallback, int &out, std::string &error) {
            v8::Local<v8::Value> value = Field(isolate, object, key);
            if (value.IsEmpty()) {
                out = fallback;
                return true;
            }
            const double number = value->IsNumber() ? v8pp::from_v8<double>(isolate, value) : NAN;
            if (!std::isfinite(number) || std::floor(number) != number || number < low || number > high) {
                error = std::string("`") + key + "` must be a whole number from " + std::to_string(low) + " to " + std::to_string(high);
                return false;
            }
            out = static_cast<int>(number);
            return true;
        }

        /** A finite number, clamped to 0..1; false for anything else. */
        bool ReadLegibility(v8::Isolate *isolate, v8::Local<v8::Value> value, float &out) {
            const double number = value->IsNumber() ? v8pp::from_v8<double>(isolate, value) : NAN;
            if (!std::isfinite(number)) {
                return false;
            }
            out = static_cast<float>(number < 0.0 ? 0.0 : (number > 1.0 ? 1.0 : number));
            return true;
        }

        bool ReadDefinition(v8::Isolate *isolate, v8::Local<v8::Value> argument, Features::Book::BookDefinition &definition, std::string &error) {
            if (!argument->IsObject()) {
                error = "Book.open takes a BookOptions object";
                return false;
            }
            v8::Local<v8::Object> options  = argument.As<v8::Object>();
            v8::Local<v8::Context> context = isolate->GetCurrentContext();

            v8::Local<v8::Value> pages = Field(isolate, options, "pages");
            if (pages.IsEmpty() || !pages->IsArray()) {
                error = "`pages` must be an array of strings";
                return false;
            }
            v8::Local<v8::Array> list = pages.As<v8::Array>();
            if (list->Length() == 0 || list->Length() > Features::Book::BookService::kMaxPages) {
                error = "`pages` must hold 1 to " + std::to_string(Features::Book::BookService::kMaxPages) + " pages";
                return false;
            }
            for (std::uint32_t index = 0; index < list->Length(); ++index) {
                v8::Local<v8::Value> page;
                if (!list->Get(context, index).ToLocal(&page) || !page->IsString()) {
                    error = "page " + std::to_string(index) + " is not a string";
                    return false;
                }
                std::string text = v8pp::from_v8<std::string>(isolate, page);
                if (text.size() > Features::Book::BookService::kMaxPageLength) {
                    error = "page " + std::to_string(index) + " is longer than " + std::to_string(Features::Book::BookService::kMaxPageLength) + " bytes";
                    return false;
                }
                definition.pages.emplace_back(std::move(text));
            }

            v8::Local<v8::Value> style = Field(isolate, options, "style");
            if (!style.IsEmpty()) {
                const std::string name = style->IsString() ? v8pp::from_v8<std::string>(isolate, style) : std::string();
                if (name == "book") {
                    definition.carrier = Features::Book::BookDefinition::Carrier::Book;
                }
                else if (name == "plainBook") {
                    definition.carrier = Features::Book::BookDefinition::Carrier::PlainBook;
                }
                else if (name == "letter") {
                    definition.carrier = Features::Book::BookDefinition::Carrier::Letter;
                }
                else {
                    error = "`style` must be \"book\", \"plainBook\" or \"letter\"";
                    return false;
                }
            }

            // A letter lays out as one unless the script says otherwise.
            const int defaultType = definition.carrier == Features::Book::BookDefinition::Carrier::Letter ? 5 : 1;
            if (!ReadRange(isolate, options, "type", 1, 6, defaultType, definition.documentType, error) || !ReadRange(isolate, options, "visual", 1, 7, 1, definition.visualCategory, error)) {
                return false;
            }

            v8::Local<v8::Value> legibility = Field(isolate, options, "legibility");
            if (!legibility.IsEmpty() && !ReadLegibility(isolate, legibility, definition.legibility)) {
                error = "`legibility` must be a number from 0 to 1";
                return false;
            }

            v8::Local<v8::Value> texture = Field(isolate, options, "texture");
            if (!texture.IsEmpty()) {
                const std::string source = texture->IsString() ? v8pp::from_v8<std::string>(isolate, texture) : std::string();
                definition.texture       = Core::NativeUIAssets::BookImagePath(source);
                if (definition.texture.empty()) {
                    error = "`texture` must be a value Book.image returned";
                    return false;
                }
            }

            v8::Local<v8::Value> images = Field(isolate, options, "images");
            if (!images.IsEmpty()) {
                if (!images->IsArray() || images.As<v8::Array>()->Length() > Features::Book::BookService::kMaxImages) {
                    error = "`images` must be an array of at most " + std::to_string(Features::Book::BookService::kMaxImages) + " BookPageImage";
                    return false;
                }
                v8::Local<v8::Array> rows = images.As<v8::Array>();
                for (std::uint32_t index = 0; index < rows->Length(); ++index) {
                    v8::Local<v8::Value> row;
                    if (!rows->Get(context, index).ToLocal(&row) || !row->IsObject()) {
                        error = "image " + std::to_string(index) + " is not a BookPageImage";
                        return false;
                    }
                    int page                   = 0;
                    v8::Local<v8::Value> image = Field(isolate, row.As<v8::Object>(), "image");
                    if (!ReadRange(isolate, row.As<v8::Object>(), "page", 0, static_cast<int>(Features::Book::BookService::kMaxPages), 0, page, error) || image.IsEmpty() || !image->IsString()) {
                        if (error.empty()) {
                            error = "image " + std::to_string(index) + " needs an `image` name";
                        }
                        return false;
                    }
                    definition.images.push_back({static_cast<std::uint32_t>(page), v8pp::from_v8<std::string>(isolate, image)});
                }
            }
            return true;
        }

        // JS: Book.open(options: BookOptions) -> number | null
        void JS_Open(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            Features::Book::BookDefinition definition;
            std::string error;
            if (info.Length() < 1 || !ReadDefinition(isolate, info[0], definition, error)) {
                ThrowError(isolate, error.empty() ? "Book.open takes a BookOptions object" : error);
                return;
            }

            Features::Book::BookService &books = Features::Book::BookService::Get();
            const std::uint32_t id             = books.Open(std::move(definition));
            if (id == 0) {
                Framework::Logging::GetLogger("Book")->debug("Book.open refused: {}", books.GetLastError());
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(static_cast<double>(id));
        }

        // JS: Book.close() -> boolean
        void JS_Close(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(Features::Book::BookService::Get().Close());
        }

        // JS: Book.setLegibility(value: number) -> boolean
        void JS_SetLegibility(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            float legibility = 1.0F;
            if (info.Length() < 1 || !ReadLegibility(isolate, info[0], legibility)) {
                ThrowError(isolate, "Book.setLegibility takes a number from 0 to 1");
                return;
            }
            info.GetReturnValue().Set(Features::Book::BookService::Get().SetLegibility(legibility));
        }

        // JS: Book.getOpenBook() -> number | null
        void JS_GetOpenBook(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const std::uint32_t id = Features::Book::BookService::Get().GetOpenBookId();
            if (id == 0) {
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(static_cast<double>(id));
        }

        // JS: Book.getLastError() -> string
        void JS_GetLastError(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(v8pp::to_v8(info.GetIsolate(), Features::Book::BookService::Get().GetLastError()));
        }

        // JS: Book.image(path: string) -> string
        void JS_Image(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsString()) {
                ThrowError(isolate, "Book.image takes the resource-relative path of a .dds file");
                return;
            }
            const std::string resource = KCDC::ResolveCallingResource(isolate);
            if (resource.empty()) {
                ThrowError(isolate, "Book.image could not tell which resource is calling");
                return;
            }

            std::string source;
            std::string archivePath;
            std::string error;
            if (!Core::NativeUIAssets::PackageBookImage(resource, v8pp::from_v8<std::string>(isolate, info[0]), source, archivePath, error) || !Core::NativeUIModule::Get().MountArchive(archivePath, error)) {
                ThrowError(isolate, "Book.image: " + error);
                return;
            }
            info.GetReturnValue().Set(v8pp::to_v8(isolate, source));
        }
    } // namespace

    namespace Events {
        void BookOpened(std::uint32_t bookId) {
            EmitReservedEvent("bookOpened", [bookId](v8::Isolate *isolate) {
                return std::vector<v8::Local<v8::Value>> {v8pp::to_v8(isolate, static_cast<double>(bookId))};
            });
        }

        void BookClosed(std::uint32_t bookId, const char *reason) {
            const std::string named = reason != nullptr ? reason : "player";
            EmitReservedEvent("bookClosed", [bookId, named](v8::Isolate *isolate) {
                return std::vector<v8::Local<v8::Value>> {v8pp::to_v8(isolate, static_cast<double>(bookId)), v8pp::to_v8(isolate, named)};
            });
        }
    } // namespace Events

    void RegisterBook(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ClientCatalog().data_type("EventMap");
        events.add_property("bookOpened", "[bookId: number]", "Dispatched when a book `Book.open` asked for is in the player's hands and showing its first page, on this machine only.");
        events.add_property("bookClosed", "[bookId: number, reason: \"player\" | \"script\" | \"failed\" | \"sessionOver\"]",
            "Dispatched when a book leaves the player's hands, on this machine only. `reason` is `player` for the player's own exit (or the game ending the reading itself), `script` for `Book.close`, `failed` for a book that never reached the hands, and `sessionOver` for the session ending. Handler promises are not awaited, and a handler may open the next book straight away.");

        auto &image = ClientCatalog().data_type("BookPageImage", "A picture for the skill-book and map layouts, which place pictures by page rather than inline.");
        image.add_property("page", "number", "Zero-based page the picture belongs to.");
        image.add_property("image", "string", "Its name under the game's own `Libs/UI/Textures/Books/` Skills or Maps folder, without the `_ui.dds` suffix, e.g. `swords1`.");

        auto &options = ClientCatalog().data_type("BookOptions", "What `Book.open` shows and how it looks.");
        options.add_property("pages", "string[]",
            "One string per page, written in the book movie's own markup: `<title>`, `<subtitle>`, `<heading>`, `<paragraph>`, `<br/>`, `<accent>`, `<inc>X</inc>` for a decorated initial, `<poem>`, `<lpoem>`, `<note>`, `<p align>`, `<font size>`, `<i>` and `<img src='...' width='W' height='H' align='center'/>`. "
            "A page break follows every page but the last; `<newpage/>` inside a page breaks it further. The player's own reading skill never scrambles it; `legibility` decides that.");
        options.add_property("style", v8pp::metadata::type{"\"book\" | \"plainBook\" | \"letter\" | undefined", {}, true}, "The object in the player's hands: a red-covered book (the default), a plain-covered one, or a folded letter.");
        options.add_property("type", v8pp::metadata::type{"number | undefined", {}, true}, "The layout the movie uses, by `document_class` id: 1 book (the default), 2 recipe, 3 skill book, 4 map, 5 letter (the default for `letter`), 6 plan.");
        options.add_property("visual", v8pp::metadata::type{"number | undefined", {}, true}, "How ornate the pages are, 1 plain to 7 embellished. Defaults to 1.");
        options.add_property("legibility", v8pp::metadata::type{"number | undefined", {}, true},
            "How much of the text can be read, 0 to 1; values outside are clamped. Defaults to 1, fully legible. Below 1 the movie swaps letters for look-alike glyphs, the same effect a vanilla book shows a player with low reading skill. "
            "It works in tenths: each step brings roughly another tenth of the character classes back, so 0.31 and 0.39 look the same. The substitution is deterministic -- the same text at the same level always reads the same way -- and markup is never touched.");
        options.add_property("texture", v8pp::metadata::type{"string | undefined", {}, true},
            "A picture `Book.image` returned, replacing the carrier's own diffuse texture: the paper and the cover together, which the model maps from one atlas. "
            "Paint it over the vanilla atlas to keep the UV layout -- `Objects/manmade/task_specific_props/read_and_write/books/book_alchemy_diff.dds` for the two book styles, `.../scrolls/scroll_diff.dds` for `letter`. The material's diffuse colour multiplies it: `book` darkens it to about 78% and `letter` to about 72%, while `plainBook` shows it as painted. "
            "It applies to this book's own copy of the material only, and goes with the book.");
        options.add_property("images", v8pp::metadata::type{"BookPageImage[] | undefined", {}, true}, "Pictures for the skill-book and map layouts. Inline pictures go in the page text as `<img>` instead.");

        v8pp::module book(isolate, ClientCatalog(), "Book",
            "Books a resource composes and opens in this player's hands.\n\n"
            "A book is shown through the game's own reading: the same book in hand, camera, page-turn animation, prompts and pagination as any book the player reads from their inventory. The player turns pages and leaves it with the game's own keys, and `bookClosed` says when they did.\n\n"
            "Client-only and local: the book exists on this machine alone, and reading it grants nothing -- no XP, no read marker, no quest progress. A server that wants a player to read something sends the pages in its own event.\n\n"
            "Pictures a resource ships go through `Book.image`, which returns what an `<img src>` and the `texture` option take.");

        book.function("open", &JS_Open,
            v8pp::metadata::docs("number | null", {v8pp::metadata::param("options", "BookOptions", false, "The pages and how they look.")}, "Opens a book in the player's hands.",
                "The book's id, which `bookOpened` and `bookClosed` carry; null when the game will not open one now -- no body yet, a book already open, or a place the player cannot read in (on horseback, in combat). `getLastError` says which. Malformed options throw."));

        book.function("close", &JS_Close, v8pp::metadata::docs("boolean", {}, "Closes the open book the way the player's own exit does.", "False when no book is open."));

        book.function("setLegibility", &JS_SetLegibility,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("value", "number", false, "0 fully scrambled to 1 fully legible; clamped.")}, "Changes how legible the open book is, while it is in the player's hands.",
                "False when no book is in the hands yet (wait for `bookOpened`). The movie applies legibility while laying the text out, so the book is laid out again, which can return it to its first page. Throws for a non-number."));

        book.function("getOpenBook", &JS_GetOpenBook, v8pp::metadata::docs("number | null", {}, "The id of the book in the player's hands.", "Null when there is none."));

        book.function("getLastError", &JS_GetLastError, v8pp::metadata::docs("string", {}, "Why the last `open` returned null.", "Empty after one that succeeded."));

        book.function("image", &JS_Image,
            v8pp::metadata::docs("string", {v8pp::metadata::param("path", "string", false, "A `.dds` file the calling resource ships, relative to the resource.")}, "Makes a picture the resource ships reachable from page text.",
                "The value for an `<img src='...'>` attribute. Give the tag a `width` and `height` too: the movie sizes the picture to them and has no size of its own for it. Throws when the file is missing, not a `.dds`, or larger than 16 MiB."));

        book.publish(global);
    }
} // namespace KCDC::Scripting
