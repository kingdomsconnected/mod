#pragma once

#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace KCDC::Shared::StreamedAssets {
    /** What a mod's script asks for, read from its text alone. */
    struct LuaScriptScan {
        /** Precompiled, or holding a byte plain Lua source never does. Such a script is refused outright. */
        bool unreadable = false;
        /** The file, OS and dynamic-loading names it uses, sorted: what the player is asked about. */
        std::vector<std::string> risky;
    };

    /**
     * Tokenises a Lua 5.1 script and lists the risky names it uses, skipping real comments and string
     * bodies. Lua can build a name at run time, so this cannot be the protection: the restricted
     * environment a script runs in is. The scan only decides who gets asked.
     */
    LuaScriptScan ScanLuaScript(std::string_view source);

    /**
     * The mods a player has accepted, as `<resource> <sha256 of its scripts>`, one per line. Holding
     * the hash means a server cannot ship different code under a name that was accepted.
     */
    class LuaScriptApprovals final {
      public:
        static LuaScriptApprovals Parse(std::string_view text);
        std::string ToText() const;
        bool Contains(std::string_view resource, std::string_view digest) const;
        void Add(std::string_view resource, std::string_view digest);

      private:
        std::set<std::string> _lines;
    };
} // namespace KCDC::Shared::StreamedAssets
