#pragma once

#include "e_compass_mark_type.h"

#include "../../types.h"
#include "../questmodule/e_quest_type.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    /**
     * One quest marker on the map screen, as
     * `C_UIQuestLog::CollectQuestMarkers` (0x180DC5E68) collects them and
     * `RefreshQuestMarkers` (0x180DC577C) turns them into
     * `C_UIFlashMapQuestMarker` rows. The game builds one per marker of every
     * active or done objective of a followed quest (0x181F457F0); the copy
     * constructor at 0x180DC8218 spells the layout.
     *
     * The source is what the row reads its position from, through slot 2 --
     * `C_LinkableObject::GetWorldPosition`, for a `const Vec3 *` -- and its
     * WUID from +8. That is all the row builder (0x1814A81D0) touches, and
     * unlike a compass mark nothing attaches to it, which is what lets a mod
     * object stand in for one. It must stay valid while the map keeps the
     * entry: the map's copy at `C_UIMap +0x680` is read again when the
     * player switches maps (0x182B02E4C).
     *
     * The refresh diffs entries by quest name, objective index and tracking
     * id alone, so a source that moves is not redrawn until its entry has
     * been absent for one refresh.
     */
    struct S_QuestMarkerEntry {
        CryStringT<char> questName;           // 0x00, the quest's node name
        CryStringT<char> objectiveIdentifier; // 0x08, C_Objective +0xD8
        E_CompassMarkType markType;           // 0x10, from QuestTypeToCompassMarkType
        std::int32_t levelId;                 // 0x14, what C_Quest::GetLevelId answers
        std::int32_t trackingId;              // 0x18, the quest's slot in the tracked table
        std::byte padding001C[4];
        CryStringT<char> objectiveText; // 0x20, passed through Localize by C_UIFlashObject
        std::int32_t objectiveIndex;    // 0x28, among the quest's objectives
        std::byte padding002C[4];
        const void *source; // 0x30, a C_LinkableObject or something shaped like one
    };

    static_assert(offsetof(S_QuestMarkerEntry, questName) == 0x00, "S_QuestMarkerEntry::questName offset mismatch");
    static_assert(offsetof(S_QuestMarkerEntry, objectiveIdentifier) == 0x08, "S_QuestMarkerEntry::objectiveIdentifier offset mismatch");
    static_assert(offsetof(S_QuestMarkerEntry, markType) == 0x10, "S_QuestMarkerEntry::markType offset mismatch");
    static_assert(offsetof(S_QuestMarkerEntry, levelId) == 0x14, "S_QuestMarkerEntry::levelId offset mismatch");
    static_assert(offsetof(S_QuestMarkerEntry, trackingId) == 0x18, "S_QuestMarkerEntry::trackingId offset mismatch");
    static_assert(offsetof(S_QuestMarkerEntry, objectiveText) == 0x20, "S_QuestMarkerEntry::objectiveText offset mismatch");
    static_assert(offsetof(S_QuestMarkerEntry, objectiveIndex) == 0x28, "S_QuestMarkerEntry::objectiveIndex offset mismatch");
    static_assert(offsetof(S_QuestMarkerEntry, source) == 0x30, "S_QuestMarkerEntry::source offset mismatch");
    static_assert(sizeof(S_QuestMarkerEntry) == 0x38, "S_QuestMarkerEntry size mismatch");

    /**
     * Appends a copy of `entry` to a vector the game allocated, through the
     * game's own `_Emplace_reallocate` (0x180DC8068), so the buffer is grown
     * and freed on the game's heap. The copy takes its own reference on each
     * string, so `entry` may borrow them.
     */
    void AppendQuestMarkerEntry(StdVector<S_QuestMarkerEntry> &entries, const S_QuestMarkerEntry &entry);

    /**
     * The compass and map icon a quest of this type is marked with.
     * Transcribed from 0x180DC662C, a leaf with no prologue worth a pattern.
     */
    inline E_CompassMarkType QuestTypeToCompassMarkType(questmodule::E_QuestType type) {
        switch (type) {
        case questmodule::E_QuestType::Main: return E_CompassMarkType::Main;
        case questmodule::E_QuestType::Side: return E_CompassMarkType::Side;
        case questmodule::E_QuestType::Activity:
        case questmodule::E_QuestType::Event:
        case questmodule::E_QuestType::Micro: return E_CompassMarkType::Micro;
        case questmodule::E_QuestType::Racing: return E_CompassMarkType::Racing;
        }
        return E_CompassMarkType::Side;
    }
} // namespace SDK::wh::guimodule
