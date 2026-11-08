#include <utils/safe_win32.h>

#include "features/player/player_progression_service.h"

#include "core/application.h"
#include "features/player/player.h"
#include "features/player/player_progression_scripting.h"
#include "common/module_support.h"

#include "shared/game/progression_catalog.h"

#include "sdk/types.h"
#include "sdk/wh/c_wh_global_environment.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/framework/wuid.h"
#include "sdk/wh/rpgmodule/c_event.h"
#include "sdk/wh/rpgmodule/c_perk.h"
#include "sdk/wh/rpgmodule/c_rpg_module.h"
#include "sdk/wh/rpgmodule/c_soul_state_storage.h"
#include "sdk/wh/rpgmodule/e_soul_derived_stat.h"
#include "sdk/wh/rpgmodule/i_cause.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

#include <mafianet/get_time.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>

#include "sdk/patterns.h"
#include "sdk/wh/rpgmodule/c_perk_learning_state.h"
#include "sdk/wh/rpgmodule/c_perk_list.h"
#include "sdk/wh/rpgmodule/c_rpg_inventory.h"
#include "sdk/wh/rpgmodule/c_skill_xp_effect.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/rpgmodule/c_stat_xp_effect.h"


namespace KCDC::Features::Player {
    thread_local bool PlayerProgressionService::_applyingOrder     = false;
    thread_local bool PlayerProgressionService::_serverGrantsGains = false;

    namespace {
        constexpr const char *kLogger = "Progression";

        // Gains are asked for in batches: a sword fight produces a gain per
        // hit, and one aggregated request per track keeps the wire and the
        // server's veto event quiet.
        constexpr std::uint64_t kGainBatchMilliseconds = 250;

        // A digest changes on every applied gain; this is the most often the
        // server hears about it.
        constexpr std::uint64_t kDigestMinimumMilliseconds = 100;

        /** The cause that produced an effect, as it names itself: `Attack`, `LockpickingResult`, ... Empty when it has none. */
        std::string SourceOf(const SDK::wh::rpgmodule::C_Effect *effect) {
            const SDK::wh::rpgmodule::C_Event *event = effect != nullptr ? effect->GetEvent() : nullptr;
            const SDK::wh::rpgmodule::I_Cause *cause = event != nullptr ? event->GetCause() : nullptr;
            const SDK::CryStringT<char> *name        = cause != nullptr ? cause->GetName() : nullptr;
            if (name == nullptr || name->m_pData == nullptr) {
                return {};
            }
            std::string source(name->m_pData);
            if (source.size() > Shared::RPC::kMaxXpSourceLength) {
                source.resize(Shared::RPC::kMaxXpSourceLength);
            }
            return source;
        }

        bool IsPerkOrder(Shared::RPC::ProgressionOrderKind kind) {
            return kind != Shared::RPC::ProgressionOrderKind::Xp && kind != Shared::RPC::ProgressionOrderKind::SetTotalXp;
        }

        /**
         * Marks every perk in `[begin, end)` and every perk they hold. The
         * perks a metaperk holds are owned with it but live in it, not in the
         * soul's list (`C_MetaPerk::Apply`, 0x180A1FFD4), and `C_Soul::HasPerk`
         * walks into them (0x181FDC220) -- so does this, or a copy's `HasPerk`
         * says no where its owner's soul says yes.
         */
        void MarkOwnedPerks(SDK::wh::rpgmodule::C_Perk *const *begin, SDK::wh::rpgmodule::C_Perk *const *end, Shared::RPC::PerkSet &perks) {
            for (SDK::wh::rpgmodule::C_Perk *const *it = begin; it != end; ++it) {
                if (*it == nullptr) {
                    continue;
                }
                const std::uint16_t index = Shared::Game::FindPerkIndex(KCDC::Common::FromCryGuid<Shared::RPC::PerkGuid>((*it)->GetPerkId()));
                if (index != Shared::RPC::kNoPerk) {
                    perks.Set(index, true);
                }
                const SDK::StdVector<SDK::wh::rpgmodule::C_Perk *> *children = (*it)->GetChildPerks();
                MarkOwnedPerks(children->begin(), children->end(), perks);
            }
        }
    } // namespace

    void PlayerProgressionService::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        net->RegisterRPC<Shared::RPC::ProgressionOrder>([](const Shared::RPC::ProgressionOrder &order, MafiaNet::Packet *) {
            if (order.IsSane() && KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerProgressionService()._pendingOrders.push_back(order);
            }
        });

        net->RegisterRPC<Shared::RPC::ProgressionPolicy>([](const Shared::RPC::ProgressionPolicy &policy, MafiaNet::Packet *) {
            if (!policy.IsSane() || KCDC::Core::gApplication == nullptr) {
                return;
            }
            auto &service = KCDC::Core::gApplication->GetPlayerProgressionService();
            service._nativeXp.store(policy.nativeXp, std::memory_order_release);
            {
                const std::lock_guard<std::mutex> lock(service._policyMutex);
                service._blocked = policy.blocked;
            }
            Framework::Logging::GetLogger(kLogger)->info("Server progression policy: native XP {}, {} perks blocked", policy.nativeXp ? "on" : "off", policy.blocked.Count());
        });

