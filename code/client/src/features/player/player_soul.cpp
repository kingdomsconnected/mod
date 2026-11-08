#include <utils/safe_win32.h>

#include "features/horse/horse_service.h"
#include "features/player/player.h"
#include "features/player/player_service.h"

#include "core/application.h"
#include "common/module_support.h"

#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/action/i_game_object.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/rpgmodule/s_soul_query_context.h"
#include "shared/features/player/player_damage.h"
#include "shared/features/player/player_death.h"

#include <mafianet/get_time.h>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <utility>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>
#include <cstdint>
#include <cstring>

#include "features/buff/custom_buff_module.h"
#include "features/player/player_duel_service.h"
#include "features/player/player_missile_service.h"

#include "sdk/wh/rpgmodule/c_health_value_effect.h"

#include "common/replication/body_life.h"


namespace KCDC::Features::Player {

    std::array<Human::SoulBinding, Human::kSoulTableSize> Human::_soulBindings;
    std::atomic<std::size_t> Human::_soulBindingCount {0};
    thread_local Human::SoulActionScope *Human::_soulAction = nullptr;

    namespace {
        constexpr const char *kLogger = "Human";

        /** The four resources, as the native storage indexes them. */
        constexpr SDK::wh::rpgmodule::E_SoulState ResourceState(std::size_t index) {
            return static_cast<SDK::wh::rpgmodule::E_SoulState>(index);
        }

        // Resample interval for the parts with no change signal -- stats,
        // skills, conditions and the capacities derived from them -- and the
        // pace at which resource drift is published (see
        // `HumanSoulState::IsEdgeFrom`).
        constexpr MafiaNet::Time kSoulResampleIntervalMs = 250;

