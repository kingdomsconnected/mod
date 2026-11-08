#include "features/inventory/inventory_gameplay.h"
#include "features/items/inventory_reconciliation.h"

#include <stdexcept>

namespace {
    void RequireReconciliation(bool value, const char *message) {
        if (!value) {
            throw std::runtime_error(message);
        }
    }

    void QuickslotsRoundTripAndRejectMalformedReferences() {
        KCDC::Shared::Inventory::Quickslots saved;
        saved.weapons[2]   = {"sword", "shield"};
        saved.items[3]     = "potion";
        saved.activeWeapon = 2;
        saved.activeItem   = 3;
        KCDC::Shared::Inventory::Quickslots decoded;
        RequireReconciliation(KCDC::Shared::Inventory::Quickslots::Read(saved.Encode(), decoded) && decoded == saved, "quickslots retain gaps, paired weapons, item assignments and selections");
        RequireReconciliation(saved.ReferencesExist([](const std::string &id) {
            return id == "sword" || id == "shield" || id == "potion";
        }),
            "all references resolve within the saved inventory");
        RequireReconciliation(!saved.ReferencesExist([](const std::string &id) {
            return id != "potion";
        }),
            "a missing row is not accepted");
        auto invalid            = saved.Encode();
        invalid["activeWeapon"] = 4U;
        RequireReconciliation(!KCDC::Shared::Inventory::Quickslots::Read(invalid, decoded) && decoded == saved, "an out-of-range selection is refused atomically");
        invalid             = saved.Encode();
        invalid["items"][0] = std::string(65, 'x');
        RequireReconciliation(!KCDC::Shared::Inventory::Quickslots::Read(invalid, decoded), "oversized row identities are refused");
        invalid = saved.Encode();
        invalid["weapons"].erase(3);
        RequireReconciliation(!KCDC::Shared::Inventory::Quickslots::Read(invalid, decoded), "the complete four-slot shape is required");
        saved.ForEachReference([](std::string &id) {
            if (id == "potion") {
                id.clear();
            }
        });
        RequireReconciliation(saved.items[3].empty() && saved.weapons[2][1] == "shield", "removing a row clears only its quickslot references");
    }

    void QuickslotRestoreVerifiesAssignmentsAndIgnoresOldEchoes() {
        KCDC::Features::Items::InventoryEquipmentReconciliation equipment;
        KCDC::Shared::Inventory::Quickslots saved;
        saved.items[2]   = "potion";
        saved.activeItem = 2;
        equipment.Receive({{"belt", 1}}, 1, 0, saved);
        RequireReconciliation(equipment.Poll(0, {{"belt", 1}}, {}) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Dress, "quickslots restore with the container");
        RequireReconciliation(equipment.Poll(2000, {{"belt", 1}}, KCDC::Shared::Inventory::Quickslots {}) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Dress && !equipment.CanReport(), "a worn container alone does not verify missing assignments");
        RequireReconciliation(equipment.Poll(4000, {{"belt", 1}}, saved) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Complete, "matching assignments and selection verify the restore");
        auto manual       = saved;
        manual.items[1]   = std::exchange(manual.items[2], {});
        manual.activeItem = 1;
        equipment.Reported({{"belt", 1}}, 4100, manual);
        equipment.Receive({{"belt", 1}}, 1, 4200, saved);
        RequireReconciliation(equipment.DesiredQuickslots() == manual && !equipment.NeedsPoll(4200), "an ordinary echo cannot undo a later quickslot choice");
        equipment.Receive({{"belt", 1}}, 2, 4300, saved);
        RequireReconciliation(!equipment.CanReport() && equipment.DesiredQuickslots() == saved, "an explicit restore replaces quickslots even at the same inventory revision");
        equipment.Reset();
        equipment.Receive({}, 0, 0);
        equipment.Poll(0, {}, manual);
        equipment.Poll(2000, {}, manual);
        RequireReconciliation(!equipment.SentQuickslots(), "initial native quickslots must still be reported when the server has no snapshot");
    }

