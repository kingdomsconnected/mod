#include "item_classes.h"
#include "mod_item_tables.h"

namespace KCDC::Shared::Game {
    const ItemClassEntry *FindItemClassByName(std::string_view name) {
        for (const ItemClassEntry &item : g_ItemClasses) {
            if (item.name == name) {
                return &item;
            }
        }
        return ModItemRegistry::Get().FindByName(name);
    }

    void ForEachItemClass(const fu2::function<void(const ItemClassEntry &) const> &visit) {
        for (const ItemClassEntry &item : g_ItemClasses) {
            visit(item);
        }
        ModItemRegistry::Get().ForEach(visit);
    }
} // namespace KCDC::Shared::Game
