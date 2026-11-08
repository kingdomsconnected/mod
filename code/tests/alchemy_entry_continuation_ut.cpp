#include "features/crafting/alchemy_entry_continuation.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
    void RequireEntry(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    KCDC::Features::Crafting::AlchemyEntryIdentity EntryIdentity() {
        return {"rataje", 100, 200, 0xABC, 3, 4, 5};
    }

    KCDC::Features::Crafting::AlchemyEntryApproval EntryApproval() {
        return {"entry-1", "alchemy-1", 7};
    }

    void QueueEntry(KCDC::Features::Crafting::AlchemyEntryContinuation &entry) {
        std::string error;
        RequireEntry(entry.Queue(EntryIdentity(), "entry-1", 7, 100, 1000, error), "queue current station request");
    }

    void ApproveEntry(KCDC::Features::Crafting::AlchemyEntryContinuation &entry) {
        QueueEntry(entry);
        std::string error;
        RequireEntry(entry.Approve(EntryApproval(), EntryIdentity(), 101, error), "approve exact server session");
    }

    void InvokeEntry(KCDC::Features::Crafting::AlchemyEntryContinuation &entry) {
        ApproveEntry(entry);
        std::string error;
        RequireEntry(entry.BeginInvocation(EntryIdentity(), 102, error), "begin one native entry continuation");
        RequireEntry(entry.BeginScriptCall(100, 200, error), "enter exact full-width Lua handle scope");
        RequireEntry(entry.ConsumeNativeStart(EntryIdentity(), 102, error), "consume exact native start permission before original");
    }
} // namespace