    void UnshownQuickslotsKeepIntentWithoutBlockingEquipmentReports() {
        using Reconciliation = KCDC::Features::Items::InventoryEquipmentReconciliation;
        using Quickslots     = KCDC::Shared::Inventory::Quickslots;
        Reconciliation equipment;
        Quickslots saved;
        saved.weapons[2]       = {"missing-sword", "shield"};
        saved.items[3]         = "missing-potion";
        saved.activeWeapon     = 2;
        saved.activeItem       = 3;
        const auto unavailable = [](const std::string &id) {
            return id.starts_with("missing-");
        };
        equipment.Receive({{"belt", 1}, {"shield", 1}, {"missing-sword", 1}}, 1, 0, saved);
        Quickslots native;
        native.weapons[2][1] = "shield";
        auto recorded        = equipment.RecordQuickslots(native, unavailable);
        RequireReconciliation(recorded == saved, "unshown primary items retain references and selections normalized by the engine");
        equipment.Poll(0, equipment.Desired(), recorded);
        RequireReconciliation(equipment.Poll(2000, equipment.Desired(), recorded) == Reconciliation::Action::Complete && equipment.CanReport(), "unshown quickslot items do not freeze equipment persistence");
        auto settled = equipment.RecordQuickslots(native, unavailable);
        RequireReconciliation(settled.activeWeapon == native.activeWeapon && settled.activeItem == native.activeItem, "after verification the native normalized selections are reported");
        native.weapons[1][0] = "visible-sword";
        native.activeWeapon  = 1;
        native.items[1]      = "visible-potion";
        native.activeItem    = 1;
        recorded             = equipment.RecordQuickslots(native, unavailable);
        RequireReconciliation(recorded.items[1] == "visible-potion" && recorded.items[3] == "missing-potion", "visible changes coexist with unavailable saved references");
        RequireReconciliation(recorded.activeWeapon == 1 && recorded.activeItem == 1, "selecting a visible slot after an unavailable-item restore must persist");
        equipment.Reported({{"new-belt", 1}}, 2100, recorded);
        RequireReconciliation(equipment.Sent().contains("new-belt") && equipment.SentQuickslots() == recorded, "ordinary equipment and quickslot reports continue after a partial native projection");
        native.items[3]   = "replacement";
        native.activeItem = 3;
        RequireReconciliation(equipment.RecordQuickslots(native, unavailable).items[3] == "replacement", "a real native replacement takes precedence over an unavailable saved item");
        RequireReconciliation(equipment.RecordQuickslots(native,
                                  [](const std::string &) {
                                      return false;
                                  })
                                  == native,
            "available items must be observed rather than assumed restored");
    }

    void ProjectionRetriesWithoutAnotherSync() {
        KCDC::Features::Items::InventoryProjectionRetry retry;
        RequireReconciliation(retry.CanAttempt(1000), "the first projection can run immediately");
        retry.Failed(1000);
        RequireReconciliation(retry.Pending() && !retry.CanAttempt(1249) && retry.CanAttempt(1250), "a failed projection retries on its deadline without another sync");
        retry.Failed(1250);
        RequireReconciliation(!retry.CanAttempt(2249) && retry.CanAttempt(2250), "a second failure backs off");
        retry.Failed(2250);
        RequireReconciliation(!retry.CanAttempt(5249) && retry.CanAttempt(5250), "a third failure backs off further");
        retry.Failed(5250);
        RequireReconciliation(!retry.Pending() && !retry.CanAttempt(100000), "a permanent failure cannot invoke native creation every frame");
        retry.Reset();
        RequireReconciliation(!retry.Pending() && retry.CanAttempt(100001), "a new authoritative update or native body permits another bounded attempt");
        retry.Failed(100001);
        retry.Reset();
        RequireReconciliation(!retry.Pending(), "a successful projection cancels its pending retry");
    }

    void RestoreWinsOverAnOutstandingEquipmentReport() {
        KCDC::Features::Items::InventoryEquipmentReconciliation equipment;
        equipment.Receive({{"old", 1}}, 0, 0);
        RequireReconciliation(equipment.Poll(0, {{"old", 1}}) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Dress && !equipment.CanReport(), "even already worn gear waits for the spawn to settle");
        RequireReconciliation(equipment.Poll(2000, {{"old", 1}}) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Complete, "an already worn initial outfit is verified after settling");
        equipment.Reported({{"manual", 1}}, 2100);
        equipment.Receive({{"saved", 1}}, 1, 2200);
        RequireReconciliation(!equipment.CanReport() && equipment.Poll(2200, {{"manual", 1}}) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Dress, "a server restore takes effect during the old five-second echo window");
        RequireReconciliation(equipment.Poll(4199, {{"saved", 1}}) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::None, "dressing is allowed to settle before success");
        RequireReconciliation(equipment.Poll(4200, {{"saved", 1}}) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Complete && equipment.CanReport(), "the new outfit must be observed before local equipment reporting resumes");
    }

