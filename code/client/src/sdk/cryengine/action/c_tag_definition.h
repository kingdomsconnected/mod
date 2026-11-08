#pragma once

#include "../../patterns.h"
#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Mannequin's tag dictionary for one controller definition.
     *
     * A tag group's members are packed as a value inside the group's bits
     * rather than one bit each, so writing a tag is the dictionary's own job
     * (`SetTag`, the native `CTagDefinition::Set` at 0x1804AC88C). Reading one
     * back is handled by `IsSet`, using the same layout:
     *
     * - `+0x08` the tags, a CryEngine `DynArray<STag>` whose element count
     *   sits in the int just before the first element (`FindCRC` compares
     *   against `[tags - 4]`);
     * - `+0x18` and `+0x20` the tag and group masks of `STagDefData`, two
     *   bytes each: which state byte, and which bits of it.
     *
     * The size is not asserted: the mod never constructs one, and the tail
     * after the group masks is not modeled.
     */
    class CTagDefinition {
      public:
        /** No tag set, which is what an empty group answers. */
        static constexpr std::uint32_t kInvalidId = 0xFFFFFFFFU;

        std::uint32_t FindCRC(std::uint32_t crc) const {
            return hook::this_call<std::uint32_t>(gPatterns.CTagDefinition__FindCRC, this, crc);
        }
        void SetTag(std::uint8_t *state, std::uint32_t tag, bool value) const {
            hook::this_call<void>(gPatterns.CTagDefinition__Set, this, state, tag, value);
        }

        /** The tag currently set in `groupId`, or kInvalidId when empty. */
        std::uint32_t GetGroupTag(const std::uint8_t *state, std::uint32_t groupId) const;

        std::uint32_t GetTagCount() const {
            return m_pTags != nullptr ? static_cast<std::uint32_t>(reinterpret_cast<const std::int32_t *>(m_pTags)[-1]) : 0;
        }

        /** The CRC-32 of the tag's lowercase name, which is what `FindCRC` takes back. */
        std::uint32_t GetTagCRC(std::uint32_t tag) const {
            return tag < GetTagCount() ? m_pTags[tag].m_nameCrc : 0;
        }

        /** The tag's name, as the database spells it, or null when there is no such tag. */
        const char *GetTagName(std::uint32_t tag) const {
            return tag < GetTagCount() ? m_pTags[tag].m_name : nullptr;
        }

        /** Whether `tag` is set in `state`: `STagStateBase::IsSet` for a lone tag, its group overload for a grouped one. */
        bool IsSet(const std::uint8_t *state, std::uint32_t tag) const {
            if (tag >= GetTagCount()) {
                return false;
            }
            const STagMask &tagMask = m_pTagMasks[tag];
            if (tagMask.mask == 0) {
                return false;
            }
            const std::int32_t group = m_pTags[tag].m_groupId;
            if (group < 0) {
                return (state[tagMask.byte] & tagMask.mask) == tagMask.mask;
            }
            const STagMask &groupMask = m_pGroupMasks[group];
            return (state[groupMask.byte] & groupMask.mask) == tagMask.mask;
        }

      private:
        /** CryEngine's `CTagDefinition::STag`. */
        struct STag {
            std::uint32_t m_priority;     // 0x00
            std::int32_t m_groupId;       // 0x04, -1 for a tag in no group
            const void *m_pTagDefinition; // 0x08
            std::uint32_t m_nameCrc;      // 0x10, the head of `STagRef`
            std::byte m_opaque0014[0x4];  // 0x14
            const char *m_name;           // 0x18, STagRef::name, a CryStringT<char> payload (STORE_TAG_STRINGS)
        };

        /** CryEngine's `STagMask`. */
        struct STagMask {
            std::uint8_t byte;
            std::uint8_t mask;
        };

        const char *m_filename;        // 0x00, CryStringT<char> payload
        const STag *m_pTags;           // 0x08
        const void *m_pTagGroups;      // 0x10, DynArray<STagGroup> data
        const STagMask *m_pTagMasks;   // 0x18
        const STagMask *m_pGroupMasks; // 0x20

        static void AssertLayout() {
            static_assert(sizeof(STag) == 0x20, "CTagDefinition::STag size mismatch");
            static_assert(offsetof(STag, m_groupId) == 0x04, "CTagDefinition::STag::m_groupId offset mismatch");
            static_assert(offsetof(STag, m_nameCrc) == 0x10, "CTagDefinition::STag::m_nameCrc offset mismatch");
            static_assert(sizeof(STagMask) == 0x2, "CTagDefinition::STagMask size mismatch");
            static_assert(offsetof(STag, m_name) == 0x18, "CTagDefinition::STag::m_name offset mismatch");
            static_assert(offsetof(CTagDefinition, m_pTagGroups) == 0x10, "CTagDefinition::m_pTagGroups offset mismatch");
            static_assert(offsetof(CTagDefinition, m_pTags) == 0x08, "CTagDefinition::m_pTags offset mismatch");
            static_assert(offsetof(CTagDefinition, m_pTagMasks) == 0x18, "CTagDefinition::m_pTagMasks offset mismatch");
            static_assert(offsetof(CTagDefinition, m_pGroupMasks) == 0x20, "CTagDefinition::m_pGroupMasks offset mismatch");
        }
    };

    /**
     * The controller definition an action controller's context runs against.
     *
     * Only `m_fragmentIDs` is proven: `CActionController::GetFragID`
     * (0x18140E294) loads the context's definition and hands `[def + 0x18]`
     * to `CTagDefinition::FindCRC`. The field order is filename, `m_tags`,
     * `m_fragmentIDs`, with KCD2's filename taking
     * 0x10 bytes. Never constructed by the mod, so its size is not asserted.
     */
    class SControllerDef {
      public:
        /** The dictionary fragment ids are indices into. */
        const CTagDefinition *GetFragmentIds() const {
            return m_pFragmentIds;
        }

      private:
        std::byte m_opaque0000[0x18];
        const CTagDefinition *m_pFragmentIds; // 0x18

        static void AssertLayout() {
            static_assert(offsetof(SControllerDef, m_pFragmentIds) == 0x18, "SControllerDef::m_pFragmentIds offset mismatch");
        }
    };

    /**
     * A tag state together with the dictionary that gives it meaning.
     *
     * The 20-byte state sits inline behind the dictionary pointer, which is
     * what the engine's group setter proves: it takes the address eight bytes
     * into itself and passes that size alongside.
     */
    class CTagState {
      public:
        static constexpr std::size_t kStateBytes = 20;

        std::uint32_t GetGroupTag(std::uint32_t groupId) const {
            return m_pDefinition != nullptr ? m_pDefinition->GetGroupTag(m_state, groupId) : CTagDefinition::kInvalidId;
        }

        /** The dictionary the state is spelled in: the controller's global tags. */
        const CTagDefinition *GetDefinition() const {
            return m_pDefinition;
        }

        /** Writes `tagId` as the only member of `groupId`, clearing it first. */
        void SetGroupTag(std::uint32_t groupId, std::uint32_t tagId);

        /** Sets or clears one tag of the state. */
        void SetTag(std::uint32_t tagId, bool value) {
            if (m_pDefinition != nullptr && tagId != CTagDefinition::kInvalidId) {
                m_pDefinition->SetTag(m_state, tagId, value);
            }
        }

        /**
         * The raw tag state, and the way to state one wholesale.
         *
         * Every fragment this body plays is selected from these twenty bytes,
         * so copying them carries a choice the mod has no name for -- a dog's
         * sit, say -- without having to know which group encodes it. The
         * dictionary is deliberately not copied: it belongs to the controller
         * definition, is the same for two bodies of the same class, and is
         * what gives the bytes meaning.
         */
        const std::uint8_t *GetRawState() const {
            return m_state;
        }

        void SetRawState(const std::uint8_t *state) {
            for (std::size_t i = 0; i < kStateBytes; ++i) {
                m_state[i] = state[i];
            }
        }

      private:
        const CTagDefinition *m_pDefinition; // 0x00
        std::uint8_t m_state[kStateBytes];   // 0x08

        static void AssertLayout() {
            static_assert(offsetof(CTagState, m_state) == 0x08, "CTagState::m_state offset mismatch");
        }
    };

    /**
     * The animation context an action controller runs against.
     *
     * IActionController::GetContext is vtable slot 23, folded onto a plain
     * `return *(this + 0x20)`. C_Actor's tag pass reaches the tag state eight
     * bytes into what it answers.
     */
    class SAnimationContext {
      public:
        CTagState &GetTagState() {
            return m_tagState;
        }

        const SControllerDef *GetControllerDef() const {
            return m_pControllerDef;
        }

      private:
        const SControllerDef *m_pControllerDef; // 0x00
        CTagState m_tagState;                   // 0x08

        static void AssertLayout() {
            static_assert(offsetof(SAnimationContext, m_tagState) == 0x08, "SAnimationContext::m_tagState offset mismatch");
        }
    };
} // namespace SDK
