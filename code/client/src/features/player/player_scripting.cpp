#include "features/player/player_scripting.h"

#include "core/application.h"
#include "core/scripting/script_context.h"
#include "features/player/player_buff_service.h"
#include "features/player/player_progression_scripting.h"
#include "common/module_support.h"

#include "shared/features/horse/horse_entity.h"
#include "shared/features/player/player_buff.h"
#include "shared/features/player/player_buff_bindings.h"
#include "shared/features/player/player_progression_bindings.h"
#include "shared/scripting/catalog.h"
#include "shared/features/player/player_bindings.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/get_time.h>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <sstream>
#include <vector>

namespace KCDC::Scripting {
    namespace {
        // JS: player.setTargetable(targetable)
        void JS_SetTargetable(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            Player *self = v8pp::class_<Player>::unwrap_object(isolate, info.This());
            if (self == nullptr || info.Length() < 1 || !info[0]->IsBoolean()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Player.setTargetable: expected (targetable: boolean)"));
                return;
            }
            KCDC::Core::gApplication->GetPlayerDuelService().SetTargetable(self->GetId(), info[0]->BooleanValue(isolate));
        }
    } // namespace

    std::unique_ptr<v8pp::class_<Player>> Player::_class;
    std::unique_ptr<v8pp::class_<LocalPlayer>> LocalPlayer::_class;

    Shared::Entities::HumanEntity *Player::ResolveHuman() const {
        return Framework::Networking::Replication::CheckedEntityCast<Shared::Entities::HumanEntity>(Resolve());
    }

    KCDC::Features::Player::Human *Player::ResolveReplica() const {
        return Framework::Networking::Replication::CheckedEntityCast<KCDC::Features::Player::Human>(Resolve());
    }

    bool Player::IsLocal() const {
        auto *replica = ResolveReplica();
        return replica != nullptr && replica->IsOwner();
    }

    std::uint32_t Player::GetEntityId() const {
        auto *replica = ResolveReplica();
        if (replica == nullptr) {
            return 0;
        }
        // The local body is the game's own player entity, never a copy this replica spawned.
        return static_cast<std::uint32_t>(replica->IsOwner() ? KCDC::Common::LocalActorEntityId() : replica->GetEntityId());
    }

    std::uint64_t Player::GetHorseId() const {
        const std::uint64_t networkId = GetId();
        auto *horse                   = KCDC::Common::FindReplica<Shared::Entities::HorseEntity>([networkId](Shared::Entities::HorseEntity *candidate) { return candidate->riderNetworkId == networkId; });
        return horse != nullptr ? horse->GetNetworkID() : 0;
    }

    std::string Player::ToString() const {
        const KCDC::Features::Player::Human *replica = ResolveReplica();
        std::ostringstream ss;
        ss << "Player{ id: " << GetId() << ", nickname: '" << (replica != nullptr ? replica->nickname : std::string()) << "', local: " << (replica != nullptr && replica->IsOwner() ? "true" : "false") << " }";
        return ss.str();
    }

    // JS: player.buffs -> BuffState[]
    v8::Local<v8::Value> Player::GetBuffStates(v8::Isolate *isolate) const {
        v8::EscapableHandleScope handleScope(isolate);
        if (!IsLocal()) {
            return handleScope.Escape(v8::Array::New(isolate));
        }
        return handleScope.Escape(BuffStatesToV8(isolate, Core::gApplication->GetPlayerBuffService().LocalBuffs(), MafiaNet::GetTime()));
    }

    v8pp::class_<Player> &Player::GetClass(v8::Isolate *isolate) {
        if (!_class) {
            // v8pp's inherit<Player> needs the framework Player registered first.
            Framework::Scripting::Builtins::Player::GetClass(isolate);
            RegisterHumanDataTypes(ClientCatalog());
            RegisterBuffDataTypes(ClientCatalog());
            RegisterProgressionDataTypes(ClientCatalog());

            _class = std::make_unique<v8pp::class_<Player>>(isolate, ClientCatalog(), "Player", "A KCDC player this client can see, and the body they occupy.");
            _class->auto_wrap_objects(true);
            _class->inherit<Framework::Scripting::Builtins::Player>()
                .ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")},
                    "Creates a script wrapper for a player this client already knows about; it neither connects nor spawns anyone."))
                .function("toString", &Player::ToString,
                    v8pp::metadata::docs("string", {}, "Formats this player handle for logging and debugging.", "The player's network entity ID, nickname, and whether they are the local player."));

            // The readonly view of the body, written once and shared with the server's own Player handle.
            RegisterHumanProperties(*_class);
            RegisterProgressionPlayerMembers(*_class);

