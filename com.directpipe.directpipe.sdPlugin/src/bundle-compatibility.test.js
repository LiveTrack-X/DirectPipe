// SPDX-License-Identifier: GPL-3.0-or-later

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const vm = require("node:vm");
const { EventEmitter, once } = require("node:events");
const { test } = require("node:test");
const { WebSocketServer } = require("ws");

const root = path.resolve(__dirname, "..");
const bundle = fs.readFileSync(path.join(root, "bin/plugin.js"), "utf8");
const manifest = JSON.parse(fs.readFileSync(path.join(root, "manifest.json"), "utf8"));

function fixture(t, definition, port) {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), "DirectPipe-SDK-"));
    fs.writeFileSync(path.join(directory, "manifest.json"), JSON.stringify(definition));
    const processShim = new EventEmitter();
    Object.assign(processShim, {
        argv: [process.execPath, "plugin.js", "-port", String(port), "-pluginUUID", "fixture-plugin",
            "-registerEvent", "registerPlugin", "-info", JSON.stringify({
                application: { version: "6.9.0", platform: "windows", language: "en" },
                plugin: { uuid: manifest.UUID, version: "4.5.0.0" },
                devices: [{ id: "owned-device", name: "Owned fixture", type: 0, size: { columns: 5, rows: 3 } }],
            })],
        execArgv: [], env: {}, platform: process.platform, versions: process.versions,
        version: process.version, cwd: () => directory, nextTick: process.nextTick.bind(process),
        stdout: process.stdout, stderr: process.stderr,
    });
    const udp = new EventEmitter();
    const binds = [];
    udp.bind = (...args) => binds.push(args); // Never opens a port or emits listening.
    udp.close = () => {};
    const timers = new Set();
    const intervals = new Set();
    const context = {
        module: { exports: {} }, exports: {}, Buffer, console, URL, TextEncoder, TextDecoder,
        AbortController, AbortSignal, crypto: require("node:crypto").webcrypto,
        process: processShim, __dirname: directory, __filename: path.join(directory, "plugin.js"),
        setTimeout: (...args) => { const timer = setTimeout(...args); timers.add(timer); return timer; },
        clearTimeout, setInterval: (...args) => { const timer = setInterval(...args); intervals.add(timer); return timer; },
        clearInterval, setImmediate, clearImmediate, queueMicrotask,
        fetch: async () => { throw new Error("Live DirectPipe HTTP access is disabled in this fixture"); },
        require: (name) => {
            if (name === "dgram" || name === "node:dgram") return { createSocket: () => udp };
            if (name === "process" || name === "node:process") return processShim;
            return require(name);
        },
    };
    t.after(() => {
        for (const timer of timers) clearTimeout(timer);
        for (const timer of intervals) clearInterval(timer);
        fs.rmSync(directory, { recursive: true, force: true });
    });
    return { execute: () => vm.runInNewContext(bundle, context, { filename: "plugin.js" }), context, binds };
}

test("actual bundle registers every unchanged manifest action with the real SDK", (t) => {
    assert.equal(manifest.Actions.length, 10);
    for (const action of manifest.Actions) {
        const missing = { ...manifest, Actions: manifest.Actions.filter((item) => item.UUID !== action.UUID) };
        const instance = fixture(t, missing, 0);
        assert.throws(instance.execute, (error) => error.message.includes(`not found within the manifest: ${action.UUID}`));
        assert.equal(instance.binds.length, 0);
    }
});

test("actual bundled SDK registers over owned WebSocket and routes a real key action", { timeout: 5000 }, async (t) => {
    const server = new WebSocketServer({ host: "127.0.0.1", port: 0 });
    t.after(() => { for (const socket of server.clients) socket.terminate(); server.close(); });
    await once(server, "listening");
    const connected = once(server, "connection");
    const instance = fixture(t, manifest, server.address().port);
    instance.execute();
    const sent = [];
    instance.context.module.exports.dpClient.sendAction = (...args) => sent.push(args);
    const [socket] = await connected;
    const [raw] = await once(socket, "message");
    assert.deepEqual(JSON.parse(raw), { event: "registerPlugin", uuid: "fixture-plugin" });
    assert.deepEqual(instance.binds, [[8767, "127.0.0.1"]]);
    const message = {
        action: "com.directpipe.directpipe.bypass-toggle", context: "owned-test-key", device: "owned-device",
        payload: { controller: "Keypad", settings: { pluginNumber: 2 }, state: 0, isInMultiAction: false,
            coordinates: { column: 0, row: 0 } },
    };
    for (const event of ["willAppear", "keyDown", "keyUp"]) socket.send(JSON.stringify({ ...message, event }));
    const deadline = Date.now() + 2000;
    while (sent.length === 0 && Date.now() < deadline) await new Promise((resolve) => setTimeout(resolve, 10));
    assert.equal(JSON.stringify(sent), JSON.stringify([["plugin_bypass", { index: 1 }]]));
    socket.send(JSON.stringify({ ...message, event: "willDisappear" }));
    socket.close();
});
