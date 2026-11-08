#pragma once

#include "../entitymodule/c_item.h"
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * Quick-access-menu equipment contract used by outfit states.
     *
     * Standalone RTTI proves an exact 39-slot table at 0x183A94B20: its
     * virtual destructor followed by 38 pure operations. String data begins
     * immediately after slot 38, proving the boundary. Stripped operation
     * signatures remain intentionally opaque.
     */
    class I_QAMManager {
      public:
        virtual ~I_QAMManager()                                                   = default;
        virtual void unk_fn0001()                                                 = 0;
        virtual void unk_fn0002()                                                 = 0;
        virtual void unk_fn0003()                                                 = 0;
        virtual void unk_fn0004()                                                 = 0;
        virtual void SelectSlot(std::uint32_t slot, bool removePreviousEquipment) = 0; // 0x1808EF794
        virtual void unk_fn0006()                                                 = 0;
        virtual void unk_fn0007()                                                 = 0;
        virtual void unk_fn0008()                                                 = 0;
        virtual void unk_fn0009()                                                 = 0;
        virtual void unk_fn0010()                                                 = 0;
        virtual void unk_fn0011()                                                 = 0;
        virtual void unk_fn0012()                                                 = 0;
        virtual void unk_fn0013()                                                 = 0;
        virtual void unk_fn0014()                                                 = 0;
        virtual void unk_fn0015()                                                 = 0;
        virtual void unk_fn0016()                                                 = 0;
        virtual void unk_fn0017()                                                 = 0;
        virtual void unk_fn0018()                                                 = 0;
        virtual void unk_fn0019()                                                 = 0;
        virtual void unk_fn0020()                                                 = 0;
        virtual void unk_fn0021()                                                 = 0;
        virtual void SetItem(entitymodule::C_Item *item, std::uint32_t slot)      = 0; // 0x18197A758
        virtual void unk_fn0023()                                                 = 0;
        virtual void unk_fn0024()                                                 = 0;
        virtual void unk_fn0025()                                                 = 0;
        virtual void ClearAllSlots()                                              = 0; // 0x1808F2B94
        virtual void unk_fn0027()                                                 = 0;
        virtual void SynchronizeSlotsFromEquipment()                              = 0; // slot 28, +0xE0
        virtual void unk_fn0029()                                                 = 0;
        virtual void unk_fn0030()                                                 = 0;
        virtual void unk_fn0031()                                                 = 0;
        virtual void unk_fn0032()                                                 = 0;
        virtual void unk_fn0033()                                                 = 0;
        virtual void unk_fn0034()                                                 = 0;
        virtual void unk_fn0035()                                                 = 0;
        virtual void unk_fn0036()                                                 = 0;
        virtual void unk_fn0037()                                                 = 0;
        virtual void unk_fn0038()                                                 = 0;
    };

    static_assert(sizeof(I_QAMManager) == 0x8, "I_QAMManager size mismatch");
} // namespace SDK::wh::playermodule
