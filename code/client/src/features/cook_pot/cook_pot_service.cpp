#include <utils/safe_win32.h>

#include "cook_pot.h"
#include "cook_pot_service.h"

#include "core/application.h"
#include "core/obfuscation.h"
#include "features/player/player.h"
#include "features/player/player_inventory_service.h"
#include "common/module_support.h"
#include "sdk/cryengine/entitysystem/i_entity_class_registry.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/scriptsystem/i_script_table.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/xgenaimodule/c_ai_object_manager.h"
#include "sdk/wh/xgenaimodule/c_intelligent_object_manager.h"
#include <algorithm>
#include <core_modules.h>
#include <glm/geometric.hpp>
#include <mafianet/get_time.h>
#include <networking/network_peer.h>
#include <string_view>

namespace KCDC::Features::CookPot {
    namespace {
        constexpr std::uint64_t kReplyDeadlineMilliseconds = 5000;
        constexpr float kReachMetres                       = 4.0F;
        constexpr std::size_t kMaximumQueuedReplies        = 32;
        constexpr std::size_t kMaximumRememberedPrompts    = 64;
        // `AHT_HOLD`, as `C_ScriptBindPlayer` publishes it (0x1817B4B33): the
        // kettle's hold action is the poisoning one.
        constexpr std::uint8_t kHintHold = 2;
        // so_fireplace.xml's kettleState (ai_enums.xml): full, eaten, halfEmpty, empty.
        constexpr float kStateFull  = 0.0F;
        constexpr float kStateEaten = 1.0F;
        constexpr float kStateEmpty = 3.0F;

        std::uint64_t NowMilliseconds() {
            return MafiaNet::GetTimeUS() / 1000;
        }

        /** The cauldron phase so_fireplace.xml would pick: 1 empty, then half/full per food. */
        int PhaseOf(const KCDC::Shared::CookPot::Stock &stock) {
            const auto state = stock.Appearance();
            if (state == KCDC::Shared::CookPot::State::Empty) {
                return 1;
            }
            return 2 + static_cast<int>(stock.food) * 2 + (state == KCDC::Shared::CookPot::State::Full ? 1 : 0);
        }

        const char *CategoryOf(const KCDC::Shared::Game::CookPotPlacement &placement) {
            return placement.home ? "fireplace_cauldron_home" : "fireplace_cauldron_camp";
        }

        /** so_fireplace.xml's `fireplaceFireCategory`, chosen by the same isHomeFireplace flag. */
        const char *FireCategoryOf(const KCDC::Shared::Game::CookPotPlacement &placement) {
            return placement.home ? "fireplace_burning" : "fireplace_burning_camp";
        }

        constexpr char kSteamCategory[] = "fireplace_steam";

        // so_fireplace.xml sets the fire and steam prefabs to phase 2 when lit and 1 when out.
        static_assert(static_cast<int>(KCDC::Shared::CookPot::Fire::Out) == 1 && static_cast<int>(KCDC::Shared::CookPot::Fire::Burning) == 2);

        /** The engine's own call, made the way so_fireplace.xml makes it; the phase hook passes it through unchanged. */
        void SetPrefabPhase(std::uint64_t guid, const char *category, int phase) {
            auto *entities = SDK::IEntitySystem::GetInstance();
            auto *prefab   = entities != nullptr ? entities->GetEntityByGuid(guid) : nullptr;
            auto *scripts  = SDK::IScriptSystem::GetInstance();
            if (prefab == nullptr || scripts == nullptr || !scripts->BeginCallNamedTableMethod(std::string(KCDC_HIDE("Game")).c_str(), std::string(KCDC_HIDE("SetPrefabPhaseById")).c_str())) {
                return;
            }
            scripts->PushFuncParamAny(SDK::ScriptAnyValue::Handle(prefab->GetId()));
            scripts->PushFuncParamAny(SDK::ScriptAnyValue(category));
            scripts->PushFuncParamAny(SDK::ScriptAnyValue(static_cast<float>(phase)));
            scripts->EndCall();
        }

