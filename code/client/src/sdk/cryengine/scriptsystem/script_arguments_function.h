#pragma once

#include "i_script_system.h"

#include <cstddef>

namespace SDK {
    /**
     * The callable behind a `std::function<void(IScriptSystem &)>`, in the
     * vtable order of the game's own STL: MSVC's `std::_Func_base` as built
     * with RTTI. `_Func_impl_no_alloc<lambda, void, IScriptSystem &>` at
     * 0x183EA99B8 has exactly these six slots, `_Do_call` third.
     *
     * Spelled out rather than taken from `<functional>`: the release client
     * is built `/GR-`, which drops `_Target_type` from this STL's `_Func_base`
     * and moves `_Delete_this` up a slot, so a `std::function` of ours handed
     * to the game would have its destructor called through the wrong slot.
     * No destructor is declared, because a virtual one would add a slot.
     */
    class IScriptArgumentsCallable {
      public:
        virtual IScriptArgumentsCallable *Copy(void *storage) const = 0; // +0x00, _Copy
        virtual IScriptArgumentsCallable *Move(void *storage)       = 0; // +0x08, _Move
        virtual void Call(IScriptSystem &scriptSystem)              = 0; // +0x10, _Do_call
        virtual const void *TargetType() const                      = 0; // +0x18, _Target_type
        virtual void DeleteThis(bool deallocate)                    = 0; // +0x20, _Delete_this
        virtual const void *Target() const                          = 0; // +0x28, _Get

      protected:
        ~IScriptArgumentsCallable() = default;
    };

    /**
     * A `std::function<void(IScriptSystem &)>` object as the game lays it out:
     * seven pointers of small-object storage, then `_Impl`. A native taking
     * one by value receives its address and destroys it itself -- it calls
     * `DeleteThis(_Impl != this)` and clears `_Impl` -- so a callable held
     * outside the storage is never freed by the game.
     */
    struct ScriptArgumentsFunction {
        std::byte m_storage[0x38];                   // 0x00
        IScriptArgumentsCallable *m_pImpl = nullptr; // 0x38, null: the empty function, which pushes nothing

        ScriptArgumentsFunction(): m_storage {} {}

        explicit ScriptArgumentsFunction(IScriptArgumentsCallable &callable): m_storage {}, m_pImpl(&callable) {}
    };

    static_assert(sizeof(ScriptArgumentsFunction) == 0x40, "ScriptArgumentsFunction size mismatch");
    static_assert(offsetof(ScriptArgumentsFunction, m_pImpl) == 0x38, "ScriptArgumentsFunction::m_pImpl offset mismatch");

    /**
     * Pushes one boolean: what the engine's own caller at 0x1808C7F58 hands
     * `C_LockBase::SetLocked` for AnimDoor's `Lock(dontClose)`. Owned by the
     * caller; the game only ever calls `Call` and `DeleteThis` on it.
     */
    class ScriptBooleanArgument final: public IScriptArgumentsCallable {
      public:
        explicit ScriptBooleanArgument(bool value): _value(value) {}

        IScriptArgumentsCallable *Copy(void *) const override {
            return nullptr;
        }
        IScriptArgumentsCallable *Move(void *) override {
            return nullptr;
        }
        void Call(IScriptSystem &scriptSystem) override {
            scriptSystem.PushFuncParamAny(ScriptAnyValue(_value));
        }
        const void *TargetType() const override {
            return nullptr;
        }
        void DeleteThis(bool) override {}
        const void *Target() const override {
            return nullptr;
        }

      private:
        bool _value;
    };
} // namespace SDK
