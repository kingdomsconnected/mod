#pragma once

#include "../../cryengine/scriptsystem/i_function_handler.h"
#include "c_anim_door.h"
#include "c_script_bind_base.h"

#include <cstddef>

namespace SDK::wh::entitymodule {
    /** Native Lua surface installed as `entity.animDoor`. */
    class C_ScriptBindAnimDoor {
      public:
        virtual ~C_ScriptBindAnimDoor()                                                      = default; // slot 0
        virtual void unk_fn0001()                                                            = 0;
        virtual void unk_fn0002()                                                            = 0;
        virtual void unk_fn0003()                                                            = 0;
        virtual void AttachTo(C_AnimDoor *door)                                              = 0; // slot 4
        virtual int UpdateBeforeAnimation(SDK::IFunctionHandler *handler)                    = 0; // slot 5
        virtual int ReportOpened(SDK::IFunctionHandler *handler, SDK::EntityId userEntityId) = 0; // slot 6
        virtual int ReportClosed(SDK::IFunctionHandler *handler, SDK::EntityId userEntityId) = 0; // slot 7
        virtual int ReportManipulationStart(SDK::IFunctionHandler *handler)                  = 0; // slot 8

        int GetUserAnimName(SDK::IFunctionHandler *handler, SDK::EntityId userEntityId, bool isOpened, bool shouldBeOpened, bool isFrontSide);
        int GetObjectAnimName(SDK::IFunctionHandler *handler, SDK::EntityId userEntityId, bool isOpened, bool shouldBeOpened, bool isFrontSide);

      private:
        ScriptBindPrefixLayout m_prefix; // 0x08

        static void AssertLayout() {
            static_assert(offsetof(C_ScriptBindAnimDoor, m_prefix) == 0x08, "C_ScriptBindAnimDoor::m_prefix offset mismatch");
        }
    };

    static_assert(sizeof(C_ScriptBindAnimDoor) == 0x70, "C_ScriptBindAnimDoor size mismatch");
} // namespace SDK::wh::entitymodule
