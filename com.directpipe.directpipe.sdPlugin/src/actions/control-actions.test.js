// SPDX-License-Identifier: GPL-3.0-or-later

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { after, afterEach, test } = require("node:test");

const sentActions = [];
let currentState = { data: { volumes: { input: 1.0, output: 1.0, monitor: 1.0 } } };
let pluginParams = [];
let pluginParamRequestCount = 0;
let pluginParamRequest = null;

const pluginModulePath = require.resolve("../plugin");
const hostApiModulePath = require.resolve("../utils/host-api");
const originalPluginModule = require.cache[pluginModulePath];
const originalHostApiModule = require.cache[hostApiModulePath];

require.cache[pluginModulePath] = {
    id: pluginModulePath,
    filename: pluginModulePath,
    loaded: true,
    exports: {
        dpClient: {
            sendAction(action, params) {
                sentActions.push({ action, params });
            },
        },
        getCurrentState() {
            return currentState;
        },
    },
};

require.cache[hostApiModulePath] = {
    id: hostApiModulePath,
    filename: hostApiModulePath,
    loaded: true,
    exports: {
        async getPluginParams() {
            pluginParamRequestCount++;
            if (pluginParamRequest)
                return pluginParamRequest();
            return pluginParams;
        },
    },
};

delete require.cache[require.resolve("./plugin-param")];
delete require.cache[require.resolve("./volume-control")];
const { PluginParamAction } = require("./plugin-param");
const { VolumeControlAction } = require("./volume-control");

const liveTimers = new Set();

function createSdkAction(id) {
    return {
        id,
        alerts: 0,
        feedback: [],
        states: [],
        titles: [],
        setFeedback(value) { this.feedback.push(value); },
        setState(value) { this.states.push(value); },
        setTitle(value) { this.titles.push(value); },
        showAlert() { this.alerts++; },
    };
}

function event(action, settings, ticks = 0) {
    return {
        action,
        payload: { settings, ticks },
    };
}

function trackTimers(instance) {
    liveTimers.add(instance);
    return instance;
}

function clearInstanceTimers(instance) {
    for (const timer of instance._dialSendTimers?.values?.() ?? [])
        clearTimeout(timer);
    instance._dialSendTimers?.clear?.();
    instance._pendingValues?.clear?.();
    instance._pendingDialValues?.clear?.();
    instance._rotationQueues?.clear?.();
}

afterEach(() => {
    for (const instance of liveTimers)
        clearInstanceTimers(instance);
    liveTimers.clear();
    sentActions.length = 0;
    pluginParams = [];
    pluginParamRequestCount = 0;
    pluginParamRequest = null;
    currentState = { data: { volumes: { input: 1.0, output: 1.0, monitor: 1.0 } } };
});

after(() => {
    if (originalPluginModule)
        require.cache[pluginModulePath] = originalPluginModule;
    else
        delete require.cache[pluginModulePath];

    if (originalHostApiModule)
        require.cache[hostApiModulePath] = originalHostApiModule;
    else
        delete require.cache[hostApiModulePath];
});

test("plugin parameter display and first rotation use the host value", async () => {
    pluginParams = [{ index: 3, name: "Mix", value: 0.73, defaultValue: 0.12 }];
    const action = createSdkAction("param-current");
    const instance = trackTimers(new PluginParamAction());
    const settings = { pluginIndex: 1, paramIndex: 3, pluginName: "Reverb", paramName: "Mix" };

    await instance.onWillAppear(event(action, settings));

    assert.equal(action.feedback.at(-1).indicator.value, 73);
    assert.equal(action.feedback.at(-1).value, "Mix: 73%");

    await instance.onDialRotate(event(action, settings, 1));

    assert.deepEqual(sentActions, [{
        action: "set_plugin_parameter",
        params: { pluginIndex: 1, paramIndex: 3, value: 0.75 },
    }]);
});