        /**
         * The engine calls this on whichever thread changed the property, and
         * only ever on a soul `PlayerService::ListenForLocalSoulChanges`
         * subscribed to -- which a body replica is what reaches, so the
         * application is up by the time this can be registered at all. The
         * guard is what a detour owes `gApplication` regardless; suppressing
         * the store before the application exists loses nothing, because the
         * flag is constructed set and nothing but the service's own capture
         * ever clears it.
         */
        void __fastcall OnLocalSoulProperty(void *, std::uint32_t) {
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetPlayerService().MarkLocalSoulDirty();
            }
        }
    } // namespace

    void PlayerService::ListenForLocalSoulChanges(SDK::wh::rpgmodule::C_Soul *nativeSoul) {
        const std::uint64_t wuid = nativeSoul != nullptr ? nativeSoul->GetWuid() : 0;
        if (nativeSoul == _listeningSoul && wuid == _listeningSoulWuid) {
            return;
        }
        _listeningSoul     = nativeSoul;
        _listeningSoulWuid = wuid;

        auto *notifier = nativeSoul != nullptr ? nativeSoul->GetPropertyEvents() : nullptr;
        if (notifier == nullptr) {
            return;
        }
        const SDK::wh::rpgmodule::S_SoulPropertyHandler handler {nullptr, &OnLocalSoulProperty};
        for (const auto property : {SDK::wh::rpgmodule::E_SoulProperty::Health, SDK::wh::rpgmodule::E_SoulProperty::Stamina, SDK::wh::rpgmodule::E_SoulProperty::Exhaust, SDK::wh::rpgmodule::E_SoulProperty::Hunger}) {
            notifier->AddPropertyListener(property, handler);
        }
        _localSoulDirty.store(true, std::memory_order_relaxed);
        Framework::Logging::GetLogger(kLogger)->info("Listening for resource changes on the local soul (wuid {})", wuid);
    }

    void PlayerService::MarkLocalSoulDirty() {
        _localSoulDirty.store(true, std::memory_order_relaxed);
    }

    const SDK::wh::rpgmodule::I_Soul *PlayerService::CapturedLocalSoul() const {
        return _localSoul.load(std::memory_order_relaxed);
    }

    void PlayerService::PublishCapturedLocalSoul(const SDK::wh::rpgmodule::I_Soul *nativeSoul) {
        _localSoul.store(nativeSoul, std::memory_order_relaxed);
    }

    bool PlayerService::ConsumeLocalSoulDirty() {
        return _localSoulDirty.exchange(false, std::memory_order_relaxed);
    }

    Human::SoulActionScope::SoulActionScope(Shared::Game::HumanSoulAction &action, SDK::wh::rpgmodule::I_Soul *source, SDK::wh::rpgmodule::I_Soul *target, const void *weapon, const void *ammo, const void *defense, bool replay)
        : _action(action)
        , _source(source)
        , _references {nullptr, source, target, weapon, ammo, defense}
        , _previous(_soulAction)
        , _replay(replay) {
        auto *storage = static_cast<SDK::wh::rpgmodule::C_Soul *>(source)->GetActiveStateStorage();
        for (std::size_t i = 0; i < _savedResources.size(); ++i) {
            _savedResources[i] = storage->GetState(ResourceState(i));
            if (replay) {
                // Raw: a notification here would announce a value that is
                // about to be taken away again.
                storage->SetReplicatedState(ResourceState(i), action.resources[i]);
            }
            else {
                action.resources[i] = _savedResources[i];
            }
        }
        _soulAction = this;
    }

    Human::SoulActionScope::~SoulActionScope() {
        _soulAction = _previous;
        if (!_replay) {
            return;
        }
        auto *storage = static_cast<SDK::wh::rpgmodule::C_Soul *>(_source)->GetActiveStateStorage();
        for (std::size_t i = 0; i < _savedResources.size(); ++i) {
            storage->SetReplicatedState(ResourceState(i), _savedResources[i]);
        }
    }

    Shared::Game::HumanSoulQuery Human::MakeSoulQuery(Shared::Game::HumanSoulQueryKind kind, std::uint32_t selector, float base, const SDK::wh::rpgmodule::S_SoulQueryContext *context) {
        Shared::Game::HumanSoulQuery query;
        query.kind       = kind;
        query.selector   = selector;
        query.base       = base;
        query.hasContext = context != nullptr;
        if (context != nullptr) {
            query.reason = context->reason;
            // Pointers become roles; an unknown address resolves past the
            // end of the table, which IsSane rejects.
            const std::array<const void *, 4> references {context->otherSoul, context->reference08, context->reference10, context->reference20};
            for (std::size_t i = 0; i < references.size(); ++i) {
                const auto found    = std::find(_soulAction->_references.begin(), _soulAction->_references.end(), references[i]);
                query.references[i] = static_cast<std::uint8_t>(found - _soulAction->_references.begin());
            }
        }
        if (!query.IsSane()) {
            _soulAction->_good = false;
        }
        return query;
    }

    bool Human::ReplaySoulQuery(const SDK::wh::rpgmodule::I_Soul *source, Shared::Game::HumanSoulQueryKind kind, std::uint32_t selector, float base, const SDK::wh::rpgmodule::S_SoulQueryContext *context, float &value) {
        if (_soulAction == nullptr || !_soulAction->_replay || _soulAction->_source != source) {
            return false;
        }
        const auto query = MakeSoulQuery(kind, selector, base, context);
        switch (_soulAction->_action.Find(query, value)) {
        case Shared::Game::HumanSoulAction::Lookup::Recorded: return true;

        case Shared::Game::HumanSoulAction::Lookup::Unmodified:
            // Replay zero attacker contribution while retaining locally
            // evaluated victim armor.
            value = base;
            return true;

        case Shared::Game::HumanSoulAction::Lookup::Rebased:
        case Shared::Game::HumanSoulAction::Lookup::Missing: break;
        }

        if (_soulAction->_good) {
            ReportMissingSoulQuery(query);
        }
        _soulAction->_good = false;
        value              = base;
        return true;
    }

    // Prints the near misses beside the wanted key: which field diverged
    // decides whether the fix belongs in the key or in the calculation.
    void Human::ReportMissingSoulQuery(const Shared::Game::HumanSoulQuery &wanted) {
        const auto &action  = _soulAction->_action;
        auto logger         = Framework::Logging::GetLogger(kLogger);
        const auto describe = [](const Shared::Game::HumanSoulQuery &query) {
            return fmt::format("base={} context={} reason={} refs=[{},{},{},{}]", query.base, query.hasContext, query.reason, query.references[0], query.references[1], query.references[2], query.references[3]);
        };

        logger->warn("Missing missile soul query: kind={} selector={} {}", static_cast<std::uint8_t>(wanted.kind), wanted.selector, describe(wanted));
        std::size_t matches = 0;
        for (std::size_t i = 0; i < action.count; ++i) {
            const auto &captured = action.queries[i];
            if (captured.kind != wanted.kind || captured.selector != wanted.selector) {
                continue;
            }
            ++matches;
            logger->warn("  attacker recorded the same kind/selector with {} result={} identity={}", describe(captured), captured.result, captured.IsIdentity());
        }
        if (matches == 0) {
            logger->warn("  attacker recorded no query of that kind/selector at all, out of {} captured", action.count);
        }
        else {
            logger->warn("  the attacker transformed its own input, so its answer cannot be moved to this one");
        }
    }

    void Human::CaptureSoulQuery(const SDK::wh::rpgmodule::I_Soul *source, Shared::Game::HumanSoulQueryKind kind, std::uint32_t selector, float base, const SDK::wh::rpgmodule::S_SoulQueryContext *context, float value) {
        if (_soulAction == nullptr || _soulAction->_replay || _soulAction->_source != source) {
            return;
        }
        auto query         = MakeSoulQuery(kind, selector, base, context);
        query.result       = value;
        _soulAction->_good = _soulAction->_action.Add(query) && _soulAction->_good;
    }

    Human::~Human() {
        // DeallocReplica hands the body to PlayerService and deletes without
        // calling Despawn, so this is the one place that covers every path out.
        ReleaseSpawnTicket();
        UnbindSoul();
        if (KCDC::Core::gApplication != nullptr) {
            KCDC::Core::gApplication->GetPlayerProgressionService().ForgetRemote(GetNetworkID());
        }
    }

    bool Human::BindSoul(SDK::wh::rpgmodule::I_Soul *nativeSoul) {
        // Walk the probe chain to its end: a slot already holding this soul
        // (an address the engine reused) is taken over, otherwise the first
        // retired slot passed, otherwise the empty one that ends the chain.
        SoulBinding *target = nullptr;
        bool replacing      = false;
        const std::size_t start = SoulSlot(nativeSoul);
        for (std::size_t probe = 0; probe < kSoulTableSize; ++probe) {
            SoulBinding &slot = _soulBindings[(start + probe) & (kSoulTableSize - 1)];
            const auto *bound = slot.soul.load(std::memory_order_relaxed);
            if (bound == nativeSoul) {
                target    = &slot;
                replacing = true;
                break;
            }
            if (bound == RetiredSoul() && target == nullptr) {
                target = &slot;
                continue;
            }
            if (bound == nullptr) {
                if (target == nullptr) {
                    target = &slot;
                }
                break;
            }
        }
        if (target == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("No free soul binding for '{}' (networkId {}); all {} slots are in use", nickname, GetNetworkID(), kSoulTableSize);
            return false;
        }

        // Retire the slot first when it changes owner, so a reader in between
        // sees no binding rather than the new owner under the old soul.
        if (replacing) {
            target->soul.store(RetiredSoul(), std::memory_order_release);
        }
        else {
            _soulBindingCount.fetch_add(1, std::memory_order_relaxed);
        }
        target->human.store(this, std::memory_order_relaxed);
        target->soul.store(nativeSoul, std::memory_order_release);
        _boundBinding = target;
        KCDC::Common::Replication::BodyLife::Get().Bind(static_cast<const SDK::wh::rpgmodule::C_Soul *>(nativeSoul), this);
        return true;
    }

    void Human::UnbindSoul() {
        KCDC::Common::Replication::BodyLife::Get().Unbind(this);
        _deathHandled    = false;
        _deathStateDirty = true;
        _knockedOut      = false;
        // Retire the slot only while it still belongs to this body.
        if (_boundBinding != nullptr && _boundBinding->human.load(std::memory_order_relaxed) == this) {
            _boundBinding->soul.store(RetiredSoul(), std::memory_order_release);
            _soulBindingCount.fetch_sub(1, std::memory_order_relaxed);
        }
        _boundBinding  = nullptr;
        _boundSoul     = nullptr;
        _boundSoulWuid = 0;
        _appliedSoul   = {};
        _applyingSoul  = false;
    }

    bool Human::IsSoulBindingLive() const {
        if (_boundSoul == nullptr || IsOwner() || _boundOwner != ownerGUID || _boundEpoch != stateEpoch) {
            return false;
        }
        auto *actor = ResolveActor();
        return actor != nullptr && actor->GetSoul() == _boundSoul && static_cast<const SDK::wh::rpgmodule::C_Soul *>(_boundSoul)->GetWuid() == _boundSoulWuid;
    }

    Human *Human::FindRemoteSoul(const SDK::wh::rpgmodule::I_Soul *nativeSoul) {
        if (nativeSoul == nullptr || _soulBindingCount.load(std::memory_order_relaxed) == 0) {
            return nullptr;
        }
        const std::size_t start = SoulSlot(nativeSoul);
        for (std::size_t probe = 0; probe < kSoulTableSize; ++probe) {
            SoulBinding &binding = _soulBindings[(start + probe) & (kSoulTableSize - 1)];
            const auto *bound    = binding.soul.load(std::memory_order_acquire);
            if (bound == nullptr) {
                return nullptr;
            }
            if (bound != nativeSoul) {
                continue;
            }
            // Answered only while the owner still holds this very soul: the
            // slot may have changed hands between the two loads. A binding
            // whose body no longer carries the soul is stale, which the game
            // thread retires on its next apply; a hook on another thread
            // only declines it.
            Human *human = binding.human.load(std::memory_order_relaxed);
            if (human != nullptr && human->_boundBinding == &binding && human->_boundSoul == nativeSoul && human->IsSoulBindingLive()) {
                return human;
            }
            return nullptr;
        }
        return nullptr;
    }

    Human *Human::FindRemoteSoulWuid(std::uint64_t soulWuid) {
        if (soulWuid == 0 || _soulBindingCount.load(std::memory_order_relaxed) == 0) {
            return nullptr;
        }
        for (SoulBinding &binding : _soulBindings) {
            const auto *bound = binding.soul.load(std::memory_order_acquire);
            if (bound == nullptr || bound == RetiredSoul()) {
                continue;
            }
            Human *human = binding.human.load(std::memory_order_relaxed);
            if (human != nullptr && human->_boundBinding == &binding && human->_boundSoulWuid == soulWuid && human->IsSoulBindingLive()) {
                return human;
            }
        }
        return nullptr;
    }

    bool Human::HasAppliedSoulState() const {
        return _appliedSoul.valid && soul.valid && _appliedSoul.incarnation == soul.incarnation && _appliedSoul.revision == soul.revision && IsSoulBindingLive();
    }

    bool Human::TryGetRemoteState(const SDK::wh::rpgmodule::C_SoulStateStorage18070F3E8 *storage, std::uint32_t state, float &value) {
        if (state >= Shared::Game::HumanSoulState::kResourceCount) {
            return false;
        }
        auto *nativeSoul = storage->GetSoul();
        auto *human      = FindRemoteSoul(nativeSoul);
        // The backup storage shares its owner with the active one, so the
        // identity check alone is not enough to tell them apart.
        if (human == nullptr || storage != nativeSoul->GetActiveStateStorage() || !human->_appliedSoul.valid) {
            return false;
        }
        value = human->_appliedSoul.resources[state].current;
        return true;
    }

    // Override final query results; baseline overrides would apply native
    // modifiers twice.
    bool Human::TryGetSoulQuery(Shared::Game::HumanSoulQueryKind kind, std::uint32_t selector, float &value) const {
        if (!_appliedSoul.valid) {
            return false;
        }
        if ((kind == Shared::Game::HumanSoulQueryKind::State || kind == Shared::Game::HumanSoulQueryKind::StateMax) && selector < _appliedSoul.resources.size()) {
            value = kind == Shared::Game::HumanSoulQueryKind::State ? _appliedSoul.resources[selector].current : _appliedSoul.resources[selector].maximum;
            return true;
        }
        if ((kind == Shared::Game::HumanSoulQueryKind::Stat || kind == Shared::Game::HumanSoulQueryKind::RelativeStat) && selector < _appliedSoul.stats.size()) {
            value = kind == Shared::Game::HumanSoulQueryKind::Stat ? _appliedSoul.stats[selector] : _appliedSoul.relativeStats[selector];
            return true;
        }
        if (kind == Shared::Game::HumanSoulQueryKind::Derived) {
            // GetStateMax(Stamina) calls MaxStamina; both use the same
            // replicated field.
            if (selector == SDK::wh::rpgmodule::E_SoulDerivedStat::MaxStamina) {
                value = _appliedSoul.resources[Shared::Game::HumanSoulState::kStamina].maximum;
                return true;
            }
            if (selector == SDK::wh::rpgmodule::E_SoulDerivedStat::MaxHealthyStamina) {
                value = _appliedSoul.healthyStamina;
                return true;
            }
            for (std::size_t i = 0; i < Shared::Game::HumanSoulState::kConditionSelectors.size(); ++i) {
                if (selector == Shared::Game::HumanSoulState::kConditionSelectors[i]) {
                    value = _appliedSoul.conditions[i];
                    return true;
                }
            }
        }
        if (kind == Shared::Game::HumanSoulQueryKind::Skill || kind == Shared::Game::HumanSoulQueryKind::RelativeSkill) {
            for (std::size_t i = 0; i < Shared::Game::HumanSoulState::kSkillSelectors.size(); ++i) {
                if (selector == Shared::Game::HumanSoulState::kSkillSelectors[i]) {
                    value = kind == Shared::Game::HumanSoulQueryKind::Skill ? _appliedSoul.skills[i] : _appliedSoul.relativeSkills[i];
                    return true;
                }
            }
        }
        return false;
    }

    Human *Human::FindCapturedLocal() {
        auto *replication = Framework::CoreModules::GetReplication();
        Human *local      = nullptr;
        if (replication != nullptr) {
            replication->ForEach<Human>([&](Human *human) {
                if (human->HasCapturedLocalSoul()) {
                    local = human;
                }
            });
        }
        return local;
    }

    bool Human::HasCapturedLocalSoul() const {
        auto *framework  = SDK::IGameFramework::GetInstance();
        auto *actor      = static_cast<SDK::wh::entitymodule::C_Actor *>(framework != nullptr ? framework->GetClientActor() : nullptr);
        auto *nativeSoul = static_cast<SDK::wh::rpgmodule::C_Soul *>(actor != nullptr ? actor->GetSoul() : nullptr);
        return IsOwner() && soul.valid && nativeSoul != nullptr && actor->GetEntityId() == _captureActorId && nativeSoul->GetWuid() == _captureSoulWuid;
    }

    void Human::CaptureSoulState() {
        auto *framework  = SDK::IGameFramework::GetInstance();
        auto *actor      = static_cast<SDK::wh::entitymodule::C_Actor *>(framework != nullptr ? framework->GetClientActor() : nullptr);
        auto *nativeSoul = static_cast<SDK::wh::rpgmodule::C_Soul *>(actor != nullptr ? actor->GetSoul() : nullptr);
        auto *object     = actor != nullptr ? actor->GetGameObject() : nullptr;

        auto &players = KCDC::Core::gApplication->GetPlayerService();

        // The resources announce themselves; the rest of the snapshot raises
        // no signal and rides the interval.
        players.ListenForLocalSoulChanges(nativeSoul);

        // The resources signal every frame they move, which while sprinting
        // or fighting is every frame. Each change is looked at as it comes,
        // so an edge leaves at once; drift is held for the resample, so the
        // reliable channel is not re-sent the whole soul at frame rate.
        const bool bodyChanged = nativeSoul != players.CapturedLocalSoul();
        const auto now         = MafiaNet::GetTime();
        _soulChangePending     = players.ConsumeLocalSoulDirty() || _soulChangePending;
        const bool resampleDue = now >= _soulResampleAt;
        if (!bodyChanged && !_soulChangePending && !resampleDue) {
            return;
        }
        players.PublishCapturedLocalSoul(nativeSoul);

        Shared::Game::HumanSoulState candidate;
        candidate.incarnation = soul.incarnation;
        candidate.revision    = soul.revision;

        if (nativeSoul != nullptr && object != nullptr) {
            if (_captureActorId != actor->GetEntityId() || _captureSoulWuid != nativeSoul->GetWuid()) {
                _captureActorId       = actor->GetEntityId();
                _captureSoulWuid      = nativeSoul->GetWuid();
                candidate.incarnation = Shared::Game::HumanSoulState::Advance(candidate.incarnation);

                // The look-as layer belonged to the soul that went with it.
                _appliedAppearance = {};
                _appearanceDirty   = true;
                _appearanceRefused = false;
            }
            candidate.valid = true;

            // Preserve the unset physics-profile sentinel when no profile
            // manager exists.
            const std::uint8_t profile = object->GetAspectProfile(SDK::EEntityAspects::Physics);
            candidate.physicsProfile   = profile <= Shared::Game::HumanSoulState::kMaxPhysicsProfile ? profile : Shared::Game::HumanSoulState::kUnknownPhysicsProfile;

            for (std::size_t i = 0; i < candidate.resources.size(); ++i) {
                candidate.resources[i] = {nativeSoul->GetState(ResourceState(i)), nativeSoul->GetStateMax(ResourceState(i))};
            }
            candidate.healthyStamina = nativeSoul->GetDerivedStatValue(SDK::wh::rpgmodule::E_SoulDerivedStat::MaxHealthyStamina);
            for (std::size_t i = 0; i < candidate.conditions.size(); ++i) {
                candidate.conditions[i] = nativeSoul->GetDerivedStatValue(static_cast<SDK::wh::rpgmodule::E_SoulDerivedStat::Type>(Shared::Game::HumanSoulState::kConditionSelectors[i]));
            }
            // Script readings gate nothing, so they keep the resample pace
            // rather than the per-frame pace resource changes can wake this at.
            if (bodyChanged || resampleDue) {
                for (std::size_t i = 0; i < statReadings.values.size(); ++i) {
                    const auto stat        = static_cast<SDK::wh::rpgmodule::E_SoulDerivedStat::Type>(Shared::Game::kDerivedPlayerStats[Shared::Game::kPlayerConditionCount + i].nativeId);
                    statReadings.values[i] = Shared::Game::PlayerStatReadings::Quantize(nativeSoul->GetDerivedStatValue(stat));
                }
            }
            for (std::uint32_t i = 0; i < candidate.stats.size(); ++i) {
                candidate.stats[i]         = nativeSoul->GetStat(i, nullptr);
                candidate.relativeStats[i] = nativeSoul->GetRelativeStat(i, nullptr);
            }
            for (std::size_t i = 0; i < candidate.skills.size(); ++i) {
                candidate.skills[i]         = nativeSoul->GetSkill(Shared::Game::HumanSoulState::kSkillSelectors[i], nullptr);
                candidate.relativeSkills[i] = nativeSoul->GetRelativeSkill(Shared::Game::HumanSoulState::kSkillSelectors[i], nullptr);
            }
        }
        else {
            // Clear capture identity and pose so the next actor starts a new
            // incarnation.
            _captureActorId  = 0;
            _captureSoulWuid = 0;
            transformReady   = false;
        }

        if (candidate == soul) {
            _soulChangePending = false;
            if (resampleDue) {
                _soulResampleAt = now + kSoulResampleIntervalMs;
            }
            return;
        }
        if (!bodyChanged && !resampleDue && !candidate.IsEdgeFrom(soul)) {
            return;
        }
        _soulChangePending = false;
        _soulResampleAt    = now + kSoulResampleIntervalMs;
        candidate.revision = Shared::Game::HumanSoulState::Advance(candidate.revision);
        if (!candidate.IsSane()) {
            // Withdraw invalid snapshots while preserving stream counters.
            Framework::Logging::GetLogger(kLogger)->warn("Local soul snapshot rejected before publishing; withdrawing body state");
            const auto incarnation = candidate.incarnation;
            const auto revision    = candidate.revision;
            candidate              = {};
            candidate.incarnation  = incarnation;
            candidate.revision     = revision;
        }
        soul = candidate;
    }

    bool Human::ApplySoulState(SDK::wh::entitymodule::C_Actor *actor) {
        auto *nativeSoul = static_cast<SDK::wh::rpgmodule::C_Soul *>(actor != nullptr ? actor->GetSoul() : nullptr);
        auto *object     = actor != nullptr ? actor->GetGameObject() : nullptr;
        if (nativeSoul == nullptr || object == nullptr || !soul.valid) {
            UnbindSoul();
            return false;
        }

        // A binding made for another soul, owner or epoch is stale; it is
        // retired here, on the game thread, and never by a hook that finds it.
        if (_boundSoul != nativeSoul || _boundSoulWuid != nativeSoul->GetWuid() || _boundEpoch != stateEpoch || _boundOwner != ownerGUID) {
            UnbindSoul();
            if (!BindSoul(nativeSoul)) {
                return false;
            }
            _boundSoul     = nativeSoul;
            _boundSoulWuid = nativeSoul->GetWuid();
            _boundEpoch    = stateEpoch;
            _boundOwner    = ownerGUID;
        }

        auto *storage = nativeSoul->GetActiveStateStorage();
        std::array<bool, Shared::Game::HumanSoulState::kResourceCount> changed {};
        const auto previous = _appliedSoul;

        _applyingSoul = true;
        _appliedSoul  = soul;

        // Write all resources before notifying listeners.
        for (std::size_t i = 0; i < changed.size(); ++i) {
            changed[i] = !previous.valid || previous.resources[i] != soul.resources[i] || storage->GetState(ResourceState(i)) != soul.resources[i].current;
            storage->SetReplicatedState(ResourceState(i), soul.resources[i].current);
        }

        // SetMaxHealth also writes current health; set the maximum before the
        // current value.
        const auto &health = soul.resources[Shared::Game::HumanSoulState::kHealth];
        if (actor->GetMaxHealth() != health.maximum) {
            actor->SetMaxHealth(health.maximum);
        }
        if (actor->GetHealth() != health.current) {
            actor->SetHealth(health.current);
        }

        for (std::size_t i = 0; i < changed.size(); ++i) {
            if (changed[i]) {
                nativeSoul->GetPropertyEvents()->NotifyStateChanged(ResourceState(i));
            }
        }

        // These two reach the soul through the query override, so no buff
        // write raises their change here. The body's condition controller
        // listens for exactly these (0x1808ABAB0) to pick the drunk walk and,
        // when poisoned, the injured one.
        for (const std::size_t i : {Shared::Game::HumanSoulState::kDrunkenness, Shared::Game::HumanSoulState::kPoisoning}) {
            if (!previous.valid || previous.conditions[i] != soul.conditions[i]) {
                nativeSoul->GetPropertyEvents()->NotifyDerivedStatChanged(static_cast<SDK::wh::rpgmodule::E_SoulDerivedStat::Type>(Shared::Game::HumanSoulState::kConditionSelectors[i]));
            }
        }

        _applyingSoul = false;
        return true;
    }

    void Human::ApplyDeathState(SDK::wh::entitymodule::C_Actor *actor) {
        if (!_deathStateDirty) {
            return;
        }
        const bool dead = _appliedSoul.resources[Shared::Game::HumanSoulState::kHealth].current <= 0.0F;

        // The game has no death in the saddle: the ride ends first, the way
        // a living rider's does, and the death stays owed until the rider
        // action's Exit has released the seat -- the same wait the engine's
        // own dispatch is held for, see HorseService::HoldDeathInSaddle.
        if (dead && !_deathHandled && IsSeated()) {
            if (!_forcedOffSaddle) {
                ForceOffSaddle(actor);
            }
            return;
        }

        _deathStateDirty = false;
        if (dead && !_deathHandled) {
            _deathHandled = true;
            for (auto &itemWuid : _appliedHandWuid) {
                itemWuid = 0;
            }
            _weaponActionAt = 0;
            WithdrawLocomotion();
            KCDC::Common::Replication::BodyLife::Get().Kill(actor);
        }
        else if (!dead && _deathHandled) {
            // Nobody carries the living: whoever holds this body lets go
            // before it gets up, rather than carrying it standing.
            KCDC::Core::gApplication->GetPlayerService().ReleaseCarriesOf(actor->GetEntityId());

            _applyingSoul = true;
            actor->Revive(SDK::wh::entitymodule::C_Actor::eReviveReason_Script);
            _applyingSoul   = false;
            _deathHandled   = false;
            _boundCharacter = nullptr;
            RefreshAnimationBinding(actor);
        }
    }

    Shared::RPC::PlayerDied Human::CaptureDeathReport() {
        Shared::RPC::PlayerDied report;
        report.valid = HasCapturedLocalSoul();
        if (!report.valid) {
            return report;
        }

        auto *nativeSoul = KCDC::Common::LocalSoul();
        const float health = nativeSoul->GetActiveStateStorage()->GetState(SDK::wh::rpgmodule::E_SoulState::Health);
        report.valid = std::isfinite(health) && health <= 0.0F;
        if (!report.valid) {
            return report;
        }
        soul.resources[Shared::Game::HumanSoulState::kHealth].current = health;
        soul.revision = Shared::Game::HumanSoulState::Advance(soul.revision);

        report.networkId     = GetNetworkID();
        report.incarnation   = soul.incarnation;
        report.deathRevision = soul.revision;
        report.epoch         = stateEpoch;
        return report;
    }

    void Human::SetResource(const Shared::RPC::SetPlayerResource &request) {
        if (!IsOwner() || !HasCapturedLocalSoul() || !request.AppliesTo(GetNetworkID(), stateEpoch, soul)) {
            return;
        }
        auto *nativeSoul = KCDC::Common::LocalSoul();
        if (nativeSoul == nullptr || nativeSoul->IsDead()) {
            return;
        }
        const auto state    = ResourceState(request.resource);
        const float maximum = nativeSoul->GetStateMax(state);
        if (!std::isfinite(maximum) || maximum <= 0.0F) {
            return;
        }
        // A kill is a blow (`BodyLife`). Health written to zero declares the
        // death with no blow for the body to react to: no death fragment, so
        // none of the Ragdoll clip it carries, and the body stays where it
        // died -- hanging from a ladder's rung. Taken through the game's own
        // damage primitive, as a ruled lethal blow is, it falls.
        if (state == SDK::wh::rpgmodule::E_SoulState::Health && request.value == 0.0F) {
            KCDC::Common::Replication::BodyLife::Get().ApplyBlow(KCDC::Common::LocalHumanActor(), 0, nativeSoul->GetState(state), static_cast<std::uint8_t>(Shared::Game::DamageReason::Scripted));
        }
        else {
            // C_Soul::SetState (0x1809DC648) clamps to [0, GetStateMax] itself.
            nativeSoul->SetState(state, request.value);
        }
        _soulResampleAt = 0;
    }

    void Human::Heal(float health) {
        if (!IsOwner() || !HasCapturedLocalSoul() || !std::isfinite(health)) {
            return;
        }
        auto *nativeSoul = KCDC::Common::LocalSoul();
        // A dead body is revived by the server's own permission, not healed.
        if (nativeSoul == nullptr || nativeSoul->IsDead()) {
            return;
        }
        const float maximum = nativeSoul->GetStateMax(SDK::wh::rpgmodule::E_SoulState::Health);
        const float wanted  = health < 0.0F ? maximum : std::min(health, maximum);
        if (!std::isfinite(wanted) || wanted <= nativeSoul->GetActiveStateStorage()->GetState(SDK::wh::rpgmodule::E_SoulState::Health)) {
            return;
        }
        // I_Soul::SetState, the step the game's own full heal takes: it raises
        // the change on the RPG bus, so everything that listens for a health
        // change hears this one too.
        nativeSoul->SetState(SDK::wh::rpgmodule::E_SoulState::Health, wanted);
    }

    void Human::NativeRevived() {
        soul.incarnation = Shared::Game::HumanSoulState::Advance(soul.incarnation);
        _soulResampleAt  = 0;
        KCDC::Core::gApplication->GetPlayerService().ForgetLocalCombat();
    }

    void Human::Respawn(const Shared::RPC::RespawnPlayer &permission) {
        if (!HasCapturedLocalSoul() || !Shared::Game::AcceptsRespawn(permission, GetNetworkID(), stateEpoch, soul)) {
            return;
        }
        auto *actor      = KCDC::Common::LocalHumanActor();
        auto *nativeSoul = static_cast<SDK::wh::rpgmodule::C_Soul *>(actor != nullptr ? actor->GetSoul() : nullptr);
        if (nativeSoul == nullptr || !nativeSoul->IsDead() || !actor->IsSettled()) {
            return;
        }
        const float health = nativeSoul->GetStateMax(SDK::wh::rpgmodule::E_SoulState::Health);
        if (!std::isfinite(health) || health <= 0.0F) {
            return;
        }
        // The native storage writer resets RPG death bookkeeping before Revive.
        auto *storage = nativeSoul->GetActiveStateStorage();

        // Off whichever shoulder this body is on here, before it gets up.
        KCDC::Core::gApplication->GetPlayerService().ReleaseCarriesOf(actor->GetEntityId());

        storage->SetState(SDK::wh::rpgmodule::E_SoulState::Health, health, nullptr);

        actor->Revive(SDK::wh::entitymodule::C_Actor::eReviveReason_Script);
        storage->SetState(SDK::wh::rpgmodule::E_SoulState::Stamina, nativeSoul->GetStateMax(SDK::wh::rpgmodule::E_SoulState::Stamina), nullptr);
        CaptureSoulState();
    }
} // namespace KCDC::Features::Player

