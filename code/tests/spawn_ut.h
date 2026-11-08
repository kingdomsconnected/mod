#pragma once

#include "common/spawn/actor_spawner.h"
#include "sdk/cryengine/scriptsystem/i_script_table.h"

#include <limits>
#include <map>
#include <memory>
#include <string>

namespace SpawnTests {
    struct TableData;
    struct Value {
        SDK::EScriptAnyType type = SDK::EScriptAnyType::Nil;
        std::string text;
        bool boolean = false;
        float number = 0;
        std::shared_ptr<TableData> table;
    };
    struct TableData {
        std::map<std::string, Value> values;
    };

    class ScriptSystem;
    class Table final: public SDK::IScriptTable {
      public:
        Table(ScriptSystem &system, std::shared_ptr<TableData> data = std::make_shared<TableData>());
        ~Table() override;
        SDK::IScriptSystem *GetScriptSystem() const override;
        void AddRef() override {
            ++references;
        }
        void Release() override {
            if (--references == 0) {
                delete this;
            }
        }
        void SetValueAny(const char *key, const SDK::ScriptAnyValue &value, bool) override {
            auto &out = data->values[key];
            out.type  = value.type;
            switch (value.type) {
            case SDK::EScriptAnyType::String: out.text = value.str; break;
            case SDK::EScriptAnyType::Boolean: out.boolean = value.boolean; break;
            case SDK::EScriptAnyType::Number: out.number = value.number; break;
            case SDK::EScriptAnyType::Table: out.table = static_cast<Table *>(value.table)->data; break;
            default: break;
            }
        }
        bool GetValueAny(const char *key, SDK::ScriptAnyValue &out, bool) override {
            const auto it = data->values.find(key);
            if (it == data->values.end() || it->second.type != out.type) {
                return false;
            }
            if (out.type == SDK::EScriptAnyType::Table) {
                out.table = new Table(system, it->second.table);
                out.table->AddRef();
            }
            else if (out.type == SDK::EScriptAnyType::String) {
                out.str = it->second.text.c_str();
            }
            return true;
        }
        bool Clone(SDK::IScriptTable *source, bool deepCopy, bool copyByReference) override;

        void Opaque02() override {}
        void Opaque05() override {}
        void Opaque06() override {}
        void Opaque09() override {}
        void Opaque10() override {}
        void Opaque11() override {}
        void Opaque12() override {}
        void Opaque13() override {}
        void Opaque14() override {}
        void Opaque15() override {}
        void Opaque16() override {}
        void Opaque17() override {}
        void Opaque18() override {}
        void Opaque19() override {}

        ScriptSystem &system;
        std::shared_ptr<TableData> data;
        int references = 0;
    };

    class ScriptSystem final: public SDK::IScriptSystem {
      public:
        SDK::IScriptTable *CreateSizedTable(int, int) override {
            return new Table(*this);
        }
        SDK::IScriptTable *CreateTable(bool) override {
            return new Table(*this);
        }
        void Opaque01() override {}
        void Opaque02() override {}
        void Opaque03() override {}
        void Opaque04() override {}
        void Opaque05() override {}
        bool ExecuteBuffer(const char *, std::size_t, const char *, SDK::IScriptTable *) override {
            return false;
        }
        void Opaque07() override {}
        void Opaque08() override {}
        void Opaque09() override {}
        void Opaque10() override {}
        void Opaque11() override {}
        void Opaque14() override {}
        bool BeginCallTableMethod(SDK::IScriptTable *, const char *) override {
            return false;
        }
        bool BeginCallNamedTableMethod(const char *, const char *) override {
            return false;
        }
        bool BeginCallNamedFunction(const char *) override {
            return false;
        }
        bool BeginCallFunction(SDK::HScriptFunction) override {
            return false;
        }
        bool EndCall() override {
            return false;
        }
        void Opaque20() override {}
        void Opaque21() override {}
        void Opaque22() override {}
        void Opaque23() override {}
        void Opaque24() override {}
        SDK::HScriptFunction AddFuncRef(SDK::HScriptFunction function) override {
            return function;
        }
        void Opaque26() override {}
        void ReleaseFunc(SDK::HScriptFunction) override {}
        void Opaque28() override {}
        void Opaque29() override {}
        void PushFuncParamAny(const SDK::ScriptAnyValue &) override {}
        int liveTables = 0;
        bool failClone = false;
    };

    inline Table::Table(ScriptSystem &system, std::shared_ptr<TableData> data): system(system), data(std::move(data)) {
        ++system.liveTables;
    }
    inline Table::~Table() {
        --system.liveTables;
    }
    inline SDK::IScriptSystem *Table::GetScriptSystem() const {
        return &system;
    }
    inline bool Table::Clone(SDK::IScriptTable *source, bool deepCopy, bool copyByReference) {
        if (system.failClone || !deepCopy || copyByReference) {
            return false;
        }
        data->values = static_cast<Table *>(source)->data->values;
        for (auto &[key, value] : data->values) {
            if (value.type == SDK::EScriptAnyType::Table) {
                Table nested(system, value.table);
                const auto copy = nested.CloneTable();
                value.table     = static_cast<Table *>(copy.get())->data;
            }
        }
        return true;
    }
} // namespace SpawnTests

