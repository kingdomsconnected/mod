#pragma once

#include "shared/features/player/player_appearance.h"

#include <v8.h>

namespace KCDC::Scripting {
    /** Publishes the `CharacterCreator` global: the game's own character stage, for the local player to choose a look on. */
    void RegisterCharacterCreator(v8::Isolate *isolate, v8::Local<v8::Object> global);

    // What the creator tells loaded resources happened on this machine.
    // Nothing here mentions V8, so CharacterCreatorService can report without
    // depending on the scripting layer's classes.
    namespace Events {
        /** A creator ended; `accepted` is the look the player accepted, null for every other ending. */
        void CharacterCreatorClosed(const Shared::Game::CharacterAppearance *accepted, const char *reason);
    } // namespace Events
} // namespace KCDC::Scripting
