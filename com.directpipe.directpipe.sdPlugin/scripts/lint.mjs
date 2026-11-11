// SPDX-License-Identifier: GPL-3.0-or-later
// The previous ESLint configuration enabled no rules: it only parsed src JS.
// Keep that contract with Node's maintained CommonJS parser, without glob deps.
import { readFile, readdir } from "node:fs/promises";
import { join } from "node:path";
import { spawnSync } from "node:child_process";

async function check(directory) {
    let count = 0;
    for (const entry of await readdir(directory, { withFileTypes: true })) {
        const file = join(directory, entry.name);
        if (entry.isDirectory()) count += await check(file);
        else if (entry.isFile() && file.endsWith(".js")) {
            const result = spawnSync(process.execPath, ["--check", "--input-type=commonjs"], {
                input: await readFile(file, "utf8"), encoding: "utf8",
            });
            if (result.error) throw result.error;
            if (result.status !== 0) {
                console.error(`${file}\n${result.stderr}`);
                process.exit(result.status ?? 1);
            }
            count++;
        }
    }
    return count;
}
console.log(`Syntax checked ${await check("src")} JavaScript files.`);