        net->RegisterRPC<Shared::RPC::PerkRefused>([](const Shared::RPC::PerkRefused &refused, MafiaNet::Packet *) {
            if (!refused.IsSane() || KCDC::Core::gApplication == nullptr) {
                return;
            }
            // Told to scripts on the tick, like everything else this service raises.
            KCDC::Core::gApplication->GetPlayerProgressionService()._pendingRefusals.push_back(refused);
        });
    }

    void PlayerProgressionService::Update() {
        // Once a tick at most, however many copies changed: a reader on a
        // worker holds a roster for one native call, and the ring only
        // guarantees that for a couple of publications.
        if (_remotesDirty) {
            _remotesDirty = false;
            PublishRemotes();
        }

        SDK::wh::rpgmodule::C_Soul *soul = KCDC::Common::LocalSoul();
        const std::uint64_t wuid         = soul != nullptr ? soul->GetWuid() : 0;
        if (_localSoulWuid.exchange(wuid, std::memory_order_acq_rel) != wuid) {
            // A new body: its progression has to be read once from scratch.
            _digestDirty.store(true, std::memory_order_release);
        }
        if (soul == nullptr) {
            // Between bodies. Orders keep until there is somewhere to put them.
            return;
        }

        SendWithheldGains();
        SendLearnRequests();
        ApplyOrders(soul);

        for (const Shared::RPC::PerkRefused &refused : _pendingRefusals) {
            if (const Shared::Game::PerkEntry *perk = Shared::Game::PerkAt(refused.perk); perk != nullptr) {
                KCDC::Scripting::Events::ProgressionPerkRefused(std::string(perk->name), refused.reason);
            }
        }
        _pendingRefusals.clear();

        const std::uint64_t now = MafiaNet::GetTime();
        if (!_digestDirty.load(std::memory_order_acquire) || (_revision != 0 && now - _lastPublishAt < kDigestMinimumMilliseconds)) {
            return;
        }
        _digestDirty.store(false, std::memory_order_release);

        Shared::RPC::ProgressionDigest digest;
        BuildDigest(soul, digest);
        const bool unchanged = _revision != 0 && digest.appliedOrder == _published.appliedOrder && digest.tracks == _published.tracks && digest.perkPoints == _published.perkPoints && digest.mainLevel == _published.mainLevel && digest.perks == _published.perks;
        if (unchanged) {
            return;
        }

        const Shared::RPC::ProgressionDigest previous = _published;
        const bool first                              = _revision == 0;
        Publish(digest);
        _lastPublishAt = now;
        if (!first) {
            EmitLocalEvents(previous, _published);
        }
    }

    bool PlayerProgressionService::IsLocalSoul(const SDK::wh::rpgmodule::C_Soul *soul) const {
        const std::uint64_t local = _localSoulWuid.load(std::memory_order_acquire);
        return soul != nullptr && local != 0 && soul->GetWuid() == local;
    }

    void PlayerProgressionService::QueueGain(std::uint8_t track, std::uint32_t xpQ7, bool scaled, const SDK::wh::rpgmodule::C_Effect *effect) {
        std::string source = SourceOf(effect);

        const std::lock_guard<std::mutex> lock(_requestMutex);
        for (Shared::RPC::XpGain &gain : _withheldGains) {
            if (gain.track == track && gain.scaled == scaled && gain.source == source) {
                gain.xpQ7 = static_cast<std::uint32_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(gain.xpQ7) + xpQ7, Shared::RPC::kMaxXpQ7));
                return;
            }
        }
        if (_withheldGains.size() >= Shared::RPC::kMaxXpGainsPerRequest) {
            // A burst wider than a request is lost rather than grown without
            // bound; the next batch starts fresh.
            return;
        }

        Shared::RPC::XpGain gain;
        gain.track  = track;
        gain.xpQ7   = std::min<std::uint32_t>(xpQ7, Shared::RPC::kMaxXpQ7);
        gain.scaled = scaled;
        gain.source = std::move(source);
        _withheldGains.push_back(std::move(gain));
    }

    bool PlayerProgressionService::WithholdsSkillGain(SDK::wh::rpgmodule::C_SkillXPEffect *effect) {
        if (_applyingOrder || effect == nullptr) {
            return false;
        }
        const std::uint64_t local = _localSoulWuid.load(std::memory_order_acquire);
        if (local == 0 || effect->GetSubjectSoulWuid().value != local) {
            return false;
        }

        effect->Withhold();
        if (_serverGrantsGains) {
            return true;
        }

        // A skill the player's tables hide -- defense, the crafts -- moves no
        // track anyone can see, and fencing is derived rather than earned. The
        // gain is still withheld: nothing moves a player's soul unordered.
        const int track = Shared::Game::FindProgressionTrack(Shared::Game::ProgressionTrackKind::Skill, effect->GetSkill());
        if (track < 0 || Shared::Game::IsInducedTrack(static_cast<std::size_t>(track)) || effect->GetRequestedXpQ7() == 0 || !_nativeXp.load(std::memory_order_acquire)) {
            return true;
        }
        QueueGain(static_cast<std::uint8_t>(track), effect->GetRequestedXpQ7(), effect->AppliesMultipliers(), effect);
        return true;
    }

    bool PlayerProgressionService::WithholdsStatGain(SDK::wh::rpgmodule::C_StatXPEffect *effect) {
        if (_applyingOrder || effect == nullptr) {
            return false;
        }
        const std::uint64_t local = _localSoulWuid.load(std::memory_order_acquire);
        if (local == 0 || effect->GetSubjectSoulWuid().value != local) {
            return false;
        }

        effect->Withhold();
        if (_serverGrantsGains) {
            return true;
        }

        const int track = Shared::Game::FindProgressionTrack(Shared::Game::ProgressionTrackKind::Stat, effect->GetStat());
        if (track < 0 || effect->GetRequestedXpQ7() == 0 || !_nativeXp.load(std::memory_order_acquire)) {
            return true;
        }
        QueueGain(static_cast<std::uint8_t>(track), effect->GetRequestedXpQ7(), effect->AppliesMultipliers(), effect);
        return true;
    }

    bool PlayerProgressionService::RefusesPerkScreenLearn(const SDK::wh::rpgmodule::I_Soul *soul, const SDK::CryGUID &perk) {
        // Every I_Soul the game creates is a C_Soul; the WUID is on the class.
        if (_applyingOrder || soul == nullptr || !IsLocalSoul(static_cast<const SDK::wh::rpgmodule::C_Soul *>(soul))) {
            return false;
        }

        // A perk this build's tables do not carry cannot be asked for, so it
        // cannot be learnt either.
        const std::uint16_t index = Shared::Game::FindPerkIndex(KCDC::Common::FromCryGuid<Shared::RPC::PerkGuid>(perk));
        if (index != Shared::RPC::kNoPerk) {
            const std::lock_guard<std::mutex> lock(_requestMutex);
            if (std::find(_learnRequests.begin(), _learnRequests.end(), index) == _learnRequests.end() && _learnRequests.size() < 16) {
                _learnRequests.push_back(index);
            }
        }

        // Dropped before any event exists: nothing is spent and the screen says
        // nothing until the server's order runs the same call for real.
        return true;
    }

    bool PlayerProgressionService::RefusesAdd(SDK::wh::rpgmodule::C_PerkList *list, const SDK::CryGUID &perk) {
        if (_applyingOrder || list == nullptr) {
            return false;
        }
        if (!IsLocalSoul(list->GetSoul())) {
            return false;
        }

        const std::uint16_t index = Shared::Game::FindPerkIndex(KCDC::Common::FromCryGuid<Shared::RPC::PerkGuid>(perk));
        if (index == Shared::RPC::kNoPerk) {
            return false;
        }
        const Shared::Game::PerkEntry *entry = Shared::Game::PerkAt(index);

        // A tree perk only ever arrives through a learn, which the learn gate
        // already turned into a request -- so one arriving here unordered did
        // not come from the perk screen, and the server would refuse to count
        // it.
        if (entry != nullptr && entry->visibility == Shared::Game::PerkVisibility::kVisible && !entry->autolearnable) {
            return true;
        }

        const std::lock_guard<std::mutex> lock(_policyMutex);
        return _blocked.Has(index);
    }

    bool PlayerProgressionService::ShowsBlocked(const SDK::wh::rpgmodule::I_Soul *soul, const SDK::CryGUID &perk) {
        // Every I_Soul the game creates is a C_Soul; the WUID is on the class.
        if (soul == nullptr || !IsLocalSoul(static_cast<const SDK::wh::rpgmodule::C_Soul *>(soul))) {
            return false;
        }
        const std::uint16_t index = Shared::Game::FindPerkIndex(KCDC::Common::FromCryGuid<Shared::RPC::PerkGuid>(perk));
        const std::lock_guard<std::mutex> lock(_policyMutex);
        return index != Shared::RPC::kNoPerk && _blocked.Has(index);
    }

    bool PlayerProgressionService::AnswersRemotePerk(const SDK::wh::rpgmodule::I_Soul *soul, const SDK::CryGUID &perk, bool &owned) const {
        Human *human = Human::FindRemoteSoul(soul);
        if (human == nullptr) {
            return false;
        }

        const std::uint64_t networkId  = human->GetNetworkID();
        const RemotePerkRoster &roster = _remotes.Current();
        for (std::size_t i = 0; i < roster.count; ++i) {
            if (roster.networkIds[i] != networkId) {
                continue;
            }
            const std::uint16_t index = Shared::Game::FindPerkIndex(KCDC::Common::FromCryGuid<Shared::RPC::PerkGuid>(perk));
            owned                     = index != Shared::RPC::kNoPerk && roster.perks[i].Has(index);
            return true;
        }

        // A copy the server has not described yet owns nothing, which is
        // also what the engine would say.
        owned = false;
        return true;
    }

    void PlayerProgressionService::NotePerkListChanged(const SDK::wh::rpgmodule::C_PerkList *list) {
        if (list == nullptr || !IsLocalSoul(list->GetSoul())) {
            return;
        }
        _digestDirty.store(true, std::memory_order_release);
    }

    void PlayerProgressionService::NotePerkEventDispatched(const SDK::wh::rpgmodule::C_PerkList *list, bool pointsChanged) {
        if (list == nullptr || !IsLocalSoul(list->GetSoul())) {
            return;
        }
        if (pointsChanged) {
            _digestDirty.store(true, std::memory_order_release);
        }

        // The RPG module is draining its queue, so whatever levels the last XP
        // order produced have become points by now.
        _awaitingDispatch.store(false, std::memory_order_release);
    }

    void PlayerProgressionService::NoteRemotePerks(std::uint64_t networkId, const Shared::RPC::PerkSet &perks) {
        auto known = std::find_if(_remotePerks.begin(), _remotePerks.end(), [networkId](const auto &entry) {
            return entry.first == networkId;
        });
        if (known != _remotePerks.end() && known->second == perks) {
            return;
        }
        if (known == _remotePerks.end()) {
            _remotePerks.emplace_back(networkId, perks);
        }
        else {
            known->second = perks;
        }
        _remotesDirty = true;
    }

    void PlayerProgressionService::ForgetRemote(std::uint64_t networkId) {
        const auto erased = std::erase_if(_remotePerks, [networkId](const auto &entry) {
            return entry.first == networkId;
        });
        if (erased != 0) {
            _remotesDirty = true;
        }
    }

    void PlayerProgressionService::SendWithheldGains() {
        const std::uint64_t now = MafiaNet::GetTime();
        if (now - _lastGainSendAt < kGainBatchMilliseconds) {
            return;
        }

        Shared::RPC::XpRequest request;
        {
            const std::lock_guard<std::mutex> lock(_requestMutex);
            request.gains.swap(_withheldGains);
        }
        if (request.gains.empty()) {
            return;
        }
        _lastGainSendAt = now;

        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net != nullptr && request.IsSane()) {
            net->BroadcastRPC(request, MafiaNet::Priority::Low, MafiaNet::Reliability::ReliableOrdered);
        }
    }

    void PlayerProgressionService::SendLearnRequests() {
        std::vector<std::uint16_t> requests;
        {
            const std::lock_guard<std::mutex> lock(_requestMutex);
            requests.swap(_learnRequests);
        }

        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        for (const std::uint16_t perk : requests) {
            Shared::RPC::LearnPerkRequest request;
            request.perk = perk;
            net->BroadcastRPC(request, MafiaNet::Priority::Medium, MafiaNet::Reliability::ReliableOrdered);
        }
    }

    void PlayerProgressionService::ApplyOrders(SDK::wh::rpgmodule::C_Soul *soul) {
        std::size_t applied = 0;
        for (; applied < _pendingOrders.size(); ++applied) {
            const Shared::RPC::ProgressionOrder &order = _pendingOrders[applied];

            // The perk-point banks follow a level through `C_PerkList::OnEvent`,
            // which runs when the RPG module drains its event queue -- not
            // inside `ProcessEvent`. A perk order behind an XP order waits for
            // that drain, or a restore's point count would be overwritten by
            // the points its levels grant afterwards.
            if (IsPerkOrder(order.kind) && _awaitingDispatch.load(std::memory_order_acquire)) {
                break;
            }
            if (!ApplyOrder(soul, order)) {
                Framework::Logging::GetLogger(kLogger)->warn("Progression order {} (kind {}) could not be applied", order.sequence, static_cast<int>(order.kind));
            }
            _appliedOrder = order.sequence;
            _digestDirty.store(true, std::memory_order_release);
            if (order.kind == Shared::RPC::ProgressionOrderKind::Xp) {
                _awaitingDispatch.store(true, std::memory_order_release);
            }
        }
        _pendingOrders.erase(_pendingOrders.begin(), _pendingOrders.begin() + static_cast<std::ptrdiff_t>(applied));
    }

    bool PlayerProgressionService::ApplyOrder(SDK::wh::rpgmodule::C_Soul *soul, const Shared::RPC::ProgressionOrder &order) {
        SDK::wh::rpgmodule::C_PerkList *list              = soul->GetActiveStateStorage()->GetPerkList();
        SDK::wh::rpgmodule::C_PerkLearningState *learning = list->GetLearningState();
        const Shared::Game::PerkEntry *perk               = Shared::Game::PerkAt(order.perk);

        _applyingOrder = true;
        bool ok        = true;
        switch (order.kind) {
        case Shared::RPC::ProgressionOrderKind::SetTotalXp: {
            if (Shared::Game::IsInducedTrack(order.track)) {
                ok = false;
                break;
            }
            const auto &track = Shared::Game::g_ProgressionTracks[order.track];
            if (order.value > Shared::Game::TotalXpQ7ForLevel(track, track.cap)) {
                ok = false;
                break;
            }
            auto *storage       = soul->GetActiveStateStorage();
            const auto &current = track.kind == Shared::Game::ProgressionTrackKind::Skill ? storage->GetSkillEntry(track.native) : storage->GetStatEntry(track.native);
            const auto before   = Shared::Game::TotalXpQ7(track, {current.level, current.xpQ7});
            // An unannounced raise is a restore: it lands as the same write a
            // lowering does, never as an XP effect, so the HUD's level-up
            // popups and the perk-point grants -- both listeners on an effect --
            // stay silent, and the banks come as their own orders.
            if (order.value > before && order.announce) {
                ok = ApplyExactXp(soul, order.track, order.value);
                if (ok) {
                    _awaitingDispatch.store(true, std::memory_order_release);
                }
            }
            else {
                const auto replacement = Shared::Game::ProgressFromTotalXpQ7(track, order.value);
                if (track.kind == Shared::Game::ProgressionTrackKind::Skill) {
                    storage->SetSkillProgress(track.native, {replacement.level, replacement.xpQ7});
                }
                else {
                    storage->SetStatProgress(track.native, {replacement.level, replacement.xpQ7});
                    soul->GetPropertyEvents()->NotifyStatProgressChanged(track.native);
                }
                // What the perk screen offers follows the levels; the engine
                // refreshes it after its own point grant, which this skips.
                list->RefreshAvailablePerks();
            }
            break;
        }
        case Shared::RPC::ProgressionOrderKind::Xp: ok = ApplyXp(soul, order.track, order.value, order.scaled); break;
        case Shared::RPC::ProgressionOrderKind::GrantPerk: ok = perk != nullptr && list->AddPerk(KCDC::Common::ToCryGuid(perk->id), nullptr); break;
        case Shared::RPC::ProgressionOrderKind::LearnPerk:
            // The perk screen's own call, so the screen refreshes and plays its
            // sound as it would have. The result lands in the event, which is
            // why the digest, not a return value, reports what stuck.
            ok = perk != nullptr && learning != nullptr;
            if (ok) {
                SDK::wh::rpgmodule::C_RPGInventory::GetInstance()->ApplyInventoryPerk(soul, KCDC::Common::ToCryGuid(perk->id), true);
            }
            break;
        case Shared::RPC::ProgressionOrderKind::RemovePerk: ok = perk != nullptr && list->RemovePerk(KCDC::Common::ToCryGuid(perk->id)); break;
        case Shared::RPC::ProgressionOrderKind::Respec: ok = list->Respec(); break;
        case Shared::RPC::ProgressionOrderKind::AddPerkPoints:
        case Shared::RPC::ProgressionOrderKind::SetPerkPoints: {
            if (learning == nullptr) {
                ok = false;
                break;
            }
            const bool add = order.kind == Shared::RPC::ProgressionOrderKind::AddPerkPoints;
            if (order.track == Shared::RPC::kMainPerkTree) {
                learning->SetMainPoints(static_cast<std::uint16_t>(std::min<std::uint32_t>((add ? learning->GetMainPoints() : 0) + order.value, 0xFFFF)));
                break;
            }
            const Shared::Game::ProgressionTrack &track = Shared::Game::g_ProgressionTracks[order.track];
            if (track.kind == Shared::Game::ProgressionTrackKind::Skill) {
                learning->SetSkillPoints(track.native, static_cast<std::uint16_t>(std::min<std::uint32_t>((add ? learning->GetSkillPoints(track.native) : 0) + order.value, 0xFFFF)));
            }
            else {
                learning->SetStatPoints(track.native, static_cast<std::uint16_t>(std::min<std::uint32_t>((add ? learning->GetStatPoints(track.native) : 0) + order.value, 0xFFFF)));
            }
            break;
        }
        default: ok = false; break;
        }
        _applyingOrder = false;
        return ok;
    }

    bool PlayerProgressionService::ApplyXp(SDK::wh::rpgmodule::C_Soul *soul, std::uint8_t trackIndex, std::uint32_t xpQ7, bool scaled) {
        auto *globals = SDK::wh::C_GlobalEnvironment::GetInstance();
        auto *rpg     = globals != nullptr ? globals->GetRPGModule() : nullptr;
        auto *manager = rpg != nullptr ? rpg->GetEventManager() : nullptr;
        if (manager == nullptr) {
            return false;
        }

        const SDK::wh::framework::WUID wuid {soul->GetWuid()};
        SDK::wh::rpgmodule::C_Event *event = SDK::wh::rpgmodule::C_Event::CreateForSoul(*manager, wuid);
        if (event == nullptr) {
            return false;
        }

        // The creator attaches the effect to the event itself; the event owns
        // it from there.
        const Shared::Game::ProgressionTrack &track = Shared::Game::g_ProgressionTracks[trackIndex];
        const bool built = track.kind == Shared::Game::ProgressionTrackKind::Skill ? SDK::wh::rpgmodule::C_SkillXPEffect::Create(event, wuid, track.native, xpQ7, scaled) != nullptr : SDK::wh::rpgmodule::C_StatXPEffect::Create(event, wuid, track.native, xpQ7, scaled) != nullptr;
        const bool ok    = built && manager->ProcessEvent(event);
        event->Release();
        return ok;
    }

    bool PlayerProgressionService::ApplyExactXp(SDK::wh::rpgmodule::C_Soul *soul, std::uint8_t trackIndex, std::uint32_t totalQ7) {
        const auto &track   = Shared::Game::g_ProgressionTracks[trackIndex];
        auto *storage       = soul->GetActiveStateStorage();
        const auto &current = track.kind == Shared::Game::ProgressionTrackKind::Skill ? storage->GetSkillEntry(track.native) : storage->GetStatEntry(track.native);
        const auto before   = Shared::Game::TotalXpQ7(track, {current.level, current.xpQ7});
        const auto target   = Shared::Game::ProgressFromTotalXpQ7(track, totalQ7);
        auto *globals       = SDK::wh::C_GlobalEnvironment::GetInstance();
        auto *rpg           = globals != nullptr ? globals->GetRPGModule() : nullptr;
        auto *manager       = rpg != nullptr ? rpg->GetEventManager() : nullptr;
        if (manager == nullptr || totalQ7 <= before) {
            return false;
        }
        const SDK::wh::framework::WUID wuid {soul->GetWuid()};
        auto *event = SDK::wh::rpgmodule::C_Event::CreateForSoul(*manager, wuid);
        if (event == nullptr) {
            return false;
        }
        const bool skill  = track.kind == Shared::Game::ProgressionTrackKind::Skill;
        auto *skillEffect = skill ? SDK::wh::rpgmodule::C_SkillXPEffect::Create(event, wuid, track.native, 0, false) : nullptr;
        auto *statEffect  = !skill ? SDK::wh::rpgmodule::C_StatXPEffect::Create(event, wuid, track.native, 0, false) : nullptr;
        const bool ok     = (skillEffect != nullptr || statEffect != nullptr) && manager->ProcessEvent(event);
        if (ok) {
            // The unscaled stat creator still applies per-stat XP perks. Run
            // a zero gain to populate native before-values, then assign the
            // canonical record and finalize its deferred notification data.
            if (skillEffect != nullptr) {
                storage->SetSkillProgress(track.native, {target.level, target.xpQ7});
                const auto parent = skillEffect->GetInducedSkill();
                skillEffect->RecordExactGain(static_cast<std::uint32_t>(totalQ7 - before), target.level, parent < SDK::wh::rpgmodule::C_SoulStateStorage18070F3E8::kSkillCount ? storage->GetSkillEntry(parent).level : 0);
            }
            else {
                storage->SetStatProgress(track.native, {target.level, target.xpQ7});
                soul->GetPropertyEvents()->NotifyStatProgressChanged(track.native);
                statEffect->RecordExactGain(static_cast<std::uint32_t>(totalQ7 - before), target.level, static_cast<std::uint32_t>(soul->GetDerivedStat(SDK::wh::rpgmodule::E_SoulDerivedStat::MainLevel, nullptr)));
            }
        }
        event->Release();
        return ok;
    }

    void PlayerProgressionService::BuildDigest(const SDK::wh::rpgmodule::C_Soul *soul, Shared::RPC::ProgressionDigest &digest) const {
        const SDK::wh::rpgmodule::C_SoulStateStorage18070F3E8 *storage = soul->GetActiveStateStorage();
        const SDK::wh::rpgmodule::C_PerkList *list                     = storage->GetPerkList();
        const SDK::wh::rpgmodule::C_PerkLearningState *learning        = list->GetLearningState();

        digest.appliedOrder = _appliedOrder;
        for (std::size_t i = 0; i < Shared::Game::g_ProgressionTracks.size() && i < Shared::RPC::kProgressionTracks; ++i) {
            const Shared::Game::ProgressionTrack &track          = Shared::Game::g_ProgressionTracks[i];
            const bool skill                                     = track.kind == Shared::Game::ProgressionTrackKind::Skill;
            const SDK::wh::rpgmodule::S_SoulProgressEntry &entry = skill ? storage->GetSkillEntry(track.native) : storage->GetStatEntry(track.native);
            digest.tracks[i]                                     = {entry.level, entry.xpQ7};
            if (learning != nullptr) {
                digest.perkPoints[i] = skill ? learning->GetSkillPoints(track.native) : learning->GetStatPoints(track.native);
            }
        }
        if (learning != nullptr) {
            digest.perkPoints[Shared::RPC::kMainPerkTree] = learning->GetMainPoints();
        }

        const float mainLevel = soul->GetDerivedStat(SDK::wh::rpgmodule::E_SoulDerivedStat::MainLevel, nullptr);
        digest.mainLevel      = static_cast<std::uint8_t>(std::clamp(mainLevel, 0.0F, 255.0F));

        MarkOwnedPerks(list->ActivePerksBegin(), list->ActivePerksEnd(), digest.perks);
    }

    void PlayerProgressionService::Publish(const Shared::RPC::ProgressionDigest &digest) {
        _revision           = _revision + 1 == 0 ? 1 : _revision + 1;
        _published          = digest;
        _published.revision = _revision;

        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net != nullptr) {
            net->BroadcastRPC(_published, MafiaNet::Priority::Low, MafiaNet::Reliability::ReliableOrdered);
        }
    }

    void PlayerProgressionService::EmitLocalEvents(const Shared::RPC::ProgressionDigest &previous, const Shared::RPC::ProgressionDigest &current) const {
        for (std::size_t i = 0; i < Shared::Game::g_ProgressionTracks.size() && i < Shared::RPC::kProgressionTracks; ++i) {
            const Shared::Game::ProgressionTrack &track = Shared::Game::g_ProgressionTracks[i];
            const std::uint64_t before                  = Shared::Game::TotalXpQ7(track, previous.tracks[i]);
            const std::uint64_t after                   = Shared::Game::TotalXpQ7(track, current.tracks[i]);
            if (after > before) {
                KCDC::Scripting::Events::ProgressionXpGained(std::string(track.name), static_cast<double>(after - before) / 128.0);
            }
            if (current.tracks[i].level > previous.tracks[i].level) {
                KCDC::Scripting::Events::ProgressionLevelUp(std::string(track.name), current.tracks[i].level, current.perkPoints[i]);
            }
        }
        Shared::RPC::PerkSet::ForEachChange(previous.perks, current.perks, [](std::size_t index, bool owned) {
            const Shared::Game::PerkEntry *perk = Shared::Game::PerkAt(index);
            if (perk == nullptr) {
                return;
            }
            if (owned) {
                KCDC::Scripting::Events::ProgressionPerkAdded(std::string(perk->name));
            }
            else {
                KCDC::Scripting::Events::ProgressionPerkRemoved(std::string(perk->name));
            }
        });
    }

    void PlayerProgressionService::PublishRemotes() {
        RemotePerkRoster &next = _remotes.Next();
        next.count             = 0;
        for (const auto &[networkId, perks] : _remotePerks) {
            if (next.count >= RemotePerkRoster::kCapacity) {
                break;
            }
            next.networkIds[next.count] = networkId;
            next.perks[next.count]      = perks;
            ++next.count;
        }
        _remotes.Publish();
    }

    void PlayerProgressionService::Reset() {
        _localSoulWuid.store(0, std::memory_order_release);
        {
            const std::lock_guard<std::mutex> lock(_requestMutex);
            _withheldGains.clear();
            _learnRequests.clear();
        }
        _lastGainSendAt = 0;
        _pendingOrders.clear();
        _pendingRefusals.clear();
        _appliedOrder = 0;
        _nativeXp.store(true, std::memory_order_release);
        {
            const std::lock_guard<std::mutex> lock(_policyMutex);
            _blocked = {};
        }
        _digestDirty.store(true, std::memory_order_release);
        _awaitingDispatch.store(false, std::memory_order_release);
        _published     = {};
        _revision      = 0;
        _lastPublishAt = 0;
        _remotePerks.clear();
        _remotesDirty = false;
        _remotes.Reset([](RemotePerkRoster &roster) {
            roster.count = 0;
        });
    }
} // namespace KCDC::Features::Player

