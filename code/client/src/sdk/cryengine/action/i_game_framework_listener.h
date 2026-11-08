#pragma once

namespace SDK {
    struct SActionEvent;

    /**
     * IGameFrameworkListener, vtable 0x183AA5560. KCD2 emits nine slots.
     *
     * Slot 1 is CryEngine's `OnPostUpdate(float)`: the framework calls it
     * once a frame on every registered listener, and `C_Player`'s
     * (0x181229578, on its +0xA68 base) ticks the player's interactors from
     * it. Slot 5 takes the action event, as `OnActionEvent` does.
     */
    class IGameFrameworkListener {
      public:
        virtual ~IGameFrameworkListener() = default; // +0x00
        virtual void OnPostUpdate(float frameTime) = 0; // +0x08
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005(const SActionEvent &event) = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0; // +0x40
    };

    static_assert(sizeof(IGameFrameworkListener) == 0x8, "IGameFrameworkListener size mismatch");
} // namespace SDK