namespace KCDC::Features::Player {
    namespace {
        using StateFn         = float(__fastcall *)(const SDK::wh::rpgmodule::I_Soul *, SDK::wh::rpgmodule::E_SoulState);
        using QueryFn         = float(__fastcall *)(const SDK::wh::rpgmodule::I_Soul *, std::uint32_t, const SDK::wh::rpgmodule::S_SoulQueryContext *);
        using ModifierFn      = float(__fastcall *)(const SDK::wh::rpgmodule::I_Soul *, std::uint32_t, float, const SDK::wh::rpgmodule::S_SoulQueryContext *);
        using SetFn           = float(__fastcall *)(SDK::wh::rpgmodule::C_SoulStateStorage18070F3E8 *, SDK::wh::rpgmodule::E_SoulState, float, const SDK::wh::rpgmodule::C_SoulStateEffect *);
        using CopyFn          = void(__fastcall *)(SDK::wh::rpgmodule::C_SoulStateStorage18070F3E8 *, const SDK::wh::rpgmodule::C_SoulStateStorage18070F3E8 *);
        SetFn s_set           = nullptr;
        CopyFn s_copy         = nullptr;
        ModifierFn s_modifier = nullptr;

        // Replay captured attack queries first; otherwise override only
        // context-free queries.
        bool Override(const SDK::wh::rpgmodule::I_Soul *soul, KCDC::Shared::Game::HumanSoulQueryKind kind, std::uint32_t selector, float base, const SDK::wh::rpgmodule::S_SoulQueryContext *context, float &value) {
            if (KCDC::Core::gApplication == nullptr) {
                return false;
            }
            if (KCDC::Features::Player::Human::ReplaySoulQuery(soul, kind, selector, base, context, value)) {
                return true;
            }
            auto *human = KCDC::Features::Player::Human::FindRemoteSoul(soul);
            return human != nullptr && context == nullptr && human->TryGetSoulQuery(kind, selector, value);
        }

