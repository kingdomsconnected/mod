#pragma once

#include "i_script_system.h"

#include <memory>

namespace SDK {
    class IScriptTable;

    struct ScriptTableRelease {
        void operator()(IScriptTable *table) const;
    };

    /** Owns one native reference; never deletes through the mod's heap. */
    using ScriptTablePtr = std::unique_ptr<IScriptTable, ScriptTableRelease>;

    /** CScriptTable vtable at 0x183A4A700. */
    class IScriptTable {
      public:
        virtual ~IScriptTable()                                                                       = default; // +0x00
        virtual IScriptSystem *GetScriptSystem() const                                                = 0;       // +0x08
        virtual void Opaque02()                                                                       = 0;
        virtual void AddRef()                                                                         = 0; // +0x18
        virtual void Release()                                                                        = 0; // +0x20
        virtual void Opaque05()                                                                       = 0;
        virtual void Opaque06()                                                                       = 0;
        virtual void SetValueAny(const char *key, const ScriptAnyValue &value, bool chain = false)    = 0; // +0x38
        virtual bool GetValueAny(const char *key, ScriptAnyValue &value, bool chain = false)          = 0; // +0x40
        virtual void Opaque09()                                                                       = 0;
        virtual void Opaque10()                                                                       = 0;
        virtual void Opaque11()                                                                       = 0;
        virtual void Opaque12()                                                                       = 0;
        virtual void Opaque13()                                                                       = 0;
        virtual void Opaque14()                                                                       = 0;
        virtual void Opaque15()                                                                       = 0;
        virtual void Opaque16()                                                                       = 0;
        virtual void Opaque17()                                                                       = 0;
        virtual void Opaque18()                                                                       = 0;
        virtual void Opaque19()                                                                       = 0;
        virtual bool Clone(IScriptTable *source, bool deepCopy = false, bool copyByReference = false) = 0; // +0xA0

        /** GetValueAny acquires a native reference for table results. */
        ScriptTablePtr GetTable(const char *key) {
            ScriptAnyValue any;
            any.type         = EScriptAnyType::Table;
            const bool found = GetValueAny(key, any);
            ScriptTablePtr result(any.table);
            if (!found) {
                return {};
            }
            return result;
        }

        ScriptTablePtr CloneTable() {
            ScriptTablePtr copy(GetScriptSystem()->CreateTable(false));
            if (!copy) {
                return {};
            }
            copy->AddRef();
            if (!copy->Clone(this, true, false)) {
                return {};
            }
            return copy;
        }

        bool GetValue(const char *key, const char *&out) {
            ScriptAnyValue any;
            any.type = EScriptAnyType::String;
            if (!GetValueAny(key, any) || any.type != EScriptAnyType::String) {
                return false;
            }
            out = any.str;
            return out != nullptr;
        }

        bool GetValue(const char *key, float &out) {
            ScriptAnyValue any;
            any.type = EScriptAnyType::Number;
            if (!GetValueAny(key, any) || any.type != EScriptAnyType::Number) {
                return false;
            }
            out = any.number;
            return true;
        }

        bool GetValue(const char *key, bool &out) {
            ScriptAnyValue any;
            any.type = EScriptAnyType::Boolean;
            if (!GetValueAny(key, any) || any.type != EScriptAnyType::Boolean) {
                return false;
            }
            out = any.boolean;
            return true;
        }

        void SetValue(const char *key, const char *value) {
            SetValueAny(key, ScriptAnyValue {value});
        }

        void SetValue(const char *key, float value) {
            SetValueAny(key, ScriptAnyValue {value});
        }

        void SetValue(const char *key, bool value) {
            SetValueAny(key, ScriptAnyValue {value});
        }

        void SetValue(const char *key, IScriptTable *value) {
            SetValueAny(key, ScriptAnyValue {value});
        }
    };

    inline void ScriptTableRelease::operator()(IScriptTable *table) const {
        table->Release();
    }

    static_assert(sizeof(IScriptTable) == 0x8, "IScriptTable size mismatch");
} // namespace SDK
