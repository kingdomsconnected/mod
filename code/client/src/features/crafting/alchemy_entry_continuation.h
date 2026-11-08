#pragma once

#include "core/obfuscation.h"

#include <cstdint>
#include <string>

namespace KCDC::Features::Crafting {
    /**
     * The table visit's identity on the game thread. The generations are
     * local fences: any change ends the visit.
     */
    struct AlchemyEntryIdentity {
        std::string level;
        std::uint32_t actorEntity        = 0;
        std::uint32_t stationEntity      = 0;
        std::uint64_t stationGuid        = 0;
        std::uint64_t nativeGeneration   = 0;
        std::uint64_t bodyGeneration     = 0;
        std::uint64_t callbackGeneration = 0;

        bool operator==(const AlchemyEntryIdentity &) const = default;
        bool Valid() const {
            return !level.empty() && level.size() <= 256 && actorEntity != 0 && stationEntity != 0 && stationGuid != 0 && nativeGeneration != 0 && bodyGeneration != 0 && callbackGeneration != 0;
        }
    };

    /** The server's admission of one table visit or batch. */
    struct AlchemyEntryApproval {
        std::string operation;
        std::string session;
        std::uint32_t ticket = 0;

        bool operator==(const AlchemyEntryApproval &) const = default;
    };

    /** Exact request identities needed to close a table visit. */
    struct AlchemyEntryCancellation {
        std::string startRequest;
        std::string session;
        std::string continuationOf;
        bool operator==(const AlchemyEntryCancellation &) const = default;
    };

    /**
     * Admission of one table visit: the server's approval, the single
     * replay of the retained Lua entry, and the native Start it must reach
     * exactly once. Calls no engine code; the owner retires the Lua
     * generation and waits for the server to close the table before it
     * completes retirement.
     */
    class AlchemyEntryContinuation final {
      public:
        enum class Phase : std::uint8_t {
            Idle,
            WaitingApproval,
            Approved,
            Invoking,
            Active,
            Retiring,
            Retired
        };

        /** Largest integer ticket exactly representable by ScriptAnyValue float. */
        static constexpr std::uint32_t kMaximumLuaTicket = 0x00FFFFFF;

        bool Queue(const AlchemyEntryIdentity &identity, const std::string &operation, std::uint32_t ticket, std::uint64_t now, std::uint64_t deadline, std::string &error) {
            if (_phase != Phase::Idle && _phase != Phase::Retired) {
                return Refuse(error, std::string(KCDC_HIDE("Alchemy entry already owns an unresolved operation")));
            }
            if (!identity.Valid() || operation.empty() || operation.size() > 64 || ticket == 0 || ticket > kMaximumLuaTicket || deadline <= now) {
                return Refuse(error, std::string(KCDC_HIDE("Invalid alchemy entry request")));
            }
            if (identity.callbackGeneration < _identity.callbackGeneration || (identity.callbackGeneration == _identity.callbackGeneration && ticket <= _ticket)) {
                return Refuse(error, std::string(KCDC_HIDE("Alchemy entry ticket or callback generation was reused")));
            }
            _identity          = identity;
            _operation         = operation;
            _ticket            = ticket;
            _deadline          = deadline;
            _lastNow           = now;
            _approval          = {};
            _insideEntry       = false;
            _insideScriptCall  = false;
            _nativeAttempted   = false;
            _nativeReturned    = false;
            _nativeAccepted    = false;
            _callbackCompleted = false;
            _batchContinuation = false;
            _previousSession.clear();
            _retirementReason.clear();
            _phase = Phase::WaitingApproval;
            error.clear();
            return true;
        }

        /**
         * A finished batch resets the holders while the table interaction
         * stays alive. The next batch takes a new server session but no
         * second OnUsed or Start.
         */
        bool QueueBatch(const AlchemyEntryIdentity &fresh, const std::string &previousSession, const std::string &operation, std::uint64_t now, std::uint64_t deadline, std::string &error) {
            if (_phase != Phase::Active || !_nativeReturned || !_nativeAccepted || !_callbackCompleted || _insideEntry || _insideScriptCall || previousSession != _approval.session || previousSession.empty() || operation.empty() || operation.size() > 64 || operation == _operation
                || deadline <= now) {
                return Refuse(error, std::string(KCDC_HIDE("Alchemy next batch lacks a completed current table session")));
            }
            if (!Observe(fresh, now, error)) {
                return false;
            }
            _previousSession   = previousSession;
            _operation         = operation;
            _deadline          = deadline;
            _approval          = {};
            _batchContinuation = true;
            _phase             = Phase::WaitingApproval;
            error.clear();
            return true;
        }