        // Bound copies retain replicated resources without running native
        // death bookkeeping.
        //
        // Every event-driven health change of the local body lands here too,
        // with the damage effect as its cause, which is the one place where
        // what a blow took, who dealt it and where it landed are all known.
        // Reported before the original runs: the write that kills declares
        // the death from inside it.
        float __fastcall SetState(SDK::wh::rpgmodule::C_SoulStateStorage18070F3E8 *storage, SDK::wh::rpgmodule::E_SoulState state, float value, const SDK::wh::rpgmodule::C_SoulStateEffect *cause) {
            if (KCDC::Core::gApplication == nullptr) {
                return s_set(storage, state, value, cause);
            }
            // The main-menu builder has no server-owned player. Protect its
            // local body before a lethal write can run death bookkeeping.
            if (state == SDK::wh::rpgmodule::E_SoulState::Health && KCDC::Core::gApplication->IsOfflineBuilderReady() && storage->GetSoul() == KCDC::Common::LocalSoul()) {
                const float current = storage->GetState(state);
                if (value < current) {
                    return current;
                }
            }
            // First, ahead of the pins below: a blow between two players is
            // the combat referee's, and what the engine would have taken is
            // what it reports -- off the local body, or off a copy the local
            // player struck. Only the local body's health is withheld here;
            // a copy's is pinned below as always.
            if (KCDC::Core::gApplication->GetPlayerDuelService().ScreenSoulWrite(storage, state, value, cause)) {
                return storage->GetState(state);
            }
            if (KCDC::Features::Player::Human::TryGetRemoteState(storage, static_cast<std::uint32_t>(state), value)) {
                return value;
            }
            // Any other replicated body: only the copy that decides its life
            // keeps the write.
            if (KCDC::Common::Replication::BodyLife::Get().FilterHealthWrite(storage, state, value, cause)) {
                return storage->GetState(state);
            }
            if (const auto *damage = SDK::wh::rpgmodule::C_HealthValueEffect::FromCause(state, cause)) {
                const float current = storage->GetState(state);
                if (value < current && storage->GetSoul() == KCDC::Core::gApplication->GetPlayerService().CapturedLocalSoul()) {
                    KCDC::Core::gApplication->GetLocalEvents().LocalBodyDamaged(damage->GetAttackerSoulWuid(), damage->GetHitSubpart(), damage->GetDeathReason(), current - value, value <= 0.0F);
                }
            }
            return s_set(storage, state, value, cause);
        }

