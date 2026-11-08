#pragma once

#include <utils/safe_win32.h>

#include "core/feature_registry.h"
#include "features/items/custom_item_bindings.h"
#include "features/items/native_inventory_action_hint.h"
#include "sdk/cryengine/scriptsystem/i_function_handler.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/guimodule/c_ui_flash_info_item.h"
#include "sdk/wh/guimodule/c_ui_flash_list_item.h"
#include "shared/features/inventory/item_stats_message.h"
#include "shared/game/custom_item_registry.h"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

namespace KCDC::Features::Items {
    /** Owns session mappings and process-lifetime native definitions. */
    class CustomItemModule final: public KCDC::Core::IFeatureService {
      public:
        static CustomItemModule &Get();
        static void Install();
        static void InstallHooks();
        void Update() override;
        void Reset() override;
        bool Ready() const {
            return _complete && _failure.empty();
        }
        const std::string &Failure() const {
            return _failure;
        }
        SDK::wh::entitymodule::S_ItemClassGUID ToNative(const KCDC::Shared::RPC::ItemClassId &id) const;
        KCDC::Shared::RPC::ItemClassId ToLogical(const SDK::wh::entitymodule::S_ItemClassGUID &id) const;
        bool IsCustom(const SDK::wh::entitymodule::C_Item *item) const;
        void Bind(SDK::wh::entitymodule::C_Item &item, const KCDC::Shared::RPC::InventoryItem &properties);
        void Read(const SDK::wh::entitymodule::C_Item &item, KCDC::Shared::RPC::InventoryItem &properties) const;
        bool Matches(const SDK::wh::entitymodule::C_Item &item, const KCDC::Shared::RPC::InventoryItem &properties) const;
        bool AllowMerge(const SDK::wh::entitymodule::C_Item *left, const SDK::wh::entitymodule::C_Item *right) const;
        bool ResolveLabel(const char *label, SDK::CryStringT<char> &out) const;
        std::string Label(const SDK::wh::entitymodule::C_Item &item, bool description = false) const;
        bool Usable(const SDK::wh::entitymodule::C_Item *item, std::string *label = nullptr) const;

        /**
         * The server's override of one class's combat numbers, as it now
         * stands on this client: the stat and the value written, or null for
         * a class the server has not touched. For the client scripting handle.
         */
        std::optional<std::vector<KCDC::Shared::RPC::ItemStats::Value>> ItemStatOverrides(const KCDC::Shared::RPC::ItemClassId &itemClass) const;

        /**
         * Game thread, at a level edge: the item database was read again from
         * its tables, which empties it first, so every class this module
         * appended is gone, and every row a stat override was written into.
         * The session's definitions are appended again and the server's stat
         * table is written onto the new rows.
         */
        void OnItemDatabaseReloaded();

      private:
        /**
         * The authored numbers a stat override replaced, per class, so the
         * class can be given them back when the server drops the override or
         * the session ends. Only written on the first override of each stat.
         */
        struct ItemStatOriginals {
            std::array<std::optional<float>, KCDC::Shared::Game::kItemStatCount> values;
        };

        /** Game thread: writes one table onto the item database. */
        void ApplyItemStats(const KCDC::Shared::RPC::ItemStats &table);
        /** Game thread: gives every overridden class its authored numbers back. */
        void RestoreItemStats();
        /** Writes or restores the stats of one class; false for a class this build does not have or whose element has no such stat. */
        bool WriteItemStats(const KCDC::Shared::RPC::ItemClassId &itemClass, const std::vector<KCDC::Shared::RPC::ItemStats::Value> &values);

        bool RegisterNative(const KCDC::Shared::Game::CustomItemDefinition &definition);
        void CopyBinding(const SDK::wh::entitymodule::C_Item &source, const SDK::wh::entitymodule::C_Item &destination);
        static SDK::wh::guimodule::C_UIFlashListItem *ListConstruct(SDK::wh::guimodule::C_UIFlashListItem *, SDK::wh::entitymodule::C_Item *, void *);
        static SDK::wh::guimodule::C_UIFlashInfoItem *InfoConstruct(SDK::wh::guimodule::C_UIFlashInfoItem *, SDK::wh::entitymodule::C_Item *, void *);
        static KCDC::Features::Items::NativeInventoryActionHint *BuildHint(void *, KCDC::Features::Items::NativeInventoryActionHint *, SDK::wh::entitymodule::C_Item *);
        static void Activate(void *, SDK::wh::entitymodule::C_Item *);
        static void InvokeAction(void *, const std::vector<SDK::wh::entitymodule::C_Item *> &, unsigned int);
        static void InvokeSecondaryAction(void *, const std::vector<SDK::wh::entitymodule::C_Item *> &, unsigned int);
        static SDK::wh::entitymodule::C_Item *Clone(const SDK::wh::entitymodule::C_Item *, SDK::wh::entitymodule::C_ItemHolder *);
        static int PickupName(void *, SDK::IFunctionHandler *);

        decltype(&ListConstruct) _listOriginal  = nullptr;
        decltype(&InfoConstruct) _infoOriginal  = nullptr;
        decltype(&BuildHint) _hintOriginal      = nullptr;
        decltype(&Activate) _activateOriginal   = nullptr;
        decltype(&InvokeAction) _actionOriginal = nullptr;
        decltype(&InvokeSecondaryAction) _secondaryActionOriginal = nullptr;
        decltype(&Clone) _cloneOriginal         = nullptr;
        decltype(&PickupName) _pickupOriginal   = nullptr;
        mutable std::mutex _mutex;
        std::vector<std::string> _incoming;
        KCDC::Features::Items::CustomItemBindings _bindings;
        std::map<std::string, std::string, std::less<>> _labels;
        // Never remove native classes: old items can still reference them. Only a database reload
        // takes them away, at a level edge where no item is left (OnItemDatabaseReloaded).
        std::set<std::string, std::less<>> _installed;
        std::map<std::string, std::string, std::less<>> _nativeToLogical;
        // The IconIds the UI image module serves, each decoded once. A database reload withdraws
        // those no class of the registry names. Game thread.
        std::set<std::string, std::less<>> _servedIcons;
        // The IconIds WIC could not decode, so a shared one is not tried for every class. Game thread.
        std::set<std::string, std::less<>> _undecodableIcons;
        bool _complete = false;
        std::string _failure;

        // Stat changes received, drained on the game thread where the item
        // database is read and written: the last whole table, and the later
        // changes per class (canonical GUID text), the newest only.
        std::optional<KCDC::Shared::RPC::ItemStats> _incomingStatTable;
        std::map<std::string, std::vector<KCDC::Shared::RPC::ItemStats::Value>, std::less<>> _incomingStatUpdates;
        // The server's stat table as it stands, per class (canonical GUID
        // text), whether or not the database has a row for the class yet: a
        // streamed mod's class has none until its table patches load, and a
        // reload replaces every row. Written again after each reload.
        std::map<std::string, std::vector<KCDC::Shared::RPC::ItemStats::Value>, std::less<>> _desiredStats;
        // What the server has written, per class (canonical GUID text), and
        // what each write replaced.
        std::map<std::string, std::vector<KCDC::Shared::RPC::ItemStats::Value>, std::less<>> _statOverrides;
        std::map<std::string, ItemStatOriginals, std::less<>> _statOriginals;
    };
} // namespace KCDC::Features::Items
