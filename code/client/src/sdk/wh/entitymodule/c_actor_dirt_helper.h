#pragma once

namespace SDK::wh::entitymodule {
    /**
     * The dirt and blood state of an actor's body, held at `C_Actor+0x260`.
     *
     * `C_Actor::CopyDirtAndBloodFromActor` (actor slot 140) forwards to
     * `CopyBodyDirtAndBloodFromActor` (0x180AB0F54) on it when present. Only
     * the vtable is proven; the size is not.
     */
    class C_ActorDirtHelper {
      public:
        virtual ~C_ActorDirtHelper() = default;
    };
} // namespace SDK::wh::entitymodule
