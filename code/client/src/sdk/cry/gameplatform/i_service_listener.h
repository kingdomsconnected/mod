#pragma once

namespace SDK::Cry::GamePlatform {
    /** Game-platform service listener mixed into the GUI module. */
    class IServiceListener {
      public:
        virtual ~IServiceListener() = default;
    };

    static_assert(sizeof(IServiceListener) == 0x8, "IServiceListener size mismatch");
} // namespace SDK::Cry::GamePlatform
