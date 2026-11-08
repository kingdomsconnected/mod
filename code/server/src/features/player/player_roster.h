#pragma once

#include "features/player/player_item_requests.h"

#include "shared/features/player/player_appearance.h"
#include "shared/features/player/player_disguise.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/player/player_set_resource.h"
#include "shared/game/buff_catalog.h"
#include "shared/game/item_classes.h"

#include <mafianet/types.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace KCDC::Features::Player {
    // Everything the server does to a connected player's body. The owning client is authoritative for that body, so almost nothing here writes state: a verb asks the owner over the wire and lets the answer come back through replication. That is why these return whether the ask went out, not whether it took effect.
    //
    // Like Horses, no V8: the chat commands and the scripting bindings are two callers of it.
    class Players final {
      public:
        // The body with this NetworkID, or nullptr for an id that names nobody.
        static Shared::Entities::HumanEntity *Find(std::uint64_t networkId);

        // The player whose connection a packet came in on, or nullptr. A report's sender is always read from here, never from the message.
        static Shared::Entities::HumanEntity *FromPacket(MafiaNet::Packet *packet);

        // The player a name or a connection slot refers to. An exact name wins over a prefix, and a prefix matching more than one player sets `ambiguous` rather than guessing. A numeric query is tried as a slot too, so a player called "3" still answers to "3".
        static Shared::Entities::HumanEntity *FindByName(std::string_view query, bool &ambiguous);

        // The player carrying this one on their shoulder, or nullptr. Read off the carriers' own `carriedNetworkId`, which is the one record of a carry.
        static Shared::Entities::HumanEntity *CarrierOf(std::uint64_t carriedNetworkId);

        // Asks a player's own client to put down whatever body is on their shoulder, with the game's own put-down unless `immediate`; a client carrying nothing ignores it. The carry ends when that client reports the put-down, which raises `playerPutDown` like any other -- unless the server already ended it. False when there is no connection to ask.
        static bool PutDown(Shared::Entities::HumanEntity *carrier, bool immediate);

        // Asks a player's own client to carry an item of this class in their arms, through the game's own carry: made straight in their hands, or taken up from the carryable stack `groundItemNetworkId` names with the game's pick-up. Sends only; PlayerService::OrderItemCarry is the verb. False when there is no connection to ask.
        static bool CarryItem(Shared::Entities::HumanEntity *player, const Shared::RPC::ItemClassId &itemClass, std::uint64_t groundItemNetworkId);

        // Asks a player's own client to put down the item in their arms, with the game's own put-down unless `immediate`; a client carrying nothing ignores it. `refused` sends an item taken up from a stack back to it rather than down as a new one. The carry ends when that client reports the put-down, which raises `playerPutDownItem`. False when there is no connection to ask.
        static bool PutDownItem(Shared::Entities::HumanEntity *carrier, bool immediate, bool refused);

        // The item class a GUID or an exact name refers to, or nullptr.
        static const Shared::Game::ItemClassEntry *ResolveItemClass(std::string_view query);

        // Asks a player's own client to put them somewhere else. Sent only to the player being moved: the owner is authoritative for its own pose, so a server-side write would be gone by its next capture. `label` is display only, echoed back in the client's teleport panel. False when the position is not finite, the label is too long, or there is no connection to ask.
        static bool Teleport(Shared::Entities::HumanEntity *player, const glm::vec3 &position, const std::string &label);

        // Puts a pace rule on one player: whether a body they spawn starts walking, and whether they may run or sprint at all. Sent only to that player, because both halves are client-side state. Nothing comes back -- what everyone else sees is the pace their body already publishes. The whole rule travels each time, so a call states both halves rather than amending one. False when there is no connection to ask.
        static bool SetMovementMode(Shared::Entities::HumanEntity *player, bool walkByDefault, bool walkEnforced);

        // Grants or revokes F7 and MapEditor.open for this connection only.
        static bool SetWorldBuilderEnabled(Shared::Entities::HumanEntity *player, bool enabled);

        // Asks a player's own client to wear a different body: the four component names that make up a KCD2 appearance, plus the gender that partitions the catalog they come from. Sent only to the player being changed, because the owner is the only peer that may publish its own body -- so this returns whether the ask went out, and the change shows up when the owner republishes it. The owner refuses an appearance its own soul cannot wear rather than publishing one only other people can see. False when a name is not in `Appearances.options` or there is no connection to ask.
        static bool SetAppearance(Shared::Entities::HumanEntity *player, const Shared::Game::CharacterAppearance &appearance);

        // Asks a player's own client to draw their body as a static mesh from the prop catalog, or as themselves again for an empty disguise. Sent only to that player, who puts it on and republishes it for everyone else, as with an appearance. False when the mesh is not in the catalog or there is no connection to ask.
        static bool SetDisguise(Shared::Entities::HumanEntity *player, const Shared::Game::PlayerDisguise &disguise);

        // Asks a player's own client to stop drawing their body, or to draw it again, everywhere -- the body itself, what it wears and what it carries. Sent only to that player, who applies it and republishes it for everyone else, as with a disguise. Nothing else about the body changes: it keeps its place and its collider, it can still be hit, and NPCs still see it. False when there is no connection to ask.
        static bool SetVisible(Shared::Entities::HumanEntity *player, bool visible);

        // Grants items into a player's inventory, which the server holds; their game shows them a tick later. False for a player with no inventory, an unknown class, or an amount outside 1..10000.
        static bool GiveItem(Shared::Entities::HumanEntity *player, const Shared::Game::ItemClassEntry &item, std::uint32_t amount);

        // Takes items of a class out of a player's inventory, across its rows, all or none, and calls `handler` at once with the outcome. False only without a player or server, and then `handler` never runs.
        static bool TakeItem(Shared::Entities::HumanEntity *player, const Shared::Game::ItemClassEntry &item, std::uint32_t amount, ItemRequests::OutcomeHandler handler);

        // The buff definition a GUID or an exact name refers to, or nullptr.
        static const Shared::Game::BuffEntry *ResolveBuff(std::string_view query);

        // Asks a player's own client to put a status effect on their body, through the game's own C_BuffManager. False when there is no connection to ask. The ask is not a promise the buff arrives -- the exclusivity rules can turn an add down, and six implementations fold a second helping into the instance already there -- so read `Buffs` back rather than assuming.
        static bool AddBuff(Shared::Entities::HumanEntity *player, const Shared::Game::BuffEntry &buff);

        // Asks a player's own client to take a status effect off, by the exact instance when a report named one and by definition otherwise.
        static bool RemoveBuff(Shared::Entities::HumanEntity *player, const Shared::Game::BuffEntry &buff);

        // Asks a player's own client to clear every buff in one of the game's own effect families -- `poison`, `bleed`, `alcohol_drunk`, `unconscious`. False for a family name no buff table uses.
        static bool ClearBuffsByTag(Shared::Entities::HumanEntity *player, std::string_view tagName);

        // Whether the player's last report carried this definition.
        static bool HasBuff(Shared::Entities::HumanEntity *player, const Shared::Game::BuffEntry &buff);

        // Everything the player last reported, with when this server first saw each of them, or nullptr for one who has reported nothing yet. Borrowed: valid until their next report.
        static const std::vector<Shared::RPC::ObservedBuff> *Buffs(Shared::Entities::HumanEntity *player);

        // The limbs the player's last report carried an injury on, one bit
        // per `body_part.xml` id: bit 1 the head through bit 6 the right leg.
        // Zero for one who has reported nothing yet.
        static std::uint32_t InjuredLimbs(Shared::Entities::HumanEntity *player);

        // Sends a value to the ready, living owner without changing the reported soul.
        static bool SetResource(Shared::Entities::HumanEntity *player, Shared::RPC::SetPlayerResource request);

        // The game's own full heal, `utils/general/healing.xml`, one step
        // per switch: `remove_injuries` and `remove_all_posions` evict their
        // whole buff class, the `bleed` family is cleared the way its quests
        // clear it, and health rises through the soul's own setter -- to
        // `health`, all the way when negative, not at all when empty. Orders
        // to the owner on one ordered channel. False for a health the wire
        // refuses or no connection to ask; nothing is sent then.
        static bool Heal(Shared::Entities::HumanEntity *player, std::optional<float> health, bool clearInjuries, bool clearPoisons, bool clearBleeding);
    };
} // namespace KCDC::Features::Player