        /**
         * The message quests light a fireplace with: the brain stores the fire
         * state, restarts its burn-out timer and switches fire, steam, chimney
         * and window smoke. `kettleFireState` (ai_enums.xml) is on 0, off 1.
         */
        void SendFireState(std::uint64_t fireplaceGuid, bool lit) {
            auto *entities  = SDK::IEntitySystem::GetInstance();
            auto *fireplace = entities != nullptr ? entities->GetEntityByGuid(fireplaceGuid) : nullptr;
            auto *scripts   = SDK::IScriptSystem::GetInstance();
            if (fireplace == nullptr || scripts == nullptr || !scripts->BeginCallNamedTableMethod(std::string(KCDC_HIDE("XGenAIModule")).c_str(), std::string(KCDC_HIDE("SendMessageToEntity")).c_str())) {
                return;
            }
            const std::string values = lit ? std::string(KCDC_HIDE("state(0),onInit(false)")) : std::string(KCDC_HIDE("state(1),onInit(false)"));
            scripts->PushFuncParamAny(SDK::ScriptAnyValue::Handle(fireplace->GetId()));
            scripts->PushFuncParamAny(SDK::ScriptAnyValue(std::string(KCDC_HIDE("kettle:signal:onFireplacePhaseChange")).c_str()));
            scripts->PushFuncParamAny(SDK::ScriptAnyValue(values.c_str()));
            scripts->EndCall();
        }

    } // namespace

    CookPotService &CookPotService::Get() {
        static CookPotService service;
        return service;
    }

