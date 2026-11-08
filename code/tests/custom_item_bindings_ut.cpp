#include "features/items/custom_item_bindings.h"
#include "features/items/custom_item_presentation.h"
#include <limits>
#include <set>
#include <stdexcept>

namespace {
    void Require(bool value, const char *message) {
        if (!value) {
            throw std::runtime_error(message);
        }
    }
} // namespace

void CustomItemBindingTests() {
    KCDC::Features::Items::CustomItemBindings bindings;
    const nlohmann::json letter = {{"name", "Letter"}, {"description", "Same visible text"}};
    const auto maximumId        = std::numeric_limits<std::uint64_t>::max();
    bindings.Bind(1, "letter", letter, "sender-a", "Letter", "Same visible text");
    bindings.Copy(1, 2, "letter");
    bindings.Copy(1, maximumId, "letter");
    bindings.Copy(1, (std::uint64_t {1} << 32) + 1, "letter");
    const auto firstLetter = bindings.ListClassId(1, "letter");
    Require(firstLetter && *firstLetter != std::numeric_limits<std::uint32_t>::max() && *firstLetter > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()), "custom list identity avoids native class indices and invalid sentinel");
    Require(firstLetter != bindings.ListClassId(2, "letter") && firstLetter != bindings.ListClassId(maximumId, "letter") && bindings.ListClassId(2, "letter") != bindings.ListClassId(maximumId, "letter"),
            "identical nonstackable letters and full-width WUID clones remain independently selectable");
    Require(firstLetter != bindings.ListClassId((std::uint64_t {1} << 32) + 1, "letter"), "WUIDs with identical low bits do not collide in the list");
    Require(!bindings.Bind(1, "letter", letter, "sender-a", "Letter", "Same visible text") && bindings.ListClassId(1, "letter") == firstLetter, "unchanged projection keeps its list identity");
    bindings.Bind(1, "letter", {{"name", "Opened letter"}}, "changed-sender", "Opened letter", "", false);
    Require(bindings.ListClassId(1, "letter") == firstLetter, "updating a nonstackable letter preserves its list identity and focus");
    Require(!bindings.ListClassId(1, "other-class") && !bindings.ListClassId(42, "letter"), "stale class and unbound item cannot expose a list identity");

    bindings.Bind(10, "timber", {}, "batch-a", "Timber", "", true);
    bindings.Copy(10, 11, "timber");
    const auto timberGroup = bindings.ListClassId(10, "timber");
    Require(timberGroup == bindings.ListClassId(11, "timber"), "equal stackable split clones share a list row");
    bindings.Bind(12, "timber", {}, "batch-b", "Timber", "", true);
    bindings.Bind(13, "other-timber", {}, "batch-a", "Timber", "", true);
    bindings.Bind(14, "timber", {{"name", "Dry timber"}}, "batch-a", "Dry timber", "", true);
    Require(timberGroup != bindings.ListClassId(12, "timber") && timberGroup != bindings.ListClassId(13, "other-timber") && timberGroup != bindings.ListClassId(14, "timber"), "private-only token, native class and public overrides each separate stackable variants");
    bindings.Prune([](std::uint64_t id, const std::string &) { return id == 11; }, 100);
    Require(bindings.ListClassId(11, "timber") == timberGroup, "retiring a split source retains the surviving group's identity");
    bindings.Copy(11, 15, "timber");
    bindings.Bind(11, "timber", {{"description", "New description"}}, "batch-a", "Timber", "New description", true);
    Require(bindings.ListClassId(15, "timber") == timberGroup && bindings.ListClassId(11, "timber") != timberGroup, "updating one split changes only its variant's selectable row");
    bindings.Bind(11, "timber", {}, "batch-a", "Timber", "", true);
    Require(bindings.ListClassId(11, "timber") == timberGroup, "returning to an existing equal variant rejoins its group");
    bindings.Bind(11, "timber", {}, "batch-a", "Timber", "", false);
    Require(bindings.ListClassId(11, "timber") != timberGroup, "nonstackable policy separates otherwise equal units");
    bindings.Copy(15, 11, "timber");
    Require(bindings.ListClassId(11, "timber") == timberGroup, "clone replacing an existing destination releases its former group");
    bindings.Prune([](std::uint64_t, const std::string &) { return false; }, 100);
    bindings.Bind(10, "timber", {}, "batch-a", "Timber", "", true);
    Require(bindings.ListClassId(10, "timber") != timberGroup, "retired groups are reclaimed without reusing an identity held by a stale UI row");
    bindings.Reset();

    const nlohmann::json custom = {{"name", "Anna's letter"}, {"description", std::string(4096, 'x')}};
    Require(bindings.Bind(1, "letter", custom, "token-a", "Anna's letter", std::string(4096, 'x')), "first binding refreshes its UI");
    Require(!bindings.Bind(1, "letter", custom, "token-a", "Anna's letter", std::string(4096, 'x')), "identical binding needs no refresh");
    std::string label;
    const auto nameKey        = KCDC::Features::Items::CustomItemPresentation::InstanceLabel(1);
    const auto descriptionKey = KCDC::Features::Items::CustomItemPresentation::InstanceLabel(1, true);
    Require(nameKey.starts_with('@') && descriptionKey.starts_with('@'), "Flash presentation requests localization for names and descriptions");
    Require(bindings.ResolveLabel(nameKey, label) && label == "Anna's letter", "generated display key resolves the instance name");
    Require(bindings.ResolveLabel(descriptionKey, label) && label.size() == 4096, "generated display key resolves the full description");
    Require(bindings.ResolveLabel("kcdc_ii_1_n", label) && label == "Anna's letter", "instance name resolves");
    Require(bindings.ResolveLabel("kcdc_ii_1_d", label) && label.size() == 4096, "maximum description resolves");
    bindings.Copy(1, 2, "letter");
    bindings.Copy(1, 1, "letter");
    Require(bindings.Size() == 2 && bindings.Find(2, "letter")->custom == custom, "split clone and self-copy preserve properties");
    bindings.Bind(1, "letter", {{"name", "Opened"}}, "token-b", "Opened", "");
    Require(bindings.ResolveLabel("kcdc_ii_2_n", label) && label == "Anna's letter", "renaming source leaves split clone unchanged");
    Require(bindings.Prune([](std::uint64_t id, const std::string &) { return id == 2; }) == 1, "destroying source retains live split clone");
    Require(!bindings.ResolveLabel("kcdc_ii_1_n", label) && !bindings.ResolveLabel("kcdc_ii_1_d", label), "retirement removes both labels with its binding");
    Require(bindings.Find(2, "other-class") == nullptr, "reused WUID cannot expose previous class properties");
    bindings.Copy(2, 3, "other-class");
    Require(bindings.Size() == 1, "wrong-class clone does not copy stale data");

    // Keep one living item as a baseline while vendors and pickup/split
    // projections create thousands of fresh WUIDs with large descriptions.
    for (std::uint64_t cycle = 0; cycle < 1000; ++cycle) {
        const auto id = 10 + cycle * 3;
        bindings.Bind(id, "letter", custom, "token-a", "Stock", std::string(4096, 'x'));
        bindings.Copy(id, id + 1, "letter");
        bindings.Copy(id + 1, id + 2, "letter");
        Require(bindings.Prune([](std::uint64_t liveId, const std::string &) { return liveId == 2; }) == 3 && bindings.Size() == 1, "stock destroy and pickup/split churn returns to baseline");
        Require(!bindings.ResolveLabel("kcdc_ii_" + std::to_string(id + 2) + "_d", label), "retired clone description is reclaimed");
    }

    bindings.Reset();
    for (std::uint64_t id = 1; id <= 200; ++id) {
        bindings.Bind(id, "letter", {}, "", "Name", "Description");
    }
    std::set<std::uint64_t> checked;
    const auto live = [&checked](std::uint64_t id, const std::string &) {
        checked.insert(id);
        return id % 2 == 0;
    };
    Require(bindings.Prune(live, 0) == 0 && checked.empty(), "zero budget does no native lookups");
    Require(bindings.Prune(live, 64) == 32 && checked.size() == 64, "cleanup respects per-tick work bound");
    bindings.Bind(0, "letter", {}, "", "New lower WUID", "");
    for (int tick = 0; tick < 4; ++tick) {
        bindings.Prune(live, 64);
    }
    Require(checked.size() == 201 && bindings.Size() == 101, "cleanup wraps and covers insertions below its cursor");
    bindings.Prune([](std::uint64_t, const std::string &) { return false; }, 1000);
    Require(bindings.Size() == 0 && bindings.Prune(live) == 0, "deleting every binding and sweeping an empty map is safe");

    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    bindings.Bind(maximum, "letter", {}, "", "Last WUID", "");
    Require(bindings.ResolveLabel(KCDC::Features::Items::CustomItemPresentation::InstanceLabel(maximum), label) && label == "Last WUID", "generated display keys preserve the full 64-bit WUID");
    Require(bindings.ResolveLabel("kcdc_ii_18446744073709551615_n", label) && label == "Last WUID", "full-width WUID labels resolve without truncation");
    bindings.Prune(live, 1);
    bindings.Bind(1, "letter", {}, "", "Wrapped", "");
    Require(bindings.Prune([](std::uint64_t, const std::string &) { return false; }, 2) == 1, "cursor wraps after maximum WUID");
    bindings.Reset();
    bindings.Bind(1, "letter", {}, "", "Reconnected", "");
    Require(bindings.ResolveLabel("kcdc_ii_1_n", label) && label == "Reconnected", "reset permits fresh WUID bindings without stale labels");
    for (const auto *invalid : {"kcdc_ii_", "kcdc_ii__n", "kcdc_ii_01_n", "kcdc_ii_-1_n", "kcdc_ii_1x_n", "kcdc_ii_1_u", "kcdc_ii_18446744073709551616_d", "kcdc_it_1_n", "@@kcdc_ii_1_n"}) {
        Require(!bindings.ResolveLabel(invalid, label), "malformed instance label is refused");
    }
    Require(bindings.Bind(1, "changed-native-class", {}, "", "New definition", "") && bindings.Find(1, "letter") == nullptr, "rebinding reused WUID replaces old class identity");
    bindings.Reset();
    Require(bindings.Size() == 0 && !bindings.ResolveLabel("kcdc_ii_1_n", label), "session reset releases all instance data");
}
