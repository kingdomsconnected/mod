#pragma once

#include <cstddef>

namespace SDK::wh::entitymodule {
    /**
     * Root item-collection runtime shared by inventories and item indexers.
     *
     * Standalone RTTI proves no base classes. Its table at 0x183A2D7F8 has
     * exactly ten slots: six pure operations, three emitted defaults, and a
     * destructor in the final slot. Construction establishes listener
     * state through +0x68, while C_ItemIndexer begins its derived state at
     * +0x70, proving the aligned base extent.
     */
    class C_ItemCollection {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006();
        virtual void unk_fn0007();
        virtual void unk_fn0008();
        virtual ~C_ItemCollection() = default;

      private:
        // Ctor 0x1803A4CA0: an embedded
        // wh::shared::C_DependentListeners<I_ItemCollectionListener, WUID, 2>
        // (its own vftable at +0x08, three zeroed qwords, then a two-entry
        // array built by 0x180A03994 at +0x28).
        std::byte m_listeners0008[0x68];
    };

    static_assert(sizeof(C_ItemCollection) == 0x70,
                  "C_ItemCollection size mismatch");
} // namespace SDK::wh::entitymodule