namespace KCDC::Features::Player {
    namespace {
        // The two XP effects' `Apply` (effect slot 3), run by
        // `C_Event::ApplyEffects` from `ProcessEvent`. A withheld gain returns
        // true: `ApplyEffects` only AND-reduces the results and rolls nothing
        // back, so the event's other effects are untouched, and an effect that
        // applied nothing is what the HUD and the perk-point listeners read
        // as "nothing happened".
        using SkillXPEffectApply_t = bool(__fastcall *)(SDK::wh::rpgmodule::C_SkillXPEffect *effect);
        using StatXPEffectApply_t  = bool(__fastcall *)(SDK::wh::rpgmodule::C_StatXPEffect *effect);

        // `C_RPGInventory` slot 38, 0x1811A0A64: the perk screen's confirmed
        // learn builds its event here. Dropped here, no event exists, so the
        // screen neither spends a point nor reports a failure.
        using ApplyInventoryPerk_t = void(__fastcall *)(SDK::wh::rpgmodule::C_RPGInventory *self, SDK::wh::rpgmodule::I_Soul *soul, const SDK::CryGUID *perk, bool learn);

        // The perk add and remove choke points, 0x18046B4A4 and 0x180A2043C,
        // and the listener slot that turns a gained level into perk points,
        // 0x1809DB228. Between them and the orders this client applies, they
        // are every way the local soul's progression changes, so the digest is
        // read only when one of them fired.
        using AddPerk_t    = bool(__fastcall *)(SDK::wh::rpgmodule::C_PerkList *list, const SDK::CryGUID *perk, SDK::wh::rpgmodule::C_Perk *sourcePerk);
        using RemovePerk_t = bool(__fastcall *)(SDK::wh::rpgmodule::C_PerkList *list, const SDK::CryGUID *perk);
        using OnEvent_t    = void(__fastcall *)(SDK::wh::rpgmodule::C_PerkList *list, SDK::wh::rpgmodule::C_Event *event);

