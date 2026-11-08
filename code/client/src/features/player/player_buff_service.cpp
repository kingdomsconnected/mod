#include <utils/safe_win32.h>

#include "features/player/player_buff_service.h"
#include "features/buff/custom_buff_module.h"

#include "core/application.h"
#include "features/player/player.h"
#include "common/module_support.h"

#include "shared/game/buff_catalog.h"

#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/rpgmodule/c_buff_instance.h"
#include "sdk/wh/rpgmodule/c_buff_manager.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/rpgmodule/c_timed_soul_buff_instance.h"
#include "sdk/wh/rpgmodule/s_buff_definition.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

#include <mafianet/get_time.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>

#include "sdk/patterns.h"


namespace KCDC::Features::Player {
    namespace {
        constexpr const char *kLogger = "BuffSync";

        // How long the same definition stays quiet after a refusal was reported,
        // so a predicate retrying every frame costs two messages a second.
        constexpr std::uint64_t kRefusalQuietMilliseconds = 500;

        bool Contains(const std::vector<Shared::RPC::BuffId> &haystack, const Shared::RPC::BuffId &needle) {
            return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
        }

        void Remember(std::vector<Shared::RPC::BuffId> &haystack, const Shared::RPC::BuffId &needle) {
            if (!Contains(haystack, needle)) {
                haystack.push_back(needle);
            }
        }

        void Forget(std::vector<Shared::RPC::BuffId> &haystack, const Shared::RPC::BuffId &needle) {
            haystack.erase(std::remove(haystack.begin(), haystack.end(), needle), haystack.end());
        }
    } // namespace

    void PlayerBuffService::Install() {
        KCDC::Features::Buff::CustomBuffModule::Install();
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        net->RegisterRPC<Shared::RPC::BuffPolicy>([](const Shared::RPC::BuffPolicy &policy, MafiaNet::Packet *) {
            if (!policy.IsSane() || KCDC::Core::gApplication == nullptr) {
                return;
            }
            KCDC::Core::gApplication->GetPlayerBuffService()._claimedClasses = policy.claimedClasses;
            Framework::Logging::GetLogger(kLogger)->info("Server claims buff classes 0x{:08X}", policy.claimedClasses);
        });

        net->RegisterRPC<Shared::RPC::ApplyBuff>([](const Shared::RPC::ApplyBuff &order, MafiaNet::Packet *) {
            if (order.IsSane() && KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerBuffService()._pendingOrders.emplace_back(order);
            }
        });

        net->RegisterRPC<Shared::RPC::RetractBuff>([](const Shared::RPC::RetractBuff &order, MafiaNet::Packet *) {
            if (order.IsSane() && KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerBuffService()._pendingOrders.emplace_back(order);
            }
        });

        net->RegisterRPC<Shared::RPC::ClearBuffsByTag>([](const Shared::RPC::ClearBuffsByTag &order, MafiaNet::Packet *) {
            if (order.IsSane() && KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerBuffService()._pendingOrders.emplace_back(order);
            }
        });
    }

    void PlayerBuffService::Update() {
        SDK::wh::rpgmodule::C_Soul *soul = KCDC::Common::LocalSoul();
        if (soul == nullptr) {
            // Between bodies. Orders keep until there is somewhere to put them.
            return;
        }

        ApplyOrders(soul);
        ReportRefusals();
        BuildDigest(soul);
        PruneAttribution();

        // A set rather than a set of countdowns, so it changes only when a buff
        // arrives or leaves. `firstSeen` is not part of that: it never travels.
        const bool sameSet = _digest.size() == _published.size() && std::equal(_digest.begin(), _digest.end(), _published.begin(), [](const Shared::RPC::ObservedBuff &left, const Shared::RPC::ObservedBuff &right) {
            return left.buff == right.buff;
        });
        if (_revision == 0 || !sameSet) {
            // A custom maximum-health effect moved the ceiling: clamp to it and
            // let the native health listeners redraw against the new one.
            if (KCDC::Features::Buff::CustomBuffModule::Get().HasHealthModifiers()) {
                const float maximumHealth = soul->GetStateMax(SDK::wh::rpgmodule::E_SoulState::Health);
                if (soul->GetState(SDK::wh::rpgmodule::E_SoulState::Health) > maximumHealth) {
                    soul->SetState(SDK::wh::rpgmodule::E_SoulState::Health, maximumHealth);
                }
                soul->GetPropertyEvents()->NotifyStateChanged(SDK::wh::rpgmodule::E_SoulState::Health);
            }
            Publish();
        }
    }

