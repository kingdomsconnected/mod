#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * MSVC unordered hash-container storage used by save-extension managers.
     *
     * The node types are template-specific and intentionally kept opaque;
     * the constructor nevertheless proves the complete native container ABI.
     */
    struct SaveExtensionHashLayout {
        float m_maxLoadFactor;
        std::byte m_padding0004[0x4];
        void *m_pListHead;
        std::uint64_t m_count;
        void **m_pBucketsBegin;
        void **m_pBucketsEnd;
        void **m_pBucketsCapacity;
        std::uint64_t m_bucketMask;
        std::uint64_t m_bucketCount;

        std::size_t GetCount() const {
            return static_cast<std::size_t>(m_count);
        }
    };

    static_assert(sizeof(SaveExtensionHashLayout) == 0x40,
                  "SaveExtensionHashLayout size mismatch");

    /**
     * Tracks entity extensions by their persistent identity for save/load.
     *
     * The C_ItemSlot specialization constructor at 0x180BEA068 and its
     * sized deleting destructor prove this exact 0x98-byte layout. Its five
     * stripped virtual operations remain opaque.
     */
    template <typename T> class C_SaveExtensionManager {
      public:
        virtual ~C_SaveExtensionManager() = default;
        virtual void unk_fn0001(T *) = 0;
        virtual void unk_fn0002(T *) = 0;
        virtual void unk_fn0003(void *) = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005(T *) = 0;

        const SaveExtensionHashLayout &GetPrimaryEntries() const {
            return m_primaryEntries;
        }
        const SaveExtensionHashLayout &GetSecondaryEntries() const {
            return m_secondaryEntries;
        }

      private:
        const void *m_pDispatchTable; // 0x08
        std::uint16_t m_dispatchState0010;
        std::uint16_t m_dispatchState0012;
        std::byte m_padding0014[0x4];
        SaveExtensionHashLayout m_primaryEntries;    // 0x18
        SaveExtensionHashLayout m_secondaryEntries; // 0x58
    };

    static_assert(sizeof(C_SaveExtensionManager<void>) == 0x98,
                  "C_SaveExtensionManager size mismatch");
} // namespace SDK::wh::entitymodule
