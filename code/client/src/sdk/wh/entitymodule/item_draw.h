#pragma once

#include "../../patterns.h"

#include <utils/hooking/hooking.h>
#include "../framework/wuid.h"
#include "c_action_actor.h"
#include "c_actor_anims_item_handling_expansion.h"
#include "i_action_actor_spec.h"
#include "s_actor_anim_request.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace SDK::wh::entitymodule {
    class C_Actor;
    class C_Item;

    /**
     * Whether this item is drawn on that actor.
     *
     * Reads the actor's hand store at `C_Actor+0x990`, sub-object `+0x258` --
     * what `SetHandItem` writes -- so it answers "is it out" rather than "is
     * it equipped".
     *
     * The id goes over by address: the native dereferences RDX, and the
     * engine's own call site passes `lea rdx, [item+0x30]`.
     */
    inline bool IsItemDrawn(C_Actor *actor, const framework::WUID &itemId) {
        return hook::call<bool>(gPatterns.WH__IsItemDrawn, actor, &itemId);
    }

    /**
     * Fills a two-entry hand array from a pair of items.
     *
     * The engine's own filler: each item lands at the hand its weapon class
     * names, `items[1]` only when it is a weapon, the rest left invalid.
     */
    inline void BuildDrawHandPair(C_Item *items[2], std::uint64_t out[2]) {
        hook::call<void *>(gPatterns.WH__BuildDrawHandPair, items, out);
    }

    namespace detail {
        /** Builds one item-handling request and hands it to the action router. */
        inline S_ActionRequestResult QueueItemHandlingAction(C_ActionActor *actionActor, bool draw, C_Item *items[2]) {
            S_ActionRequestResult result {};
            result.status = 0xFF;

            auto *expansion = actionActor != nullptr ? actionActor->GetItemHandlingExpansion() : nullptr;
            auto *spec      = actionActor != nullptr ? actionActor->GetActionActorSpec() : nullptr;
            if (expansion == nullptr || spec == nullptr) {
                return result;
            }

            std::shared_ptr<S_ActionRequest> request;
            if (draw) {
                expansion->InvokeCreateDrawRequestRaw(&request);
            }
            else {
                expansion->InvokeCreateHolsterRequestRaw(&request);
            }
            if (!request) {
                return result;
            }

            if (draw) {
                BuildDrawHandPair(items, static_cast<S_ActorAnimDrawRequest *>(request.get())->GetHandItems());
            }
            return spec->RequestAction(std::move(request));
        }
    } // namespace detail

    /**
     * Queues the game's own animated weapon draw on any actor.
     *
     * The animation is the action, not the attachment: it occupies the
     * actor's `drawWeapons` scope and the items reach the hands from its own
     * `Attach` events. Attaching directly is only the end of that.
     *
     * The request is built here rather than through the engine's
     * `BuildDrawItemAction` because that builder always marks the optional
     * QAM slot present, and the action's entry step then dereferences
     * `C_InventorySoul::m_pPlayerInventorySoul` unchecked -- null on anything
     * that is not the player. The constructor leaves the optional clear.
     */
    inline S_ActionRequestResult DrawWeaponSetAnimated(C_ActionActor *actionActor, C_Item *items[2]) {
        return detail::QueueItemHandlingAction(actionActor, true, items);
    }

    /** The holster twin; it carries no pair and puts away what is out. */
    inline S_ActionRequestResult HolsterWeaponSetAnimated(C_ActionActor *actionActor) {
        return detail::QueueItemHandlingAction(actionActor, false, nullptr);
    }
} // namespace SDK::wh::entitymodule