    void PlayerBuffService::ApplyOrders(SDK::wh::rpgmodule::C_Soul *soul) {
        if (_pendingOrders.empty()) {
            return;
        }
        SDK::wh::rpgmodule::C_BuffManager *manager = SDK::wh::rpgmodule::C_BuffManager::GetInstance();
        if (manager == nullptr) {
            return;
        }

        // Human captures before this service. Require the snapshot to name
        // the soul being used, not merely the last body it reported.
        const auto *owner      = Human::FindLocal();
        const auto incarnation = owner != nullptr && owner->soul.valid && KCDC::Core::gApplication->GetPlayerService().CapturedLocalSoul() == soul ? owner->soul.incarnation : 0;
        for (const auto &pending : _pendingOrders) {
            if (const auto *clear = std::get_if<Shared::RPC::ClearBuffsByTag>(&pending)) {
                manager->RemoveAllBuffsByAITag(soul, clear->aiTag);
                continue;
            }
            if (const auto *retract = std::get_if<Shared::RPC::RetractBuff>(&pending)) {
                Forget(_serverDefinitions, retract->buff);
                std::erase(_serverHandles, retract->handle);
                if (retract->handle != 0 && manager->FindBuffInstance(retract->handle) != nullptr) {
                    manager->RemoveBuff(retract->handle);
                }
                else {
                    const auto guid = KCDC::Features::Buff::CustomBuffModule::Get().ToNative(retract->buff);
                    manager->RemoveAllBuffsByGuid(soul, guid);
                }
                continue;
            }
            const auto &order = std::get<Shared::RPC::ApplyBuff>(pending);
            if (!order.AppliesTo(incarnation)) {
                continue;
            }
            const SDK::CryGUID buff = KCDC::Features::Buff::CustomBuffModule::Get().ToNative(order.buff);
            if (buff.hipart == 0 && buff.lopart == 0) {
                continue;
            }
            if (const auto *definition = Shared::Game::CustomBuffRegistry::Get().Find(Shared::Game::CryGuidToString(order.buff.low, order.buff.high))) {
                std::vector<std::pair<double, std::uint64_t>> existing;
                for (auto it = soul->ActiveBuffsBegin(); it != soul->ActiveBuffsEnd(); ++it) {
                    if (*it != nullptr && (*it)->GetDefinition() != nullptr && (*it)->GetDefinition()->buffId.hipart == buff.hipart && (*it)->GetDefinition()->buffId.lopart == buff.lopart) {
                        existing.emplace_back(static_cast<const SDK::wh::rpgmodule::C_TimedSoulBuffInstance *>(*it)->GetElapsed(), (*it)->GetHandle());
                    }
                }
                const bool replacing = existing.size() >= definition->maxStacks;
                if (replacing && !definition->refresh) {
                    continue;
                }
                std::size_t reportable = 0;
                for (auto it = soul->ActiveBuffsBegin(); it != soul->ActiveBuffsEnd(); ++it) {
                    const auto *row = *it != nullptr ? (*it)->GetDefinition() : nullptr;
                    if (row != nullptr && IsReportable(*row, KCDC::Features::Buff::CustomBuffModule::Get().ToLogical(row->buffId))) {
                        ++reportable;
                    }
                }
                // A refresh swaps the oldest stack for the new one, so it takes
                // no extra slot. Checked before the removal, so a refused order
                // does not cost the player a stack.
                if (reportable - (replacing && reportable > 0 ? 1 : 0) >= Shared::RPC::kMaxReportedBuffs) {
                    continue;
                }
                if (replacing) {
                    manager->RemoveBuff(std::max_element(existing.begin(), existing.end())->second);
                }
            }

            // The funnel is handed the requested GUID, which is what was
            // ordered, so the gate matches on it before any remap.
            _applyingBuff                                    = order.buff;
            SDK::wh::rpgmodule::C_BuffInstanceBase *instance = manager->AddBuffToSoul(soul, buff, nullptr, SDK::CryGUID {});
            _applyingBuff                                    = {};

            // Null is not a failure: six implementations fold the incoming
            // instance into the one already there and refuse the attach, so a
            // successful stack looks exactly like this. Let the digest say
            // what stuck.
            Remember(_serverDefinitions, order.buff);
            if (instance != nullptr) {
                _serverHandles.push_back(instance->GetHandle());
            }
        }
        _pendingOrders.clear();
    }

