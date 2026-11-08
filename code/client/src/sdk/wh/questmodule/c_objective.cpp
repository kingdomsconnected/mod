#include "c_objective.h"

#include "c_quest.h"

#include "../../patterns.h"
#include "../../types.h"

#include <utils/hooking/hooking.h>

#include <algorithm>
#include <cstring>

namespace SDK::wh::questmodule {
    C_Objective *C_Objective::Create() {
        void *storage = hook::call<void *>(gPatterns.CryActionAllocate, sizeof(C_Objective));
        std::memset(storage, 0, sizeof(C_Objective));
        hook::this_call<void>(gPatterns.C_Objective__Construct, storage);

        C_Objective *objective = static_cast<C_Objective *>(storage);
        objective->AddRef();
        return objective;
    }

    bool C_Objective::SetProgressLogs(C_LogBase *const *logs, const char *const *progressNames, std::size_t count) {
        if (m_logs != nullptr) {
            for (std::uint64_t index = 0; index < m_logCount; ++index) {
                hook::this_call<void>(gPatterns.CryStringT_char__Destruct, &m_logs[index].portName);
            }
            hook::call<void>(gPatterns.FreeActionStorage, m_logs);
            m_logs        = nullptr;
            m_logCount    = 0;
            m_logCapacity = 0;
        }

        if (count == 0) {
            return true;
        }

        if (count > kMaxProgressLogs) {
            return false;
        }

        // The lookup is a binary search, so the list has to go in sorted by
        // name. Sorting the order the entries are written in rather than
        // asking the caller to hand them over sorted, because the caller has
        // no way to know that it matters: a list in the wrong order answers
        // the static default for some of its own entries, and an objective
        // that reads back the default is dropped from the journal in silence.
        std::size_t order[kMaxProgressLogs];
        for (std::size_t index = 0; index < count; ++index) {
            order[index] = index;
        }
        std::sort(order, order + count, [progressNames](std::size_t left, std::size_t right) {
            return std::strcmp(progressNames[left], progressNames[right]) < 0;
        });

        auto *entries = hook::call<S_ObjectiveLog *>(gPatterns.CryActionAllocate, count * sizeof(S_ObjectiveLog));
        if (entries == nullptr) {
            return false;
        }

        for (std::size_t index = 0; index < count; ++index) {
            // Built through the engine's own string constructor, so the buffer
            // belongs to the game's allocator like every other CryString in a
            // node it will one day destroy.
            hook::this_call<void>(gPatterns.CryStringT_char__Construct, &entries[index].portName, progressNames[order[index]]);
            entries[index].log           = logs[order[index]];
            entries[index].sharedControl = nullptr;
        }

        m_logs     = entries;
        m_logCount = count;

        // Not cosmetic. The objective's destructor frees this block only when
        // the capacity is non-zero -- it is the flag that says the array owns
        // its storage -- so leaving it at zero leaks the block on teardown.
        m_logCapacity = count;
        return true;
    }

    void C_Objective::SetProgress(E_QuestProgress progress) {
        BindProgressPort(m_progress, progress);
    }
} // namespace SDK::wh::questmodule