        // C_Soul::HasPerk, primary vtable slot 115. Detoured at the body,
        // which every caller reaches through the vtable.
        using HasPerk_t = bool(__fastcall *)(SDK::wh::rpgmodule::I_Soul *soul, const SDK::CryGUID *perk, bool notifyUse);

        SkillXPEffectApply_t g_skillXPEffectApplyOriginal = nullptr;
        StatXPEffectApply_t g_statXPEffectApplyOriginal   = nullptr;
        ApplyInventoryPerk_t g_applyInventoryPerkOriginal = nullptr;
        AddPerk_t g_addPerkOriginal                       = nullptr;
        RemovePerk_t g_removePerkOriginal                 = nullptr;
        OnEvent_t g_perkListOnEventOriginal               = nullptr;
        HasPerk_t g_hasPerkOriginal                       = nullptr;

        bool __fastcall SkillXPEffectApply_Hook(SDK::wh::rpgmodule::C_SkillXPEffect *effect) {
            if (KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetPlayerProgressionService().WithholdsSkillGain(effect)) {
                return true;
            }
            return g_skillXPEffectApplyOriginal(effect);
        }

        bool __fastcall StatXPEffectApply_Hook(SDK::wh::rpgmodule::C_StatXPEffect *effect) {
            if (KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetPlayerProgressionService().WithholdsStatGain(effect)) {
                return true;
            }
            return g_statXPEffectApplyOriginal(effect);
        }

