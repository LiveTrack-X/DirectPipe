// SPDX-License-Identifier: GPL-3.0-or-later

const assert = require("node:assert/strict");
const { EventEmitter } = require("node:events");
const { after, afterEach, beforeEach, test } = require("node:test");

class FakeWebSocket extends EventEmitter {
    static CONNECTING = 0;
    static OPEN = 1;
    static CLOSING = 2;
    static CLOSED = 3;
    static instances = [];

    constructor(url) {
        super();
        this.url = url;
        this.readyState = FakeWebSocket.CONNECTING;
        this.sent = [];
        FakeWebSocket.instances.push(this);
    }

    open() {
        this.readyState = FakeWebSocket.OPEN;
        this.emit("open");
    }

    beginClosing() {
        this.readyState = FakeWebSocket.CLOSING;
    }

    finishClose() {
        this.readyState = FakeWebSocket.CLOSED;
        this.emit("close", 1006, Buffer.from("test close"));
    }

    send(message) {
        this.sent.push(message);
    }

    ping() {}

    close() {
        this.readyState = FakeWebSocket.CLOSED;
    }
}

const wsModulePath = require.resolve("ws");
const clientModulePath = require.resolve("./websocket-client");
const originalWsExports = require(wsModulePath);
require.cache[wsModulePath].exports = FakeWebSocket;
delete require.cache[clientModulePath];

const { DirectPipeClient, parseDirectPipeReady } = require("./websocket-client");
const clients = new Set();

function createClient() {
    const client = new DirectPipeClient("ws://localhost:8765", {
        autoReconnect: true,
        reconnectInterval: 10,
        maxReconnectInterval: 10,
    });
    clients.add(client);
    return client;
}

after(() => {
    require.cache[wsModulePath].exports = originalWsExports;
    delete require.cache[clientModulePath];
});

beforeEach(() => {
    FakeWebSocket.instances.length = 0;
});

afterEach(() => {
    for (const client of clients) {
        client.disconnect();
    }
    clients.clear();
});

test("parses the WebSocket port from a DirectPipe discovery packet", () => {
    assert.equal(parseDirectPipeReady?.(Buffer.from("DIRECTPIPE_READY:9001")), 9001);
    assert.equal(parseDirectPipeReady?.("DIRECTPIPE_READY:0"), null);
    assert.equal(parseDirectPipeReady?.("DIRECTPIPE_READY:65536"), null);
    assert.equal(parseDirectPipeReady?.("DIRECTPIPE_READY:not-a-port"), null);
});

test("reconnectNow switches a pending connection to the discovered URL", () => {
    const client = createClient();

    client.connect();
    assert.equal(FakeWebSocket.instances.length, 1);

    client.reconnectNow("ws://localhost:9001");

    assert.equal(FakeWebSocket.instances.length, 2);
    assert.equal(FakeWebSocket.instances[1].url, "ws://localhost:9001");
});

test("switching a connected discovery endpoint publishes connecting state", () => {
    const client = createClient();
    let connectingCount = 0;
    client.on("connecting", () => { connectingCount++; });

    client.connect();
    FakeWebSocket.instances[0].open();
    const beforeSwitch = connectingCount;

    client.reconnectNow("ws://localhost:9001");

    assert.equal(connectingCount, beforeSwitch + 1);
    assert.equal(client.isConnected(), false);
    assert.equal(FakeWebSocket.instances[1].url, "ws://localhost:9001");
});

test("a stale socket error is ignored after a newer socket becomes current", () => {
    const client = createClient();
    let errorCount = 0;
    client.on("error", () => { errorCount++; });

    client.connect();
    const staleSocket = FakeWebSocket.instances[0];
    staleSocket.beginClosing();
    client.reconnectNow();
    const currentSocket = FakeWebSocket.instances[1];
    currentSocket.open();

    staleSocket.emit("error", new Error("stale socket error"));

    assert.equal(errorCount, 0);
    assert.equal(client.isConnected(), true);
});

test("a stale socket close cannot disconnect or reschedule a newer connection", () => {
    const client = createClient();
    let disconnectedCount = 0;
    client.on("disconnected", () => { disconnectedCount++; });

    client.connect();
    const staleSocket = FakeWebSocket.instances[0];
    staleSocket.beginClosing();
    client.reconnectNow();
    const currentSocket = FakeWebSocket.instances[1];
    currentSocket.open();

    staleSocket.finishClose();

    assert.equal(disconnectedCount, 0);
    assert.equal(client.isConnected(), true);
    assert.equal(client._reconnectTimer, null);
    assert.equal(FakeWebSocket.instances.length, 2);
});
