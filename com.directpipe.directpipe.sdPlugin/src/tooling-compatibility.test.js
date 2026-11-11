// SPDX-License-Identifier: GPL-3.0-or-later
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { spawnSync } = require("node:child_process");
const { test } = require("node:test");

test("syntax lint retains CommonJS parsing and rejects invalid or ESM source", (t) => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), "DirectPipe-lint-"));
    fs.mkdirSync(path.join(directory, "src"));
    t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
    const lint = path.resolve(__dirname, "../scripts/lint.mjs");
    for (const [source, accepted] of [
        ["module.exports = (value) => value?.name ?? 'ok';", true],
        ["const = ;", false],
        ["import fs from 'node:fs'; export default fs;", false],
    ]) {
        fs.writeFileSync(path.join(directory, "src/fixture.js"), source);
        const result = spawnSync(process.execPath, [lint], { cwd: directory, encoding: "utf8" });
        assert.ifError(result.error);
        assert.equal(result.status === 0, accepted, result.stdout + result.stderr);
    }
});