        void __fastcall ApplyInventoryPerk_Hook(SDK::wh::rpgmodule::C_RPGInventory *self, SDK::wh::rpgmodule::I_Soul *soul, const SDK::CryGUID *perk, bool learn) {
            if (learn && KCDC::Core::gApplication != nullptr && perk != nullptr && KCDC::Core::gApplication->GetPlayerProgressionService().RefusesPerkScreenLearn(soul, *perk)) {
                return;
            }
            g_applyInventoryPerkOriginal(self, soul, perk, learn);
        }

        bool __fastcall AddPerk_Hook(SDK::wh::rpgmodule::C_PerkList *list, const SDK::CryGUID *perk, SDK::wh::rpgmodule::C_Perk *sourcePerk) {
            if (KCDC::Core::gApplication == nullptr || perk == nullptr) {
                return g_addPerkOriginal(list, perk, sourcePerk);
            }
            auto &service = KCDC::Core::gApplication->GetPlayerProgressionService();
            if (service.RefusesAdd(list, *perk)) {
                return false;
            }
            const bool added = g_addPerkOriginal(list, perk, sourcePerk);
            if (added) {
                service.NotePerkListChanged(list);
            }
            return added;
        }

        bool __fastcall RemovePerk_Hook(SDK::wh::rpgmodule::C_PerkList *list, const SDK::CryGUID *perk) {
            const bool removed = g_removePerkOriginal(list, perk);
            if (removed && KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerProgressionService().NotePerkListChanged(list);
            }
            return removed;
        }