        /** Call on each game tick and immediately before native continuation. */
        bool Observe(const AlchemyEntryIdentity &fresh, std::uint64_t now, std::string &error) {
            if (_phase == Phase::Idle || _phase == Phase::Retired) {
                return Refuse(error, std::string(KCDC_HIDE("Alchemy entry has no current operation")));
            }
            if (_phase == Phase::Retiring) {
                return Refuse(error, _retirementReason);
            }
            if (fresh != _identity) {
                Retire(std::string(KCDC_HIDE("Alchemy entry identity changed")));
                return Refuse(error, _retirementReason);
            }
            if (now < _lastNow || (_phase != Phase::Active && now >= _deadline)) {
                Retire(now < _lastNow ? std::string(KCDC_HIDE("Alchemy entry clock moved backwards")) : std::string(KCDC_HIDE("Alchemy entry approval expired")));
                return Refuse(error, _retirementReason);
            }
            _lastNow = now;
            error.clear();
            return true;
        }

        bool Approve(const AlchemyEntryApproval &approval, const AlchemyEntryIdentity &fresh, std::uint64_t now, std::string &error) {
            // An unrelated delayed reply must not retire the current attempt.
            if (approval.operation != _operation || approval.ticket != _ticket) {
                return Refuse(error, std::string(KCDC_HIDE("Alchemy approval belongs to a different operation")));
            }
            if (_phase == Phase::Idle || _phase == Phase::Retiring || _phase == Phase::Retired) {
                return Refuse(error, std::string(KCDC_HIDE("Alchemy approval arrived after retirement")));
            }
            if (!Observe(fresh, now, error)) {
                return false;
            }
            if (_phase != Phase::WaitingApproval) {
                if (approval == _approval) {
                    error.clear();
                    return true;
                }
                Retire(std::string(KCDC_HIDE("Alchemy approval changed after acceptance")));
                return Refuse(error, _retirementReason);
            }
            if (approval.session.empty() || approval.session.size() > 160 || (_batchContinuation && approval.session == _previousSession)) {
                return Refuse(error, std::string(KCDC_HIDE("Alchemy approval does not name a new table session")));
            }
            _approval = approval;
            _phase    = Phase::Approved;
            error.clear();
            return true;
        }

        /** Consumes admission before calling the retained Lua original. */
        bool BeginInvocation(const AlchemyEntryIdentity &fresh, std::uint64_t now, std::string &error) {
            if (_phase != Phase::Approved || _batchContinuation) {
                return Refuse(error, std::string(KCDC_HIDE("Alchemy native continuation is not approved")));
            }
            if (!Observe(fresh, now, error)) {
                return false;
            }
            _insideEntry = true;
            _phase       = Phase::Invoking;
            error.clear();
            return true;
        }

        /** Consume a successor approval without replaying native entry. */
        bool BeginBatch(const AlchemyEntryIdentity &fresh, std::uint64_t now, std::string &error) {
            if (_phase != Phase::Approved || !_batchContinuation || !_nativeAccepted || !_callbackCompleted || _insideEntry || _insideScriptCall) {
                return Refuse(error, std::string(KCDC_HIDE("Alchemy next batch is not approved")));
            }
            if (!Observe(fresh, now, error)) {
                return false;
            }
            _phase = Phase::Active;
            error.clear();
            return true;
        }

        /**
         * Exact StartAlchemy Lua bridge scope. Script handles are 64-bit;
         * compare before narrowing so high bits cannot alias an EntityId.
         */
        bool BeginScriptCall(std::uint64_t actorHandle, std::uint64_t stationHandle, std::string &error) {
            if (_phase != Phase::Invoking || !_insideEntry || _insideScriptCall || _nativeAttempted || actorHandle != _identity.actorEntity || stationHandle != _identity.stationEntity) {
                return Refuse(error, std::string(KCDC_HIDE("Alchemy script start has no unused exact permission")));
            }
            _insideScriptCall = true;
            error.clear();
            return true;
        }

        bool ConsumeNativeStart(const AlchemyEntryIdentity &fresh, std::uint64_t now, std::string &error) {
            if (_phase != Phase::Invoking || !_insideEntry || !_insideScriptCall || _nativeAttempted) {
                return Refuse(error, std::string(KCDC_HIDE("Alchemy native start has no unused exact permission")));
            }
            if (!Observe(fresh, now, error)) {
                return false;
            }
            // Latch before the original: callbacks can re-enter or disconnect.
            _nativeAttempted = true;
            error.clear();
            return true;
        }

        /** Record the original native Boolean even if a callback retired us. */
        bool NativeStartReturned(bool accepted, std::string &error) {
            if (!_insideEntry || !_insideScriptCall || !_nativeAttempted || _nativeReturned) {
                return Refuse(error, std::string(KCDC_HIDE("Alchemy native return has no matching invocation")));
            }
            _nativeReturned = true;
            _nativeAccepted = accepted;
            if (!accepted) {
                Retire(std::string(KCDC_HIDE("Native alchemy start refused the approved operation")));
            }
            error.clear();
            return true;
        }