        // Preserve replicated resources when restoring backup soul storage.
        void __fastcall CopyFrom(SDK::wh::rpgmodule::C_SoulStateStorage18070F3E8 *storage, const SDK::wh::rpgmodule::C_SoulStateStorage18070F3E8 *source) {
            float value = 0.0F;
            if (KCDC::Core::gApplication == nullptr || !KCDC::Features::Player::Human::TryGetRemoteState(storage, KCDC::Shared::Game::HumanSoulState::kHealth, value)) {
                s_copy(storage, source);
                return;
            }
            // A byte copy, not a C++ one: the storage embeds the perk list, whose
            // vtable must stay the game's, and no native container destructor or
            // allocator may cross the module boundary.
            alignas(SDK::wh::rpgmodule::C_SoulStateStorage18070F3E8) std::byte bytes[sizeof(SDK::wh::rpgmodule::C_SoulStateStorage18070F3E8)];
            std::memcpy(bytes, source, sizeof(bytes));
            auto *held = reinterpret_cast<SDK::wh::rpgmodule::C_SoulStateStorage18070F3E8 *>(bytes);
            for (std::uint32_t i = 0; i < KCDC::Shared::Game::HumanSoulState::kResourceCount; ++i) {
                KCDC::Features::Player::Human::TryGetRemoteState(storage, i, value);
                held->SetReplicatedState(static_cast<SDK::wh::rpgmodule::E_SoulState>(i), value);
            }
            s_copy(storage, held);
        }

