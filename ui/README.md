# KCDC multiplayer frontend

The in-game menu is a Preact and TypeScript application bundled by Vite. Its
production assets are committed under `files/kcdc/ui` so a native-only build
can still package the menu.

The menu stores the quick-connect host, port and password plus the player's
name in origin-scoped `localStorage`, so closing and reopening it keeps the
last entered connection details.

Every length in `menu.css` is authored against 1920x1080, and CEF hands the
page the framebuffer at a device ratio of 1 -- so without help the menu comes
out half size on a 4K client rather than twice as sharp. `scale.ts` zooms the
document root to whatever the client's resolution fits.

Choosing a realm in the server browser opens a details panel beside the list:
the operator's crest, the realm's address and numbers, and whichever of their
Discord invite and website the masterlist carries. Those two arrive as
`metadata` pairs on the directory row (`discord_invite_url`, `website_url`)
and are set from the server's page on the MafiaHub dashboard, so a server that
fills neither in simply shows none. The page never follows a link itself -- a
CEF view that navigates away from the menu has no way back -- so it sends the
address to the client as `menu.openUrl`, which opens it in the player's own
browser after checking it is a plain http(s) address.

The top bar carries the only way out of the game the mod owns. It asks first,
and the confirmation reaches the client as `menu.quit`, which the main-menu
state answers with the engine's own quit -- the same call the game's own exit
makes.

The in-session chat is not part of this page: it is the framework's own
ImGui chat box, as in the other MafiaHub mods. UI feedback is routed to KCD2's
native GUI sound controller so it follows the game's own audio mix and
settings.

```powershell
npm install
npm run build
```

When `node_modules` is present, the normal `KCDCClient` CMake target rebuilds
the frontend automatically before copying game assets.
