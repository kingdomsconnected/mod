#pragma once

#include <v8.h>

#include <cstdint>

namespace KCDC::Scripting {
    /** Publishes the `Book` global: books a resource composes and opens in the player's hands. */
    void RegisterBook(v8::Isolate *isolate, v8::Local<v8::Object> global);

    // What this feature tells loaded resources happened on this machine.
    // Nothing here mentions V8, so BookService can report without depending
    // on the scripting layer's classes.
    namespace Events {
        /** The book reached the player's hands and its first page is showing. */
        void BookOpened(std::uint32_t bookId);

        /** The book left the player's hands; `reason` is player, script, failed or sessionOver. */
        void BookClosed(std::uint32_t bookId, const char *reason);
    } // namespace Events
} // namespace KCDC::Scripting
