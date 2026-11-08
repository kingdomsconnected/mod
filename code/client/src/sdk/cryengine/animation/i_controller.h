#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Which channels an `IController` query wrote. The samplers test exactly
     * these bits on what `GetOPS` returns: position into the joint's local
     * translation, orientation into its local rotation, scale into its
     * uniform scale (`Command::SampleAddAnimFull::Execute`, 0x180591600).
     */
    enum EJointState : std::uint8_t {
        eJS_Position    = 0x01,
        eJS_Orientation = 0x02,
        eJS_Scale       = 0x04,
    };

    /**
     * One joint's animation track inside a clip: CryAnimation's `IController`
     * over a multithreaded reference target.
     *
     * Its own table at 0x183A790E8 is the base's four entries followed by the
     * pure queries. Slots 9 and 10 share one emitted body answering -1
     * (`GetO_numKey`/`GetP_numKey`). The four pure entries after them are
     * key counts and sizes, not told apart here. The in-place
     * controllers a DBA builds (`CControllerOptNonVirtual`, table
     * 0x183A506F0) are not refcounted: both reference slots are empty.
     *
     * The samplers only ever call `GetOPS` on what
     * `Command::GatherControllers` (0x1808B72B0) hands them.
     */
    class IController {
      public:
        virtual ~IController()        = default; // +0x00
        virtual void AddRef() const   = 0;       // +0x08
        virtual void Release() const  = 0;       // +0x10
        virtual bool Unique() const   = 0;       // +0x18

        /** Samples every channel at a key time; answers the EJointState bits written. */
        virtual std::uint8_t GetOPS(float key, Quat &rotation, Vec3 &position, Diag33 &scale) const = 0; // +0x20
        virtual std::uint8_t GetOP(float key, Quat &rotation, Vec3 &position) const                 = 0; // +0x28
        virtual std::uint8_t GetO(float key, Quat &rotation) const                                   = 0; // +0x30
        virtual std::uint8_t GetP(float key, Vec3 &position) const                                   = 0; // +0x38
        virtual std::uint8_t GetS(float key, Diag33 &scale) const                                    = 0; // +0x40
        virtual std::int32_t GetO_numKey() const                                                     = 0; // +0x48
        virtual std::int32_t GetP_numKey() const                                                     = 0; // +0x50
        virtual std::size_t unk_fn0011() const                                                       = 0; // +0x58
        virtual std::size_t unk_fn0012() const                                                       = 0; // +0x60
        virtual std::size_t unk_fn0013() const                                                       = 0; // +0x68
        virtual std::size_t unk_fn0014() const                                                       = 0; // +0x70

      protected:
        std::int32_t m_nRefCounter = 0;    // 0x08, the reference target's
        std::byte m_opaque000C[0x4] = {};  // 0x0C, the base's padding
        std::uint32_t m_nControllerId = 0; // 0x10, the joint-name CRC32 the clip keys this track by

      private:
        static void AssertLayout() {
            static_assert(offsetof(IController, m_nRefCounter) == 0x08, "IController::m_nRefCounter offset mismatch");
            static_assert(offsetof(IController, m_opaque000C) == 0x0C, "IController::m_opaque000C offset mismatch");
            static_assert(offsetof(IController, m_nControllerId) == 0x10, "IController::m_nControllerId offset mismatch");
        }
    };

    static_assert(sizeof(IController) == 0x18, "IController size mismatch");
} // namespace SDK
