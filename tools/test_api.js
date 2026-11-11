// DirectPipe Automated API Test Suite
// Usage: node tools/test_api.js
//   - DirectPipe.exe must be running on localhost
//   - HTTP API port 8766, WebSocket port 8765

const http = require('http');
const fs = require('fs');
const net = require('net');
const crypto = require('crypto');

const API = 'http://127.0.0.1:8766/api';
const WS_PORT = 8765;
// Isolated candidate fixtures pass explicit paths; ordinary manual runs keep
// the existing Windows profile locations.
const CONFIG_DIR = process.env.DIRECTPIPE_TEST_CONFIG_DIR || process.env.APPDATA + '\\DirectPipe';

// ──────────── Helpers ────────────

function api(path) {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error('API timeout: ' + path)), 5000);
    http.get(API + path, res => {
      let d = '';
      res.on('data', c => d += c);
      res.on('end', () => {
        clearTimeout(timeout);
        resolve({ code: res.statusCode, body: d, json: () => JSON.parse(d) });
      });
    }).on('error', e => { clearTimeout(timeout); reject(e); });
  });
}

async function getState() { return (await api('/status')).json().data; }
function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }
function nearEq(a, b, eps = 0.05) { return Math.abs(a - b) < eps; }

// Poll until active_slot matches target (handles heavy plugin loads)
async function waitForSlot(target, timeoutMs = 8000) {
  const start = Date.now();
  while (Date.now() - start < timeoutMs) {
    const s = await getState();
    if (s.active_slot === target) return s;
    await sleep(300);
  }
  return await getState(); // final read even on timeout
}

let pass = 0, fail = 0, skip = 0;
function check(name, expected, actual) {
  if (Object.is(actual, expected)) {
    console.log('  \x1b[32mPASS\x1b[0m:', name);
    pass++;
  } else {
    console.log('  \x1b[31mFAIL\x1b[0m:', name, '(expected:', expected, ', got:', actual, ')');
    fail++;
  }
}
function isAcceptedResponse(response, action) {
  const body = response.json();
  return response.code === 202 && body?.ok === true && body.accepted === true && body.action === action;
}
function checkAccepted(name, response, action) {
  check(name + ' (202 accepted)', true, isAcceptedResponse(response, action));
}
function isStateBroadcast(message) {
  const data = message?.data;
  return message?.type === 'state' && data !== null && typeof data === 'object' && !Array.isArray(data)
    && Array.isArray(data.plugins) && data.volumes !== null && typeof data.volumes === 'object'
    && ['input', 'output', 'monitor'].every(name => Number.isFinite(data.volumes[name]))
    && Number.isInteger(data.active_slot);
}
function canSkipCheck(name, environment = process.env) {
  return !environment.DIRECTPIPE_TEST_CONFIG_DIR
    || (name === 'Recorded audio sample progression' && environment.DIRECTPIPE_TEST_NO_AUDIO === '1');
}
function skipped(name, reason) {
  if (!canSkipCheck(name)) {
    check(name + ' required by isolated fixture: ' + reason, true, false);
    return;
  }
  console.log('  \x1b[33mSKIP\x1b[0m:', name, '—', reason);
  skip++;
}

// ──────────── WebSocket minimal client ────────────

class WebSocketResponseParser {
  constructor(key) {
    this.accept = crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
    this.buffer = Buffer.alloc(0);
    this.accepted = false;
    this.closed = false;
    this.messages = [];
    this.pings = [];
    this.fragments = [];
    this.fragmentBytes = 0;
  }
  push(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    if (!this.accepted) {
      const end = this.buffer.indexOf('\r\n\r\n');
      if (end < 0) {
        if (this.buffer.length > 16384) throw new Error('Oversized WebSocket handshake');
        return;
      }
      const lines = this.buffer.subarray(0, end).toString('ascii').split('\r\n');
      if (!/^HTTP\/1\.[01] 101(?: |$)/.test(lines.shift())) throw new Error('WebSocket upgrade did not return 101');
      const headers = new Map(lines.map(line => {
        const colon = line.indexOf(':');
        return [line.slice(0, colon).trim().toLowerCase(), line.slice(colon + 1).trim()];
      }));
      if (headers.get('sec-websocket-accept') !== this.accept) throw new Error('Invalid WebSocket accept proof');
      if (headers.get('upgrade')?.toLowerCase() !== 'websocket'
          || !headers.get('connection')?.toLowerCase().split(/\s*,\s*/).includes('upgrade'))
        throw new Error('Invalid WebSocket upgrade headers');
      this.buffer = this.buffer.subarray(end + 4);
      this.accepted = true;
    }
    while (this.buffer.length >= 2) {
      const first = this.buffer[0], second = this.buffer[1];
      const final = (first & 0x80) !== 0, opcode = first & 15;
      if (first & 0x70) throw new Error('Unexpected WebSocket reserved bits');
      if (second & 0x80) throw new Error('Unexpected masked server frame');
      let length = second & 127, offset = 2;
      if (length === 126) {
        if (this.buffer.length < 4) return;
        length = this.buffer.readUInt16BE(2); offset = 4;
      } else if (length === 127) {
        if (this.buffer.length < 10) return;
        const wideLength = this.buffer.readBigUInt64BE(2);
        if (wideLength > 1024n * 1024n) throw new Error('Oversized WebSocket frame');
        length = Number(wideLength); offset = 10;
      }
      if (length > 1024 * 1024) throw new Error('Oversized WebSocket frame');
      if (this.buffer.length < offset + length) return;
      const payload = this.buffer.subarray(offset, offset + length);
      this.buffer = this.buffer.subarray(offset + length);
      if (opcode >= 8) {
        if (!final || length > 125) throw new Error('Invalid WebSocket control frame');
        if (opcode === 8) { this.closed = true; return; }
        if (opcode === 9) this.pings.push(payload);
        else if (opcode !== 10) throw new Error('Invalid WebSocket control opcode');
        continue;
      }
      if (opcode === 1) {
        if (this.fragments.length) throw new Error('Overlapping WebSocket text messages');
      } else if (opcode !== 0 || !this.fragments.length) {
        throw new Error('Unexpected WebSocket data opcode');
      }
      this.fragments.push(payload); this.fragmentBytes += length;
      if (this.fragmentBytes > 1024 * 1024) throw new Error('Oversized WebSocket message');
      if (final) {
        this.messages.push(Buffer.concat(this.fragments).toString('utf8'));
        this.fragments = []; this.fragmentBytes = 0;
      }
    }
  }
}

