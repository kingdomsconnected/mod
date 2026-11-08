#include <utils/safe_win32.h>

#include "features/ground_item/ground_stock.h"
#include "shared/features/player/item_carry_policy.h"
#include "shared/features/player/player_carry.h"
#include "shared/game/animation_catalog.h"
#include "shared/game/item_classes.h"

#include <mafianet/bit_stream.h>

#include <stdexcept>

namespace {
    void RequireCarry(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    template <typename T> T RoundTrip(T sent) {
        MafiaNet::BitStream stream;
        sent.Serialize(&stream, true);
        T received;
        received.Serialize(&stream, false);
        return received;
    }

    KCDC::Shared::RPC::ItemClassId ClassNamed(std::string_view name) {
        for (const auto &entry : KCDC::Shared::Game::g_ItemClasses) {
            if (entry.name == name) {
                return KCDC::Shared::Game::ItemClassIdFromGuid(entry.guid);
            }
        }
        throw std::runtime_error("item class missing from the catalog");
    }
} // namespace

void ItemCarryTests() {
    const KCDC::Shared::RPC::ItemClassId basket = ClassNamed("basket_full_wood");

    // --- The wire ---------------------------------------------------------

    KCDC::Shared::RPC::CarryItem order;
    order.itemClass           = basket;
    order.groundItemNetworkId = 77;
    const auto receivedOrder  = RoundTrip(order);
    RequireCarry(receivedOrder.valid && receivedOrder.IsSane() && receivedOrder.itemClass == basket && receivedOrder.groundItemNetworkId == 77, "a carry order names its class and the prop it takes up");
    RequireCarry(!KCDC::Shared::RPC::CarryItem {}.IsSane(), "a carry order without a class is refused");

    KCDC::Shared::RPC::PlayerCarryItem report;
    report.itemClass           = basket;
    report.carrying            = false;
    report.dropped             = true;
    report.groundItemNetworkId = 0;
    const auto receivedReport  = RoundTrip(report);
    RequireCarry(receivedReport.IsSane() && !receivedReport.carrying && receivedReport.dropped && receivedReport.itemClass == basket, "a put-down report says the item went to the ground");
    report.carrying            = true;
    report.dropped             = false;
    report.groundItemNetworkId = 12;
    const auto pickupReport    = RoundTrip(report);
    RequireCarry(pickupReport.carrying && !pickupReport.dropped && pickupReport.groundItemNetworkId == 12, "a pick-up report names the prop it took up");
    report.groundItemNetworkId = 0;
    report.puttingDown         = true;
    const auto putDownStarted  = RoundTrip(report);
    RequireCarry(putDownStarted.IsSane() && putDownStarted.carrying && putDownStarted.puttingDown, "a put-down start keeps the carry and says the clip began");

    KCDC::Shared::RPC::RequestCarryGroundItem request;
    RequireCarry(!request.IsSane(), "a carry request has to name a prop");
    request.groundItemNetworkId = 5;
    RequireCarry(RoundTrip(request).groundItemNetworkId == 5 && RoundTrip(request).IsSane(), "a carry request names the prop the prompt was over");

    KCDC::Shared::RPC::PutDownCarriedItem putDown;
    putDown.immediate = true;
    putDown.refused   = true;
    const auto receivedPutDown = RoundTrip(putDown);
    RequireCarry(receivedPutDown.IsSane() && receivedPutDown.immediate && receivedPutDown.refused, "a refusal travels with the put-down");

    // A truncated stream is not a valid report.
    MafiaNet::BitStream truncated;
    truncated.Write(basket.low);
    KCDC::Shared::RPC::PlayerCarryItem cut;
    cut.Serialize(&truncated, false);
    RequireCarry(!cut.IsSane(), "a truncated report is refused");

    // --- Which bodies walk carrying what ------------------------------------

    constexpr std::uint8_t both = KCDC::Shared::Game::kAnimationBodyMale | KCDC::Shared::Game::kAnimationBodyFemale;
    RequireCarry(KCDC::Shared::Game::CarryWalkBodies("firewood") == both, "both bodies carry firewood");
    RequireCarry(KCDC::Shared::Game::CarryWalkBodies("stoneBasket") == both, "both bodies carry a stone basket");
    RequireCarry(KCDC::Shared::Game::CarryWalkBodies("sack") == KCDC::Shared::Game::kAnimationBodyMale, "only a man carries a sack");
    RequireCarry(KCDC::Shared::Game::CarryWalkBodies("axe_tool") == 0, "an axe has no carrying walk");
    RequireCarry(KCDC::Shared::Game::CarryWalkBodies("") == 0, "nothing has no carrying walk");
    RequireCarry((KCDC::Shared::Game::CarryWalkBodies(basket) & KCDC::Shared::Game::kAnimationBodyMale) != 0, "a class resolves through its prop to a walk");
    RequireCarry(KCDC::Shared::Game::CarryWalkBodies(KCDC::Shared::RPC::ItemClassId {1, 2}) == 0, "a class the catalog lacks has no walk");
    RequireCarry(KCDC::Shared::Game::FindItemClass(basket) != nullptr && KCDC::Shared::Game::FindItemClass(basket)->name == "basket_full_wood", "a wire class finds its catalog entry");

    RequireCarry(KCDC::Shared::Game::CarryWalkBodies("eggBasket") == KCDC::Shared::Game::kAnimationBodyFemale, "only a woman carries an egg basket");

    // --- Who may take an item up ------------------------------------------------

    KCDC::Shared::Game::ItemCarryState free;
    free.canAct = true;
    RequireCarry(KCDC::Shared::Game::ItemCarryRefusal(free) == nullptr, "a free body may take an item up");
    auto refused = [&](auto change) {
        KCDC::Shared::Game::ItemCarryState state = free;
        change(state);
        return KCDC::Shared::Game::ItemCarryRefusal(state) != nullptr;
    };
    RequireCarry(refused([](auto &state) { state.canAct = false; }), "a body that cannot act takes nothing up");
    RequireCarry(refused([](auto &state) { state.mounted = true; }), "a rider takes nothing up: the use key is the saddle's");
    RequireCarry(refused([](auto &state) { state.carryingBody = true; }), "a body on the shoulder leaves no arms free");
    RequireCarry(refused([](auto &state) { state.carryingItem = true; }), "a pair of arms holds one item");

    // --- How a reported carry is judged -------------------------------------------

    auto judge = [](KCDC::Shared::Game::ItemCarryReport report) {
        const char *refusal = nullptr;
        const auto verdict  = KCDC::Shared::Game::JudgeItemCarryReport(report, refusal);
        RequireCarry((verdict == KCDC::Shared::Game::ItemCarryVerdict::Refuse) == (refusal != nullptr), "a refusal always says why, and only a refusal does");
        return verdict;
    };
    KCDC::Shared::Game::ItemCarryReport known;
    known.classKnown = true;
    known.walkBodies = KCDC::Shared::Game::kAnimationBodyMale;

    KCDC::Shared::Game::ItemCarryReport unknown = known;
    unknown.classKnown                         = false;
    unknown.ordered                            = true;
    RequireCarry(judge(unknown) == KCDC::Shared::Game::ItemCarryVerdict::Refuse, "a class the game lacks is refused even when ordered");

    KCDC::Shared::Game::ItemCarryReport ordered = known;
    ordered.ordered                            = true;
    ordered.walkBodies                         = 0;
    RequireCarry(judge(ordered) == KCDC::Shared::Game::ItemCarryVerdict::Accept, "a carry the server ordered is accepted as it stands");
    RequireCarry(judge(known) == KCDC::Shared::Game::ItemCarryVerdict::AskGamemode, "a carry the player started at a pile is the gamemode's to judge");

    KCDC::Shared::Game::ItemCarryReport noWalk = known;
    noWalk.walkBodies                         = 0;
    RequireCarry(judge(noWalk) == KCDC::Shared::Game::ItemCarryVerdict::Refuse, "an unordered carry of something nobody carries is refused");

    KCDC::Shared::Game::ItemCarryReport claimed = known;
    claimed.groundItemNetworkId                = 9;
    claimed.groundClaimed                      = true;
    RequireCarry(judge(claimed) == KCDC::Shared::Game::ItemCarryVerdict::Accept, "a prop claimed for the carrier is theirs");
    claimed.ordered = true;
    RequireCarry(judge(claimed) == KCDC::Shared::Game::ItemCarryVerdict::Accept, "an ordered pick-up of the claimed prop is accepted");

    KCDC::Shared::Game::ItemCarryReport promptCarried = known;
    promptCarried.groundItemNetworkId                = 9;
    promptCarried.groundClaimable                    = true;
    RequireCarry(judge(promptCarried) == KCDC::Shared::Game::ItemCarryVerdict::AskGamemode, "a prop the game's own prompt carried is claimed on the spot, gamemode willing");

    KCDC::Shared::Game::ItemCarryReport lapsed = promptCarried;
    lapsed.ordered                            = true;
    RequireCarry(judge(lapsed) == KCDC::Shared::Game::ItemCarryVerdict::Refuse, "an ordered pick-up whose claim lapsed is not claimed again behind the server's back");

    KCDC::Shared::Game::ItemCarryReport taken = known;
    taken.groundItemNetworkId                = 9;
    RequireCarry(judge(taken) == KCDC::Shared::Game::ItemCarryVerdict::Refuse, "a prop somebody else holds, or out of reach, is refused");

    // --- Who holds which stack ------------------------------------------------------

    constexpr std::uint64_t stack = 41;
    constexpr std::uint64_t alice = 1;
    constexpr std::uint64_t bob   = 2;
    std::uint64_t now             = 1000;
    KCDC::Features::GroundItem::GroundCarryClaims claims;
    RequireCarry(!claims.Claim(stack, 0, now), "nobody is no carrier");
    RequireCarry(claims.IsClaimable(stack, alice, now) && !claims.IsHeld(stack, now), "an unclaimed stack is free");
    RequireCarry(claims.Claim(stack, alice, now) && claims.Holds(stack, alice, now) && claims.IsHeld(stack, now), "a claim holds the stack for its carrier");
    RequireCarry(!claims.Claim(stack, bob, now) && !claims.Holds(stack, bob, now) && !claims.IsClaimable(stack, bob, now), "a claimed stack is nobody else's");
    RequireCarry(claims.Claim(stack, alice, now + 10), "the carrier may renew their own claim");

    now += KCDC::Features::GroundItem::GroundCarryClaims::kClaimTimeoutMs + 10;
    RequireCarry(!claims.Holds(stack, alice, now) && !claims.IsHeld(stack, now), "a claim lapses");
    RequireCarry(claims.Claim(stack, bob, now) && claims.Holds(stack, bob, now), "a lapsed claim frees the stack for somebody else");

    claims.Release(stack);
    RequireCarry(!claims.IsHeld(stack, now) && claims.IsClaimable(stack, alice, now), "a released claim frees the stack");

    claims.Claim(stack, alice, now);
    claims.Retire(stack, now);
    RequireCarry(claims.IsHeld(stack, now) && !claims.Holds(stack, alice, now), "a stack a carry took up is held, but by no claim");
    RequireCarry(!claims.IsClaimable(stack, alice, now) && !claims.IsClaimable(stack, bob, now), "a retiring stack is nobody's to take up");
    RequireCarry(claims.TakeDue(now + 1).empty(), "a retiring stack stays while the other clients take it up");
    const std::vector<std::uint64_t> due = claims.TakeDue(now + KCDC::Features::GroundItem::GroundCarryClaims::kRetireDelayMs);
    RequireCarry(due.size() == 1 && due.front() == stack, "a retiring stack is due once the delay has passed");
    RequireCarry(claims.TakeDue(now + KCDC::Features::GroundItem::GroundCarryClaims::kRetireDelayMs + 1).empty() && !claims.IsHeld(stack, now), "a due stack is handed out once");

    claims.Claim(stack, alice, now);
    claims.Forget(stack);
    RequireCarry(!claims.IsHeld(stack, now) && claims.IsClaimable(stack, bob, now), "a stack that left the ground is forgotten");
}