        // Runs for every event the RPG module dispatches, so the digest is only
        // marked when the banks actually moved.
        void __fastcall PerkListOnEvent_Hook(SDK::wh::rpgmodule::C_PerkList *list, SDK::wh::rpgmodule::C_Event *event) {
            const SDK::wh::rpgmodule::C_PerkLearningState *learning = list != nullptr ? list->GetLearningState() : nullptr;
            if (KCDC::Core::gApplication == nullptr || learning == nullptr) {
                g_perkListOnEventOriginal(list, event);
                return;
            }
            const auto before = learning->GetAllPoints();
            g_perkListOnEventOriginal(list, event);
            KCDC::Core::gApplication->GetPlayerProgressionService().NotePerkEventDispatched(list, learning->GetAllPoints() != before);
        }

        // What the perk screen draws a perk as (0x18119F604): 0 owned, 1
        // learnable, 2 no points, 3 locked, 4 blocked, which the game itself
        // answers for a perk its own rules block. A perk the server blocks
        // reads the same, rather than learnable until the press is refused.
        // The first argument is never read; the row is handed on as the
        // perk's GUID, its first member.
        constexpr std::uint8_t kPerkScreenOwned   = 0;
        constexpr std::uint8_t kPerkScreenBlocked = 4;

        std::uint8_t(__fastcall *g_perkScreenStateOriginal)(void *, const SDK::CryGUID *, SDK::wh::rpgmodule::I_Soul *) = nullptr;