        /**
         * The original StartAlchemy binding returns zero Lua values. Its
         * EndFunction slot is a plain return-zero, so the bridge can return
         * the real accepted Boolean afterward via EndFunctionAny. False
         * must stop the Lua callback before OnUse/tutorial side effects.
         */
        bool EndScriptCall(int originalValueCount, std::string &error) {
            if (!_insideEntry || !_insideScriptCall) {
                return Refuse(error, std::string(KCDC_HIDE("Alchemy script return has no matching invocation")));
            }
            _insideScriptCall = false;
            if (_phase == Phase::Retiring) {
                return Refuse(error, _retirementReason);
            }
            if (originalValueCount != 0 || !_nativeAttempted || !_nativeReturned || !_nativeAccepted) {
                Retire(std::string(KCDC_HIDE("Alchemy script bridge did not capture a native acceptance")));
                return Refuse(error, _retirementReason);
            }
            error.clear();
            return true;
        }

        /**
         * The Lua binding discards Start's Boolean, so only the recorded
         * native acceptance counts. Runs after the Lua stack has unwound.
         */
        bool EndInvocation(bool callbackCompleted, std::string &error) {
            if (!_insideEntry || _insideScriptCall) {
                return Refuse(error, std::string(KCDC_HIDE("Alchemy entry callback already completed or has a native bridge on its stack")));
            }
            _insideEntry       = false;
            _callbackCompleted = callbackCompleted;
            if (_phase == Phase::Retiring) {
                return Refuse(error, _retirementReason);
            }
            if (!_nativeAttempted || !_nativeReturned || !_nativeAccepted || !callbackCompleted) {
                Retire(std::string(KCDC_HIDE("Alchemy entry did not complete an accepted native start")));
                return Refuse(error, _retirementReason);
            }
            _phase = Phase::Active;
            error.clear();
            return true;
        }

        /** No engine cleanup here: a detour may still be on the native stack. */
        void Retire(const std::string &reason) {
            if (_phase == Phase::Idle || _phase == Phase::Retired || _phase == Phase::Retiring) {
                return;
            }
            _retirementReason = reason.empty() ? std::string(KCDC_HIDE("Alchemy entry retired")) : reason;
            _phase            = Phase::Retiring;
        }

        bool CompleteRetirement(bool tableClosed, bool nativeInactive, bool callbackGenerationRetired, std::string &error) {
            if (_phase != Phase::Retiring || _insideEntry || !tableClosed || !nativeInactive || !callbackGenerationRetired) {
                return Refuse(error, std::string(KCDC_HIDE("Alchemy retirement still has unresolved server or native work")));
            }
            _phase = Phase::Retired;
            error.clear();
            return true;
        }

        /**
         * The server drops the session with the connection; this only
         * proves no old holder or callback survives into the next one.
         */
        bool CompleteDisconnectedRetirement(bool nativeInactive, bool callbackGenerationRetired, std::string &error) {
            if ((_phase != Phase::Idle && _phase != Phase::Retired && _phase != Phase::Retiring) || _insideEntry || _insideScriptCall || !nativeInactive || !callbackGenerationRetired) {
                return Refuse(error, std::string(KCDC_HIDE("Disconnected alchemy still owns native holders or callbacks")));
            }
            _phase    = Phase::Retired;
            _approval = {};
            _operation.clear();
            _previousSession.clear();
            _nativeAttempted = _nativeReturned = _nativeAccepted = _callbackCompleted = _batchContinuation = false;
            error.clear();
            return true;
        }

        Phase GetPhase() const {
            return _phase;
        }
        const AlchemyEntryApproval &Approval() const {
            return _approval;
        }
        const std::string &Operation() const {
            return _operation;
        }
        const std::string &PreviousSession() const {
            return _previousSession;
        }
        AlchemyEntryCancellation Cancellation() const {
            if (_phase != Phase::Retiring || _operation.empty()) {
                return {};
            }
            // A lost successor admission still holds the predecessor's table.
            // The server closes whichever of these sessions still owns it.
            return {_operation, _approval.session, _batchContinuation ? _previousSession : std::string()};
        }
        bool NativeCleanupRequired() const {
            return _phase == Phase::Retiring && _nativeAttempted;
        }
        bool MayPerformNativeCleanup() const {
            return _phase == Phase::Retiring && !_insideEntry;
        }
        bool IsInsideInvocation() const {
            return _insideEntry;
        }
        bool HasNativeAcceptance() const {
            return _nativeReturned && _nativeAccepted;
        }
        bool IsBatchContinuation() const {
            return _batchContinuation;
        }

      private:
        static bool Refuse(std::string &error, const std::string &reason) {
            error = reason;
            return false;
        }
        Phase _phase = Phase::Idle;
        AlchemyEntryIdentity _identity;
        AlchemyEntryApproval _approval;
        std::string _operation;
        std::string _previousSession;
        std::string _retirementReason;
        std::uint32_t _ticket   = 0;
        std::uint64_t _deadline = 0;
        std::uint64_t _lastNow  = 0;
        bool _insideEntry       = false;
        bool _insideScriptCall  = false;
        bool _nativeAttempted   = false;
        bool _nativeReturned    = false;
        bool _nativeAccepted    = false;
        bool _callbackCompleted = false;
        bool _batchContinuation = false;
    };
} // namespace KCDC::Features::Crafting
