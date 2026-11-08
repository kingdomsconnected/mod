#pragma once

#include "../../patterns.h"
#include "../entitysystem/i_entity.h"
#include "c_tag_definition.h"
#include "i_mannequin_action.h"
#include "i_procedural_context.h"

#include <utils/hooking/hooking.h>

#include <cstdint>

namespace SDK {
    /**
     * Mannequin's per-actor action controller.
     *
     * CActionController's table at 0x1840519E8 is the concrete side. Only the
     * slots this mod has proven against the game's own call sites are named;
     * the rest stay opaque so the indices below stay honest.
     *
     * CAnimatedCharacter's scope-context refresh proves slots 5 and 6: it
     * resolves the `Char3P`, `Char1P` and `Audio` context ids through the
     * controller definition and then binds or clears each one. Slot 3 is
     * named in the table itself (`CActionController::Flush`, 0x1804B011C),
     * in CryEngine's declared order after OnEvent, OnAnimationEvent and
     * Reset. C_Actor's
     * per-frame tag pass proves slot 23, reaching the tag state eight bytes
     * into what it answers. The wh action layer's fragment starter proves
     * slot 19, queuing every C_CallbackAction it builds through it.
     */
    struct IActionController {
        std::uint32_t FindFragment(std::uint32_t crc) const {
            return hook::this_call<std::uint32_t>(gPatterns.CActionController__GetFragID, this, crc);
        }
        const CTagDefinition *GetFragmentTagDefinition(std::uint32_t fragment) const {
            return hook::this_call<const CTagDefinition *>(gPatterns.CActionController__GetTagDefinition, this, fragment);
        }

        virtual ~IActionController() = default; // +0x000
        virtual void unk_fn0001()    = 0;
        virtual void unk_fn0002()    = 0;

        /**
         * Ends every action on every scope and empties the queue, leaving each
         * queued action's status at none. Never from inside the controller's
         * own update. An actor whose tag pass runs builds its locomotion
         * action again from that none (`C_Actor::EnsureLocomotionAction`), so
         * a flushed body is handed back to its own locomotion.
         */
        virtual void Flush() = 0; // +0x018, slot 3

        virtual void unk_fn0004()    = 0;

        /** Binds a character and its database to one scope context. */
        virtual void SetScopeContext(std::uint32_t scopeContextId, IEntity *entity, void *character, void *animationDatabase) = 0; // +0x028

        /** Unbinds a scope context, leaving its scopes with nothing to play. */
        virtual void ClearScopeContext(std::uint32_t scopeContextId, bool flush) = 0; // +0x030

        virtual void unk_fn0007() = 0;

        /**
         * Whether a scope can play (0x1836B86C0, a bit test of the active
         * mask at +0x40). A scope is active only while its scope context has
         * a character and a database bound, so for a context such as
         * `SlaveObject` this is whether anything is bound to it. An action
         * installs only on the scopes active at that moment and is not
         * extended to one bound later. No bounds check: the id must be real.
         */
        virtual bool IsScopeActive(std::uint32_t scopeId) const = 0; // +0x040, slot 8

        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;

        /** The scope of that name in this controller's definition, or `kInvalidScope` (0x1819BD694). */
        virtual std::uint32_t GetScopeID(const char *name) const = 0; // +0x070, slot 14

        static constexpr std::uint32_t kInvalidScope = 0xFFFFFFFFU;
        virtual void unk_fn0015() = 0;
        virtual void unk_fn0016() = 0;
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;

        /**
         * Queues one Mannequin action. The enqueue takes its own reference,
         * so an action constructed at refcount zero belongs to the controller
         * from here on. `queueTime` is how long it may wait to (re)install
         * before it expires Finished; -1 never expires, which is how C_Player's setup queues its
         * movement action (0x18113A1FC).
         */
        virtual void Queue(IAction &action, float queueTime) = 0; // +0x098, slot 19

        virtual void unk_fn0020() = 0;

        /** Advances scope animation and resolves queued-action lifecycle edges. */
        virtual void Update(float frameTime) = 0; // +0x0A8, slot 21

        virtual void unk_fn0022() = 0;

        /**
         * The context this controller runs against, which owns the tag state
         * every fragment is selected through.
         */
        virtual SAnimationContext *GetContext() = 0; // +0x0B8, slot 23

        virtual void unk_fn0024() = 0;
        virtual void unk_fn0025() = 0;
        virtual void unk_fn0026() = 0;
        virtual void unk_fn0027() = 0;
        virtual void unk_fn0028() = 0;
        virtual void unk_fn0029() = 0;
        virtual void unk_fn0030() = 0;
        virtual void unk_fn0031() = 0;
        virtual void unk_fn0032() = 0;
        virtual void unk_fn0033() = 0;
        virtual void unk_fn0034() = 0; // RegisterListener
        virtual void unk_fn0035() = 0; // UnregisterListener

        /** Slot 36 (0x180947D00): slot 38, then `CreateProceduralContext` (slot 40) on a miss. */
        virtual IProceduralContext *FindOrCreateProceduralContext(const char *name) = 0; // +0x120

        virtual void unk_fn0037() = 0; // the lookup by lowercase-name CRC that slot 38 wraps

        /**
         * Slot 38 (0x180947D3C): the context of that registered name, or null
         * when this controller never created one. Find only. KCD2 keys
         * contexts by name where stock CryEngine keys them by class id;
         * `C_Actor::SetupProceduralAnimationContexts` (0x180684928) asks for
         * "ProceduralContextAim", "ProceduralContextLook" and
         * "ProceduralContextHold". Slot 39 is the same function, the const
         * overload folded onto it.
         */
        virtual IProceduralContext *FindProceduralContext(const char *name) = 0; // +0x130

        /**
         * The CRC-32 of a fragment id's lowercase name, or 0 when this
         * controller has no such fragment: the inverse of `FindFragment`, and
         * the form a fragment can leave this machine in, since the id is an
         * index into this controller's own definition.
         */
        std::uint32_t GetFragmentCRC(std::uint32_t fragment) {
            SAnimationContext *context        = GetContext();
            const SControllerDef *definition  = context != nullptr ? context->GetControllerDef() : nullptr;
            const CTagDefinition *fragmentIds = definition != nullptr ? definition->GetFragmentIds() : nullptr;
            return fragmentIds != nullptr ? fragmentIds->GetTagCRC(fragment) : 0;
        }
    };
} // namespace SDK