test("plugin parameter dial press restores the VST-reported default", async () => {
    pluginParams = [{ index: 2, name: "Threshold", value: 0.81, defaultValue: 0.27 }];
    const action = createSdkAction("param-default");
    const instance = trackTimers(new PluginParamAction());
    const settings = { pluginIndex: 0, paramIndex: 2, paramName: "Threshold" };

    await instance.onWillAppear(event(action, settings));
    await instance.onDialDown(event(action, settings));

    assert.deepEqual(sentActions.at(-1), {
        action: "set_plugin_parameter",
        params: { pluginIndex: 0, paramIndex: 2, value: 0.27 },
    });
    assert.equal(action.feedback.at(-1).indicator.value, 27);
});

test("plugin parameter does not fall back to 50 percent when host state is unavailable", async () => {
    pluginParams = null;
    const action = createSdkAction("param-unavailable");
    const instance = trackTimers(new PluginParamAction());
    const settings = { pluginIndex: 0, paramIndex: 0 };

    await instance.onWillAppear(event(action, settings));
    await instance.onDialRotate(event(action, settings, 1));

    assert.equal(sentActions.length, 0);
    assert.equal(action.alerts, 1);
    assert.equal(action.feedback.at(-1).indicator.enabled, false);
});

test("plugin parameter treats null host values as unavailable", async () => {
    pluginParams = [{ index: 0, name: "Gain", value: null, defaultValue: null }];
    const action = createSdkAction("param-null");
    const instance = trackTimers(new PluginParamAction());
    const settings = { pluginIndex: 0, paramIndex: 0 };

    await instance.onWillAppear(event(action, settings));
    await instance.onDialRotate(event(action, settings, 1));

    assert.equal(sentActions.length, 0);
    assert.equal(action.alerts, 1);
    assert.equal(action.feedback.at(-1).indicator.enabled, false);
});

test("preset state change refreshes the displayed plugin parameter value", async () => {
    pluginParams = [{ index: 0, name: "Gain", value: 0.2, defaultValue: 0.5 }];
    const action = createSdkAction("param-preset");
    const instance = trackTimers(new PluginParamAction());
    const settings = { pluginIndex: 0, paramIndex: 0, paramName: "Gain" };
    Object.defineProperty(instance, "actions", { value: [action] });

    await instance.onWillAppear(event(action, settings));
    pluginParams = [{ index: 0, name: "Gain", value: 0.84, defaultValue: 0.5 }];
    await instance.updateAllFromState({
        data: { active_slot: 1, preset: "B", plugins: [{ name: "Compressor", loaded: true }] },
    });

    assert.equal(action.feedback.at(-1).indicator.value, 84);
    const requestsAfterChange = pluginParamRequestCount;

    await instance.updateAllFromState({
        data: { active_slot: 1, preset: "B", plugins: [{ name: "Compressor", loaded: true }] },
    });
    assert.equal(pluginParamRequestCount, requestsAfterChange);
});

test("first parameter turn after idle rebases on an external host change", async () => {
    pluginParams = [{ index: 0, name: "Gain", value: 0.2, defaultValue: 0.5 }];
    const action = createSdkAction("param-external-change");
    const instance = trackTimers(new PluginParamAction());
    const settings = { pluginIndex: 0, paramIndex: 0, paramName: "Gain" };

    await instance.onWillAppear(event(action, settings));
    pluginParams = [{ index: 0, name: "Gain", value: 0.8, defaultValue: 0.5 }];
    await instance.onDialRotate(event(action, settings, 1));

    assert.equal(pluginParamRequestCount, 2);
    assert.equal(sentActions.at(-1).action, "set_plugin_parameter");
    assert.equal(sentActions.at(-1).params.pluginIndex, 0);
    assert.equal(sentActions.at(-1).params.paramIndex, 0);
    assert.ok(Math.abs(sentActions.at(-1).params.value - 0.82) < 1e-12);
});

test("a disconnected state invalidates an in-flight parameter refresh", async () => {
    let resolveRequest;
    pluginParamRequest = () => new Promise((resolve) => { resolveRequest = resolve; });
    const action = createSdkAction("param-stale-refresh");
    const instance = trackTimers(new PluginParamAction());
    const settings = { pluginIndex: 0, paramIndex: 0, paramName: "Gain" };
    Object.defineProperty(instance, "actions", { value: [action] });

    const refresh = instance.onWillAppear(event(action, settings));
    await Promise.resolve();
    instance.setDisconnectedState();
    resolveRequest([{ index: 0, name: "Gain", value: 0.9, defaultValue: 0.5 }]);
    await refresh;

    assert.equal(action.feedback.at(-1).title, "Disconnected");
    assert.equal(instance._lastValues.size, 0);
});

