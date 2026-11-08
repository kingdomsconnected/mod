import { cp, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { existsSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

// Builds @kingdomsconnected/types, the committed declarations as an npm package, so a gamemode
// takes them with `npm ci` instead of downloading files by hand.
//
// scripting-api/ is the package: its package.json is the manifest, and the default gamemode links
// the directory as it stands. The published copy is that directory's `files` with the release
// version written in and the package's own README beside them, so what a release publishes is
// exactly what the repository compiled against.

const repositoryRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const apiRoot = path.join(repositoryRoot, "scripting-api");
const notices = ["LICENSE.txt", "NOTICE.txt"];

const option = (name) => {
  const index = process.argv.indexOf(name);
  return index === -1 ? undefined : process.argv[index + 1];
};

const main = async () => {
  const output = option("--out");
  const version = option("--version");
  if (!output || !version) throw new Error("Usage: node scripts/build_types_package.mjs --out <directory> --version <semver>");
  if (!/^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$/.test(version)) throw new Error(`Invalid package version '${version}'`);

  const outputRoot = path.resolve(repositoryRoot, output);
  if (outputRoot === path.parse(outputRoot).root || outputRoot === repositoryRoot || repositoryRoot.startsWith(`${outputRoot}${path.sep}`)) {
    throw new Error("Package output must not be a filesystem root, the repository root, or one of its parents");
  }

  const sides = ["server", "client"];
  for (const side of sides) {
    const declaration = path.join(apiRoot, "generated", `${side}-api.d.ts`);
    if (!existsSync(declaration)) throw new Error(`${path.relative(repositoryRoot, declaration)} is missing; a package must carry both sides`);
  }

  const manifest = JSON.parse(await readFile(path.join(apiRoot, "package.json"), "utf8"));
  manifest.files = [...new Set([...manifest.files, ...notices])];
  await rm(outputRoot, { recursive: true, force: true });
  for (const file of manifest.files) {
    await mkdir(path.dirname(path.join(outputRoot, file)), { recursive: true });
    await cp(path.join(notices.includes(file) ? repositoryRoot : apiRoot, file), path.join(outputRoot, file));
  }
  await cp(path.join(apiRoot, "package", "README.md"), path.join(outputRoot, "README.md"));

  manifest.version = version;
  await writeFile(path.join(outputRoot, "package.json"), `${JSON.stringify(manifest, null, 2)}\n`, "utf8");
  console.log(`${manifest.name}@${version} built at ${outputRoot}`);
};

main().catch((error) => {
  console.error(error instanceof Error ? error.message : error);
  process.exitCode = 1;
});