            // What this machine's engine picked for the body it draws: each
            // client animates every body itself from the replicated input, so
            // these are local observations, never sent anywhere.
            _class->property("moveSpeedTag", [](const Player &self) { const Shared::Entities::HumanEntity *human = self.ResolveHuman(); return static_cast<std::uint32_t>(human != nullptr ? human->moveSpeedTag : Shared::Entities::HumanEntity::kNoTag); },
                v8pp::metadata::property_docs("number", "The locomotion pace tag -- walk, jog, sprint -- this machine's animation system picked for this body. Raw Mannequin tag ids; 255 means nothing is set."));
            _class->property("moveDirTag", [](const Player &self) { const Shared::Entities::HumanEntity *human = self.ResolveHuman(); return static_cast<std::uint32_t>(human != nullptr ? human->moveDirTag : Shared::Entities::HumanEntity::kNoTag); },
                v8pp::metadata::property_docs("number", "The movement direction tag this machine's animation system picked for this body. Raw Mannequin tag ids; 255 means nothing is set."));
            _class->property("stanceTag", [](const Player &self) { const Shared::Entities::HumanEntity *human = self.ResolveHuman(); return static_cast<std::uint32_t>(human != nullptr ? human->stanceTag : Shared::Entities::HumanEntity::kNoTag); },
                v8pp::metadata::property_docs("number", "The stance tag -- upright, sneaking, sitting, lying -- this machine's animation system picked for this body. Raw Mannequin tag ids; 255 means nothing is set."));
            _class->property("local", &Player::IsLocal, v8pp::metadata::property_docs("boolean", "Whether this handle is the player sitting at this machine, rather than someone else's copy."));
            _class->property("entityId", &Player::GetEntityId,
                v8pp::metadata::property_docs("number", "The engine entity behind this player: the game's own player entity for the local player, otherwise the entity the copy was spawned as, or 0 across a level load and before it exists. Not stable across a session: the engine reuses entity ids."));
            _class->property("horseId", &Player::GetHorseId, v8pp::metadata::property_docs("number", "Network ID of the horse this player is riding, or 0 when they are on foot."));
            _class->property("mounted", &Player::IsMounted, v8pp::metadata::property_docs("boolean", "Whether this player is in a saddle."));
            _class->prototype_function("setTargetable", &JS_SetTargetable,
                v8pp::metadata::docs("void", {v8pp::metadata::param("targetable", "boolean", false, "False to keep this player's body out of the local lock-on, true to let it back.")},
                    "Keeps another player's body out of this machine's combat lock-on and opponent picker, or lets it back in -- for someone lying wounded, say. Only this client's picker is affected, so a resource that wants it everywhere tells every client. It does not stop a swing that lands anyway; damage is the server's to refuse. Lasts until changed or the session ends. Throws unless `targetable` is a boolean."));
            _class->property("buffs", [](const Player &self, v8::Isolate *isolate) { return self.GetBuffStates(isolate); },
                v8pp::metadata::property_docs("BuffState[]",
                    "The status effects on this body: potions, poison, drunkenness, injuries, illness, unconsciousness, and anything the server added. Perks and equipment effects are not in here -- they follow state that already replicates.\n\nOnly the local player has this list. Nobody else's client publishes their effects, because what everyone else's body is wearing reaches this machine as the consequences already -- so it reads empty for any other handle."));
        }
        return *_class;
    }

    void Player::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ClientCatalog().data_type("EventMap");
        events.add_property("playerReady", "[player: Player]",
            "Dispatched once per session, when the local player can play: the session is established and the game has taken its own loading screen down. The game raises that moment itself -- it is its own gameplay-start signal, not a guess from timing.\n\nResources are running long before this, so it is not the moment a resource may start listening; it is the moment the player is looking at the world. Put what the player should see on arrival here -- a welcome, a first page -- rather than in `resourceStart`, which runs behind the loading screen. The server raises its own `playerReady` for this player right after.");

        RegisterPlayerStatEnums(isolate, global, ClientCatalog());
        GetClass(isolate).publish(global);
    }

    void Events::LocalPlayerReady() {
        EmitReservedEvent("playerReady", [](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {LocalPlayer::Resolve(isolate)};
        });
    }

    // --- LocalPlayer ---------------------------------------------------------

    v8pp::class_<LocalPlayer> &LocalPlayer::GetClass(v8::Isolate *isolate) {
        if (!_class) {
            Player::GetClass(isolate);

            // Undocumented on purpose: to a script this is a Player, and the global accessor below is where that is written down. A catalog entry of its own would document a second type, with no members, that nobody can name.
            _class = std::make_unique<v8pp::class_<LocalPlayer>>(isolate);
            _class->auto_wrap_objects(true);
            _class->inherit<Player>();
            _class->ctor<std::uint64_t>();
        }
        return *_class;
    }

    v8::Local<v8::Value> LocalPlayer::Resolve(v8::Isolate *isolate) {
        v8::EscapableHandleScope handleScope(isolate);

        auto *local = KCDC::Features::Player::Human::FindLocal();
        if (local == nullptr) {
            return handleScope.Escape(v8::Null(isolate));
        }

        GetClass(isolate);
        return handleScope.Escape(v8pp::class_<LocalPlayer>::create_object(isolate, local->GetNetworkID()));
    }

    void LocalPlayer::JS_Get(const v8::FunctionCallbackInfo<v8::Value> &info) {
        info.GetReturnValue().Set(Resolve(info.GetIsolate()));
    }

    void LocalPlayer::RegisterGlobal(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        GetClass(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Function> getter = v8::FunctionTemplate::New(isolate, &LocalPlayer::JS_Get)->GetFunction(context).ToLocalChecked();
        v8pp::set_global_accessor(isolate, global, ClientCatalog(), "LocalPlayer", getter, {.type = "Player | null", .description = "The player sitting at this machine, or null while this client has no body -- before the session spawns one, and again once it ends. Read it fresh each time rather than holding onto it."});
    }
} // namespace KCDC::Scripting