    void CookPotService::Install() {
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<KCDC::Shared::RPC::CookPotContext>([](const auto &context, MafiaNet::Packet *) {
            auto &service = Get();
            std::scoped_lock lock(service._mutex);
            if (context.valid && context.revision > service._serverContext.revision) {
                service._serverContext = context;
            }
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<KCDC::Shared::RPC::CookPotReply>([](const auto &reply, MafiaNet::Packet *) {
            auto &service = Get();
            std::scoped_lock lock(service._mutex);
            if (reply.valid && service._incoming.size() < kMaximumQueuedReplies) {
                service._incoming.push_back(reply);
            }
        });
    }

    void CookPotService::Reset() {
        Drop();
        _level.clear();
        _byTrigger.clear();
        _byCauldron.clear();
        _byFire.clear();
        _fireStale.clear();
        _stock.clear();
        _prompts.clear();
        _kettleClass  = nullptr;
        _context      = {};
        _levelChanged = true;
        _stockChanged = true;
        std::scoped_lock lock(_mutex);
        _incoming.clear();
        _serverContext = {};
        // Sequence persists across connections: late replies cannot name a
        // new meal. RPC registration belongs to the peer, not the session.
    }

    void CookPotService::NativeContextChanged() {
        _levelChanged = true;
        _stockChanged = true;
    }

    bool CookPotService::RefreshLevel() {
        if (!_levelChanged.exchange(false)) {
            return !_byTrigger.empty();
        }
        // A new level, body or session: nothing held or remembered still names
        // a live prompt or trigger.
        Drop();
        _prompts.clear();
        auto *entities = SDK::IEntitySystem::GetInstance();
        auto *classes  = entities != nullptr ? entities->GetClassRegistry() : nullptr;
        _kettleClass   = classes != nullptr ? classes->FindClass(SDK::EntityClassNames::KettleActionTrigger) : nullptr;
        if (auto level = KCDC::Core::gApplication->GetCurrentLevelName(); level != _level) {
            _level = std::move(level);
            _byTrigger.clear();
            _byCauldron.clear();
            _byFire.clear();
            _fireStale.clear();
            for (const auto &placement : KCDC::Shared::Game::g_CookPotPlacements) {
                if (_level == placement.level) {
                    _byTrigger.emplace(placement.triggerGuid, &placement);
                    _byCauldron.emplace(placement.cauldronGuid, &placement);
                    _byFire.emplace(placement.fireGuid, &placement);
                    _byFire.emplace(placement.steamGuid, &placement);
                }
            }
        }
        return !_byTrigger.empty();
    }

    const KCDC::Shared::CookPot::Stock *CookPotService::StockOf(const KCDC::Shared::Game::CookPotPlacement &placement) const {
        const auto found = _stock.find(placement.guid);
        return found == _stock.end() ? nullptr : &found->second;
    }

    void CookPotService::RescanStock() {
        std::unordered_map<std::uint64_t, KCDC::Shared::CookPot::Stock> stock;
        if (_context.revision != 0) {
            Framework::CoreModules::GetReplication()->ForEach<CookPot>([&](auto *pot) {
                if (pot->stockWorld == _context.world) {
                    stock.emplace(pot->guid, pot->stock);
                }
            });
        }
        std::vector<std::uint64_t> changed;
        for (const auto &[guid, current] : stock) {
            const auto previous = _stock.find(guid);
            if (previous == _stock.end() || previous->second != current) {
                changed.push_back(guid);
            }
            // Only a new fire is sent: a meal eaten leaves the fire alone.
            if (previous == _stock.end() ? current.fire != KCDC::Shared::CookPot::Fire::Native : previous->second.fire != current.fire) {
                _fireStale.insert(guid);
            }
        }
        _stock.swap(stock);
        if (changed.empty()) {
            return;
        }
        for (const auto &[trigger, placement] : _byTrigger) {
            if (std::find(changed.begin(), changed.end(), placement->guid) != changed.end()) {
                Apply(*placement);
            }
        }
    }

    void CookPotService::SyncState(const KCDC::Shared::Game::CookPotPlacement &placement, bool edible) const {
        auto *manager = SDK::wh::xgenaimodule::C_IntelligentObjectManager::GetInstance();
        if (manager == nullptr) {
            return;
        }
        auto *variable = manager->FindBrainVariable(SDK::wh::xgenaimodule::FindWuidByEntityGuid(placement.guid), std::string(KCDC_HIDE("persistentStorage")).c_str());
        if (variable == nullptr) {
            return;
        }
        SDK::ScriptAnyValue value;
        variable->GetValue(value);
        if (value.type != SDK::EScriptAnyType::Table || value.table == nullptr) {
            return;
        }
        // The struct is written back whole: a key missing from the table would
        // be erased from the brain.
        SDK::ScriptTablePtr storage(value.table);
        const std::string stateKey = std::string(KCDC_HIDE("state"));
        float state                = kStateFull;
        storage->GetValue(stateKey.c_str(), state);
        const bool stateEdible = state == kStateFull || state == kStateEaten;
        if (stateEdible == edible) {
            return;
        }
        storage->SetValue(stateKey.c_str(), edible ? kStateFull : kStateEmpty);
        if (edible) {
            // What the brain's own refill does: the half-empty count restarts.
            storage->SetValue(std::string(KCDC_HIDE("playerEatingCount")).c_str(), 0.0F);
        }
        variable->SetValue(SDK::ScriptAnyValue(storage.get()));
    }

    bool CookPotService::SyncFood(const KCDC::Shared::Game::CookPotPlacement &placement, KCDC::Shared::Game::CookPotFood food) const {
        auto *entities  = SDK::IEntitySystem::GetInstance();
        auto *fireplace = entities != nullptr ? entities->GetEntityByGuid(placement.guid) : nullptr;
        auto *script    = KCDC::Common::EntityScriptTable(fireplace);
        if (script == nullptr) {
            return false;
        }
        auto properties = script->GetTable(std::string(KCDC_HIDE("Properties")).c_str());
        auto settings   = properties ? properties->GetTable(std::string(KCDC_HIDE("Fireplace")).c_str()) : SDK::ScriptTablePtr {};
        if (!settings) {
            return false;
        }
        const auto name = KCDC::Shared::Game::CookPotFoodName(food);
        if (name.empty()) {
            return false;
        }
        settings->SetValue(std::string(KCDC_HIDE("esFireplaceCauldronType")).c_str(), name.data());
        script->SetValue(std::string(KCDC_HIDE("cauldronType")).c_str(), name.data());
        return true;
    }

    void CookPotService::Apply(const KCDC::Shared::Game::CookPotPlacement &placement, bool forceEdible) const {
        const auto *stock = StockOf(placement);
        if (stock == nullptr) {
            return;
        }
        SyncFood(placement, stock->food);
        SyncState(placement, forceEdible || stock->portions > 0);
        SetPrefabPhase(placement.cauldronGuid, CategoryOf(placement), PhaseOf(*stock));
    }

    void CookPotService::SyncFires() {
        for (const auto guid : _fireStale) {
            const auto stock = _stock.find(guid);
            // Until a script sets it, and after it hands it back, the fire stays the local brain's.
            if (stock != _stock.end() && stock->second.fire != KCDC::Shared::CookPot::Fire::Native) {
                // The brain applies the message only while its behaviour runs:
                // past its init it was logged leaving fire and steam as they
                // were. So both are set here too, as its handler sets them; a
                // fireplace not streamed in is told by its brain's init, through
                // the phase hook.
                const auto placement = std::find_if(_byTrigger.begin(), _byTrigger.end(), [guid](const auto &entry) {
                    return entry.second->guid == guid;
                });
                if (placement != _byTrigger.end()) {
                    SetPrefabPhase(placement->second->fireGuid, FireCategoryOf(*placement->second), static_cast<int>(stock->second.fire));
                    SetPrefabPhase(placement->second->steamGuid, kSteamCategory, static_cast<int>(stock->second.fire));
                }
                SendFireState(guid, stock->second.fire == KCDC::Shared::CookPot::Fire::Burning);
            }
        }
        _fireStale.clear();
    }

    int CookPotService::OverridePhase(SDK::EntityId prefab, const char *category, int requested) {
        auto *entity = KCDC::Common::ResolveEntity(prefab);
        if (entity == nullptr || category == nullptr) {
            return requested;
        }
        // The brain lights the fire when a villager stokes it, puts it out on
        // its own timer and replays its own state on streaming in; a fire the
        // server set keeps the server's phase, and the brain is told the
        // server's state again so its smoke and timer follow.
        if (const auto fire = _byFire.find(entity->GetGuid()); fire != _byFire.end()) {
            const auto *stock = StockOf(*fire->second);
            const bool ours   = std::string_view(category) == FireCategoryOf(*fire->second) || std::string_view(category) == kSteamCategory;
            if (!ours || stock == nullptr || stock->fire == KCDC::Shared::CookPot::Fire::Native) {
                return requested;
            }
            if (requested != static_cast<int>(stock->fire)) {
                // Not from inside the brain's own handler: sent on the next update.
                _fireStale.insert(fire->second->guid);
            }
            return static_cast<int>(stock->fire);
        }
        const auto found = _byCauldron.find(entity->GetGuid());
        if (found == _byCauldron.end() || std::string_view(category) != CategoryOf(*found->second)) {
            return requested;
        }
        const auto *stock = StockOf(*found->second);
        if (stock == nullptr) {
            return requested;
        }
        // Every state the kettle brain writes is followed by this call, so a
        // local refill, shutdown or half-empty switch is put back here.
        if (!_replaying) {
            SyncFood(*found->second, stock->food);
        }
        SyncState(*found->second, _replaying || stock->portions > 0);
        return PhaseOf(*stock);
    }

    bool CookPotService::IsStewTrigger(SDK::IEntity *entity) {
        auto *script = KCDC::Common::EntityScriptTable(entity);
        if (script == nullptr) {
            return true;
        }
        auto properties = script->GetTable(std::string(KCDC_HIDE("Properties")).c_str());
        SDK::ScriptAnyValue wine;
        if (!properties || !properties->GetValueAny(std::string(KCDC_HIDE("bIsWineBarrel")).c_str(), wine)) {
            return true;
        }
        // Authored as "1"; read either way the script system may hand it back.
        const bool isWine = (wine.type == SDK::EScriptAnyType::Boolean && wine.boolean) || (wine.type == SDK::EScriptAnyType::Number && wine.number != 0.0F);
        return !isWine;
    }

    void CookPotService::OnActionRegistering(const SDK::wh::entitymodule::C_PlayerInteractor &interactor, SDK::wh::entitymodule::S_InteractorActionRecord &record, SDK::HScriptFunction function) {
        if (function == 0 || _kettleClass == nullptr) {
            return;
        }
        auto *entity = KCDC::Common::ResolveEntity(interactor.GetTargetEntityId());
        if (entity == nullptr || entity->GetClass() != _kettleClass) {
            return;
        }
        const auto found = _byTrigger.find(entity->GetGuid());
        if (found == _byTrigger.end() && !IsStewTrigger(entity)) {
            return;
        }
        if (_prompts.size() >= kMaximumRememberedPrompts) {
            _prompts.clear();
        }
        // An uncatalogued stew pot has no server stock, and poisoning only
        // touches the local AI: neither can be offered.
        if (found == _byTrigger.end() || record.m_hintType == kHintHold) {
            record.m_enabled   = false;
            record.m_uiVisible = record.m_hintType != kHintHold;
            _prompts.insert_or_assign(function, Prompt {Handler::Refused, nullptr});
            return;
        }
        _prompts.insert_or_assign(function, Prompt {Handler::Eat, found->second});
        // An empty pot is refused by the game itself, from the state kept in
        // its brain; a disabled or unknown one is refused here.
        const auto *stock = StockOf(*found->second);
        if (_context.revision == 0 || stock == nullptr || !stock->enabled) {
            record.m_enabled = false;
        }
    }

    bool CookPotService::InterceptInteraction(const SDK::wh::entitymodule::S_LuaActionInvocation &invocation) {
        if (_replaying) {
            return false;
        }
        const auto prompt = _prompts.find(invocation.m_function);
        if (prompt == _prompts.end()) {
            return false;
        }
        if (prompt->second.handler == Handler::Refused || _waiting) {
            return true;
        }
        auto *entity          = KCDC::Common::ResolveEntity(invocation.m_targetEntityId);
        auto *scripts         = SDK::IScriptSystem::GetInstance();
        const auto *placement = prompt->second.placement;
        const auto *stock     = StockOf(*placement);
        if (entity == nullptr || entity->GetGuid() != placement->triggerGuid || scripts == nullptr || _context.revision == 0 || stock == nullptr || !stock->enabled || !KCDC::Core::gApplication->GetPlayerInventoryService().IsSynced()) {
            return true;
        }
        // The prompt's own reference goes when its list is rebuilt, which can
        // happen before the answer lands; this one lasts until released.
        const auto function = scripts->AddFuncRef(invocation.m_function);
        if (function == 0) {
            return true;
        }
        _pending                         = {};
        _pending.function                = function;
        _pending.trigger                 = invocation.m_targetEntityId;
        _pending.placement               = placement;
        _pending.deadline                = NowMilliseconds() + kReplyDeadlineMilliseconds;
        _pending.request.sequence        = ++_sequence;
        _pending.request.guid            = placement->guid;
        _pending.request.world           = _context.world;
        _pending.request.contextRevision = _context.revision;
        _pending.request.incarnation     = _context.incarnation;
        _waiting                         = true;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(_pending.request, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        return true;
    }

    bool CookPotService::IsReplayingFood(const SDK::wh::entitymodule::C_Item *item) const {
        if (!_replaying || item == nullptr) {
            return false;
        }
        const auto actual    = item->InvokeGetItemClassRaw();
        const auto &expected = KCDC::Shared::Game::g_CookPotFoodItemClasses[static_cast<int>(_replayFood)];
        return actual.m_low == expected.hipart && actual.m_high == expected.lopart;
    }

    void CookPotService::Settle(const KCDC::Shared::RPC::CookPotReply &reply) {
        const bool granted    = reply.granted;
        auto *actor           = KCDC::Common::LocalPlayer();
        auto *soul            = KCDC::Common::LocalSoul();
        auto *owner           = KCDC::Features::Player::Human::FindLocal();
        const auto &placement = *_pending.placement;
        const auto &item      = KCDC::Shared::Game::g_CookPotFoodItemClasses[static_cast<int>(reply.food)];
        const bool nearby     = actor != nullptr && glm::distance(KCDC::Common::ToGlm(actor->GetWorldPos()), placement.position) <= kReachMetres;
        // The replay's own CanEat runs the same evaluator in the same frame, so
        // asking first decides whether the meal is eaten or refunded.
        bool eaten = granted && nearby && soul != nullptr && owner != nullptr && owner->soul.CanAct() && NowMilliseconds() <= _pending.deadline && soul->CanEat(SDK::CryGUID {item.hipart, item.lopart});
        if (eaten) {
            // The reserved portion may already be gone from the replica; the
            // brain is held edible for exactly this replay.
            _replaying = true;
            _foodEffects = reply.nativeEffects;
            _replayFood  = reply.food;
            _ate         = false;
            Apply(placement, true);
            eaten = SyncFood(placement, reply.food);
            if (eaten) {
                SDK::wh::entitymodule::InvokeLuaAction(_pending.function, _pending.trigger);
            }
            eaten        = eaten && _ate;
            _replaying = false;
            _foodEffects = true;
            Apply(placement);
        }
        if (granted) {
            _pending.request.completion = true;
            _pending.request.eaten      = eaten;
            Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(_pending.request, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
        Drop();
    }

    void CookPotService::Drop() {
        if (_waiting && _pending.function != 0) {
            if (auto *scripts = SDK::IScriptSystem::GetInstance()) {
                scripts->ReleaseFunc(_pending.function);
            }
        }
        _pending = {};
        _waiting = false;
    }

    void CookPotService::Update() {
        auto *owner = KCDC::Features::Player::Human::FindLocal();
        if (owner == nullptr || KCDC::Common::LocalPlayer() == nullptr) {
            return;
        }
        KCDC::Shared::RPC::CookPotContext context;
        {
            std::scoped_lock lock(_mutex);
            context = _serverContext;
        }
        // Only a context naming this body's incarnation admits meals.
        if (context.incarnation != owner->soul.incarnation) {
            context = {};
        }
        if (context.revision != _context.revision) {
            Drop();
            _context      = context;
            _stockChanged = true;
        }
        if (!RefreshLevel()) {
            return;
        }
        if (_stockChanged.exchange(false)) {
            RescanStock();
        }
        SyncFires();
        std::vector<KCDC::Shared::RPC::CookPotReply> replies;
        {
            std::scoped_lock lock(_mutex);
            replies.swap(_incoming);
        }
        for (const auto &reply : replies) {
            if (_waiting && reply.sequence == _pending.request.sequence && reply.guid == _pending.request.guid) {
                Settle(reply);
            }
        }
        // No answer: the server spends what it may have reserved.
        if (_waiting && NowMilliseconds() > _pending.deadline) {
            Drop();
        }
    }
} // namespace KCDC::Features::CookPot
