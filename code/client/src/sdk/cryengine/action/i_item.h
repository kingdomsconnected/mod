#pragma once

#include "../../types.h"
#include "../../wh/entitymodule/c_item.h"
#include "../system/cry_name.h"
#include "i_actor.h"

#include <functional>

namespace SDK {

    /**
     * CryAction IItem interface implemented by
     * wh::entitymodule::C_PickableItem.
     *
     * C_PickableItem's primary table is at 0x183EC85E0. Slots 0-28 are the
     * IGameObjectExtension prefix. Slots 29-44 retain the established item
     * prefix; slot 45 is the KCD2 physics-profile request. The remaining
     * entries stay opaque. Its separate table at 0x183EC85A8 belongs to
     * IGameObjectProfileManager and is not part of this interface.
     */
    class IItem: public IGameObjectExtension {
      public:
        enum eItemBackAttachment {
            eIBA_Primary = 0,
            eIBA_Secondary,
            eIBA_Unknown,
        };

        enum eItemHand {
            eIH_Right = 0,
            eIH_Left,
            eIH_Last,
        };

        virtual ~IItem()                                                                                   = default;
        virtual const char *GetType() const                                                                = 0; // +0x0E8
        virtual EntityId GetOwnerId() const                                                                = 0;
        virtual void SetOwnerId(EntityId ownerId)                                                          = 0;
        virtual void EnableUpdate(bool enable, int slot = -1)                                              = 0;
        virtual void RequireUpdate(int slot)                                                               = 0;
        virtual void ForcePendingActions(std::uint8_t blockedActions = 0)                                  = 0;
        virtual void OnAction(EntityId actorId, const CCryName &actionId, int activationMode, float value) = 0;
        virtual void OnParentSelect(bool select)                                                           = 0;
        virtual void OnAttach(bool attach)                                                                 = 0;
        virtual void OnPickedUp(EntityId actorId, bool destroyed)                                          = 0;
        virtual void OnHit(float damage, int hitType)                                                      = 0;
        virtual void Select(bool select)                                                                   = 0;
        virtual bool IsSelected() const                                                                    = 0;
        virtual bool CanSelect() const                                                                     = 0;
        virtual bool CanDeselect() const                                                                   = 0;
        virtual void RemoveOwnerAttachedAccessories()                                                      = 0;
        /**
         * Requests a native physics transition, applied by the item's update.
         * C_PickableItem (0x181F1BF20) queues the profile and owns the callback;
         * its update (0x182A8B978) applies it before invoking the callback.
         * Profiles: 0 = none, 1 = rigid, 2 = static, 4 = static that only
         * rays hit (0x180A5619C).
         * std::function is required by this native ABI, including its by-value
         * ownership; an empty callback needs no game or mod allocation.
         */
        virtual void RequestPhysicsProfile(int profile, std::function<void(IItem &)> onApplied) = 0; // +0x168
        virtual void unk_fn0046()                                                                          = 0;
        virtual void unk_fn0047()                                                                          = 0;
        virtual void unk_fn0048()                                                                          = 0;
        virtual void unk_fn0049()                                                                          = 0;
        virtual void unk_fn0050()                                                                          = 0;
        virtual void unk_fn0051()                                                                          = 0;
        virtual void unk_fn0052()                                                                          = 0;
        virtual void unk_fn0053()                                                                          = 0;
        virtual void unk_fn0054()                                                                          = 0;
        virtual void unk_fn0055()                                                                          = 0;
        virtual void unk_fn0056()                                                                          = 0;
        virtual void unk_fn0057()                                                                          = 0;
        virtual void unk_fn0058()                                                                          = 0;
        virtual void unk_fn0059()                                                                          = 0;
        virtual void unk_fn0060()                                                                          = 0;
        virtual void unk_fn0061()                                                                          = 0;
        virtual void unk_fn0062()                                                                          = 0;
        virtual void unk_fn0063()                                                                          = 0;
        virtual void unk_fn0064()                                                                          = 0;
        virtual void unk_fn0065()                                                                          = 0;
        virtual void unk_fn0066()                                                                          = 0;
        virtual void unk_fn0067()                                                                          = 0;
        virtual void unk_fn0068()                                                                          = 0;
        virtual void unk_fn0069()                                                                          = 0;
        virtual void unk_fn0070()                                                                          = 0;
        virtual void unk_fn0071()                                                                          = 0;
        virtual void unk_fn0072()                                                                          = 0;
        virtual void unk_fn0073()                                                                          = 0;
        virtual void unk_fn0074()                                                                          = 0;
        virtual void unk_fn0075()                                                                          = 0;
        virtual void unk_fn0076()                                                                          = 0;
        virtual void unk_fn0077()                                                                          = 0;
        virtual void unk_fn0078()                                                                          = 0;
        virtual void unk_fn0079()                                                                          = 0;
        virtual void unk_fn0080()                                                                          = 0;
        virtual void unk_fn0081()                                                                          = 0;
        virtual void unk_fn0082()                                                                          = 0;
        virtual void unk_fn0083()                                                                          = 0;
        virtual void unk_fn0084()                                                                          = 0;
        virtual void unk_fn0085()                                                                          = 0;
        virtual void unk_fn0086()                                                                          = 0;
        virtual void unk_fn0087()                                                                          = 0;
        virtual void unk_fn0088()                                                                          = 0;
        virtual void unk_fn0089()                                                                          = 0;
        virtual void unk_fn0090()                                                                          = 0;
        virtual void unk_fn0091()                                                                          = 0;
        virtual void unk_fn0092()                                                                          = 0;
        virtual void unk_fn0093()                                                                          = 0;
        virtual void unk_fn0094()                                                                          = 0;
        virtual void unk_fn0095()                                                                          = 0;
        virtual void unk_fn0096()                                                                          = 0;
        virtual void unk_fn0097()                                                                          = 0;
        virtual void unk_fn0098()                                                                          = 0;
        virtual void unk_fn0099()                                                                          = 0;
        virtual void unk_fn0100()                                                                          = 0;
        virtual void unk_fn0101()                                                                          = 0;
        virtual void unk_fn0102()                                                                          = 0;
        virtual void unk_fn0103()                                                                          = 0;
        virtual void unk_fn0104()                                                                          = 0;
        virtual void unk_fn0105()                                                                          = 0;
        virtual void unk_fn0106()                                                                          = 0;
        virtual void unk_fn0107()                                                                          = 0;
        virtual void unk_fn0108()                                                                          = 0;
        virtual void unk_fn0109()                                                                          = 0;
        virtual void unk_fn0110()                                                                          = 0;
        virtual void unk_fn0111()                                                                          = 0;
        virtual void unk_fn0112()                                                                          = 0;
        virtual void unk_fn0113()                                                                          = 0;
        virtual void unk_fn0114()                                                                          = 0;
        virtual void unk_fn0115()                                                                          = 0;
        virtual void unk_fn0116()                                                                          = 0;
        virtual void unk_fn0117()                                                                          = 0;
        virtual void unk_fn0118()                                                                          = 0;
        virtual void unk_fn0119()                                                                          = 0;
        virtual void unk_fn0120()                                                                          = 0;
        virtual void unk_fn0121()                                                                          = 0;
        virtual void unk_fn0122()                                                                          = 0;
        virtual void unk_fn0123()                                                                          = 0;
        virtual void unk_fn0124()                                                                          = 0;
        virtual void unk_fn0125()                                                                          = 0;
        virtual void unk_fn0126()                                                                          = 0;
        virtual void unk_fn0127()                                                                          = 0;
        // GetUIName (0x182ABDBB4) calls +0x400, returning the RPG item.
        virtual SDK::wh::entitymodule::C_Item *GetRpgItem()                                                = 0;
        virtual void unk_fn0129()                                                                          = 0; // +0x408
    };

    static_assert(sizeof(IItem) == 0x40, "IItem size mismatch");
} // namespace SDK
