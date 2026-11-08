#pragma once

#include <v8.h>

namespace KCDC::Scripting {
    /**
     * Publishes `ActionHint`: "Press [key] to ..." hints drawn by the game's
     * own HUD, with a handler run when the player presses the key. Every verb
     * goes to KCDC::Features::UI::ActionHints; the handlers live here, keyed
     * by the hint's id, and go with the resource that registered them.
     */
    void RegisterActionHints(v8::Isolate *isolate, v8::Local<v8::Object> global);

    namespace Events {
        /** What the HUD's hints say changed: one appeared, went, changed text or its ring moved. */
        void ActionHintsChanged();
    } // namespace Events
} // namespace KCDC::Scripting
