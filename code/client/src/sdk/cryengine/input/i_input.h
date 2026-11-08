#pragma once

#include "i_input_event_listener.h"
#include "s_input_event.h"

namespace SDK {

    /**
     * CryInput's input system, published as gEnv->pInput (+0x40) by
     * CEngineModule_CryInput::Initialize.
     *
     * KCD2 shuffles this interface; only the slots proven against CDXInput's
     * table at 0x184078F18 are named:
     *
     *   +0x38 / +0x40  a plain setter/getter pair over the exclusive-listener
     *                  field at CDXInput+0xA8.
     *   +0x58          stores its argument at CDXInput+0xD8 and logs
     *                  "InputDebug: EnableEventPosting(%s)".
     *   +0x60          returns that same byte.
     *
     * PostInputEvent (+0x68) drops every non-forced event while that byte is
     * clear, so EnableEventPosting(false) silences the whole input system --
     * action maps, direct listeners and raw device deltas alike.
     */
    class IInput {
      public:
        virtual ~IInput() = default;

        virtual void unk_fn0001()                                        = 0; // AddEventListener
        virtual void unk_fn0002()                                        = 0; // RemoveEventListener
        virtual void unk_fn0003()                                        = 0; // AddConsoleEventListener
        virtual void unk_fn0004()                                        = 0; // RemoveConsoleEventListener
        virtual void unk_fn0005()                                        = 0; // AddTouchEventListener
        virtual void unk_fn0006()                                        = 0; // RemoveTouchEventListener
        virtual void SetExclusiveListener(IInputEventListener *listener) = 0; // +0x38
        virtual IInputEventListener *GetExclusiveListener()              = 0; // +0x40
        virtual void unk_fn0009()                                        = 0;
        virtual void unk_fn0010()                                        = 0;
        virtual void EnableEventPosting(bool enable)                     = 0; // +0x58
        virtual bool IsEventPostingEnabled() const                       = 0; // +0x60
        virtual void PostInputEvent(const SInputEvent &event, bool force = false) = 0; // +0x68, CBaseInput::PostInputEvent

        static IInput *GetInstance();
    };
} // namespace SDK