function wsConnect(port) {
  return new Promise((resolve, reject) => {
    const key = crypto.randomBytes(16).toString('base64');
    const parser = new WebSocketResponseParser(key);
    const sock = new net.Socket();
    const readers = [];
    let registered = false, closedError = null;
    const fail = (error) => {
      closedError ??= error;
      clearTimeout(timeout);
      reject(error);
      for (const reader of readers.splice(0)) { clearTimeout(reader.timeout); reader.reject(error); }
      sock.destroy();
    };
    const timeout = setTimeout(() => fail(new Error('WebSocket handshake timed out')), 3000);
    const readFrame = () => {
      if (parser.messages.length) return Promise.resolve(parser.messages.shift());
      if (closedError) return Promise.reject(closedError);
      return new Promise((resolveFrame, rejectFrame) => {
        const reader = { resolve: resolveFrame, reject: rejectFrame };
        reader.timeout = setTimeout(() => {
          const index = readers.indexOf(reader);
          if (index >= 0) readers.splice(index, 1);
          rejectFrame(new Error('WebSocket state frame timed out'));
        }, 3000);
        readers.push(reader);
      });
    };
    sock.connect(port, '127.0.0.1', () => {
      sock.write('GET / HTTP/1.1\r\nHost: 127.0.0.1:' + port
        + '\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ' + key
        + '\r\nSec-WebSocket-Version: 13\r\n\r\n');
    });
    sock.on('data', chunk => {
      try {
        parser.push(chunk);
        for (const payload of parser.pings.splice(0)) {
          const mask = crypto.randomBytes(4), pong = Buffer.alloc(6 + payload.length);
          pong[0] = 0x8a; pong[1] = 0x80 | payload.length; mask.copy(pong, 2);
          for (let i = 0; i < payload.length; i++) pong[6 + i] = payload[i] ^ mask[i % 4];
          sock.write(pong);
        }
        if (parser.accepted && !registered) {
          registered = true; clearTimeout(timeout);
          resolve({ sock, accepted: true, readFrame, close: () => sock.destroy() });
        }
        while (readers.length && parser.messages.length) {
          const reader = readers.shift(); clearTimeout(reader.timeout); reader.resolve(parser.messages.shift());
        }
        if (parser.closed) fail(new Error('WebSocket closed by server'));
      } catch (error) { fail(error); }
    });
    sock.on('error', fail);
    sock.on('close', () => fail(new Error('WebSocket connection closed')));
  });
}

module.exports = { WebSocketResponseParser, wsConnect, isAcceptedResponse, isStateBroadcast, canSkipCheck };

// ──────────── Tests ────────────