        std::uint8_t __fastcall PerkScreenState_Hook(void *unused, const SDK::CryGUID *perk, SDK::wh::rpgmodule::I_Soul *soul) {
            const std::uint8_t state = g_perkScreenStateOriginal(unused, perk, soul);
            if (state != kPerkScreenOwned && perk != nullptr && KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetPlayerProgressionService().ShowsBlocked(soul, *perk)) {
                return kPerkScreenBlocked;
            }
            return state;
        }

        bool __fastcall HasPerk_Hook(SDK::wh::rpgmodule::I_Soul *soul, const SDK::CryGUID *perk, bool notifyUse) {
            bool owned = false;
            if (KCDC::Core::gApplication != nullptr && perk != nullptr && KCDC::Core::gApplication->GetPlayerProgressionService().AnswersRemotePerk(soul, *perk, owned)) {
                return owned;
            }
            return g_hasPerkOriginal(soul, perk, notifyUse);
        }
    } // namespace
} // namespace KCDC::Features::Player

static InitFunction initProgressionHooks(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_SkillXPEffect__Apply), reinterpret_cast<LPVOID>(&KCDC::Features::Player::SkillXPEffectApply_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_skillXPEffectApplyOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_StatXPEffect__Apply), reinterpret_cast<LPVOID>(&KCDC::Features::Player::StatXPEffectApply_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_statXPEffectApplyOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_RPGInventory__ApplyInventoryPerk), reinterpret_cast<LPVOID>(&KCDC::Features::Player::ApplyInventoryPerk_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_applyInventoryPerkOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_PerkList__AddPerk), reinterpret_cast<LPVOID>(&KCDC::Features::Player::AddPerk_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_addPerkOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_PerkList__RemovePerk), reinterpret_cast<LPVOID>(&KCDC::Features::Player::RemovePerk_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_removePerkOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_PerkList__OnEvent), reinterpret_cast<LPVOID>(&KCDC::Features::Player::PerkListOnEvent_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_perkListOnEventOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Soul__HasPerk), reinterpret_cast<LPVOID>(&KCDC::Features::Player::HasPerk_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_hasPerkOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.PerkScreenState), reinterpret_cast<LPVOID>(&KCDC::Features::Player::PerkScreenState_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::g_perkScreenStateOriginal));
    },
    "Progression");