        StateFn s_GetState = nullptr;
        float __fastcall GetState(const SDK::wh::rpgmodule::I_Soul *soul, SDK::wh::rpgmodule::E_SoulState state) {
            const auto selector = static_cast<std::uint32_t>(state);
            float value         = 0.0F;
            if (Override(soul, KCDC::Shared::Game::HumanSoulQueryKind::State, selector, 0.0F, nullptr, value)) {
                return value;
            }
            value = s_GetState(soul, state);
            KCDC::Features::Player::Human::CaptureSoulQuery(soul, KCDC::Shared::Game::HumanSoulQueryKind::State, selector, 0.0F, nullptr, value);
            return value;
        }

        StateFn s_GetStateMax = nullptr;
        float __fastcall GetStateMax(const SDK::wh::rpgmodule::I_Soul *soul, SDK::wh::rpgmodule::E_SoulState state) {
            const auto selector = static_cast<std::uint32_t>(state);
            float value         = 0.0F;
            if (Override(soul, KCDC::Shared::Game::HumanSoulQueryKind::StateMax, selector, 0.0F, nullptr, value)) {
                return value;
            }
            value = s_GetStateMax(soul, state);
            if (KCDC::Core::gApplication != nullptr && state == SDK::wh::rpgmodule::E_SoulState::Health) {
                value = KCDC::Features::Buff::CustomBuffModule::Get().HealthMaximum(soul, value);
            }
            KCDC::Features::Player::Human::CaptureSoulQuery(soul, KCDC::Shared::Game::HumanSoulQueryKind::StateMax, selector, 0.0F, nullptr, value);
            return value;
        }