if (require.main === module) (async () => {
  const startTime = Date.now();
  const SLOT_WAIT = 1500;

  console.log('============================================');
  console.log('  DirectPipe Automated Test Suite');
  console.log('============================================');

  // Check server is running
  try {
    await api('/status');
  } catch (e) {
    console.log('\n\x1b[31mERROR: DirectPipe is not running on port 8766\x1b[0m');
    console.log('Start DirectPipe.exe first, then run this test.');
    process.exit(1);
  }

  // ──── Snapshot initial state ────
  let s = await getState();
  if (process.env.DIRECTPIPE_TEST_CONFIG_DIR) {
    const fixture = JSON.parse(fs.readFileSync(CONFIG_DIR + '\\settings.dppreset', 'utf8').replace(/^\uFEFF/, ''));
    const expectedPlugins = fixture.plugins.map(plugin => plugin.name);
    const deadline = Date.now() + 8000;
    while ((s.active_slot !== fixture.activeSlot
        || JSON.stringify(s.plugins.map(plugin => plugin.name)) !== JSON.stringify(expectedPlugins))
        && Date.now() < deadline) {
      await sleep(200); s = await getState();
    }
    check('Isolated fixture active slot loaded', fixture.activeSlot, s.active_slot);
    check('Isolated fixture plugin chain loaded', JSON.stringify(expectedPlugins), JSON.stringify(s.plugins.map(plugin => plugin.name)));
  }
  if (s.muted) { await api('/mute/panic'); await sleep(500); s = await getState(); }
  const initialState = {
    monitorVolume: s.volumes.monitor,
    inputVolume: s.volumes.input,
    outputVolume: s.volumes.output,
    outputMuted: s.output_muted,
    inputMuted: s.input_muted,
    monitorEnabled: s.monitor_enabled,
    ipcEnabled: s.ipc_enabled,
    masterBypassed: s.master_bypassed,
    activeSlot: s.active_slot,
    muted: false, // we already unmuted above
    pluginBypass: s.plugins.map(p => p.bypass),
  };
  console.log('\n  Initial state captured:');
  console.log('    Slot:', 'ABCDE'[initialState.activeSlot],
    '| Monitor vol:', initialState.monitorVolume.toFixed(2),
    '| Input vol:', initialState.inputVolume.toFixed(2));
  console.log('    Monitor:', initialState.monitorEnabled ? 'ON' : 'OFF',
    '| IPC:', initialState.ipcEnabled ? 'ON' : 'OFF',
    '| Master bypass:', initialState.masterBypassed ? 'ON' : 'OFF');
  console.log('    Plugins:', initialState.pluginBypass.length,
    '| Output muted:', initialState.outputMuted,
    '| Input muted:', initialState.inputMuted);

  // ──── Restore function ────
  async function restoreState() {
    console.log('\n  Restoring initial state...');
    try {
      // Unmute if muted (panic)
      s = await getState();
      if (s.muted) { await api('/mute/panic'); await sleep(400); }

      // Stop recording if active
      s = await getState();
      if (s.recording) { await api('/recording/toggle'); await sleep(400); }

      // Restore volumes
      await api('/volume/monitor/' + initialState.monitorVolume);
      await api('/volume/input/' + initialState.inputVolume);
      await api('/volume/output/' + initialState.outputVolume);
      await sleep(400);

      // Restore slot
      s = await getState();
      if (s.active_slot !== initialState.activeSlot) {
        await api('/slot/' + initialState.activeSlot);
        await waitForSlot(initialState.activeSlot);
        await sleep(1000); // extra wait for plugin chain load
      }

      // Restore master bypass
      s = await getState();
      if (s.master_bypassed !== initialState.masterBypassed) {
        await api('/bypass/master');
        await sleep(400);
      }

      // Restore monitor enabled
      s = await getState();
      if (s.monitor_enabled !== initialState.monitorEnabled) {
        await api('/monitor/toggle');
        await sleep(400);
      }

      // Restore IPC
      s = await getState();
      if (s.ipc_enabled !== initialState.ipcEnabled) {
        await api('/ipc/toggle');
        await sleep(400);
      }

      // Restore input mute
      s = await getState();
      if (s.input_muted !== initialState.inputMuted) {
        await api('/input-mute/toggle');
        await sleep(400);
      }

      // Restore plugin bypass states
      s = await getState();
      for (let i = 0; i < Math.min(s.plugins.length, initialState.pluginBypass.length); i++) {
        if (s.plugins[i].bypass !== initialState.pluginBypass[i]) {
          await api('/bypass/' + i + '/toggle');
          await sleep(300);
        }
      }

      // Final verification
      s = await getState();
      const ok =
        s.active_slot === initialState.activeSlot &&
        nearEq(s.volumes.monitor, initialState.monitorVolume) &&
        nearEq(s.volumes.input, initialState.inputVolume) &&
        nearEq(s.volumes.output, initialState.outputVolume) &&
        s.monitor_enabled === initialState.monitorEnabled &&
        s.ipc_enabled === initialState.ipcEnabled &&
        s.master_bypassed === initialState.masterBypassed;
      console.log(ok
        ? '  \x1b[32mState restored successfully\x1b[0m'
        : '  \x1b[33mState restore partial — check manually\x1b[0m');
    } catch (e) {
      console.log('  \x1b[31mState restore failed: ' + e.message + '\x1b[0m');
    }
  }

  // ──── Run all tests with guaranteed restore ────
  try {

  // Ensure clean state
  s = await getState();

  // ──── TEST 1: Volume Control ────
  console.log('\n--- TEST 1: Volume Control (9 checks) ---');
  await api('/volume/monitor/0.6');
  await sleep(400);
  s = await getState();
  check('Monitor volume ~0.6', true, nearEq(s.volumes.monitor, 0.6));

  await api('/volume/monitor/0.0');
  await sleep(400);
  s = await getState();
  check('Monitor volume 0.0 (min boundary)', true, nearEq(s.volumes.monitor, 0.0));

  await api('/volume/monitor/1.0');
  await sleep(400);
  s = await getState();
  check('Monitor volume 1.0 (max boundary)', true, nearEq(s.volumes.monitor, 1.0));

  await api('/volume/input/1.5');
  await sleep(400);
  s = await getState();
  check('Input gain ~1.5', true, nearEq(s.volumes.input, 1.5));

  await api('/volume/input/2.0');
  await sleep(400);
  s = await getState();
  check('Input gain 2.0 (max boundary)', true, nearEq(s.volumes.input, 2.0));

  await api('/volume/input/0.0');
  await sleep(400);
  s = await getState();
  check('Input gain 0.0 (min boundary)', true, nearEq(s.volumes.input, 0.0));

  await api('/volume/input/1.0');
  await sleep(400);

  let r = await api('/volume/output/0.5');
  checkAccepted('Output volume accepted', r, 'set_volume');
  await sleep(400);
  s = await getState();
  check('Output volume ~0.5 applied', true, nearEq(s.volumes.output, 0.5));
  await api('/volume/output/' + initialState.outputVolume);

  r = await api('/volume/input/2.1');
  check('Input > 2.0 rejected (400)', 400, r.code);

  r = await api('/volume/monitor/-0.1');
  check('Negative volume rejected (400)', 400, r.code);

  // ──── TEST 2: Input Gain Adjust (delta) ────
  console.log('\n--- TEST 2: Input Gain Adjust (3 checks) ---');
  await api('/volume/input/1.0');
  await sleep(400);
  s = await getState();
  const baseGain = s.volumes.input;

  await api('/gain/0.5');
  await sleep(400);
  s = await getState();
  check('Gain +0.5 applied', true, s.volumes.input > baseGain);

  await api('/gain/-0.5');
  await sleep(400);
  s = await getState();
  check('Gain -0.5 applied', true, nearEq(s.volumes.input, baseGain, 0.1));

  await api('/gain/0');
  await sleep(400);
  s = await getState();
  check('Gain 0 (no change)', true, nearEq(s.volumes.input, baseGain, 0.1));

  await api('/volume/input/1.0');
  await sleep(300);

  // ──── TEST 3: Global Mute Toggle ────
  console.log('\n--- TEST 3: Global Mute Toggle (3 checks) ---');
  s = await getState();
  check('Initially not muted', false, s.muted);

  await api('/mute/toggle');
  await sleep(400);
  s = await getState();
  check('Global mute activated', true, s.muted);

  await api('/mute/toggle');
  await sleep(400);
  s = await getState();
  check('Global mute deactivated', false, s.muted);

  // ──── TEST 4: Input Mute Toggle ────
  console.log('\n--- TEST 4: Input Mute Toggle (2 checks) ---');
  s = await getState();
  const beforeInputMute = s.input_muted;
  await api('/input-mute/toggle');
  await sleep(400);
  s = await getState();
  check('Input mute toggled', !beforeInputMute, s.input_muted);
  await api('/input-mute/toggle');
  await sleep(400);
  s = await getState();
  check('Input mute restored', beforeInputMute, s.input_muted);

  // ──── TEST 5: Panic Mute Lockout ────
  console.log('\n--- TEST 5: Panic Mute Lockout (8 checks) ---');
  await api('/volume/monitor/0.5');
  await sleep(400);
  s = await getState();
  const preVol = s.volumes.monitor;
  const preIpc = s.ipc_enabled;

  await api('/mute/panic');
  await sleep(400);
  s = await getState();
  check('Panic activated', true, s.muted);
  check('Monitor disabled by panic', false, s.monitor_enabled);

  // Blocked actions during panic
  await api('/volume/monitor/0.1');
  await sleep(400);
  s = await getState();
  check('Monitor vol blocked', true, nearEq(s.volumes.monitor, preVol));

  await api('/monitor/toggle');
  await sleep(400);
  s = await getState();
  check('Monitor toggle blocked', false, s.monitor_enabled);

  await api('/ipc/toggle');
  await sleep(400);
  s = await getState();
  check('IPC toggle blocked during panic', preIpc, s.ipc_enabled);

  // Unmute
  await api('/mute/panic');
  await sleep(400);
  s = await getState();
  check('Panic deactivated', false, s.muted);
  check('Monitor vol preserved', true, nearEq(s.volumes.monitor, preVol));
  check('IPC restored after panic', preIpc, s.ipc_enabled);

  // ──── TEST 6: Slot Switching ────
  console.log('\n--- TEST 6: Slot Switching (8 checks) ---');
  for (let i = 0; i < 5; i++) {
    await api('/slot/' + i);
    s = await waitForSlot(i);
    check('Slot ' + 'ABCDE'[i] + ' (' + i + ')', i, s.active_slot);
  }
  await api('/slot/0');
  s = await waitForSlot(0);
  check('Back to Slot A', 0, s.active_slot);

  // Same-slot click (should not error)
  await api('/slot/0');
  await sleep(500);
  s = await getState();
  check('Same-slot re-click OK', 0, s.active_slot);

  // Rapid switching must remain safe; the final slot may depend on queued load timing.
  await api('/slot/3');
  await api('/slot/4');
  await sleep(SLOT_WAIT);
  s = await getState();
  check('Rapid switch no crash', true, s.active_slot >= 0 && s.active_slot <= 4);

  await api('/slot/0');
  s = await waitForSlot(0);

  // ──── TEST 7: IPC Toggle ────
  console.log('\n--- TEST 7: IPC Toggle (2 checks) ---');
  s = await getState();
  const beforeIpc = s.ipc_enabled;
  await api('/ipc/toggle');
  await sleep(400);
  s = await getState();
  check('IPC toggled', !beforeIpc, s.ipc_enabled);
  await api('/ipc/toggle');
  await sleep(400);
  s = await getState();
  check('IPC restored', beforeIpc, s.ipc_enabled);

  // ──── TEST 8: Master Bypass ────
  console.log('\n--- TEST 8: Master Bypass (2 checks) ---');
  s = await getState();
  const beforeBypass = s.master_bypassed;
  await api('/bypass/master');
  await sleep(400);
  s = await getState();
  check('Master bypass toggled', !beforeBypass, s.master_bypassed);
  await api('/bypass/master');
  await sleep(400);
  s = await getState();
  check('Master bypass restored', beforeBypass, s.master_bypassed);

  // ──── TEST 9: Plugin Bypass ────
  console.log('\n--- TEST 9: Plugin Bypass (2 checks) ---');
  s = await getState();
  if (s.plugins.length > 0) {
    const bp = s.plugins[0].bypass;  // field is "bypass", not "bypassed"
    await api('/bypass/0/toggle');
    await sleep(500);
    s = await getState();
    check('Plugin 0 bypass toggled', !bp, s.plugins[0].bypass);
    await api('/bypass/0/toggle');
    await sleep(500);
    s = await getState();
    check('Plugin 0 bypass restored', bp, s.plugins[0].bypass);
  } else {
    skipped('Plugin bypass toggle', 'no plugins loaded');
    skipped('Plugin bypass restore', 'no plugins loaded');
  }
  // Error case: plugin param with no plugins
  r = await api('/plugin/0/param/0/0.5');
  checkAccepted('Plugin param queued (handler validates internally)', r, 'set_plugin_parameter');

  r = await api('/plugin/abc/param/0/0.5');
  check('Plugin param bad index rejected', 400, r.code);

  // ──── TEST 10: Recording ────
  console.log('\n--- TEST 10: Recording (3 checks) ---');
  await api('/recording/toggle');
  await sleep(500);
  s = await getState();
  check('Recording started', true, s.recording);
  await sleep(2000);
  s = await getState();
  if (process.env.DIRECTPIPE_TEST_NO_AUDIO === '1') {
    skipped('Recorded audio sample progression',
      'exact candidate log reports audio device initialization failure; no recorded-audio proof');
  } else {
    check('Recording duration > 0', true, s.recording_seconds > 0);
  }
  await api('/recording/toggle');
  await sleep(500);
  s = await getState();
  check('Recording stopped', false, s.recording);

  // ──── TEST 11: API Error Handling ────
  console.log('\n--- TEST 11: API Error Handling (9 checks) ---');
  r = await api('/nonexistent');
  check('404 unknown endpoint', 404, r.code);

  r = await api('/slot/5');
  check('400 slot > 4', 400, r.code);

  r = await api('/slot/99');
  check('400 slot 99', 400, r.code);

  r = await api('/slot/abc');
  check('400 slot non-numeric', 400, r.code);

  r = await api('/preset/5');
  check('400 preset > 4', 400, r.code);

  r = await api('/preset/abc');
  check('400 preset non-numeric', 400, r.code);

  r = await api('/volume/xyz/0.5');
  check('400 unknown volume target', 400, r.code);

  r = await api('/volume/monitor/1.5');
  check('400 monitor > 1.0', 400, r.code);

  r = await api('/plugin/0/param/0/1.5');
  check('400 param value > 1.0', 400, r.code);

  // ──── TEST 12: Auto-Save ────
  console.log('\n--- TEST 12: Auto-Save (1 check) ---');
  const settingsPath = CONFIG_DIR + '\\settings.dppreset';
  let beforeMod = 0;
  try { beforeMod = fs.statSync(settingsPath).mtimeMs; } catch(e) {}
  await api('/volume/monitor/0.85');
  await sleep(2500);
  let afterMod = 0;
  try { afterMod = fs.statSync(settingsPath).mtimeMs; } catch(e) {}
  check('Auto-save triggered within 2.5s', true, afterMod > beforeMod);

  // ──── TEST 13: Device State ────
  console.log('\n--- TEST 13: Device State (4 checks) ---');
  s = await getState();
  check('Sample rate is finite and > 0', true, Number.isFinite(s.sample_rate) && s.sample_rate > 0);
  check('Buffer size is a positive integer', true, Number.isInteger(s.buffer_size) && s.buffer_size > 0);
  check('Recording duration is finite and nonnegative', true, Number.isFinite(s.recording_seconds) && s.recording_seconds >= 0);
  check('Channel mode valid', true, [1, 2].includes(s.channel_mode));
  check('Device not lost', false, s.device_lost);

  // ──── TEST 14: State JSON Completeness ────
  console.log('\n--- TEST 14: State JSON Completeness (1 check) ---');
  s = await getState();
  const requiredFields = [
    'plugins', 'volumes', 'master_bypassed', 'muted', 'output_muted',
    'input_muted', 'preset', 'latency_ms', 'monitor_latency_ms',
    'cpu_percent', 'sample_rate', 'buffer_size', 'channel_mode',
    'monitor_enabled', 'active_slot', 'recording', 'recording_seconds',
    'ipc_enabled', 'device_lost', 'monitor_lost'
  ];
  const missing = requiredFields.filter(f => !(f in s));
  check('All state fields present', 0, missing.length);
  if (missing.length > 0) console.log('    Missing:', missing.join(', '));

  // ──── TEST 15: WebSocket Connection ────
  console.log('\n--- TEST 15: WebSocket (2 checks) ---');
  try {
    const ws = await wsConnect(WS_PORT);
    check('WebSocket handshake (101)', true, ws.accepted);

    // Should receive state broadcast within 1 second
    try {
      const stateFrame = JSON.parse(await ws.readFrame());
      check('Receives typed state broadcast', true, isStateBroadcast(stateFrame));
    } finally { ws.close(); }
  } catch (e) {
    check('Required WebSocket handshake/state: ' + e.message, true, false);
  }

  // ──── TEST 16: Panic Mute State Restore ────
  console.log('\n--- TEST 16: Panic State Restore (3 checks) ---');
  // Set known state: monitor enabled, IPC off
  s = await getState();
  if (s.muted) { await api('/mute/panic'); await sleep(400); }
  if (!s.monitor_enabled) { await api('/monitor/toggle'); await sleep(400); }
  if (s.ipc_enabled) { await api('/ipc/toggle'); await sleep(400); }

  s = await getState();
  const preMon = s.monitor_enabled;
  const preIpcState = s.ipc_enabled;

  // Panic -> unmute -> check restore
  await api('/mute/panic');
  await sleep(400);
  await api('/mute/panic');
  await sleep(400);
  s = await getState();
  check('Monitor restored after panic cycle', preMon, s.monitor_enabled);
  check('IPC restored after panic cycle', preIpcState, s.ipc_enabled);
  check('Not muted after panic cycle', false, s.muted);

  // ──── TEST 17: Slot Data Integrity (with plugins) ────
  console.log('\n--- TEST 17: Slot Data Integrity (4 checks) ---');
  s = await getState();
  if (s.plugins.length > 0) {
    // Remember current slot's plugin count
    const slotAPlugins = s.plugins.length;
    const slotANames = s.plugins.map(p => p.name).join(',');

    // Switch to empty slot and back
    await api('/slot/2');
    s = await waitForSlot(2);
    check('Empty slot has 0 plugins', 0, s.plugins.length);

    await api('/slot/0');
    s = await waitForSlot(0);
    // Wait extra for plugin chain to fully load
    await sleep(1000);
    s = await getState();
    check('Slot A plugin count preserved', slotAPlugins, s.plugins.length);

    const restoredNames = s.plugins.map(p => p.name).join(',');
    check('Slot A plugin names preserved', slotANames, restoredNames);

    // Switch to another empty slot and back again
    await api('/slot/4');
    s = await waitForSlot(4);
    await api('/slot/0');
    s = await waitForSlot(0);
    await sleep(1000);
    s = await getState();
    check('Slot A survives 2x empty-slot round trip', slotAPlugins, s.plugins.length);
  } else {
    skipped('Slot data integrity', 'no plugins loaded');
    skipped('Slot data integrity', 'no plugins loaded');
    skipped('Slot data integrity', 'no plugins loaded');
    skipped('Slot data integrity', 'no plugins loaded');
  }

  // ──── TEST 18: Preset File Integrity ────
  console.log('\n--- TEST 18: Preset File Integrity (2 checks) ---');
  const slotDir = CONFIG_DIR + '\\slots';
  try {
    const files = fs.readdirSync(slotDir).filter(f => f.endsWith('.dppreset'));
    check('Slot directory exists', true, true);
    // Slot A should have a file if plugins are loaded
    s = await getState();
    if (s.active_slot === 0 && s.plugins.length > 0) {
      const slotAFile = files.find(f => f.includes('slot_A') || f.includes('slot_0'));
      check('Active slot A has preset file', true, slotAFile !== undefined);
    } else {
      check('Slot directory accessible', true, Array.isArray(files));
    }
  } catch (e) {
    skipped('Slot directory check', 'directory not found');
    skipped('Slot file check', 'directory not found');
  }

  // ──── TEST 19: Monitor Toggle ────
  console.log('\n--- TEST 19: Monitor Toggle (2 checks) ---');
  s = await getState();
  const beforeMon = s.monitor_enabled;
  await api('/monitor/toggle');
  await sleep(400);
  s = await getState();
  check('Monitor toggled', !beforeMon, s.monitor_enabled);
  await api('/monitor/toggle');
  await sleep(400);
  s = await getState();
  check('Monitor restored', beforeMon, s.monitor_enabled);

  // ──── TEST 20: API Info Endpoint ────
  console.log('\n--- TEST 20: API Info & CORS (3 checks) ---');
  r = await api('');
  check('GET /api returns 200', 200, r.code);
  check('API info contains version', true, r.body.includes('DirectPipe'));

  // CORS header check
  r = await new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error('timeout')), 5000);
    http.get(API + '/status', res => {
      clearTimeout(timeout);
      resolve({ headers: res.headers });
    }).on('error', e => { clearTimeout(timeout); reject(e); });
  });
  check('CORS header present', '*', r.headers['access-control-allow-origin']);

  // ──── TEST 21: Preset Load ────
  console.log('\n--- TEST 21: Preset Load (3 checks) ---');
  r = await api('/preset/0');
  checkAccepted('Preset 0 load accepted', r, 'load_preset');
  s = await waitForSlot(0);
  check('Preset 0 load applied', 0, s.active_slot);

  r = await api('/preset/4');
  checkAccepted('Preset 4 load accepted', r, 'load_preset');
  s = await waitForSlot(4);
  check('Preset 4 load applied', 4, s.active_slot);

  r = await api('/preset/0');
  checkAccepted('Preset 0 reload accepted', r, 'load_preset');
  s = await waitForSlot(0);
  check('Preset 0 reload applied', 0, s.active_slot);

  // Restore slot after preset tests
  await api('/slot/' + initialState.activeSlot);
  s = await waitForSlot(initialState.activeSlot);
  await sleep(500);

  // ──── TEST 22: State Field Types ────
  console.log('\n--- TEST 22: State Field Types (10 checks) ---');
  s = await getState();
  check('plugins is array', true, Array.isArray(s.plugins));
  check('volumes is object', true, typeof s.volumes === 'object' && s.volumes !== null);
  check('muted is boolean', true, typeof s.muted === 'boolean');
  check('output_muted is boolean', true, typeof s.output_muted === 'boolean');
  check('master_bypassed is boolean', true, typeof s.master_bypassed === 'boolean');
  check('sample_rate is number', true, typeof s.sample_rate === 'number');
  check('buffer_size is number', true, typeof s.buffer_size === 'number');
  check('latency_ms is number', true, typeof s.latency_ms === 'number');
  check('cpu_percent is number', true, typeof s.cpu_percent === 'number');
  check('active_slot is 0-4', true, s.active_slot >= 0 && s.active_slot <= 4);

  // ──── TEST 23: Plugin Slot Info ────
  console.log('\n--- TEST 23: Plugin Slot Info (3 checks) ---');
  s = await getState();
  if (s.plugins.length > 0) {
    const p = s.plugins[0];
    check('Plugin has name field', true, typeof p.name === 'string' && p.name.length > 0);
    check('Plugin has bypass field', true, typeof p.bypass === 'boolean');
    check('Plugin name not empty', true, p.name.length > 0);
  } else {
    skipped('Plugin name field', 'no plugins loaded');
    skipped('Plugin bypass field', 'no plugins loaded');
    skipped('Plugin name not empty', 'no plugins loaded');
  }

  // ──── TEST 24: Multiple Plugin Bypass ────
  console.log('\n--- TEST 24: Multiple Plugin Bypass (2 checks) ---');
  s = await getState();
  if (s.plugins.length >= 2) {
    // Bypass plugins 0 and 1
    const bp0 = s.plugins[0].bypass;
    const bp1 = s.plugins[1].bypass;
    await api('/bypass/0/toggle');
    await api('/bypass/1/toggle');
    await sleep(500);
    s = await getState();
    check('Plugin 0+1 both toggled', true,
      s.plugins[0].bypass === !bp0 && s.plugins[1].bypass === !bp1);
    // Restore
    await api('/bypass/0/toggle');
    await api('/bypass/1/toggle');
    await sleep(500);
    s = await getState();
    check('Plugin 0+1 both restored', true,
      s.plugins[0].bypass === bp0 && s.plugins[1].bypass === bp1);
  } else {
    skipped('Multi-plugin bypass toggle', 'need 2+ plugins');
    skipped('Multi-plugin bypass restore', 'need 2+ plugins');
  }

  // ──── TEST 25: Gain Adjust Boundaries ────
  console.log('\n--- TEST 25: Gain Adjust Clamp (2 checks) ---');
  await api('/volume/input/1.8');
  await sleep(300);
  await api('/gain/0.5');
  await sleep(400);
  s = await getState();
  check('Gain clamps at 2.0 max', true, s.volumes.input <= 2.05);

  await api('/volume/input/0.1');
  await sleep(300);
  await api('/gain/-0.5');
  await sleep(400);
  s = await getState();
  check('Gain clamps at 0.0 min', true, s.volumes.input >= -0.05);
  await api('/volume/input/1.0');
  await sleep(300);

  // ──── TEST 26: Recording File Creation ────
  console.log('\n--- TEST 26: Recording File (2 checks) ---');
  // Recording dir may vary — check common locations
  const recDirCandidates = [
    process.env.DIRECTPIPE_TEST_RECORDING_DIR,
    process.env.APPDATA + '\\DirectPipe\\recordings',
    process.env.USERPROFILE + '\\Documents\\DirectPipe',
    process.env.USERPROFILE + '\\Music\\DirectPipe',
  ].filter(Boolean);
  let recDir = null;
  for (const d of recDirCandidates) {
    try { fs.statSync(d); recDir = d; break; } catch(e) {}
  }
  if (recDir) {
    let recCountBefore = 0;
    try { recCountBefore = fs.readdirSync(recDir).filter(f => f.endsWith('.wav')).length; } catch(e) {}
    await api('/recording/toggle');
    await sleep(1500);
    await api('/recording/toggle');
    await sleep(500);
    let recCountAfter = 0;
    try { recCountAfter = fs.readdirSync(recDir).filter(f => f.endsWith('.wav')).length; } catch(e) {}
    check('WAV file created in ' + recDir.split('\\').pop(), true, recCountAfter > recCountBefore);
  } else {
    // Verify recording works via state even if dir unknown
    await api('/recording/toggle');
    await sleep(1000);
    s = await getState();
    check('Recording active (dir unknown)', true, s.recording);
    await api('/recording/toggle');
    await sleep(500);
  }
  s = await getState();
  check('Recording stopped after toggle', false, s.recording);

  // ──── TEST 27: Keep-Old-Until-Ready Timing ────
  console.log('\n--- TEST 27: Slot Switch Timing (2 checks) ---');
  s = await getState();
  if (s.plugins.length > 0) {
    // Measure slot switch time: A(heavy) -> empty -> A(heavy)
    await api('/slot/0');
    s = await waitForSlot(0);
    await sleep(500);

    const t1 = Date.now();
    await api('/slot/2'); // empty slot
    s = await waitForSlot(2);
    const emptyMs = Date.now() - t1;
    check('Empty slot switch < 3s', true, emptyMs < 3000);

    const t2 = Date.now();
    await api('/slot/0'); // heavy slot
    s = await waitForSlot(0);
    await sleep(500);
    const heavyMs = Date.now() - t2;
    console.log('    (empty: ' + emptyMs + 'ms, heavy: ' + heavyMs + 'ms)');
    check('Heavy slot switch < 10s', true, heavyMs < 10000);
  } else {
    skipped('Empty slot timing', 'no plugins loaded');
    skipped('Heavy slot timing', 'no plugins loaded');
  }

  // ──── TEST 28: WebSocket Dead Client Cleanup ────
  console.log('\n--- TEST 28: WebSocket Dead Client (1 check) ---');
  try {
    const ws1 = await wsConnect(WS_PORT);
    ws1.close(); // abrupt disconnect
    await sleep(1500); // wait for a few broadcast cycles
    // If server handles cleanup, we can still connect fine
    const ws2 = await wsConnect(WS_PORT);
    check('New WS connects after dead client', true, ws2.accepted);
    ws2.close();
  } catch (e) {
    check('Required WebSocket dead-client cleanup: ' + e.message, true, false);
  }

  // ──── TEST 29: Concurrent API Requests ────
  console.log('\n--- TEST 29: Concurrent API Requests (2 checks) ---');
  const concurrent = await Promise.all([
    api('/status'),
    api('/status'),
    api('/status'),
    api('/volume/monitor/0.7'),
    api('/volume/input/0.8'),
  ]);
  check('Concurrent status reads return 200', true, concurrent.slice(0, 3).every(r => r.code === 200));
  check('Concurrent volume writes accepted', true,
    concurrent.slice(3).every(r => isAcceptedResponse(r, 'set_volume')));
  await sleep(400);
  s = await getState();
  check('State consistent after concurrency', true,
    nearEq(s.volumes.monitor, 0.7) && nearEq(s.volumes.input, 0.8));
  await api('/volume/monitor/1.0');
  await api('/volume/input/1.0');
  await sleep(300);

  // ──── TEST 30: API Edge Cases ────
  console.log('\n--- TEST 30: API Edge Cases (6 checks) ---');
  r = await api('/bypass/-1/toggle');
  check('Bypass negative index rejected', 400, r.code);

  r = await api('/slot/-1');
  check('Slot negative index rejected', 400, r.code);

  r = await api('/plugin/0/param/abc/0.5');
  check('Plugin param non-numeric index rejected', 400, r.code);

  r = await api('/plugin/0/param/0/-0.1');
  check('Plugin param negative value rejected', 400, r.code);

  r = await api('/volume/monitor/0.5?foo=bar');
  checkAccepted('Query string ignored (volume accepted)', r, 'set_volume');
  await sleep(300);
  s = await getState();
  check('Volume correct after query string', true, nearEq(s.volumes.monitor, 0.5));
  await api('/volume/monitor/1.0');
  await sleep(300);

  // ──── TEST 31: Settings File Exists ────
  console.log('\n--- TEST 31: Settings File (1 check) ---');
  try {
    const stat = fs.statSync(settingsPath);
    check('settings.dppreset exists and has content', true, stat.size > 0);
  } catch (e) {
    skipped('Settings file check', 'file not found');
  }

  // ──── TEST 32: Volume Precision ────
  console.log('\n--- TEST 32: Volume Precision (2 checks) ---');
  await api('/volume/monitor/0.333');
  await sleep(400);
  s = await getState();
  check('Monitor vol 0.333 precision', true, nearEq(s.volumes.monitor, 0.333, 0.01));

  await api('/volume/input/1.777');
  await sleep(400);
  s = await getState();
  check('Input vol 1.777 precision', true, nearEq(s.volumes.input, 1.777, 0.01));
  await api('/volume/monitor/1.0');
  await api('/volume/input/1.0');
  await sleep(300);

  // ──── TEST 33: Panic During Slot Switch ────
  console.log('\n--- TEST 33: Panic During Operations (3 checks) ---');
  await api('/mute/panic');
  await sleep(400);
  s = await getState();
  check('Panic active', true, s.muted);

  // Slot switch should be blocked during panic
  const slotBefore = s.active_slot;
  await api('/slot/' + ((slotBefore + 1) % 5));
  await sleep(SLOT_WAIT);
  s = await getState();
  // Slot may or may not switch depending on implementation
  check('No crash during panic slot switch', true, s.active_slot >= 0 && s.active_slot <= 4);

  await api('/mute/panic');
  await sleep(400);
  s = await getState();
  check('Panic deactivated after slot test', false, s.muted);

  // Restore slot if changed
  if (s.active_slot !== initialState.activeSlot) {
    await api('/slot/' + initialState.activeSlot);
    await waitForSlot(initialState.activeSlot);
  }

  } finally {
    // ──── Restore original state ────
    await restoreState();
  }

  // ──── Summary ────
  const elapsed = ((Date.now() - startTime) / 1000).toFixed(1);
  console.log('\n============================================');
  console.log('  RESULTS: \x1b[32m' + pass + ' passed\x1b[0m, ' +
    (fail > 0 ? '\x1b[31m' : '') + fail + ' failed\x1b[0m, ' +
    (skip > 0 ? '\x1b[33m' : '') + skip + ' skipped\x1b[0m');
  console.log('  Time: ' + elapsed + 's');
  console.log('============================================');

  if (fail > 0) process.exit(1);
})();
