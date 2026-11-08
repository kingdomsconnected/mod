#pragma once

#include <cstdint>

namespace KCDC::Shared::Network {
    inline constexpr char GameName[]    = "Kingdom Come: Deliverance II";
    inline constexpr char GameVersion[] = "1.0.0";
    inline constexpr char ModName[]     = "KCDC";
    inline constexpr char ModSlug[]     = "kcdc";

    // The mod's own name as a player reads it: the window caption, the loading
    // screen, anything the mod signs. `ModName` and `ModSlug` stay the short
    // machine-facing forms.
    inline constexpr char ModDisplayName[] = "Kingdoms Connected";

    // MafiaHub's shared masterlist (apps/masterlist in mafiahub/services). The
    // server announces itself to it and the menu's server browser reads the
    // list back out, so both sides spell the address once, here.
    inline constexpr char MasterlistUrl[] = "https://masterlist.mafiahub.dev";

    // Client assets are served over HTTP on this same port since Framework
    // 40.2; the server has no second listener.
    inline constexpr int32_t Port = 27015;

    // Both the shipped default and the ceiling `server.json` cannot raise.
    inline constexpr int32_t MaxPlayers = 512;
} // namespace KCDC::Shared::Network
