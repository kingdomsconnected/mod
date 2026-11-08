#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK::wh {
    /** Native tree header used to index one streamable entity group. */
    struct StreamableEntityClassTreeLayout {
        void *m_pHead;
        std::size_t m_size;
    };

    /** Asynchronous update job embedded in C_StreamableEntityClass. */
    struct StreamableEntityUpdateJobLayout {
        void *m_pJobState; // 0x00, JobManager::SJobState::SJobStateImpl
        std::byte m_unset0008[0x40]; // 0x08, neither the ctor (0x180CB346C) nor the job-state factory 0x18078BDEC writes it
        void *m_pUpdatePayload; // 0x48
        std::uint32_t m_completionState; // 0x50, atomically reset
        std::byte m_padding0054[0x4];
    };

    struct StreamableEntityPointerVectorLayout {
        void **m_pBegin;
        void **m_pEnd;
        void **m_pCapacity;

        std::size_t GetCount() const {
            return m_pBegin != nullptr
                       ? static_cast<std::size_t>(m_pEnd - m_pBegin)
                       : 0;
        }
    };

    static_assert(sizeof(StreamableEntityClassTreeLayout) == 0x10,
                  "StreamableEntityClassTreeLayout size mismatch");
    static_assert(sizeof(StreamableEntityUpdateJobLayout) == 0x58,
                  "StreamableEntityUpdateJobLayout size mismatch");
    static_assert(sizeof(StreamableEntityPointerVectorLayout) == 0x18,
                  "StreamableEntityPointerVectorLayout size mismatch");

    /**
     * Abstract per-class controller for asynchronously streamed entities.
     *
     * RTTI identifies the exact source name wh::C_StreamableEntityClass. Its
     * constructor at 0x180CB346C initializes the embedded Cry job, class-key
     * tree, group-name CryString payload, and group limit through +0x80. The
     * base table contains twelve slots; stripped contracts remain opaque.
     */
    class C_StreamableEntityClass {
      public:
        virtual ~C_StreamableEntityClass() = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() = 0;

        const char *GetGroupName() const { return m_pGroupName; }
        std::uint32_t GetGroupLimitRaw() const { return m_groupLimit; }
        const StreamableEntityClassTreeLayout &GetClassKeys() const {
            return m_classKeys;
        }
        const StreamableEntityUpdateJobLayout &GetUpdateJob() const {
            return m_updateJob;
        }

      private:
        void *m_pUpdateResult; // 0x08, S_UpdateResult *
        StreamableEntityUpdateJobLayout m_updateJob; // 0x10
        StreamableEntityClassTreeLayout m_classKeys; // 0x68
        const char *m_pGroupName; // 0x78, CryStringT<char> payload
        std::uint32_t m_groupLimit; // 0x80
        std::byte m_padding0084[0x4];
    };

    static_assert(sizeof(C_StreamableEntityClass) == 0x88,
                  "C_StreamableEntityClass size mismatch");

    /**
     * Concrete volume-backed streaming controller.
     *
     * The four manager-owned instances are allocated as exactly 0xA8 bytes
     * and use the complete table at 0x183A8EF68 without appending slots.
     */
    class C_VolumeStreamableEntityClass : public C_StreamableEntityClass {
      public:
        ~C_VolumeStreamableEntityClass() override = default;

        const StreamableEntityPointerVectorLayout &GetTrackedEntries() const {
            return m_trackedEntries;
        }

      private:
        StreamableEntityPointerVectorLayout m_trackedEntries; // 0x88
        std::uint32_t m_state00A0;
        std::byte m_padding00A4[0x4];
    };

    static_assert(sizeof(C_VolumeStreamableEntityClass) == 0xA8,
                  "C_VolumeStreamableEntityClass size mismatch");
} // namespace SDK::wh
