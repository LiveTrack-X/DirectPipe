// SPDX-License-Identifier: GPL-3.0-or-later
import { build } from "esbuild";
import { readFile, writeFile } from "node:fs/promises";

await build({
    entryPoints: ["src/plugin.js"],
    outfile: "bin/plugin.js",
    bundle: true,
    platform: "node",
    target: "node20",
    format: "cjs",
    sourcemap: false,
    legalComments: "inline",
});

const notices = [];
for (const name of ["@elgato/streamdeck", "@elgato/utils", "@elgato/schemas", "zod", "ws"]) {
    const root = `node_modules/${name}`;
    const info = JSON.parse(await readFile(`${root}/package.json`, "utf8"));
    notices.push(`${name} ${info.version}\n${await readFile(`${root}/LICENSE`, "utf8")}`);
}
await writeFile("bin/THIRD_PARTY_NOTICES.txt", notices.join("\n\n"));
