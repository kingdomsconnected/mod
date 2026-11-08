import { spawn } from "node:child_process";
import { createRequire } from "node:module";

// Watches both halves in one terminal. They are separate TypeScript programs (see the README), so
// this runs one tsc --watch per tsconfig and stops both when either exits or on Ctrl+C.

const tsc = createRequire(import.meta.url).resolve("typescript/bin/tsc");
const programs = ["tsconfig.json", "src/client/tsconfig.json"];

const children = programs.map((project) => spawn(process.execPath, [tsc, "-p", project, "--watch", "--preserveWatchOutput"], { stdio: "inherit" }));

const stop = (code) => {
  for (const child of children) child.kill();
  process.exitCode = code;
};
for (const child of children) child.on("exit", (code) => stop(code ?? 0));
process.on("SIGINT", () => stop(0));
