#include "i_console.h"

#include "s_system_global_environment.h"

#include "../../patterns.h"

namespace SDK {
    IConsole *IConsole::GetInstance() {
        return SSystemGlobalEnvironment::Get().pConsole;
    }

    void IConsole::SetIntCVar(const char *name, int value) {
        TrySetIntCVar(name, value);
    }

    int IConsole::GetIntCVar(const char *name, int fallback) {
        ICVar *variable = GetInstance()->GetCVar(name);
        return variable != nullptr ? variable->GetIVal() : fallback;
    }

    bool IConsole::TrySetIntCVar(const char *name, int value, int *resulting) {
        ICVar *variable = GetInstance()->GetCVar(name);
        if (variable == nullptr) {
            return false;
        }

        const std::uint32_t flags = variable->GetFlags();
        if ((flags & ICVar::kWriteBlockingFlags) != 0) {
            variable->SetFlags(flags & ~ICVar::kWriteBlockingFlags);
            variable->Set(value);
            variable->SetFlags(flags);
        }
        else {
            variable->Set(value);
        }

        const int applied = variable->GetIVal();
        if (resulting != nullptr) {
            *resulting = applied;
        }
        return applied == value;
    }

    float IConsole::GetFloatCVar(const char *name, float fallback) {
        ICVar *variable = GetInstance()->GetCVar(name);
        return variable != nullptr ? variable->GetFVal() : fallback;
    }

    bool IConsole::HasCVar(const char *name) {
        return GetInstance()->GetCVar(name) != nullptr;
    }

    bool IConsole::TrySetFloatCVar(const char *name, float value, float *resulting) {
        ICVar *variable = GetInstance()->GetCVar(name);
        if (variable == nullptr) {
            return false;
        }

        // The retail console drops writes to cheat, read-only and const
        // variables on the floor without reporting anything, so the blocking
        // bits come off for the write and go straight back on afterwards.
        const std::uint32_t flags = variable->GetFlags();
        if ((flags & ICVar::kWriteBlockingFlags) != 0) {
            variable->SetFlags(flags & ~ICVar::kWriteBlockingFlags);
            variable->Set(value);
            variable->SetFlags(flags);
        }
        else {
            variable->Set(value);
        }

        const float applied = variable->GetFVal();
        if (resulting != nullptr) {
            *resulting = applied;
        }

        // An integer variable truncates, so a request of 0.4 legitimately
        // lands as 0; the write counts as honoured when the variable moved to
        // the value it is able to represent.
        return applied == value || applied == static_cast<float>(static_cast<int>(value));
    }
} // namespace SDK
