#include "s_quest_marker_entry.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::guimodule {
    void AppendQuestMarkerEntry(StdVector<S_QuestMarkerEntry> &entries, const S_QuestMarkerEntry &entry) {
        // A member of the vector: `this` in RCX, the insert position, then
        // the value. It reallocates whatever the spare capacity, which costs
        // a copy of a short list and needs no second native for the other
        // branch.
        hook::this_call<S_QuestMarkerEntry *>(gPatterns.std_vector_S_QuestMarkerEntry__Emplace_reallocate, &entries, entries.last, &entry);
    }
} // namespace SDK::wh::guimodule
