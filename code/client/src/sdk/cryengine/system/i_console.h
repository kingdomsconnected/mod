#pragma once

#include <cstdint>

namespace SDK {
    /**
     * Console-variable flags, the `VF_` set a cvar is registered with and
     * `ICVar::GetFlags` answers with.
     *
     * Only the six `CXConsole::OnBeforeVarChange` (0x180B9409C) tests are
     * named, because those are the six that were read off its own masks:
     * `flags & 0x3000002` is the cheat family, `flags & 0x800` read-only,
     * `flags & 0x40000000` deprecated, and slot 17 -- the +0x88 predicate it
     * calls first -- is itself `flags & 0x800000`. They carry CryEngine's
     * stock values, and the rest of the set is left out rather than guessed.
     */
    enum EVarFlags : std::uint32_t {
        VF_CHEAT              = 0x00000002,
        VF_READONLY           = 0x00000800,
        VF_CONST_CVAR         = 0x00800000,
        VF_CHEAT_ALWAYS_CHECK = 0x01000000,
        VF_CHEAT_NOCHECK      = 0x02000000,
        VF_DEPRECATED         = 0x40000000,
    };

    /**
     * A single console variable.
     *
     * Slots proven against the game's own calls: CIntroMovieRenderer::Initialize
     * reads wh_sys_StartupVideoFileName through +0x28, C_UISaveLoad's +map path
     * writes wh_sys_HideIntroVideo through +0x38, CTimeOfDay::SetAdvancedInfo
     * reads e_TimeOfDaySpeed through +0x20 and writes it through +0x40 with
     * the float in xmm1, and CGame::CompleteInit writes ai_CompatibilityMode
     * through +0x48. CXConsole::OnBeforeVarChange pins +0x60 and +0x68.
     */
    class ICVar {
      public:
        virtual ~ICVar() = default;

        virtual void unk_fn0001() = 0;                 // +0x08 Release
        virtual int GetIVal()     = 0;                 // +0x10
        virtual std::int64_t GetI64Val()          = 0; // +0x18
        virtual float GetFVal()                   = 0; // +0x20
        virtual const char *GetString()           = 0; // +0x28
        virtual const char *GetDataProbeString()  = 0; // +0x30
        // Declared float first: MSVC lays same-name virtual overloads out in
        // reverse, so this order puts Set(int) at +0x38 and Set(float) at
        // +0x40, as the game's own calls have them.
        virtual void Set(float value)             = 0; // +0x40
        virtual void Set(int value)               = 0; // +0x38
        virtual void SetString(const char *value) = 0; // +0x48
        virtual void unk_fn0010()                 = 0; // +0x50
        virtual void unk_fn0011()                 = 0; // +0x58
        virtual std::uint32_t GetFlags()          = 0; // +0x60
        virtual void SetFlags(std::uint32_t flags) = 0; // +0x68

        /**
         * Flag bits CXConsole::OnBeforeVarChange refuses a write for: the
         * cheat family, read-only, const, and the +0x88 predicate that is
         * itself `flags & 0x800000`. Outside dev mode the refusal is silent.
         */
        static constexpr std::uint32_t kWriteBlockingFlags = VF_CHEAT | VF_READONLY | VF_CONST_CVAR | VF_CHEAT_ALWAYS_CHECK | VF_CHEAT_NOCHECK | VF_DEPRECATED;
    };

    /**
     * CryEngine's console, published as gEnv->pConsole (+0xA8).
     *
     * Only GetCVar is resolved. Every call the game makes goes through
     * +0xB8 with a plain (this, name) pair, which pins slot 23.
     */
    class IConsole {
      public:
        virtual ~IConsole() = default;

        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual void unk_fn0016() = 0;
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() = 0;
        virtual void unk_fn0020() = 0;
        virtual void unk_fn0021() = 0;
        virtual void unk_fn0022() = 0;
        virtual ICVar *GetCVar(const char *name) = 0; // +0xB8

        static IConsole *GetInstance();

        /** Convenience: resolve `name` and assign `value`. */
        static void SetIntCVar(const char *name, int value);

        /** Reads an integer variable, returning `fallback` before registration. */
        static int GetIntCVar(const char *name, int fallback);

        /**
         * Assigns an integer variable through ICVar::Set(int), temporarily
         * removing retail write-blocking flags and verifying the result.
         */
        static bool TrySetIntCVar(const char *name, int value, int *resulting = nullptr);

        /**
         * Reads `name` as a float, falling back to `fallback`. A module's
         * variables only exist once it has registered them, so an early lookup
         * legitimately misses.
         */
        static float GetFloatCVar(const char *name, float fallback);

        /**
         * Assigns `value` to `name`, dropping the blocking flags above for the
         * write, and reports whether the variable took it by reading back
         * rather than trusting the call. `resulting` receives what it ended up
         * holding, which differs when an integer variable truncates.
         */
        static bool TrySetFloatCVar(const char *name, float value, float *resulting = nullptr);

        /** True when `name` resolves to a registered variable. */
        static bool HasCVar(const char *name);
    };
} // namespace SDK
