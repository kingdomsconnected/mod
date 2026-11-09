#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace KCDC::Core::StreamedAssets {
    /**
     * Runs the Lua of the KCD2 mods a server ships, for as long as the session lasts.
     *
     * The game starts a mod's scripts once at boot, before any server is known, so the client runs
     * them itself once the session's paks are read. Every mod gets its own restricted environment,
     * and a script bind reaches it unchanged except where a session has to be able to end:
     * anything the scripts register or set is recorded and undone by Stop.
     *
     * This chunk exists because a mod's Lua is the thing being run; there is no native edge to use
     * in its place. Game thread only.
     */
    class ModScriptHost final {
      public:
        /** A script's normalized path and its text. */
        using Source = std::pair<std::string, std::string>;

        /** Forgets what is staged for the next start. What is running keeps running until Stop. */
        void ClearStaged();

        /** Takes one resource's scripts from its pak. False with `error` for a script that may not run. */
        bool Stage(const std::string &resource, const std::string &pakPath, std::vector<Source> sources, std::string &error);

        /** One line per staged mod that asks for risky access and has not been accepted, naming what it asks for. */
        std::vector<std::string> DescribeUnapproved() const;

        /** The player accepted every staged mod: they are not asked about these scripts again. */
        void ApproveStaged();

        /** Starts the staged mods. */
        void Start();

        /** Ends what is running: its callbacks stop, and what it registered or set is undone. */
        void Stop();

      private:
        struct Mod {
            std::string resource;
            std::string digest;
            std::vector<std::string> risky;
            std::vector<Source> sources;
        };

        bool Call(std::string_view method, std::initializer_list<const char *> arguments) const;
        bool Install();

        std::vector<Mod> _staged;
        std::filesystem::path _approvalsFile;
        bool _installed = false;
        bool _running   = false;
    };
} // namespace KCDC::Core::StreamedAssets
