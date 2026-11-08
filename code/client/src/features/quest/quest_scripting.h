#pragma once

#include <string>

namespace KCDC::Scripting {
    // Puts this feature's events in the client catalog the TypeScript contract is generated from. Events only: the server owns the `Quest` class, and this side reports.
    void RegisterQuestMetadata();

    // What this feature tells resources loaded on this machine. Nothing here mentions V8, so QuestService can report without depending on the scripting layer.
    namespace Events {
        /** The player started or stopped following a server quest. Raised on this machine before the server has heard anything. */
        void QuestTrackingChanged(const std::string &questKey, bool tracked);
    } // namespace Events
} // namespace KCDC::Scripting
