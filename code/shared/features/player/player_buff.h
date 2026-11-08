#pragma once

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace KCDC::Shared::RPC {
    /** A receiver must never size an allocation from a number a peer chose. */
    inline constexpr std::size_t kMaxReportedBuffs = 96;

    /** The engine's 16-byte buff `CryGUID` without a client-SDK dependency. */
    struct BuffId {
        std::uint64_t low  = 0;
        std::uint64_t high = 0;

        bool operator==(const BuffId &) const = default;

        bool IsValid() const {
            return low != 0 || high != 0;
        }

        bool Serialize(MafiaNet::BitStream *bs, bool write) {
            return bs->Serialize(write, low) && bs->Serialize(write, high);
        }
    };

    enum class BuffSource : std::uint8_t {
        Native = 0,
        Server = 1,
    };

    /** One live buff, named by the definition that attached rather than the one asked for. */
    struct ActiveBuff {
        BuffId buff;
        std::uint64_t handle = 0;
        BuffSource source    = BuffSource::Native;

        bool operator==(const ActiveBuff &) const = default;

        bool IsSane() const {
            return buff.IsValid() && handle != 0 && static_cast<std::uint8_t>(source) <= 1;
        }

        bool Serialize(MafiaNet::BitStream *bs, bool write) {
            std::uint8_t wireSource = static_cast<std::uint8_t>(source);
            const bool ok           = buff.Serialize(bs, write) && bs->Serialize(write, handle) && bs->Serialize(write, wireSource);
            if (!write) {
                source = static_cast<BuffSource>(wireSource);
            }
            return ok;
        }
    };

    /** An `ActiveBuff` and when this side first saw it. Never serialized: two machines' clocks are not comparable, so each times its own view. */
    struct ObservedBuff {
        ActiveBuff buff;
        std::uint64_t firstSeen = 0; // milliseconds, this machine's clock
    };

    // Orders carry no sequence number: they travel ReliableOrdered, and the one
    // outcome a resend could cover -- the engine refusing the attach -- is one
    // a resend does not change.
    struct ApplyBuff {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::ApplyBuff");

        BuffId buff;
        // Zero keeps the scripting API's next-available-body behavior. A
        // nonzero value restricts an effect to the body that earned it.
        std::uint32_t incarnation = 0;
        bool valid                = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = buff.Serialize(bs, write) && bs->Serialize(write, incarnation);
        }

        bool IsSane() const {
            return valid && buff.IsValid();
        }

        bool AppliesTo(std::uint32_t currentIncarnation) const {
            return IsSane() && (incarnation == 0 || incarnation == currentIncarnation);
        }
    };

    struct RetractBuff {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::RetractBuff");

        BuffId buff;

        // The exact instance when a report named one. Zero removes every
        // instance of the definition instead.
        std::uint64_t handle = 0;

        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = buff.Serialize(bs, write) && bs->Serialize(write, handle);
        }

        bool IsSane() const {
            return valid && buff.IsValid();
        }
    };

    struct ClearBuffsByTag {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::ClearBuffsByTag");

        std::int32_t aiTag = -1;
        bool valid         = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, aiTag);
        }

        bool IsSane() const {
            return valid && aiTag >= 0 && aiTag < 64;
        }
    };

    /** Bit N set means the server decides `buff_class_id` N. Empty by default. */
    struct BuffPolicy {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::BuffPolicy");

        std::uint32_t claimedClasses = 0;
        bool valid                   = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, claimedClasses);
        }

        bool IsSane() const {
            return valid;
        }
    };

    /** Owner to server: the claim gate turned a locally originated buff down. Without it a claim deletes the gameplay instead of moving it here. */
    struct BuffRefused {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::BuffRefused");

        BuffId buff;
        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = buff.Serialize(bs, write);
        }

        bool IsSane() const {
            return valid && buff.IsValid();
        }
    };

    /** Owner to server, on change: everything currently on this body. */
    struct BuffReport {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::BuffReport");

        std::uint32_t revision = 0;
        std::vector<ActiveBuff> active;
        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, revision) && SerializeActive(bs, write);
        }

        bool IsSane() const {
            if (!valid || revision == 0 || active.size() > kMaxReportedBuffs) {
                return false;
            }
            for (const auto &buff : active) {
                if (!buff.IsSane()) {
                    return false;
                }
            }
            return true;
        }

      private:
        bool SerializeActive(MafiaNet::BitStream *bs, bool write) {
            std::uint16_t count = write ? static_cast<std::uint16_t>(active.size()) : 0;
            if (write && active.size() > kMaxReportedBuffs) {
                return false;
            }
            if (!bs->Serialize(write, count)) {
                return false;
            }
            if (!write) {
                if (count > kMaxReportedBuffs) {
                    return false;
                }
                active.assign(count, ActiveBuff {});
            }
            for (std::uint16_t i = 0; i < count; ++i) {
                if (!active[i].Serialize(bs, write)) {
                    return false;
                }
            }
            return true;
        }
    };
} // namespace KCDC::Shared::RPC