    void PlayerBuffService::BuildDigest(SDK::wh::rpgmodule::C_Soul *soul) {
        _digest.clear();
        const std::uint64_t now = MafiaNet::GetTime();

        for (const SDK::wh::rpgmodule::C_BuffInstanceBase *const *it = soul->ActiveBuffsBegin(); it != soul->ActiveBuffsEnd(); ++it) {
            const SDK::wh::rpgmodule::C_BuffInstanceBase *instance = *it;
            if (instance == nullptr) {
                continue;
            }

            const SDK::wh::rpgmodule::S_BuffDefinition *definition = instance->GetDefinition();
            if (definition == nullptr) {
                continue;
            }

            const Shared::RPC::BuffId buff = KCDC::Features::Buff::CustomBuffModule::Get().ToLogical(definition->buffId);

            if (!IsReportable(*definition, buff)) {
                continue;
            }

            if (_digest.size() >= Shared::RPC::kMaxReportedBuffs) {
                if (!_overflowLogged) {
                    _overflowLogged = true;
                    Framework::Logging::GetLogger(kLogger)->warn("Local body carries more than {} reportable buffs; the digest is truncated", Shared::RPC::kMaxReportedBuffs);
                }
                break;
            }

            Shared::RPC::ActiveBuff entry;
            entry.buff   = buff;
            entry.handle = instance->GetHandle();
            entry.source = std::find(_serverHandles.begin(), _serverHandles.end(), entry.handle) != _serverHandles.end() || Contains(_serverDefinitions, buff) ? Shared::RPC::BuffSource::Server : Shared::RPC::BuffSource::Native;
            if (!entry.IsSane()) {
                continue;
            }

            // A handle already published keeps the moment it arrived.
            const auto seen = std::find_if(_published.begin(), _published.end(), [&entry](const Shared::RPC::ObservedBuff &previous) {
                return previous.buff.handle == entry.handle;
            });
            _digest.push_back({entry, seen != _published.end() ? seen->firstSeen : now});
        }

        // The active index is a vector a removal reorders, so put the digest in
        // an order of ours: two reports then compare equal when they name the
        // same buffs.
        std::sort(_digest.begin(), _digest.end(), [](const Shared::RPC::ObservedBuff &left, const Shared::RPC::ObservedBuff &right) {
            return left.buff.handle < right.buff.handle;
        });
    }

    bool PlayerBuffService::IsReportable(const SDK::wh::rpgmodule::S_BuffDefinition &definition, const Shared::RPC::BuffId &buff) const {
        // An ordered buff is reportable whatever class it is in: without
        // that, `addBuff` on one of the 710 rows outside the stateful
        // classes succeeds and then reads back as absent forever.
        const std::uint32_t reportedClasses = Shared::Game::kStatefulBuffClasses | _claimedClasses;
        const bool reportedClass            = definition.buffClassId >= 0 && definition.buffClassId < 32 && (reportedClasses & Shared::Game::BuffClass::Bit(static_cast<std::uint8_t>(definition.buffClassId))) != 0;
        return reportedClass || Contains(_serverDefinitions, buff);
    }

    void PlayerBuffService::PruneAttribution() {
        // Attribution must not outlive what it describes: unpruned, a recycled
        // handle would relabel someone else's buff.
        std::erase_if(_serverHandles, [this](std::uint64_t handle) {
            return std::none_of(_digest.begin(), _digest.end(), [handle](const Shared::RPC::ObservedBuff &entry) {
                return entry.buff.handle == handle;
            });
        });
        std::erase_if(_serverDefinitions, [this](const Shared::RPC::BuffId &buff) {
            return std::none_of(_digest.begin(), _digest.end(), [&buff](const Shared::RPC::ObservedBuff &entry) {
                return entry.buff.buff == buff;
            });
        });
    }

    void PlayerBuffService::Publish() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        _revision  = _revision + 1 == 0 ? 1 : _revision + 1;
        _published = _digest;

