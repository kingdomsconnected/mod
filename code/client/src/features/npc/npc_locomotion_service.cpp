#include "features/npc/npc_locomotion_service.h"

#include "common/word_packing.h"

#include <logging/logger.h>

#include <array>
#include <atomic>

namespace KCDC::Features::Npc {
    namespace {
        constexpr std::uint64_t PackOverride(SDK::EntityId entityId, std::uint32_t state, std::uint8_t moveSpeedTag, std::uint8_t moveDirTag, std::uint8_t stanceTag) {
            return (static_cast<std::uint64_t>(entityId) << 32) | (static_cast<std::uint64_t>(stanceTag) << 24) | (static_cast<std::uint64_t>(moveDirTag) << 16) | (static_cast<std::uint64_t>(moveSpeedTag) << 8) | (state & 0xFFU);
        }
        /** The raw tag bytes, three words wide, with the published flag on top. */
        void PackTagState(const std::uint8_t *tagState, std::uint64_t &word0, std::uint64_t &word1, std::uint64_t &word2) {
            word0 = 0;
            word1 = 0;
            word2 = 0;
            if (tagState == nullptr) {
                return;
            }
            for (std::size_t i = 0; i < 8; ++i) {
                word0 |= static_cast<std::uint64_t>(tagState[i]) << (i * 8);
                word1 |= static_cast<std::uint64_t>(tagState[i + 8]) << (i * 8);
            }
            for (std::size_t i = 0; i < 4; ++i) {
                word2 |= static_cast<std::uint64_t>(tagState[i + 16]) << (i * 8);
            }
            word2 |= 1ULL << 56;
        }

        void UnpackTagState(std::uint64_t word0, std::uint64_t word1, std::uint64_t word2, LocomotionOverride &result) {
            result.hasTagState = ((word2 >> 56) & 0xFFU) != 0;
            if (!result.hasTagState) {
                return;
            }
            for (std::size_t i = 0; i < 8; ++i) {
                result.tagState[i]     = static_cast<std::uint8_t>((word0 >> (i * 8)) & 0xFFU);
                result.tagState[i + 8] = static_cast<std::uint8_t>((word1 >> (i * 8)) & 0xFFU);
            }
            for (std::size_t i = 0; i < 4; ++i) {
                result.tagState[i + 16] = static_cast<std::uint8_t>((word2 >> (i * 8)) & 0xFFU);
            }
        }
    } // namespace

    LocomotionOverride NpcLocomotionService::GetLocomotionOverride(SDK::EntityId entityId) {
        LocomotionOverride result;
        if (entityId == 0 || _publishedCount.load(std::memory_order_relaxed) == 0) {
            return result;
        }
        const std::uint16_t slotPlusOne = _slotByEntityIndex[entityId & kEntityIndexMask].load(std::memory_order_acquire);
        if (slotPlusOne == 0) {
            return result;
        }
        LocomotionSlot &slot           = _locomotionOverrides[slotPlusOne - 1];
        const std::uint32_t generation = slot.generation.load(std::memory_order_acquire);
        if ((generation & 1U) != 0) {
            return result;
        }
        const std::uint64_t packed = slot.identity.load(std::memory_order_relaxed);
        if (static_cast<SDK::EntityId>(packed >> 32) != entityId) {
            return result;
        }
        result.state        = static_cast<std::uint32_t>(packed & 0xFFU);
        result.moveSpeedTag = static_cast<std::uint8_t>((packed >> 8) & 0xFFU);
        result.moveDirTag   = static_cast<std::uint8_t>((packed >> 16) & 0xFFU);
        result.stanceTag    = static_cast<std::uint8_t>((packed >> 24) & 0xFFU);
        KCDC::Common::UnpackFloats(slot.velocityXY.load(std::memory_order_relaxed), result.velocityX, result.velocityY);
        result.velocityZ = std::bit_cast<float>(slot.velocityZ.load(std::memory_order_relaxed));
        KCDC::Common::UnpackFloats(slot.rotationXY.load(std::memory_order_relaxed), result.rotationX, result.rotationY);
        KCDC::Common::UnpackFloats(slot.rotationZW.load(std::memory_order_relaxed), result.rotationZ, result.rotationW);
        UnpackTagState(slot.tagState0.load(std::memory_order_relaxed), slot.tagState1.load(std::memory_order_relaxed), slot.tagState2.load(std::memory_order_relaxed), result);

        // The fence is what makes the recheck a recheck: an acquire load only
        // stops later reads from moving up, and the data reads above are
        // earlier -- free to sink past it and read a torn slot the recheck
        // then blesses.
        std::atomic_thread_fence(std::memory_order_acquire);
        if (slot.generation.load(std::memory_order_relaxed) != generation) {
            return {};
        }
        return result;
    }

    void NpcLocomotionService::Reset() {
        for (auto &slot : _locomotionOverrides) {
            const std::uint64_t packed = slot.identity.load(std::memory_order_acquire);
            if (packed == 0) {
                continue;
            }
            _slotByEntityIndex[static_cast<SDK::EntityId>(packed >> 32) & kEntityIndexMask].store(0, std::memory_order_release);
            slot.generation.fetch_add(1, std::memory_order_acq_rel);
            slot.tagState2.store(0, std::memory_order_relaxed);
            slot.identity.store(0, std::memory_order_relaxed);
            slot.generation.fetch_add(1, std::memory_order_release);
        }
        _publishedCount.store(0, std::memory_order_relaxed);
        _capacityWarningIssued = false;
    }