void AlchemyEntryContinuationTests() {
    std::string error;
    {
        KCDC::Features::Crafting::AlchemyEntryContinuation entry;
        InvokeEntry(entry);
        RequireEntry(!entry.ConsumeNativeStart(EntryIdentity(), 102, error), "native re-entry cannot invoke Start twice");
        RequireEntry(entry.NativeStartReturned(true, error) && entry.EndScriptCall(0, error) && entry.EndInvocation(true, error), "real accepted Start and completed Lua entry both required");
        RequireEntry(entry.GetPhase() == KCDC::Features::Crafting::AlchemyEntryContinuation::Phase::Active, "entry active once native Start was accepted");
        RequireEntry(!entry.BeginInvocation(EntryIdentity(), 103, error), "completed original may never be invoked again");
        RequireEntry(entry.Approve(EntryApproval(), EntryIdentity(), 103, error), "identical delayed approval is harmless");
        RequireEntry(entry.GetPhase() == KCDC::Features::Crafting::AlchemyEntryContinuation::Phase::Active, "duplicate approval does not restore invocation permission");
    }
    {
        KCDC::Features::Crafting::AlchemyEntryContinuation entry;
        QueueEntry(entry);
        auto stale      = EntryApproval();
        stale.operation = "older-entry";
        RequireEntry(!entry.Approve(stale, EntryIdentity(), 101, error), "unrelated delayed approval is rejected");
        RequireEntry(entry.GetPhase() == KCDC::Features::Crafting::AlchemyEntryContinuation::Phase::WaitingApproval, "unrelated packet does not retire current operation");
        auto unnamed    = EntryApproval();
        unnamed.session = "";
        RequireEntry(!entry.Approve(unnamed, EntryIdentity(), 101, error), "approval without a session cannot start");
        RequireEntry(!entry.ConsumeNativeStart(EntryIdentity(), 101, error), "no native action before exact approval");
    }
    {
        KCDC::Features::Crafting::AlchemyEntryContinuation entry;
        ApproveEntry(entry);
        auto changed    = EntryApproval();
        changed.session = "substituted-session";
        RequireEntry(!entry.Approve(changed, EntryIdentity(), 102, error), "same operation cannot substitute its session");
        RequireEntry(entry.GetPhase() == KCDC::Features::Crafting::AlchemyEntryContinuation::Phase::Retiring && !entry.NativeCleanupRequired(), "conflicting approval retires before native action");
    }
    {
        KCDC::Features::Crafting::AlchemyEntryContinuation entry;
        ApproveEntry(entry);
        auto replacement = EntryIdentity();
        ++replacement.bodyGeneration;
        RequireEntry(!entry.BeginInvocation(replacement, 102, error), "reused entity IDs do not bypass the body-generation fence");
        RequireEntry(!entry.NativeCleanupRequired(), "replacement body is never touched by an old entry");
    }
    {
        KCDC::Features::Crafting::AlchemyEntryContinuation entry;
        ApproveEntry(entry);
        auto replacement = EntryIdentity();
        ++replacement.callbackGeneration;
        RequireEntry(!entry.BeginInvocation(replacement, 102, error), "script reload retires retained callback permission");
    }
    {
        KCDC::Features::Crafting::AlchemyEntryContinuation entry;
        ApproveEntry(entry);
        auto elsewhere  = EntryIdentity();
        elsewhere.level = "kuttenberg";
        RequireEntry(!entry.BeginInvocation(elsewhere, 102, error), "a level change retires the admission");
    }
    {
        KCDC::Features::Crafting::AlchemyEntryContinuation entry;
        ApproveEntry(entry);
        RequireEntry(entry.BeginInvocation(EntryIdentity(), 102, error), "enter retained callback for handle checks");
        RequireEntry(!entry.ConsumeNativeStart(EntryIdentity(), 102, error), "direct native Start cannot bypass the retained Lua bridge scope");
        RequireEntry(!entry.BeginScriptCall((std::uint64_t(1) << 32) | 100, 200, error), "full-width actor handle cannot alias expected entity");
        RequireEntry(!entry.BeginScriptCall(100, (std::uint64_t(1) << 32) | 200, error), "full-width station handle cannot alias expected entity");
        RequireEntry(entry.BeginScriptCall(100, 200, error), "exact handles enter binding");
        RequireEntry(!entry.BeginScriptCall(100, 200, error), "recursive script binding cannot acquire another scope");
        RequireEntry(!entry.EndInvocation(true, error), "outer callback cannot finish while script binding remains on stack");
        RequireEntry(entry.ConsumeNativeStart(EntryIdentity(), 102, error) && entry.NativeStartReturned(true, error), "original native Start accepted once");
        RequireEntry(!entry.EndScriptCall(1, error), "unexpected original Lua return count cannot be treated as the verified ABI");
        RequireEntry(!entry.EndInvocation(true, error) && entry.MayPerformNativeCleanup(), "unexpected native bridge return retires without replay");
    }
    {
        KCDC::Features::Crafting::AlchemyEntryContinuation entry;
        InvokeEntry(entry);
        RequireEntry(entry.NativeStartReturned(false, error), "capture real native refusal");
        RequireEntry(!entry.EndScriptCall(0, error), "native false is returned to Lua before its post-start effects");
        RequireEntry(!entry.EndInvocation(true, error), "successful Lua return cannot invent acceptance discarded by retail binding");
        RequireEntry(entry.NativeCleanupRequired() && entry.MayPerformNativeCleanup(), "failed native attempt is reconciled after its stack unwinds");
    }
    {
        KCDC::Features::Crafting::AlchemyEntryContinuation entry;
        ApproveEntry(entry);
        RequireEntry(entry.BeginInvocation(EntryIdentity(), 102, error), "enter retained callback");
        RequireEntry(!entry.EndInvocation(true, error), "Lua callback early exit without native Start is not a successful session");
        RequireEntry(!entry.NativeCleanupRequired(), "early Lua exit does not pretend the native game started");
    }
    {
        KCDC::Features::Crafting::AlchemyEntryContinuation entry;
        InvokeEntry(entry);
        entry.Retire("disconnect during native Start");
        RequireEntry(!entry.MayPerformNativeCleanup() && entry.NativeCleanupRequired(), "disconnect inside native call latches retirement without re-entering cleanup");
        RequireEntry(!entry.CompleteRetirement(true, true, true, error), "no completion while original callback is on stack");
        RequireEntry(entry.NativeStartReturned(true, error), "late native return remains recorded during retirement");
        RequireEntry(!entry.EndScriptCall(0, error), "disconnect returns false to Lua even if native Start already returned true");
        RequireEntry(!entry.EndInvocation(true, error) && entry.MayPerformNativeCleanup(), "unwind unlocks cleanup without reviving the session");
        RequireEntry(!entry.Approve(EntryApproval(), EntryIdentity(), 103, error), "late accepted reply cannot revive a retiring session");
        RequireEntry(!entry.CompleteRetirement(false, true, true, error), "an open server table cannot be replaced by native cleanup");
        RequireEntry(!entry.CompleteRetirement(true, false, true, error), "live native minigame prevents retirement acknowledgment");
        RequireEntry(!entry.CompleteRetirement(true, true, false, error), "cached callback generation must be retired");
        RequireEntry(entry.CompleteRetirement(true, true, true, error), "closed table plus complete native cleanup retires exactly once");
        RequireEntry(!entry.CompleteRetirement(true, true, true, error), "duplicate retirement never repeats cleanup");
        RequireEntry(!entry.Queue(EntryIdentity(), "entry-2", 7, 104, 1000, error), "ticket reuse within callback generation is rejected after cleanup");
        RequireEntry(entry.Queue(EntryIdentity(), "entry-2", 8, 104, 1000, error), "new ticket may start only after all old work is retired");
        RequireEntry(!entry.Approve(EntryApproval(), EntryIdentity(), 105, error), "old approval cannot approve the next ticket");
    }
    {
        KCDC::Features::Crafting::AlchemyEntryContinuation entry;
        QueueEntry(entry);
        entry.Retire("cancel while reply is outstanding");
        RequireEntry(entry.Cancellation() == KCDC::Features::Crafting::AlchemyEntryCancellation {"entry-1", "", ""}, "an unanswered start closes only by its request, without an invented session");
        RequireEntry(!entry.NativeCleanupRequired() && !entry.CompleteRetirement(false, true, true, error), "the table close must still be answered before release");
        RequireEntry(!entry.Approve(EntryApproval(), EntryIdentity(), 101, error), "post-cancel approval does not replay native entry");
    }
    {
        KCDC::Features::Crafting::AlchemyEntryContinuation entry;
        InvokeEntry(entry);
        entry.Retire("connection lost");
        RequireEntry(!entry.CompleteDisconnectedRetirement(true, true, error), "disconnect cannot clear ownership while native entry remains on stack");
        RequireEntry(entry.NativeStartReturned(true, error), "capture native acceptance while disconnected");
        RequireEntry(!entry.EndScriptCall(0, error) && !entry.EndInvocation(false, error), "unwind disconnected native entry without reporting success");
        RequireEntry(!entry.CompleteDisconnectedRetirement(false, true, error), "a new connection cannot bypass an old live native minigame");
        RequireEntry(!entry.CompleteDisconnectedRetirement(true, false, error), "disconnect must retire cached Lua wrappers first");
        RequireEntry(entry.CompleteDisconnectedRetirement(true, true, error), "local disconnect cleanup completes without claiming how the visit ended");
        RequireEntry(entry.Approval().session.empty() && entry.Operation().empty() && !entry.HasNativeAcceptance(), "disconnected retirement clears old continuation state");
        RequireEntry(!entry.Approve(EntryApproval(), EntryIdentity(), 103, error), "late old approval cannot revive cleared disconnect state");
        auto next = EntryIdentity();
        ++next.callbackGeneration;
        RequireEntry(entry.Queue(next, "next-connection", 1, 104, 1000, error), "the next connection starts under a fresh callback generation");
    }
    {
        KCDC::Features::Crafting::AlchemyEntryContinuation entry;
        ApproveEntry(entry);
        RequireEntry(!entry.BeginInvocation(EntryIdentity(), 1000, error), "approval expires before any native effect at exact deadline");
    }
    {
        KCDC::Features::Crafting::AlchemyEntryContinuation entry;
        ApproveEntry(entry);
        RequireEntry(!entry.Observe(EntryIdentity(), 100, error), "clock rollback cannot extend pending admission");
    }
    {
        KCDC::Features::Crafting::AlchemyEntryContinuation entry;
        const std::uint64_t beforeWrap = std::numeric_limits<std::uint32_t>::max() - 5ULL;
        RequireEntry(entry.Queue(EntryIdentity(), "entry-1", 7, beforeWrap, beforeWrap + 20, error), "full-width entry deadline spans the legacy millisecond wrap");
        RequireEntry(entry.Approve(EntryApproval(), EntryIdentity(), beforeWrap + 1, error), "long-running client accepts current approval before 32-bit boundary");
        RequireEntry(entry.BeginInvocation(EntryIdentity(), beforeWrap + 10, error), "crossing 49.7 days does not become a backwards clock or expire native admission");
    }
    {
        KCDC::Features::Crafting::AlchemyEntryContinuation entry;
        InvokeEntry(entry);
        RequireEntry(entry.NativeStartReturned(true, error), "accepted native start before script failure");
        RequireEntry(entry.EndScriptCall(0, error), "native Boolean returned before later Lua failure");
        RequireEntry(!entry.EndInvocation(false, error) && entry.NativeCleanupRequired(), "later Lua failure retires the actual native session without invoking Start again");
        RequireEntry(entry.Cancellation() == KCDC::Features::Crafting::AlchemyEntryCancellation {"entry-1", "alchemy-1", ""}, "failed Lua continuation still names the admitted table to cancel");
        RequireEntry(entry.MayPerformNativeCleanup(), "Lua failure permits cleanup after the callback unwinds");
        RequireEntry(!entry.CompleteRetirement(false, true, true, error), "Lua failure cannot skip server closure");
        RequireEntry(!entry.CompleteRetirement(true, false, true, error), "Lua failure cannot leave the native interaction running");
        RequireEntry(!entry.CompleteRetirement(true, true, false, error), "Lua failure must retire its failed callback generation");
        RequireEntry(entry.CompleteRetirement(true, true, true, error), "failed entry retires after server, native and script cleanup");
        auto fresh = EntryIdentity();
        ++fresh.nativeGeneration;
        ++fresh.callbackGeneration;
        RequireEntry(entry.Queue(fresh, "entry-2", 1, 104, 1000, error), "fresh callback generation can retry without reconnecting");
        RequireEntry(entry.Approve({"entry-2", "alchemy-2", 1}, fresh, 105, error) && entry.BeginInvocation(fresh, 106, error), "retry receives its own admission");
        RequireEntry(entry.BeginScriptCall(100, 200, error) && entry.ConsumeNativeStart(fresh, 106, error), "retry reaches native start once");
        RequireEntry(entry.NativeStartReturned(true, error) && entry.EndScriptCall(0, error) && entry.EndInvocation(true, error), "retry completes a new table visit without disconnect cleanup");
    }
    {
        KCDC::Features::Crafting::AlchemyEntryContinuation entry;
        InvokeEntry(entry);
        RequireEntry(entry.NativeStartReturned(true, error) && entry.EndScriptCall(0, error) && entry.EndInvocation(true, error), "first table entry accepts native Start");
        RequireEntry(!entry.QueueBatch(EntryIdentity(), "another-session", "entry-2", 104, 1000, error), "successor must name the actual prior session");
        RequireEntry(!entry.QueueBatch(EntryIdentity(), "alchemy-1", "entry-1", 104, 1000, error), "successor needs a request of its own");
        RequireEntry(entry.QueueBatch(EntryIdentity(), "alchemy-1", "entry-2", 104, 1000, error), "same active native table can request a successor");
        RequireEntry(entry.PreviousSession() == "alchemy-1", "successor remembers the session holding the table");
        auto successor      = EntryApproval();
        successor.operation = "entry-2";
        RequireEntry(!entry.Approve(successor, EntryIdentity(), 105, error), "successor cannot revive the completed session");
        successor.session = "alchemy-2";
        RequireEntry(entry.Approve(successor, EntryIdentity(), 106, error), "distinct successor session is accepted");
        RequireEntry(!entry.BeginInvocation(EntryIdentity(), 107, error), "successor approval can never invoke OnUsed or native Start again");
        RequireEntry(!entry.BeginScriptCall(100, 200, error) && !entry.ConsumeNativeStart(EntryIdentity(), 107, error), "successor cannot acquire script or native start permissions");
        RequireEntry(entry.BeginBatch(EntryIdentity(), 107, error), "successor admission becomes active directly");
        RequireEntry(!entry.BeginBatch(EntryIdentity(), 108, error), "successor continuation is consumed once");
        RequireEntry(entry.Approve(successor, EntryIdentity(), 108, error), "duplicate successor approval does not restore native permission");
        RequireEntry(entry.GetPhase() == KCDC::Features::Crafting::AlchemyEntryContinuation::Phase::Active, "successor stays active");
        entry.Retire("disconnect from successor");
        RequireEntry(entry.NativeCleanupRequired(), "successor still owns the original native table lifetime on disconnect");
        RequireEntry(entry.Cancellation() == KCDC::Features::Crafting::AlchemyEntryCancellation {"entry-2", "alchemy-2", "alchemy-1"}, "accepted successor closes its own session and names its predecessor");
    }
    {
        KCDC::Features::Crafting::AlchemyEntryContinuation entry;
        InvokeEntry(entry);
        RequireEntry(entry.NativeStartReturned(true, error) && entry.EndScriptCall(0, error) && entry.EndInvocation(true, error), "unanswered successor fixture starts native table once");
        RequireEntry(entry.QueueBatch(EntryIdentity(), "alchemy-1", "entry-2", 104, 1000, error), "completed recipe keeps original table while successor request is pending");
        entry.Retire("exit before successor reply");
        const auto close = entry.Cancellation();
        RequireEntry(close == KCDC::Features::Crafting::AlchemyEntryCancellation {"entry-2", "", "alchemy-1"}, "an unanswered successor closes by its request and the retained predecessor");
        auto late      = EntryApproval();
        late.operation = "entry-2";
        late.session   = "alchemy-2";
        RequireEntry(!entry.Approve(late, EntryIdentity(), 105, error) && entry.Cancellation() == close, "late approval cannot replace the close target");
        RequireEntry(entry.CompleteDisconnectedRetirement(true, true, error) && entry.Cancellation().startRequest.empty(), "new connection cannot inherit a prior table close target");
    }
}
