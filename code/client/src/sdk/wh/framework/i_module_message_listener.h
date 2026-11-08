#pragma once

namespace SDK::wh::framework {
    /**
     * Listener for Warhorse framework module messages.
     *
     * The abstract table at 0x183A65150 contains exactly a virtual destructor
     * and one callback whose source signature is not present in the binary.
     */
    class I_ModuleMessageListener {
      public:
        virtual ~I_ModuleMessageListener() = default;
        virtual void unk_fn0001() = 0;
    };

    static_assert(sizeof(I_ModuleMessageListener) == 0x8, "I_ModuleMessageListener size mismatch");
} // namespace SDK::wh::framework
