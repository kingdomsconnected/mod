# @kingdomsconnected/types

TypeScript declarations for the Kingdoms Connected scripting API, generated from the runtime's own
bindings and published with every release. The package version is the release version.

```sh
pnpm add -D @kingdomsconnected/types
```

A resource runs in the server's Node.js isolate or in a client's sandboxed V8 one, and the two
declare different globals -- the server's `Player` has the connection verbs, the client's does
not -- so they are two TypeScript programs, each naming its side in `types`:

```jsonc
// tsconfig.json (server half)
{
  "compilerOptions": {
    "lib": ["ES2022"],
    "types": ["@kingdomsconnected/types/server"]
  }
}
```

```jsonc
// src/client/tsconfig.json (client half)
{
  "compilerOptions": {
    "lib": ["ES2022"],
    "types": ["@kingdomsconnected/types/client"]
  }
}
```

Everything is declared as a global, as the runtime installs it: `Events`, `console`, the timers,
`Player`, `Vector3` and the rest need no import. The DOM library would declare a second
`setTimeout` and `console`, so leave it out of `lib`.

Install the version matching the server you deploy to; a newer package may declare bindings an
older server does not have.

The package includes `LICENSE.txt` and `NOTICE.txt`. Its source-available
license permits using these declarations to develop scripts and game modes
for official Kingdoms Connected releases.
