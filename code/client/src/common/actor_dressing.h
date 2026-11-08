#pragma once

#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/s_item_class_guid.h"

#include <cstddef>
#include <function2/function2.hpp>
#include <string>
#include <string_view>

// Putting clothes on a replicated body.
//
// A KCD2 soul decides what its body wears, and keeps deciding: every time an
// armor item enters or leaves its inventory it arms a pending outfit, and the
// next outfit application (`ReconcileArmorToOutfit`) fills each slot its
// clothing preset covers with the best inventory item and empties the rest. So
// clothes equipped directly do not stay on -- the soul puts its own back.
//
// The body is therefore dressed through the soul's own input rather than its
// output. The list the wire carries becomes a clothing preset in the game's
// own database, appended through the table loader, and is pushed onto the
// soul's preset stack as the Lua `actor:EquipClothingPreset` binding does. The
// engine then makes the items, fills the slots in the order their rules need,
// deletes what the previous preset made, and re-applies the same outfit
// whenever it re-applies. Players, NPCs and horses all go through here, from
// the same wire format.
namespace KCDC::Common {
    /** Hex digits one item class takes in the packed wire string. */
    inline constexpr std::size_t kPackedItemClassWidth = 32;

    /** Appends one item class as the wire spells it: low then high, 16 hex digits each. */
    void AppendPackedItemClass(std::string &out, const SDK::wh::entitymodule::S_ItemClassGUID &itemClass);

    /** Reads the item class at `offset`; false when the text is malformed or short. */
    bool ReadPackedItemClass(const std::string &packed, std::size_t offset, SDK::wh::entitymodule::S_ItemClassGUID &out);

    /** How far a dressing attempt got. */
    struct DressResult {
        /**
         * False when the body was not ready to be dressed -- no soul, no
         * inventory runtime yet. The caller should keep its dirty flag and try
         * again on a later frame rather than treating the body as dressed.
         */
        bool ready = false;

        /**
         * False when the outfit could not be made into a preset the database
         * accepts. The body is left as it was; the reason is logged.
         */
        bool dressed = false;

        /** Whether this call changed the soul's outfit at all. */
        bool changed = false;

        /** A weapon change must wait for a hand action or ground transfer. */
        bool pendingWeapons = false;

        /** Armor pieces the outfit names. */
        std::size_t armorPieces = 0;
    };

    /** What differs between dressing one kind of body and another. */
    struct DressOptions {
        /**
         * Whether to make sure the body has the weapon/equipment listener a
         * person's equipment events expect. Asking for it constructs the
         * actor's shooting expansion, which an animal has no business with.
         */
        bool weaponListener = true;

        /**
         * What an empty list means. False: a bare body. True: nothing of ours
         * -- the pushed outfit comes off and the soul's inventory is reset to
         * what its own presets say it owns, which is what an NPC with nothing
         * specified wears.
         */
        bool emptyRestoresOwnOutfit = false;

        /** Players supply their complete weapon-slot state with the outfit. */
        bool reconcileWeaponSlots = false;

        /** A weapon currently owned by the caller's hand/drop transition. */
        fu2::function<bool(const SDK::wh::entitymodule::S_ItemClassGUID &) const> deferWeaponChange;
    };

    /**
     * Makes `actor` wear the item classes in `packedEquipment`.
     *
     * Armor goes through the soul's clothing preset as described above.
     * Everything else in the list -- the holstered weapons a player carries --
     * is still equipped directly, as a difference against what is worn; the
     * held-item slot is left alone, because what is in a body's hands is its
     * own state.
     */
    DressResult ApplyActorEquipment(SDK::wh::entitymodule::C_Actor *actor, const std::string &packedEquipment, const DressOptions &options = {});

    /** Hex digits one piece's wear takes in the packed wear string. */
    inline constexpr std::size_t kPackedWearWidth = 24;

    /**
     * How worn each piece `packedEquipment` names is on `actor`, in the same
     * order: dirt, the six blood zones and quality as two hex digits each,
     * then the health's float bits as eight. A piece the body does not wear
     * as armour is all zeros. Empty while the body has no equipment manager.
     */
    std::string CaptureEquipmentWear(SDK::wh::entitymodule::C_Actor *actor, const std::string &packedEquipment);

    /**
     * Puts the wear `CaptureEquipmentWear` read off the owner's body onto the
     * pieces of the same classes `actor` wears, through the game's own
     * setters: they redraw the clothing, and do nothing for a value already
     * there.
     */
    void ApplyEquipmentWear(SDK::wh::entitymodule::C_Actor *actor, const std::string &packedEquipment, const std::string &packedWear);

    /**
     * Puts on a clothing preset the game's own database has, by its GUID, the
     * way the Lua `actor:EquipClothingPreset` binding does
     * (`C_ScriptBindActor::EquipClothingPreset`, 0x1823DCEC8): pushed onto the
     * soul's preset stack, its items made and the outfit applied now. False
     * when the GUID does not parse or the soul's inventory is not up yet --
     * the caller tries again on a later frame.
     */
    bool EquipClothingPreset(SDK::wh::entitymodule::C_Actor *actor, std::string_view presetGuid);
} // namespace KCDC::Common