    void OrdinarySyncDoesNotUndoLocalEquipmentOrRestartDressing() {
        KCDC::Features::Items::InventoryEquipmentReconciliation equipment;
        equipment.Receive({{"old", 1}}, 0, 0);
        equipment.Poll(0, {{"old", 1}});
        equipment.Poll(2000, {{"old", 1}});
        equipment.Reported({{"manual", 1}}, 2100);
        equipment.Receive({{"old", 1}}, 0, 2200);
        RequireReconciliation(!equipment.NeedsPoll(10000) && equipment.Poll(10000, {{"manual", 1}}) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::None, "an ordinary item sync cannot undo a later local equip");
        equipment.Receive({{"saved", 1}}, 1, 11000);
        equipment.Poll(11000, {});
        equipment.Receive({{"saved", 1}}, 1, 12000);
        RequireReconciliation(!equipment.NeedsPoll(12999) && equipment.NeedsPoll(13000), "an ordinary sync does not restart the verification timer");
    }

    void LegacyDeferredEquipmentIsRevisitedWithoutTraffic() {
        KCDC::Features::Items::InventoryEquipmentReconciliation equipment;
        equipment.Receive({{"old", 1}}, std::nullopt, 0);
        equipment.Poll(0, {{"old", 1}});
        equipment.Poll(2000, {{"old", 1}});
        equipment.Reported({{"manual", 1}}, 2100);
        equipment.Receive({{"saved", 1}}, std::nullopt, 2200);
        RequireReconciliation(!equipment.NeedsPoll(7099), "an older server's ambiguous record waits for the outstanding echo");
        RequireReconciliation(equipment.NeedsPoll(7100) && equipment.Poll(7100, {{"manual", 1}}) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Dress, "the latest deferred record is revisited without another network packet");
    }

    void PartialDressingSettlesOnTheBody() {
        KCDC::Features::Items::InventoryEquipmentReconciliation equipment;
        const std::map<std::string, std::uint32_t> requested {{"helmet", 1}, {"coat", 1}};
        equipment.Receive(requested, 4, 0);
        for (std::uint64_t now = 0; now < 8000; now += 2000) {
            RequireReconciliation(equipment.Poll(now, {{"coat", 1}}) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Dress && !equipment.CanReport(), "missing pieces retry without reporting a partial outfit");
        }
        RequireReconciliation(equipment.Poll(8000, {{"coat", 1}}) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Failed, "permanently missing gear ends as a failure");
        const std::map<std::string, std::uint32_t> worn {{"coat", 1}};
        RequireReconciliation(equipment.CanReport() && equipment.Desired() == worn && !equipment.NeedsPoll(100000), "after the last attempt the body's outfit is the record, reporting resumes and native retries stop");
        equipment.Reported(worn, 8001);
        RequireReconciliation(equipment.Sent() == worn, "the body's outfit is what reaches the server");
        equipment.Receive({{"other", 1}}, 5, 100001);
        RequireReconciliation(equipment.Poll(100001, {{"coat", 1}}) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Dress, "a newer outfit supersedes a failed restore");
        RequireReconciliation(equipment.Poll(102001, {{"other", 1}}) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Complete, "a later successful outfit recovers reporting");
    }

    void DressingRecoversOnALaterAttempt() {
        KCDC::Features::Items::InventoryEquipmentReconciliation equipment;
        equipment.Receive({{"helmet", 1}, {"arrows", 10}}, 1, 0);
        equipment.Poll(0, {});
        equipment.Poll(2000, {{"helmet", 1}});
        RequireReconciliation(equipment.Poll(4000, {{"helmet", 1}, {"arrows", 9}}) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Complete, "a late piece completes dressing; a shot's lower arrow count does not fail the outfit");
        equipment.Restart(5000);
        RequireReconciliation(!equipment.CanReport() && equipment.Poll(5000, {}) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Dress, "rebinding the native body re-applies the current requested outfit");
        equipment.Reset();
        RequireReconciliation(!equipment.CanReport() && !equipment.NeedsPoll(100000) && !equipment.Restore() && equipment.Desired().empty(), "disconnect clears outfit intent and retry state");
    }

