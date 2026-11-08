#include "features/player/player_combat_blow.h"

#include "core/server.h"
#include "shared/game/cry_guid_text.h"
#include "shared/game/progression_catalog.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace KCDC::Features::Player {
    float CombatBlow::Skill(KCDC::Shared::Entities::HumanEntity &player, std::string_view name) {
        const int track = KCDC::Shared::Game::FindProgressionTrack(name);
        if (track < 0 || KCDC::Shared::Game::g_ProgressionTracks[track].kind != KCDC::Shared::Game::ProgressionTrackKind::Skill) {
            return 0.0F;
        }
        const std::uint32_t native = KCDC::Shared::Game::g_ProgressionTracks[track].native;
        const auto &selectors      = KCDC::Shared::Game::HumanSoulState::kSkillSelectors;
        const auto replicated      = std::find(selectors.begin(), selectors.end(), native);
        if (replicated != selectors.end() && player.soul.valid) {
            return player.soul.skills[static_cast<std::size_t>(replicated - selectors.begin())];
        }
        // Not every skill travels with the soul -- `weapon_shield` does not --
        // and the accepted level is the base the engine starts from.
        Server *server     = Server::Get();
        const auto *digest = server != nullptr ? server->GetProgression().Accepted(player.GetNetworkID()) : nullptr;
        return digest != nullptr && static_cast<std::size_t>(track) < digest->tracks.size() ? static_cast<float>(digest->tracks[track].level) : 0.0F;
    }

    std::vector<float> CombatBlow::ItemHealths(std::uint64_t playerNetworkId, const std::vector<KCDC::Shared::RPC::ItemClassId> &itemClasses) {
        // Below any health a row can have: a class no row of which is carried.
        constexpr float kNotCarried = -2.0F;
        std::vector<float> best(itemClasses.size(), kNotCarried);
        Server *server        = Server::Get();
        const auto *inventory = server != nullptr && !itemClasses.empty() ? server->GetInventory().Find(playerNetworkId) : nullptr;
        if (inventory != nullptr) {
            // The rows say which kinds of an item are carried, not which copy
            // is worn: the best one is the benefit of the doubt.
            for (const auto &[id, row] : inventory->Rows()) {
                KCDC::Shared::RPC::ItemClassId rowClass;
                if (!KCDC::Shared::Game::CryGuidFromText(row.itemClass, rowClass.low, rowClass.high)) {
                    continue;
                }
                const auto wanted = std::find(itemClasses.begin(), itemClasses.end(), rowClass);
                if (wanted == itemClasses.end()) {
                    continue;
                }
                const auto health      = row.metadata.find("health");
                const float value      = health != row.metadata.end() && health->is_number() ? health->get<float>() : -1.0F;
                const float normalized = value < 0.0F ? 1.0F : std::min(value, 1.0F);
                float &slot            = best[static_cast<std::size_t>(wanted - itemClasses.begin())];
                slot                   = std::max(slot, normalized);
            }
        }
        for (float &health : best) {
            health = health == kNotCarried ? -1.0F : health;
        }
        return best;
    }

    namespace {
        /** The classes `HumanEntity::equipment` carries back to back, 32 hex digits each, low half first. */
        std::vector<KCDC::Shared::RPC::ItemClassId> WornClasses(const std::string &equipment) {
            std::vector<KCDC::Shared::RPC::ItemClassId> worn;
            for (std::size_t offset = 0; offset + 32 <= equipment.size(); offset += 32) {
                KCDC::Shared::RPC::ItemClassId itemClass;
                const char *text = equipment.data() + offset;
                const auto low   = std::from_chars(text, text + 16, itemClass.low, 16);
                const auto high  = std::from_chars(text + 16, text + 32, itemClass.high, 16);
                if (low.ec == std::errc() && low.ptr == text + 16 && high.ec == std::errc() && high.ptr == text + 32 && itemClass.IsValid()) {
                    worn.push_back(itemClass);
                }
            }
            return worn;
        }

        const KCDC::Shared::Game::CombatWeaponEntry *HandWeapon(const KCDC::Shared::Entities::HumanEntity &player, std::size_t hand) {
            return KCDC::Shared::Game::FindCombatWeapon(player.handItemClass[hand]);
        }
    } // namespace

    std::optional<CombatBlow> CombatBlow::From(const KCDC::Shared::RPC::CombatHitReport &report, KCDC::Shared::Entities::HumanEntity &attacker, KCDC::Shared::Entities::HumanEntity &victim, bool counter) {
        if (report.Source() != KCDC::Shared::RPC::CombatHitSource::Melee || !report.hasInputs || report.attackTypeId < 0) {
            return std::nullopt;
        }
        const KCDC::Shared::RPC::CombatBlowInputs &inputs = report.inputs;

        CombatBlow blow;
        blow._blow.attackTypeId       = report.attackTypeId;
        blow._blow.damageTypes        = inputs.damageTypes;
        blow._blow.staminaCoefficient = inputs.staminaCoefficient;
        blow._blow.energy             = inputs.energy;
        blow._blow.mountedSpeed       = inputs.relativeSpeed;
        blow._blow.mounted            = inputs.mounted;
        blow._blow.blocked            = (report.flags & KCDC::Shared::RPC::CombatHitFlags::kBlocked) != 0;
        blow._blow.weakBlock          = (report.flags & KCDC::Shared::RPC::CombatHitFlags::kWeakBlock) != 0;
        blow._blow.perfectBlock       = (report.flags & KCDC::Shared::RPC::CombatHitFlags::kPerfectBlock) != 0;
        blow._blow.blockBroken        = (report.flags & KCDC::Shared::RPC::CombatHitFlags::kBlockBroken) != 0;
        blow._blow.counterAttack      = inputs.counterAttack;
        blow._blow.attackCharge       = counter && inputs.attackCharged ? 1.0F : 0.0F;
        blow._blow.hitSubpart         = report.hitSubpart;
        blow._blow.attackCoefficient  = inputs.attackCoefficient;
        blow._blow.angle              = inputs.angle;

        // The weapon in hand: a dagger is drawn in the left one. A shield is
        // nothing to strike with.
        const KCDC::Shared::Game::CombatWeaponEntry *right = HandWeapon(attacker, KCDC::Shared::Entities::HumanEntity::kRightHand);
        const KCDC::Shared::Game::CombatWeaponEntry *left  = HandWeapon(attacker, KCDC::Shared::Entities::HumanEntity::kLeftHand);
        blow._attacker.weapon                              = right != nullptr ? right : (left != nullptr && left->weaponClass != KCDC::Shared::Game::kShieldWeaponClass ? left : nullptr);
        if (blow._attacker.weapon != nullptr) {
            blow._attacker.weaponHealth                           = ItemHealths(attacker.GetNetworkID(), {blow._attacker.weapon->itemClass}).front();
            const KCDC::Shared::Game::CombatWeaponClassEntry *cls = KCDC::Shared::Game::FindCombatWeaponClass(blow._attacker.weapon->weaponClass);
            blow._attacker.skill                                  = cls != nullptr ? Skill(attacker, cls->skill) : 0.0F;
        }
        else {
            blow._attacker.skill = Skill(attacker, "weapon_unarmed");
        }
        blow._attacker.strength  = attacker.soul.valid ? attacker.soul.stats[0] : 0.0F;
        blow._attacker.archetype = attacker.appearance.gender == KCDC::Shared::Game::AppearanceGender::Female ? &KCDC::Shared::Game::g_CombatHeroFemaleArchetype : &KCDC::Shared::Game::g_CombatHeroArchetype;

        std::vector<KCDC::Shared::RPC::ItemClassId> armorClasses;
        for (const KCDC::Shared::RPC::ItemClassId &worn : WornClasses(victim.equipment)) {
            if (const KCDC::Shared::Game::CombatArmorEntry *armor = KCDC::Shared::Game::FindCombatArmor(worn)) {
                blow._victim.armor.push_back({armor, -1.0F});
                armorClasses.push_back(worn);
            }
        }
        const std::vector<float> armorHealths = ItemHealths(victim.GetNetworkID(), armorClasses);
        for (std::size_t piece = 0; piece < armorHealths.size(); ++piece) {
            blow._victim.armor[piece].health = armorHealths[piece];
        }
        // A shield takes the block when one is drawn; else the weapon in the
        // right hand; else the bare hands.
        const KCDC::Shared::Game::CombatWeaponEntry *shield = HandWeapon(victim, KCDC::Shared::Entities::HumanEntity::kLeftHand);
        blow._victim.blockWeapon                            = shield != nullptr && shield->weaponClass == KCDC::Shared::Game::kShieldWeaponClass ? shield : HandWeapon(victim, KCDC::Shared::Entities::HumanEntity::kRightHand);
        blow._victim.defenseSkill                           = Skill(victim, "defense");
        // The victim's stamina as the server holds it: a claim's figure is the
        // attacker's to make up, and the server's lags the contact by no more
        // than one replication of the victim's soul.
        const KCDC::Shared::Game::HumanSoulState::Resource &stamina = victim.soul.resources[KCDC::Shared::Game::HumanSoulState::kStamina];
        blow._victim.stamina                                        = victim.soul.valid ? stamina.current : inputs.victimStamina;
        blow._victim.staminaMaximum                                 = victim.soul.valid ? stamina.maximum : inputs.victimStaminaMaximum;
        blow._victim.archetype                                      = victim.appearance.gender == KCDC::Shared::Game::AppearanceGender::Female ? &KCDC::Shared::Game::g_CombatHeroFemaleArchetype : &KCDC::Shared::Game::g_CombatHeroArchetype;

        blow._attackerTerms.Collect(attacker.GetNetworkID(), inputs.mounted);
        blow._victimTerms.Collect(victim.GetNetworkID(), false);
        return blow;
    }

    KCDC::Shared::Game::CombatPrice CombatBlow::Price(const KCDC::Shared::Game::CombatPricingScales &scales) const {
        const CombatPricingModifiers modifiers(_attackerTerms, _victimTerms);
        return KCDC::Shared::Game::PriceMeleeBlow(_blow, _attacker, _victim, modifiers, scales);
    }
} // namespace KCDC::Features::Player