        // `firstSeen` stays here: the receiver times its own view.
        Shared::RPC::BuffReport report;
        report.revision = _revision;
        report.active.reserve(_published.size());
        for (const Shared::RPC::ObservedBuff &observed : _published) {
            report.active.push_back(observed.buff);
        }
        net->BroadcastRPC(report, MafiaNet::Priority::Low, MafiaNet::Reliability::ReliableOrdered);
    }

    void PlayerBuffService::ReportRefusals() {
        const std::uint64_t now = MafiaNet::GetTime();
        std::erase_if(_recentRefusals, [now](const Refusal &refusal) {
            return now - refusal.reportedAt >= kRefusalQuietMilliseconds;
        });

        if (_pendingRefusals.empty()) {
            return;
        }

        auto *net = Framework::CoreModules::GetNetworkPeer();
        for (const Shared::RPC::BuffId &buff : _pendingRefusals) {
            const bool quiet = std::any_of(_recentRefusals.begin(), _recentRefusals.end(), [&buff](const Refusal &refusal) {
                return refusal.buff == buff;
            });
            if (quiet || net == nullptr) {
                continue;
            }

            Shared::RPC::BuffRefused refused;
            refused.buff = buff;
            net->BroadcastRPC(refused, MafiaNet::Priority::Low, MafiaNet::Reliability::ReliableOrdered);
            _recentRefusals.push_back({buff, now});
        }
        _pendingRefusals.clear();
    }

    void PlayerBuffService::Reset() {
        _claimedClasses = 0;
        _pendingOrders.clear();

        _pendingRefusals.clear();
        _recentRefusals.clear();
        _serverHandles.clear();
        _serverDefinitions.clear();
        _digest.clear();
        _published.clear();
        _revision       = 0;
        _overflowLogged = false;
        _applyingBuff   = {};
    }

    bool PlayerBuffService::RefusesLocalAdd(const SDK::wh::rpgmodule::C_Soul *soul, const SDK::CryGUID &requested) {
        if (_claimedClasses == 0 || soul == nullptr) {
            return false;
        }
        const Shared::RPC::BuffId asked = KCDC::Features::Buff::CustomBuffModule::Get().ToLogical(requested);
        if (_applyingBuff.IsValid() && _applyingBuff == asked) {
            return false;
        }
        if (KCDC::Common::LocalSoul() != soul) {
            return false;
        }

        // Only reached for the local body under a non-empty claim. A definition
        // no shipped table names is not one the server asked for.
        const Shared::Game::BuffEntry *entry = Shared::Game::FindBuffById(asked);
        if (entry == nullptr) {
            return false;
        }
        if ((_claimedClasses & Shared::Game::BuffClass::Bit(entry->classId)) == 0) {
            return false;
        }

        // Told on the mod's own tick: this is the middle of the engine call
        // being refused.
        if (_pendingRefusals.size() < Shared::RPC::kMaxReportedBuffs) {
            Remember(_pendingRefusals, asked);
        }
        return true;
    }
} // namespace KCDC::Features::Player

namespace KCDC::Features::Player {
    namespace {
        // C_BuffManager's create-and-attach funnel: every buff the game gives
        // anyone comes through here. The return is a _smart_ptr through the
        // hidden sret pointer MSVC passes in RDX, hence the spelled-out `out`.
        // 0x1804675A8, vtable slot 2.
        using CreateAndAttachBuff_t = void **(__fastcall *)(void *manager, void **out, SDK::wh::rpgmodule::C_Soul *soul, const SDK::CryGUID *buff, void *initParams, const SDK::CryGUID *sourceGuid);

        CreateAndAttachBuff_t g_createAndAttachBuffOriginal = nullptr;

        // An empty smart pointer is the same answer the exclusivity arbiter
        // gives when a conflicting buff wins, so every native caller handles
        // it and nothing has been allocated to leak.
        //
        // A hit's buffs come here too (`C_AddBuffEffect::Apply`, 0x1809DC01C,
        // through both `AddBuff` slots), so this is also where a horse copy
        // is spared the knockout of a blow this client dealt it.
        void **__fastcall CreateAndAttachBuff_Hook(void *manager, void **out, SDK::wh::rpgmodule::C_Soul *soul, const SDK::CryGUID *buff, void *initParams, const SDK::CryGUID *sourceGuid) {
            if (KCDC::Core::gApplication != nullptr && out != nullptr && buff != nullptr && (KCDC::Core::gApplication->GetPlayerBuffService().RefusesLocalAdd(soul, *buff) || KCDC::Core::gApplication->GetHorseService().RefusesKnockout(soul, *buff))) {
                *out = nullptr;
                return out;
            }
            return g_createAndAttachBuffOriginal(manager, out, soul, buff, initParams, sourceGuid);
        }
    } // namespace
} // namespace KCDC::Features::Player

static InitFunction initBuffHooks(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_BuffManager__CreateAndAttachBuff), reinterpret_cast<LPVOID>(&KCDC::Features::Player::CreateAndAttachBuff_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_createAndAttachBuffOriginal));
    },
    "Buff");
