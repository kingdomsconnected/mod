#pragma once

#include "sdk/types.h"
#include "sdk/wh/c_mod_manager.h"
#include "sdk/wh/i_mod.h"
#include "sdk/wh/s_mod_info.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace KCDC::Core::Boot {
    /**
     * Which KCD2 mods the game's mod manager reports as enabled.
     *
     * The player's own mods never load: the boot scan's enabled list is
     * emptied, so no mod pak, `mod.cfg`, Lua or string patch of theirs runs.
     * The mods the server streams are reported instead, to anything that asks
     * after the scan. The table loader is what asks: it merges
     * `<table>__<id>.xml` row by row only when `<id>` is an enabled mod's.
     *
     * Game thread only.
     */
    class LocalMods final {
      public:
        static LocalMods &Get();

        /** The mod ids of the KCD2 mods the server streams, from now on reported as enabled. */
        void SetServerModIds(const std::set<std::string> &ids);

        /** After the boot scan: empties the enabled list the player's `mod_order.txt` filled. */
        void IgnorePlayerMods(SDK::wh::C_ModManager &manager) const;

        /** Hands each server mod to a `ForEachEnabledMod` callable, ahead of the game's own list. */
        void VisitServerMods(SDK::wh::ModFunction &function) const;

      private:
        /**
         * A server mod as the game's mod manager hands its own out. Its folder
         * is one that does not exist, so the manager's pak, mod.cfg and
         * localization-pak consumers find nothing under it. Never empty: the
         * level-pak consumer appends the level's folder to it, and an empty one
         * reopens the level's own paks. It ends in '/' like C_Mod's.
         */
        class ServerMod final: public SDK::wh::I_Mod {
          public:
            explicit ServerMod(const std::string &id);
            ~ServerMod();

            ServerMod(const ServerMod &)            = delete;
            ServerMod &operator=(const ServerMod &) = delete;

            const SDK::CryStringT<char> &GetPath() const override {
                return _path.Get();
            }
            const SDK::wh::S_ModInfo &GetInfo() const override {
                return _info;
            }
            const SDK::CryStringT<char> &GetModId() const override {
                return _id.Get();
            }

          private:
            SDK::ScopedCryString _path;
            SDK::ScopedCryString _id;
            SDK::wh::S_ModInfo _info;
        };

        std::vector<std::unique_ptr<ServerMod>> _serverMods;
    };
} // namespace KCDC::Core::Boot