    void NpcLocomotionService::PublishLocomotionOverride(SDK::EntityId entityId, std::uint32_t state, std::uint8_t moveSpeedTag, std::uint8_t moveDirTag, std::uint8_t stanceTag, const glm::vec3 &velocity, const glm::quat &rotation, const std::uint8_t *tagState) {
        if (entityId == 0) {
            return;
        }

        const std::uint64_t wanted     = PackOverride(entityId, state, moveSpeedTag, moveDirTag, stanceTag);
        const std::uint64_t velocityXY = KCDC::Common::PackFloats(velocity.x, velocity.y);
        const std::uint32_t velocityZ  = std::bit_cast<std::uint32_t>(velocity.z);
        const std::uint64_t rotationXY = KCDC::Common::PackFloats(rotation.x, rotation.y);
        const std::uint64_t rotationZW = KCDC::Common::PackFloats(rotation.z, rotation.w);
        std::uint64_t tagState0        = 0;
        std::uint64_t tagState1        = 0;
        std::uint64_t tagState2        = 0;
        PackTagState(tagState, tagState0, tagState1, tagState2);

        std::atomic<std::uint16_t> &index = _slotByEntityIndex[entityId & kEntityIndexMask];
        const std::uint16_t slotPlusOne   = index.load(std::memory_order_relaxed);
        if (slotPlusOne != 0) {
            LocomotionSlot &slot       = _locomotionOverrides[slotPlusOne - 1];
            const std::uint64_t packed = slot.identity.load(std::memory_order_relaxed);
            // The index names this entity's slot for as long as the entity
            // holds one; a different id under the same index is a reused
            // index whose old body was never withdrawn, and is replaced.
            if (static_cast<SDK::EntityId>(packed >> 32) != entityId) {
                index.store(0, std::memory_order_release);
                slot.generation.fetch_add(1, std::memory_order_acq_rel);
                slot.tagState2.store(0, std::memory_order_relaxed);
                slot.identity.store(0, std::memory_order_relaxed);
                slot.generation.fetch_add(1, std::memory_order_release);
                _publishedCount.fetch_sub(1, std::memory_order_relaxed);
            }
            else {
                slot.generation.fetch_add(1, std::memory_order_acq_rel);
                if (state == 0) {
                    index.store(0, std::memory_order_release);
                    slot.tagState2.store(0, std::memory_order_relaxed);
                    slot.identity.store(0, std::memory_order_relaxed);
                    slot.generation.fetch_add(1, std::memory_order_release);
                    _publishedCount.fetch_sub(1, std::memory_order_relaxed);
                    _capacityWarningIssued = false;
                    return;
                }
                slot.velocityXY.store(velocityXY, std::memory_order_relaxed);
                slot.velocityZ.store(velocityZ, std::memory_order_relaxed);
                slot.rotationXY.store(rotationXY, std::memory_order_relaxed);
                slot.rotationZW.store(rotationZW, std::memory_order_relaxed);
                slot.tagState0.store(tagState0, std::memory_order_relaxed);
                slot.tagState1.store(tagState1, std::memory_order_relaxed);
                slot.tagState2.store(tagState2, std::memory_order_relaxed);
                slot.identity.store(wanted, std::memory_order_relaxed);
                slot.generation.fetch_add(1, std::memory_order_release);
                return;
            }
        }
        if (state == 0) {
            return;
        }

        // A new body: the first free slot, found by a walk that only runs
        // when a body starts being overridden.
        LocomotionSlot *freeSlot = nullptr;
        for (auto &slot : _locomotionOverrides) {
            if (slot.identity.load(std::memory_order_relaxed) == 0) {
                freeSlot = &slot;
                break;
            }
        }

        // NPCs and dogs share this capacity. Report overflow once until a
        // slot is freed, without logging on every publication tick.
        if (freeSlot != nullptr) {
            freeSlot->generation.fetch_add(1, std::memory_order_acq_rel);
            freeSlot->velocityXY.store(velocityXY, std::memory_order_relaxed);
            freeSlot->velocityZ.store(velocityZ, std::memory_order_relaxed);
            freeSlot->rotationXY.store(rotationXY, std::memory_order_relaxed);
            freeSlot->rotationZW.store(rotationZW, std::memory_order_relaxed);
            freeSlot->tagState0.store(tagState0, std::memory_order_relaxed);
            freeSlot->tagState1.store(tagState1, std::memory_order_relaxed);
            freeSlot->tagState2.store(tagState2, std::memory_order_relaxed);
            freeSlot->identity.store(wanted, std::memory_order_relaxed);
            freeSlot->generation.fetch_add(1, std::memory_order_release);
            // Published after the slot is whole, so a reader that finds the
            // index finds the slot complete.
            index.store(static_cast<std::uint16_t>(freeSlot - _locomotionOverrides.data() + 1), std::memory_order_release);
            _publishedCount.fetch_add(1, std::memory_order_relaxed);
        }
        else if (!_capacityWarningIssued) {
            _capacityWarningIssued = true;
            Framework::Logging::GetLogger("Locomotion")->error("Locomotion override capacity ({}) exhausted; entity {} has no animation override", kMaxOverriddenBodies, entityId);
        }
    }

} // namespace KCDC::Features::Npc
