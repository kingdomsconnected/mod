#pragma once

#include "i_attachment_object.h"
#include "../../types.h"
#include "../3dengine/i_particle_emitter.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete CryAnimation effect attachment payload.
     *
     * RTTI identifies this global-namespace type as a struct and proves the
     * hierarchy CEffectAttachment -> IAttachmentObject. Its table at
     * 0x183A78EF0 implements exactly the 22 inherited entries and adds no
     * operations. Sized deletion proves the complete 0x38-byte layout.
     */
    struct CEffectAttachment : IAttachmentObject {
        ~CEffectAttachment() override = default;

      private:
        IParticleEmitter *m_pEmitter; // 0x08, _smart_ptr<IParticleEmitter>
        void *m_pEffect;              // 0x10, _smart_ptr<IParticleEffect>, assigned in the constructors at 0x180B00A4C / 0x18106999C
        QuatTS m_loc;                 // 0x18, ParticleLoc built from the constructor's offset/direction/scale

        static void AssertLayout() {
            static_assert(offsetof(CEffectAttachment, m_pEmitter) == 0x08, "CEffectAttachment::m_pEmitter offset mismatch");
            static_assert(offsetof(CEffectAttachment, m_pEffect) == 0x10, "CEffectAttachment::m_pEffect offset mismatch");
            static_assert(offsetof(CEffectAttachment, m_loc) == 0x18, "CEffectAttachment::m_loc offset mismatch");
        }
    };

    static_assert(sizeof(CEffectAttachment) == 0x38,
                  "CEffectAttachment size mismatch");
} // namespace SDK
