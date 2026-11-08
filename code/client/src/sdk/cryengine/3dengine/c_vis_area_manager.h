#pragma once

#include "i_vis_area_manager.h"

#include <cstddef>

namespace SDK {
    /**
     * Concrete indoor/outdoor visibility-area manager.
     *
     * CVisAreaManager's table at 0x183A64EB0 implements all eleven
     * IVisAreaManager entries and appends two concrete operations. Matching
     * allocation and scalar-deletion paths prove the complete 0x220-byte
     * size. The added contracts remain opaque because their source signatures
     * are absent from the binary.
     */
    class CVisAreaManager : public IVisAreaManager {
      public:
        ~CVisAreaManager() override = default;

        static CVisAreaManager *GetInstance();

        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0; // +0x60

      private:
        std::byte m_opaque0008[0x218];
    };

    static_assert(sizeof(CVisAreaManager) == 0x220,
                  "CVisAreaManager size mismatch");
} // namespace SDK
