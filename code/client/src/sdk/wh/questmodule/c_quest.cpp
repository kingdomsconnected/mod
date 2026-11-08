#include "c_quest.h"

#include "../../patterns.h"
#include "../../types.h"
#include "../conceptmodule/c_constant_port.h"

#include <utils/hooking/hooking.h>

#include <cstring>

namespace SDK::wh::questmodule {
    void BindProgressPort(conceptmodule::C_PortRef &reference, E_QuestProgress progress) {
        const SDK::ScopedCryString portName("Progress");

        // Create only takes a string, so the port is born holding one and is
        // immediately written with the typed value. MakeVariant move-assigns,
        // which destroys the string the constructor put there.
        SDK::StdString placeholder("");
        conceptmodule::C_ConstantPort *port = conceptmodule::C_ConstantPort::Create(portName, placeholder);
        hook::call<void>(gPatterns.MakeQuestProgressVariant, port->GetValueStorage(), &progress);

        reference.BindResolvedTarget(port);

        // BindResolvedTarget took its own reference; the one Create handed
        // back is this function's to drop.
        port->Release();
    }

    C_Quest *C_Quest::Create() {
        void *storage = hook::call<void *>(gPatterns.CryActionAllocate, sizeof(C_Quest));
        std::memset(storage, 0, sizeof(C_Quest));
        hook::this_call<void>(gPatterns.C_Quest__Construct, storage);

        C_Quest *quest = static_cast<C_Quest *>(storage);
        quest->AddRef();
        return quest;
    }

    void C_Quest::SetProgress(E_QuestProgress progress) {
        BindProgressPort(m_progress, progress);
    }

    E_QuestProgress C_Quest::GetProgress() const {
        return static_cast<E_QuestProgress>(hook::this_call<std::uint32_t>(gPatterns.C_Quest__GetProgress, this));
    }

    void C_Quest::SetObjectives(C_Objective *const *objectives, std::size_t count) {
        conceptmodule::AssignWeakResourceVector(m_objectives, reinterpret_cast<conceptmodule::C_SharedResource *const *>(objectives), count);
    }
} // namespace SDK::wh::questmodule