test("a disappearing action cannot send after an in-flight parameter refresh", async () => {
    let resolveRequest;
    pluginParamRequest = () => new Promise((resolve) => { resolveRequest = resolve; });
    const action = createSdkAction("param-disappeared");
    const instance = trackTimers(new PluginParamAction());
    const settings = { pluginIndex: 0, paramIndex: 0, paramName: "Gain" };

    const appear = instance.onWillAppear(event(action, settings));
    await Promise.resolve();
    resolveRequest([{ index: 0, name: "Gain", value: 0.2, defaultValue: 0.5 }]);
    await appear;

    const rotate = instance.onDialRotate(event(action, settings, 1));
    await Promise.resolve();
    instance.onWillDisappear(event(action, settings));
    resolveRequest([{ index: 0, name: "Gain", value: 0.8, defaultValue: 0.5 }]);
    await rotate;

    assert.equal(sentActions.length, 0);
});

test("plugin parameter throttle is isolated by action and parameter", async () => {
    pluginParams = [
        { index: 0, name: "A", value: 0.2, defaultValue: 0.1 },
        { index: 1, name: "B", value: 0.7, defaultValue: 0.4 },
    ];
    const actionA = createSdkAction("param-a");
    const actionB = createSdkAction("param-b");
    const instance = trackTimers(new PluginParamAction());
    const settingsA = { pluginIndex: 0, paramIndex: 0 };
    const settingsB = { pluginIndex: 0, paramIndex: 1 };

    await instance.onWillAppear(event(actionA, settingsA));
    await instance.onWillAppear(event(actionB, settingsB));
    await Promise.all([
        instance.onDialRotate(event(actionA, settingsA, 1)),
        instance.onDialRotate(event(actionB, settingsB, 1)),
    ]);

    assert.equal(sentActions.length, 2);
    assert.deepEqual(sentActions.map(({ params }) => params.paramIndex), [0, 1]);
});

test("plugin parameter throttle keeps the latest value for one action", async () => {
    pluginParams = [{ index: 0, name: "Mix", value: 0.2, defaultValue: 0.5 }];
    const action = createSdkAction("param-coalesce");
    const instance = trackTimers(new PluginParamAction());
    const settings = { pluginIndex: 0, paramIndex: 0 };

    await instance.onWillAppear(event(action, settings));
    await instance.onDialRotate(event(action, settings, 1));
    await instance.onDialRotate(event(action, settings, 2));
    await new Promise((resolve) => setTimeout(resolve, 70));

    assert.equal(sentActions.length, 2);
    assert.equal(sentActions[0].params.value, 0.22);
    assert.equal(sentActions[1].params.value, 0.26);
});

test("plugin parameter reset cancels a pending coalesced rotate value", async () => {
    pluginParams = [{ index: 0, name: "Mix", value: 0.2, defaultValue: 0.5 }];
    const action = createSdkAction("param-reset-pending");
    const instance = trackTimers(new PluginParamAction());
    const settings = { pluginIndex: 0, paramIndex: 0 };

    await instance.onWillAppear(event(action, settings));
    await instance.onDialRotate(event(action, settings, 1));
    await instance.onDialRotate(event(action, settings, 2));
    await instance.onDialDown(event(action, settings));
    await new Promise((resolve) => setTimeout(resolve, 70));

    assert.deepEqual(sentActions.map(({ params }) => params.value), [0.22, 0.5]);
});