    void SameInventoryRevisionCanCarryANewerRestore() {
        KCDC::Features::Items::InventoryEquipmentReconciliation equipment;
        equipment.Receive({{"helmet", 1}}, 1, 0);
        equipment.Poll(0, {{"helmet", 1}});
        equipment.Poll(2000, {{"helmet", 1}});
        equipment.Receive({}, 2, 2100);
        RequireReconciliation(equipment.Poll(2100, {{"helmet", 1}}) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Dress, "an equipment-only restore can undress an unchanged inventory");
        std::uint64_t revision = 0;
        std::map<std::string, std::uint32_t> items;
        nlohmann::json report {{"revision", 10U}, {"equipmentRestore", 1U}, {"items", {{"helmet", 1U}}}};
        RequireReconciliation(!KCDC::Features::Inventory::InventoryGameplay::ReadEquipment(report, revision, items, 2), "an old report cannot overwrite a restore with the same inventory revision");
        report["equipmentRestore"] = 2U;
        RequireReconciliation(KCDC::Features::Inventory::InventoryGameplay::ReadEquipment(report, revision, items, 2), "a report that has seen the restore is accepted");
        report["equipmentRestore"] = "2";
        RequireReconciliation(!KCDC::Features::Inventory::InventoryGameplay::ReadEquipment(report, revision, items, 2), "malformed restore identities are refused");
        report.erase("equipmentRestore");
        RequireReconciliation(KCDC::Features::Inventory::InventoryGameplay::ReadEquipment(report, revision, items, 2), "legacy clients retain the existing revision-based admission");
    }

    void AReboundBodyKeepsTheLatestLocalChoice() {
        KCDC::Features::Items::InventoryEquipmentReconciliation equipment;
        equipment.Receive({{"old", 1}}, 0, 0);
        equipment.Poll(0, {{"old", 1}});
        equipment.Poll(2000, {{"old", 1}});
        equipment.Reported({{"manual", 1}}, 2100);
        equipment.Receive({{"old", 1}}, 0, 2200);
        equipment.Restart(2300);
        RequireReconciliation(equipment.Desired() == std::map<std::string, std::uint32_t> {{"manual", 1}}, "an older echo cannot replace the choice reapplied to a rebound body");
        RequireReconciliation(equipment.Poll(2300, {}) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Dress, "the rebound body is dressed again");
        RequireReconciliation(equipment.Poll(4300, {{"manual", 1}}) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::Complete, "the latest local choice is verified on the new body");
    }

    void ALateNativeChangeAfterTheRetryLimitIsAnOrdinaryReport() {
        KCDC::Features::Items::InventoryEquipmentReconciliation equipment;
        equipment.Receive({{"helmet", 1}}, 1, 0);
        for (std::uint64_t now = 0; now <= 8000; now += 2000) {
            equipment.Poll(now, {});
        }
        RequireReconciliation(!equipment.NeedsPoll(9000) && !equipment.NeedsPoll(9000, true) && equipment.CanReport() && equipment.Desired().empty(), "an exhausted restore settles on the bare body and issues no more native attempts");
        RequireReconciliation(equipment.Poll(9000, {{"helmet", 1}}) == KCDC::Features::Items::InventoryEquipmentReconciliation::Action::None && equipment.CanReport(), "a piece the body takes later is reported like any local change, not verified as the old restore");
    }
} // namespace

void InventoryReconciliationTests() {
    QuickslotsRoundTripAndRejectMalformedReferences();
    QuickslotRestoreVerifiesAssignmentsAndIgnoresOldEchoes();
    UnshownQuickslotsKeepIntentWithoutBlockingEquipmentReports();
    ProjectionRetriesWithoutAnotherSync();
    RestoreWinsOverAnOutstandingEquipmentReport();
    OrdinarySyncDoesNotUndoLocalEquipmentOrRestartDressing();
    LegacyDeferredEquipmentIsRevisitedWithoutTraffic();
    PartialDressingSettlesOnTheBody();
    DressingRecoversOnALaterAttempt();
    SameInventoryRevisionCanCarryANewerRestore();
    AReboundBodyKeepsTheLatestLocalChoice();
    ALateNativeChangeAfterTheRetryLimitIsAnOrdinaryReport();
}