MODULE(spawn_contracts, {
    IT("releases every acquired property handle, including nested and missing reads", {
        SpawnTests::ScriptSystem system;
        {
            SpawnTests::Table root(system);
            auto properties                       = std::make_shared<SpawnTests::TableData>();
            properties->values["Physics"].type    = SDK::EScriptAnyType::Table;
            properties->values["Physics"].table   = std::make_shared<SpawnTests::TableData>();
            root.data->values["Properties"].type  = SDK::EScriptAnyType::Table;
            root.data->values["Properties"].table = properties;
            for (int i = 0; i < 100; ++i) {
                auto props   = root.GetTable("Properties");
                auto physics = props->GetTable("Physics");
                EQUALS(system.liveTables, 3);
                EQUALS(bool(props->GetTable("Missing")), false);
                EQUALS(system.liveTables, 3);
            }
            EQUALS(system.liveTables, 1);
        }
        EQUALS(system.liveTables, 0);
    });

    IT("detaches archetype soul and nested physics without mutating shared defaults", {
        SpawnTests::ScriptSystem system;
        SpawnTests::Table archetype(system);
        archetype.SetValue("guidSharedSoulId", "original");
        auto physics                            = std::make_shared<SpawnTests::TableData>();
        physics->values["bPhysicalize"].type    = SDK::EScriptAnyType::Boolean;
        physics->values["bPhysicalize"].boolean = true;
        archetype.data->values["Physics"].type  = SDK::EScriptAnyType::Table;
        archetype.data->values["Physics"].table = physics;
        {
            auto copy = archetype.CloneTable();
            copy->SetValue("guidSharedSoulId", "override");
            copy->GetTable("Physics")->SetValue("bPhysicalize", false);
            EQUALS(archetype.data->values["guidSharedSoulId"].text, std::string("original"));
            EQUALS(physics->values["bPhysicalize"].boolean, true);
            const auto copiedPhysics = static_cast<SpawnTests::Table *>(copy.get())->data->values["Physics"].table;
            EQUALS(copiedPhysics->values["bPhysicalize"].type == SDK::EScriptAnyType::Boolean, true);
            EQUALS(copiedPhysics->values["bPhysicalize"].boolean, false);
        }
        EQUALS(system.liveTables, 1);
        system.failClone = true;
        EQUALS(bool(archetype.CloneTable()), false);
        EQUALS(system.liveTables, 1);
    });

    IT("rejects nonfinite and degenerate transforms before native calls", {
        const float nan      = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();
        KCDC::Common::Spawn::WorldPlacement placement;
        EQUALS(KCDC::Common::Spawn::NormalizeSpawnTransform(placement), true);
        for (const float scale : {0.0F, -1.0F, nan, infinity}) {
            EQUALS(KCDC::Common::Spawn::NormalizeSpawnTransform(placement, scale), false);
        }
        placement.position.z = nan;
        EQUALS(KCDC::Common::Spawn::NormalizeSpawnTransform(placement), false);
        placement.position.z = 0;
        placement.rotation.w = infinity;
        EQUALS(KCDC::Common::Spawn::NormalizeSpawnTransform(placement), false);
        placement.rotation = {};
        EQUALS(KCDC::Common::Spawn::NormalizeSpawnTransform(placement), false);
        placement.rotation.w = 2;
        EQUALS(KCDC::Common::Spawn::NormalizeSpawnTransform(placement), true);
        EQUALS(placement.rotation.w, 1.0F);
        placement.rotation.x = std::numeric_limits<float>::max();
        EQUALS(KCDC::Common::Spawn::NormalizeSpawnTransform(placement), true);
        EQUALS(placement.rotation.x, 1.0F);
    });

    IT("retries temporary refusals but not broken initialization", {
        EQUALS(KCDC::Common::Spawn::IsRetryableSpawnResult(KCDC::Common::Spawn::SpawnResult::NoEntitySystem), true);
        EQUALS(KCDC::Common::Spawn::IsRetryableSpawnResult(KCDC::Common::Spawn::SpawnResult::ClassNotRegistered), true);
        EQUALS(KCDC::Common::Spawn::IsRetryableSpawnResult(KCDC::Common::Spawn::SpawnResult::SpawnRefused), true);
        EQUALS(KCDC::Common::Spawn::IsRetryableSpawnResult(KCDC::Common::Spawn::SpawnResult::InitFailed), false);
        EQUALS(KCDC::Common::Spawn::IsRetryableSpawnResult(KCDC::Common::Spawn::SpawnResult::InvalidTransform), false);
        EQUALS(KCDC::Common::Spawn::IsRetryableSpawnResult(KCDC::Common::Spawn::SpawnResult::InvalidSoulGuid), false);
    });
});
