#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    class IScriptTable;

    using HScriptFunction = std::uint32_t;

    enum class EScriptAnyType : std::int32_t {
        Any      = 0,
        Nil      = 1,
        Boolean  = 2,
        Handle   = 3,
        Number   = 4,
        String   = 5,
        Table    = 6,
        Function = 7,
        UserData = 8,
        Vector   = 9,
    };

    /** Native tagged value; table payloads passed to setters are borrowed. */
    struct ScriptAnyValue {
        EScriptAnyType type       = EScriptAnyType::Any;
        std::uint32_t padding0004 = 0;
        union {
            const char *str;
            IScriptTable *table;
            void *pointer;
            std::uint64_t handle;
            HScriptFunction function;
            float number;
            bool boolean;
            float vector[3];
        };

        ScriptAnyValue(): pointer(nullptr) {}

        explicit ScriptAnyValue(const char *value): type(EScriptAnyType::String) {
            str = value;
        }

        explicit ScriptAnyValue(float value): type(EScriptAnyType::Number) {
            number = value;
        }

        explicit ScriptAnyValue(bool value): type(EScriptAnyType::Boolean) {
            boolean = value;
        }

        explicit ScriptAnyValue(IScriptTable *value): type(EScriptAnyType::Table) {
            table = value;
        }

        static ScriptAnyValue Nil() {
            ScriptAnyValue value;
            value.type = EScriptAnyType::Nil;
            return value;
        }

        static ScriptAnyValue Handle(std::uint64_t rawHandle) {
            ScriptAnyValue value;
            value.type   = EScriptAnyType::Handle;
            value.handle = rawHandle;
            return value;
        }
    };

    static_assert(sizeof(ScriptAnyValue) == 0x18, "ScriptAnyValue size mismatch");
    static_assert(offsetof(ScriptAnyValue, type) == 0x00, "ScriptAnyValue::type offset mismatch");
    static_assert(offsetof(ScriptAnyValue, padding0004) == 0x04, "ScriptAnyValue::padding0004 offset mismatch");
    static_assert(offsetof(ScriptAnyValue, str) == 0x08, "ScriptAnyValue::str offset mismatch");
    static_assert(offsetof(ScriptAnyValue, table) == 0x08, "ScriptAnyValue::table offset mismatch");
    static_assert(offsetof(ScriptAnyValue, pointer) == 0x08, "ScriptAnyValue::pointer offset mismatch");
    static_assert(offsetof(ScriptAnyValue, handle) == 0x08, "ScriptAnyValue::handle offset mismatch");
    static_assert(offsetof(ScriptAnyValue, function) == 0x08, "ScriptAnyValue::function offset mismatch");
    static_assert(offsetof(ScriptAnyValue, number) == 0x08, "ScriptAnyValue::number offset mismatch");
    static_assert(offsetof(ScriptAnyValue, boolean) == 0x08, "ScriptAnyValue::boolean offset mismatch");
    static_assert(offsetof(ScriptAnyValue, vector) == 0x08, "ScriptAnyValue::vector offset mismatch");

    /** KCD2 CScriptSystem primary vtable at 0x183B8C610. */
    class IScriptSystem {
      public:
        virtual ~IScriptSystem()                                                 = default;
        virtual void Opaque01()                                                  = 0;
        virtual void Opaque02()                                                  = 0;
        virtual void Opaque03()                                                  = 0;
        virtual void Opaque04()                                                  = 0;
        virtual void Opaque05()                                                  = 0;
        virtual bool ExecuteBuffer(const char *buffer, std::size_t size, const char *bufferDescription, IScriptTable *environment) = 0; // +0x30
        virtual void Opaque07()                                                  = 0;
        virtual void Opaque08()                                                  = 0;
        virtual void Opaque09()                                                  = 0;
        virtual void Opaque10()                                                  = 0;
        virtual void Opaque11()                                                  = 0;
        /**
         * The native `CreateTable(int, int)` overload. Named apart from
         * `CreateTable(bool)` for the same reason as the call methods below:
         * as overloads MSVC swaps their slots, and `CreateTable(false)` then
         * lands here with a garbage record capacity and panics Lua.
         */
        virtual IScriptTable *CreateSizedTable(int arrayCapacity, int recordCapacity) = 0; // +0x60
        // Returns a table with no references; callers must AddRef.
        virtual IScriptTable *CreateTable(bool empty = false) = 0; // +0x68
        virtual void Opaque14() = 0; // +0x70

        /**
         * Looks up `table[method]` and begins a zero-argument Lua call. It
         * does not supply Lua `self`; colon-method callers must push `table`.
         *
         * These C++ names are deliberately distinct. The native methods are
         * overloads, but declaring them as overloads makes MSVC group their
         * virtual slots in a different order from this shipped vtable.
         */
        virtual bool BeginCallTableMethod(IScriptTable *table, const char *method) = 0; // +0x78
        virtual bool BeginCallNamedTableMethod(const char *table, const char *method) = 0; // +0x80
        virtual bool BeginCallNamedFunction(const char *function) = 0;                    // +0x88
        virtual bool BeginCallFunction(HScriptFunction function) = 0;                     // +0x90
        virtual bool EndCall() = 0;                                                            // +0x98
        virtual void Opaque20() = 0;                                                           // +0xA0
        virtual void Opaque21() = 0;                                                           // +0xA8
        virtual void Opaque22() = 0;                                                           // +0xB0
        virtual void Opaque23() = 0;                                                           // +0xB8
        virtual void Opaque24() = 0;                                                           // +0xC0
        /**
         * Takes a second Lua-registry reference to the function `function`
         * names, which outlives the first: `CScriptSystem::AddFuncRef`, slot 25.
         * Pair every one with ReleaseFunc.
         */
        virtual HScriptFunction AddFuncRef(HScriptFunction function) = 0; // +0xC8
        virtual void Opaque26() = 0;                                                           // +0xD0
        /** Releases one Lua-registry function reference. */
        virtual void ReleaseFunc(HScriptFunction function) = 0; // +0xD8
        virtual void Opaque28() = 0;                             // +0xE0
        virtual void Opaque29() = 0;                             // +0xE8

        /** Pushes one already-tagged value into the active call. */
        virtual void PushFuncParamAny(const ScriptAnyValue &value) = 0; // +0xF0

        /** Converts and consumes the top Lua stack value. */
        bool PopStackValue(ScriptAnyValue &value);

        /**
         * `gEnv->pScriptSystem`: the sixth pointer of SSystemGlobalEnvironment,
         * +0x28, and the global the game's
         * own natives read (0x18492D828, e.g. CallOnLockpickedScript).
         */
        static IScriptSystem *GetInstance();
    };

    static_assert(sizeof(IScriptSystem) == 0x8, "IScriptSystem size mismatch");
} // namespace SDK