        QueryFn s_GetStat = nullptr;
        float __fastcall GetStat(const SDK::wh::rpgmodule::I_Soul *soul, std::uint32_t selector, const SDK::wh::rpgmodule::S_SoulQueryContext *context) {
            float value = 0.0F;
            if (Override(soul, KCDC::Shared::Game::HumanSoulQueryKind::Stat, selector, 0.0F, context, value)) {
                return value;
            }
            value = s_GetStat(soul, selector, context);
            KCDC::Features::Player::Human::CaptureSoulQuery(soul, KCDC::Shared::Game::HumanSoulQueryKind::Stat, selector, 0.0F, context, value);
            return value;
        }

        QueryFn s_GetRelativeStat = nullptr;
        float __fastcall GetRelativeStat(const SDK::wh::rpgmodule::I_Soul *soul, std::uint32_t selector, const SDK::wh::rpgmodule::S_SoulQueryContext *context) {
            float value = 0.0F;
            if (Override(soul, KCDC::Shared::Game::HumanSoulQueryKind::RelativeStat, selector, 0.0F, context, value)) {
                return value;
            }
            value = s_GetRelativeStat(soul, selector, context);
            KCDC::Features::Player::Human::CaptureSoulQuery(soul, KCDC::Shared::Game::HumanSoulQueryKind::RelativeStat, selector, 0.0F, context, value);
            return value;
        }

