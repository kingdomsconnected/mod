#pragma once

#include "features/items/inventory_use_handlers.h"

MODULE(inventory_use, {
    using Handlers = KCDC::Features::Items::InventoryUseHandlers<int>;

    IT("prefers an instance override and restores the newest class override on removal", {
        Handlers handlers;
        const auto first    = handlers.Add("food", {"apple", 0}, 1);
        const auto instance = handlers.Add("quest", {{}, 99}, 2);
        const auto latest   = handlers.Add("food", {"apple", 0}, 3);
        EQUALS(handlers.Match("apple", 99), instance);
        EQUALS(handlers.Match("apple", 100), latest);
        EQUALS(handlers.Match("bread", 100), 0U);
        EQUALS(handlers.Remove(instance, "food"), false);
        EQUALS(handlers.Remove(instance, "quest"), true);
        EQUALS(handlers.Match("apple", 99), latest);
        EQUALS(handlers.Remove(latest, "food"), true);
        EQUALS(handlers.Match("apple", 99), first);
    });

    IT("keeps a captured use attached to its handler when another resource registers", {
        Handlers handlers;
        const auto first = handlers.Add("food", {"apple", 0}, 1);
        handlers.Queue({first, 99, 8, "apple", "row-a", "secondary"});
        handlers.Add("quest", {"apple", 0}, 2);
        const auto pending = handlers.Drain();
        EQUALS(pending.size(), std::size_t(1));
        EQUALS(pending[0].handler, first);
        EQUALS(pending[0].revision, 8ULL);
        EQUALS(pending[0].id, std::string("row-a"));
        EQUALS(handlers.Find(pending[0].handler)->callback, 1);
    });

    IT("resource shutdown drops queued callbacks and invalidates an already drained batch", {
        Handlers handlers;
        const auto food  = handlers.Add("food", {"apple", 0}, 1);
        const auto quest = handlers.Add("quest", {{}, 99}, 2);
        handlers.Queue({food, 100, 1, "apple", "row-a", "primary"});
        handlers.Queue({quest, 99, 1, "apple", "row-b", "secondary"});
        handlers.Release("food");
        auto batch = handlers.Drain();
        EQUALS(batch.size(), std::size_t(1));
        EQUALS(batch[0].handler, quest);
        handlers.Release("quest");
        EQUALS(handlers.Find(batch[0].handler) == nullptr, true);
        EQUALS(handlers.Match("apple", 99), 0U);
    });

    IT("deduplicates a native action and bounds the pending queue", {
        Handlers handlers;
        const auto id = handlers.Add("food", {"apple", 0}, 1);
        handlers.Queue({id, 99, 1, "apple", "row-a", "primary"});
        handlers.Queue({id, 99, 1, "apple", "row-a", "activate"});
        EQUALS(handlers.Drain().size(), std::size_t(1));
        for (std::uint64_t uid = 1; uid <= 25; ++uid) {
            handlers.Queue({id, uid, 1, "apple", "row-a", "secondary"});
        }
        EQUALS(handlers.Drain().size(), std::size_t(16));
        EQUALS(handlers.Drain().empty(), true);
    });

    IT("removes pending uses without allowing stale ids to target a new session", {
        Handlers handlers;
        const auto old = handlers.Add("food", {"apple", 0}, 1);
        handlers.Queue({old, 99, 1, "apple", "row-a", "secondary"});
        EQUALS(handlers.Remove(old, "food"), true);
        EQUALS(handlers.Drain().empty(), true);
        const auto beforeReset = handlers.Add("food", {"apple", 0}, 2);
        handlers.Queue({beforeReset, 99, 1, "apple", "row-a", "secondary"});
        handlers.Reset();
        EQUALS(handlers.Drain().empty(), true);
        const auto current = handlers.Add("food", {"apple", 0}, 3);
        EQUALS(current > beforeReset, true);
        EQUALS(handlers.Remove(beforeReset, "food"), false);
        handlers.Queue({old, 99, 1, "apple", "row-a", "primary"});
        EQUALS(handlers.Drain().empty(), true);
    });

    IT("keeps native UIDs exact beyond JavaScript integer precision", {
        std::uint64_t uid = 0;
        EQUALS(Handlers::ParseUid("18446744073709551615", uid), true);
        EQUALS(uid, std::numeric_limits<std::uint64_t>::max());
        EQUALS(Handlers::ParseUid("9007199254740993", uid), true);
        EQUALS(uid, 9007199254740993ULL);
        for (const auto *invalid : {"", "0", "01", "-1", "+1", "1.0", "1x", " 1", "18446744073709551616"}) {
            EQUALS(Handlers::ParseUid(invalid, uid), false);
        }
    });

    IT("requires a resource and selector and bounds registrations", {
        Handlers handlers;
        EQUALS(handlers.Add("", {"apple", 0}, 0), 0U);
        EQUALS(handlers.Add("food", {}, 0), 0U);
        for (int i = 0; i < 256; ++i) {
            EQUALS(handlers.Add("food", {"apple", 0}, i) != 0, true);
        }
        EQUALS(handlers.Add("food", {"apple", 0}, 257), 0U);
        handlers.Release("food");
        EQUALS(handlers.Add("food", {"apple", 0}, 258) != 0, true);
    });
});
