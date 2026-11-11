// SPDX-License-Identifier: GPL-3.0-or-later

const assert = require("node:assert/strict");
const { test } = require("node:test");

test("oversized bounded collections fail before allocating per-element issues", () => {
    const z = require("zod/v4-mini");
    const result = z.safeParse(z.array(z.number()).check(z.maxLength(3)), Array(10000).fill("invalid"));
    assert.equal(result.success, false);
    assert.equal(result.error.issues.length, 1);
    assert.equal(result.error.issues[0].code, "too_big");
});

test("Elgato JSON-RPC schemas retain request and response acceptance", async () => {
    const { JsonRpcRequest, JsonRpcResponse, JsonRpcError } = await import("@elgato/utils/rpc");
    for (const params of [undefined, { value: 3 }, [1, "text", null]]) {
        assert.equal(JsonRpcRequest.safeParse({ jsonrpc: "2.0", method: "echo", id: "1", params }).success, true);
    }
    assert.equal(JsonRpcRequest.safeParse({ jsonrpc: "1.0", method: "echo" }).success, false);
    assert.equal(JsonRpcRequest.safeParse({ jsonrpc: "2.0", method: 7 }).success, false);
    assert.equal(JsonRpcResponse.safeParse({ jsonrpc: "2.0", id: "1", result: { ok: true } }).success, true);
    for (const result of [null, undefined]) {
        assert.equal(JsonRpcResponse.safeParse({ jsonrpc: "2.0", id: "1", result }).success, true);
    }
    assert.equal(JsonRpcResponse.safeParse({ jsonrpc: "2.0", id: "1" }).success, true);
    assert.equal(JsonRpcResponse.safeParse({ jsonrpc: "2.0", id: "1", result: null, extra: true }).success, false);
    assert.equal(JsonRpcResponse.safeParse({ jsonrpc: "2.0", id: "1", error: { code: -32601, message: "missing", data: null } }).success, true);
    assert.equal(JsonRpcError.safeParse({ code: "invalid", message: "error", data: null }).success, false);
    assert.equal(JsonRpcError.safeParse({ code: -32601, message: "missing" }).success, true);
    assert.equal(JsonRpcResponse.safeParse({ jsonrpc: "2.0", id: "1", error: { code: -32601, message: "missing" } }).success, true);
});

test("Elgato real RPC client and server exchange success and method errors", { timeout: 2000 }, async () => {
    const { RpcClient, RpcServer } = await import("@elgato/utils/rpc");
    const server = new RpcServer((message) => client.receive(message));
    const client = new RpcClient((message) => server.receive(message));
    server.addMethod("echo", (parameters) => parameters);
    assert.deepEqual((await client.request({ method: "echo", params: { value: 42 }, timeout: 500 })).result, { value: 42 });
    assert.equal((await client.request({ method: "absent", timeout: 500 })).error.code, -32601);
    assert.equal((await client.request({ method: "echo", timeout: 500 })).result, null);
    assert.equal(await server.receive({ jsonrpc: "2.0", method: 123 }), false);
});