        QueryFn s_GetDerivedStat = nullptr;
        float __fastcall GetDerivedStat(const SDK::wh::rpgmodule::I_Soul *soul, std::uint32_t selector, const SDK::wh::rpgmodule::S_SoulQueryContext *context) {
            float value = 0.0F;
            if (Override(soul, KCDC::Shared::Game::HumanSoulQueryKind::Derived, selector, 0.0F, context, value)) {
                return value;
            }
            value = s_GetDerivedStat(soul, selector, context);
            KCDC::Features::Player::Human::CaptureSoulQuery(soul, KCDC::Shared::Game::HumanSoulQueryKind::Derived, selector, 0.0F, context, value);
            return value;
        }

        QueryFn s_GetSkill = nullptr;
        float __fastcall GetSkill(const SDK::wh::rpgmodule::I_Soul *soul, std::uint32_t selector, const SDK::wh::rpgmodule::S_SoulQueryContext *context) {
            float value = 0.0F;
            if (Override(soul, KCDC::Shared::Game::HumanSoulQueryKind::Skill, selector, 0.0F, context, value)) {
                return value;
            }
            value = s_GetSkill(soul, selector, context);
            KCDC::Features::Player::Human::CaptureSoulQuery(soul, KCDC::Shared::Game::HumanSoulQueryKind::Skill, selector, 0.0F, context, value);
            return value;
        }

        QueryFn s_GetRelativeSkill = nullptr;
        float __fastcall GetRelativeSkill(const SDK::wh::rpgmodule::I_Soul *soul, std::uint32_t selector, const SDK::wh::rpgmodule::S_SoulQueryContext *context) {
            float value = 0.0F;
            if (Override(soul, KCDC::Shared::Game::HumanSoulQueryKind::RelativeSkill, selector, 0.0F, context, value)) {
                return value;
            }
            value = s_GetRelativeSkill(soul, selector, context);
            KCDC::Features::Player::Human::CaptureSoulQuery(soul, KCDC::Shared::Game::HumanSoulQueryKind::RelativeSkill, selector, 0.0F, context, value);
            return value;
        }

        float __fastcall ApplyModifiers(const SDK::wh::rpgmodule::I_Soul *soul, std::uint32_t kind, float base, const SDK::wh::rpgmodule::S_SoulQueryContext *context) {
            float value = base;
            if (Override(soul, KCDC::Shared::Game::HumanSoulQueryKind::Modifier, kind, base, context, value)) {
                return value;
            }
            if (KCDC::Core::gApplication != nullptr && kind == SDK::wh::rpgmodule::I_Soul::kReloadAnimationSpeedModifier && base == 1.0F && context == nullptr && KCDC::Features::Player::PlayerMissileService::TryGetRemoteReloadSpeed(soul, value)) {
                return value;
            }
            value = s_modifier(soul, kind, base, context);
            KCDC::Features::Player::Human::CaptureSoulQuery(soul, KCDC::Shared::Game::HumanSoulQueryKind::Modifier, kind, base, context, value);
            return value;
        }
    } // namespace
} // namespace KCDC::Features::Player

static InitFunction initSoulHooks(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_SoulStateStorage__SetState), reinterpret_cast<LPVOID>(&KCDC::Features::Player::SetState), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_set));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_SoulStateStorage__CopyFrom), reinterpret_cast<LPVOID>(&KCDC::Features::Player::CopyFrom), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_copy));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Soul__ApplyModifiers), reinterpret_cast<LPVOID>(&KCDC::Features::Player::ApplyModifiers), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_modifier));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Soul__GetState), reinterpret_cast<LPVOID>(&KCDC::Features::Player::GetState), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_GetState));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Soul__GetStateMax), reinterpret_cast<LPVOID>(&KCDC::Features::Player::GetStateMax), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_GetStateMax));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Soul__GetStat), reinterpret_cast<LPVOID>(&KCDC::Features::Player::GetStat), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_GetStat));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Soul__GetRelativeStat), reinterpret_cast<LPVOID>(&KCDC::Features::Player::GetRelativeStat), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_GetRelativeStat));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Soul__GetDerivedStat), reinterpret_cast<LPVOID>(&KCDC::Features::Player::GetDerivedStat), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_GetDerivedStat));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Soul__GetSkill), reinterpret_cast<LPVOID>(&KCDC::Features::Player::GetSkill), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_GetSkill));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Soul__GetRelativeSkill), reinterpret_cast<LPVOID>(&KCDC::Features::Player::GetRelativeSkill), reinterpret_cast<LPVOID *>(&KCDC::Features::Player::s_GetRelativeSkill));
    },
    "HumanSoul");