test("plugin parameter reset remains final when rotate is still queued", async () => {
    pluginParams = [{ index: 0, name: "Mix", value: 0.2, defaultValue: 0.5 }];
    const action = createSdkAction("param-reset-queued");
    const instance = trackTimers(new PluginParamAction());
    const settings = { pluginIndex: 0, paramIndex: 0 };

    await instance.onWillAppear(event(action, settings));

    let resolveRequest;
    pluginParamRequest = () => new Promise((resolve) => { resolveRequest = resolve; });
    const rotate = instance.onDialRotate(event(action, settings, 1));
    const reset = instance.onDialDown(event(action, settings));
    await Promise.resolve();
    await Promise.resolve();
    resolveRequest([{ index: 0, name: "Mix", value: 0.8, defaultValue: 0.5 }]);
    await Promise.all([rotate, reset]);

    assert.equal(sentActions.length, 2);
    assert.ok(Math.abs(sentActions[0].params.value - 0.82) < 1e-12);
    assert.equal(sentActions[1].params.value, 0.5);
});

test("connection transition cancels a queued plugin parameter rotation", async () => {
    pluginParams = [{ index: 0, name: "Mix", value: 0.2, defaultValue: 0.5 }];
    const action = createSdkAction("param-connect-cancel");
    const instance = trackTimers(new PluginParamAction());
    const settings = { pluginIndex: 0, paramIndex: 0 };
    Object.defineProperty(instance, "actions", { value: [action] });

    await instance.onWillAppear(event(action, settings));

    let resolveRequest;
    pluginParamRequest = () => new Promise((resolve) => { resolveRequest = resolve; });
    const rotate = instance.onDialRotate(event(action, settings, 1));
    await Promise.resolve();
    await Promise.resolve();
    instance.setDisconnectedState();
    resolveRequest([{ index: 0, name: "Mix", value: 0.8, defaultValue: 0.5 }]);
    await rotate;

    assert.equal(sentActions.length, 0);
    assert.equal(action.feedback.at(-1).title, "Disconnected");
});

test("volume throttle is isolated by action and target", () => {
    const actionA = createSdkAction("volume-input");
    const actionB = createSdkAction("volume-output");
    const instance = trackTimers(new VolumeControlAction());

    instance.onDialRotate(event(actionA, { target: "input" }, 1));
    instance.onDialRotate(event(actionB, { target: "output" }, -1));

    assert.equal(sentActions.length, 2);
    assert.deepEqual(sentActions.map(({ params }) => params.target), ["input", "output"]);
});

test("volume connection transition cancels a pending throttled value", async () => {
    const action = createSdkAction("volume-reconnect");
    const instance = trackTimers(new VolumeControlAction());
    Object.defineProperty(instance, "actions", { value: [action] });
    const settings = { target: "output" };

    instance.onDialRotate(event(action, settings, 1));
    instance.onDialRotate(event(action, settings, 1));
    instance.setDisconnectedState();
    await new Promise((resolve) => setTimeout(resolve, 70));

    assert.equal(sentActions.length, 1);
    assert.equal(instance._pendingDialValues.size, 0);
    assert.equal(instance._localOverrideUntil.size, 0);
});

test("manifest advertises only implemented encoder gestures", () => {
    const root = path.resolve(__dirname, "..", "..");
    const manifest = JSON.parse(fs.readFileSync(path.join(root, "manifest.json"), "utf8"));
    const byId = (id) => manifest.Actions.find((action) => action.UUID === id);

    assert.deepEqual(
        byId("com.directpipe.directpipe.preset-bar").Encoder.TriggerDescription,
        { Rotate: "Cycle presets" },
    );
    assert.deepEqual(
        byId("com.directpipe.directpipe.volume-control").Encoder.TriggerDescription,
        { Rotate: "Adjust volume", Push: "Toggle mute" },
    );
    assert.deepEqual(
        byId("com.directpipe.directpipe.performance-monitor").Encoder.TriggerDescription,
        { Push: "Reset XRun count" },
    );
});

test("npm package script delegates to the official Stream Deck pack command", () => {
    const root = path.resolve(__dirname, "..", "..");
    const packageJson = JSON.parse(fs.readFileSync(path.join(root, "package.json"), "utf8"));

    assert.match(packageJson.scripts.package, /^streamdeck pack /);
    assert.equal(fs.existsSync(path.join(root, "scripts", "package.mjs")), false);
});
