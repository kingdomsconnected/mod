#pragma once

#include "inventory.h"

#include "shared/game/crafting_catalog.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace KCDC::Features::Inventory {
    /** What a native consumption spent a unit on; decides which classes qualify. */
    enum class InventoryConsumption : std::uint8_t {
        Use,  // eating, drinking, potions and ointments
        Shot, // one fired arrow, bolt or ball
    };

    /** One line of what a victim's pockets offer a thief: a class and how many of it. */
    struct PocketLine {
        std::string itemClass;
        std::uint32_t amount = 0;

        bool operator==(const PocketLine &) const = default;
    };

    /**
     * The owner's native gameplay, checked against the items it touched.
     *
     * The game acts first -- a meal removes the bread, a shot spends the
     * arrow, gear is put on -- and the owner names the rows it touched. These
     * rules never grant: a consumption only spends consumable units of the
     * right kind, an equipment set only records rows the player holds, and a
     * theft only moves unworn units the thief was offered.
     */
    class InventoryGameplay final {
      public:
        static constexpr std::size_t kMaximumConsumedUnits = 16;
        static constexpr std::uint32_t kMaximumUseAmount   = 10;
        static constexpr std::uint32_t kMaximumShotAmount  = 16;
        static constexpr std::size_t kMaximumEquippedRows  = 48;
        /**
         * The soonest a theft can settle after the offer went out. The loot
         * screen opens only once the native charge fills
         * (C_Pickpocketing::UpdateCharge 0x182EB3B34, +0x84 against +0x90),
         * which takes several seconds live; a result sooner than this did not
         * come from the charge.
         */
        static constexpr std::uint64_t kMinimumTheftMs = 2000;
        /** How far apart thief and victim may be when a theft settles; the same slack as the request's. */
        static constexpr float kMaximumTheftReach = 6.0F;

        static bool IsEquippableClass(const std::string &itemClass);
        /** Worn on the body rather than held: armour, helmets and hoods. */
        static bool IsClothingClass(const std::string &itemClass);
        static bool IsConsumableClass(const std::string &itemClass, InventoryConsumption kind);
        /**
         * What scripts call a consumed class: "shot", "ointment", "potion" or
         * "food". Food type 1 in the native tables identifies potions.
         */
        static const char *UseKind(const std::string &itemClass);

        /** `{ request, kind: "use" | "shot", units: [{ id, amount }] }` from the owner; a `revision` it sends is not read. */
        static bool ReadConsume(const nlohmann::json &message, std::string &request, InventoryConsumption &kind, std::vector<InventoryUnit> &units);
        /**
         * `{ request, kind: "repair", equipment: [{ id, before, after, equipped }],
         * kits: [{ id, before, after, spent }] }` from the owner. Each part is
         * one distinct unit; a kit reused across the selection appears once.
         * The older single-equipment
         * object is also accepted.
         */
        static bool ReadRepair(const nlohmann::json &message, std::string &request, std::vector<InventoryRepairPart> &kits, std::vector<InventoryRepairPart> &equipment);
        /** `{ request, kind: "sharpen", equipment: { id, before, after }, equipped }` from the owner. */
        static bool ReadSharpen(const nlohmann::json &message, std::string &request, InventoryRepairPart &weapon);
        /** `{ request, kind: "dry" | "smoke" | "cook", units: [{ id, amount }] }` from the owner. */
        static bool ReadPreparation(const nlohmann::json &message, std::string &request, KCDC::Shared::Game::PreparationKind &kind, std::vector<InventoryUnit> &units);
        /**
         * What a drying rack, smokehouse or fire makes of `units`, one product
         * per unit in order: the class its row's class dries, smokes or cooks
         * into. Empty when a unit names no row, or a class that does not
         * convert that way.
         */
        static std::vector<InventoryCredit> Prepared(const Inventory &inventory, KCDC::Shared::Game::PreparationKind kind, const std::vector<InventoryUnit> &units);
        /**
         * Spends what one native use or shot took, all or nothing; `spent`
         * receives each unit's row as it was. Any revision is accepted: the
         * exact units bound what can leave. A shot spends only rows in
         * `loaded`, the ammunition that has been in the quiver: the game loads
         * a bow from that slot alone (RefreshShootingAmmunition 0x180940D30).
         */
        static InventoryResult Consume(Inventory &inventory, InventoryConsumption kind, const std::set<std::string> &loaded, const std::vector<InventoryUnit> &units, std::vector<InventoryRow> *spent = nullptr);

        /** `{ revision, items: { rowId: wornUnits } }`, at most 48 rows. */
        static bool ReadEquipment(const nlohmann::json &message, std::uint64_t &revision, std::map<std::string, std::uint32_t> &items, std::uint64_t equipmentRestore = 0);
        /**
         * What a body can wear out of a reported set: rows it holds of gear
         * classes, each piece once, and the fullest ammunition stack as the
         * quiver. Equal class/properties rows share that native stack; retain
         * every reported row in it. Other ammunition stacks are left out.
         */
        static std::map<std::string, std::uint32_t> CountEquipment(const Inventory &inventory, const std::map<std::string, std::uint32_t> &items);
        /** Drops worn counts for rows that left, and lowers those that shrank. */
        static void ClampEquipment(const Inventory &inventory, std::map<std::string, std::uint32_t> &equipped);

        /** One line per class of what a thief may take: every unit not worn. */
        static std::vector<PocketLine> Pockets(const Inventory &victim, const std::map<std::string, std::uint32_t> &equipped);
        /** Moves a claim out of the victim's unworn units, each line within the offer, all or nothing. */
        static InventoryResult Steal(Inventory &victim, Inventory &thief, const std::map<std::string, std::uint32_t> &equipped, const std::vector<PocketLine> &offer, const std::vector<PocketLine> &claim);

        /** Why a thief's reported theft cannot stand, or empty when it can. */
        static std::string TheftRefusal(std::uint64_t offeredAtMs, std::uint64_t nowMs, const glm::vec3 &thief, std::uint32_t thiefWorld, const glm::vec3 &victim, std::uint32_t victimWorld);

        /** What a death drop moves: every unit, less the worn ones with `keepEquipped`, at most `maximumRows` rows. */
        static std::vector<InventoryUnit> DropUnits(const Inventory &inventory, const std::map<std::string, std::uint32_t> &equipped, bool keepEquipped, std::size_t maximumRows);
    };

    /** A round a shot spent, remembered so it can be found again where it landed. */
    struct SpentRound {
        std::string itemClass;
        nlohmann::json metadata = nlohmann::json::object();
        std::uint64_t firedAtMs    = 0;
        std::uint32_t virtualWorld = 0;
    };

    /** A native impact, with an optional item identity for source adoption. */
    struct AmmunitionRecovery {
        std::string fired;
        std::string landed;
        glm::vec3 position {0.0F};
        glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};
        std::uint64_t nativeItemWuid = 0;
    };

    /** One player's spent rounds, oldest first, until they are recovered or expire. */
    class SpentRounds final {
      public:
        static constexpr std::size_t kMaximum         = 128;
        static constexpr std::uint64_t kLifetimeMs    = 20 * 60 * 1000;
        static constexpr float kMaximumRecoveryRange  = 300.0F;

        /** Whether a round could have landed there: within a bow's reach of its archer. */
        static bool WithinReach(const glm::vec3 &shooter, const glm::vec3 &landing);
        /**
         * Whether a fired round can come to rest as `landed`: itself, or the
         * broken round of its own kind that CProjectile::ReplaceWithBrokenAmmunition
         * (0x182AA0138) swaps in when the impact breaks it.
         */
        static bool LandsAs(const std::string &fired, const std::string &landed);
        /** Reads a bounded, finite impact pose; adoption requires its rotation. */
        static bool ReadRecovery(const nlohmann::json &message, AmmunitionRecovery &recovery);

        void Fire(const InventoryRow &round, std::uint32_t amount, std::uint32_t virtualWorld, std::uint64_t nowMs);
        /** The oldest live round of this class fired in this world, left in place; null when there is none. */
        const SpentRound *Find(const std::string &itemClass, std::uint32_t virtualWorld, std::uint64_t nowMs);
        /** The oldest live round of this class fired in this world, taken out; nothing when there is none. */
        std::optional<SpentRound> Claim(const std::string &itemClass, std::uint32_t virtualWorld, std::uint64_t nowMs);
        std::size_t Size() const {
            return _rounds.size();
        }

      private:
        void Expire(std::uint64_t nowMs);

        std::deque<SpentRound> _rounds;
    };
} // namespace KCDC::Features::Inventory
