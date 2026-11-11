// SPDX-License-Identifier: GPL-3.0-or-later
const assert = require('node:assert/strict');
const { test } = require('node:test');
const crypto = require('node:crypto');
const net = require('node:net');
const { once } = require('node:events');
const { WebSocketResponseParser, wsConnect, isAcceptedResponse, isStateBroadcast, canSkipCheck, recordingDirectoryCandidates } = require('./test_api');

const key = Buffer.from('owned-api-fixture').toString('base64');
function handshake(accept = crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64')) {
  return Buffer.from('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + accept + '\r\n\r\n');
}
function frame(text, opcode = 1, final = true) {
  const payload = Buffer.from(text);
  const header = payload.length < 126 ? Buffer.from([(final ? 0x80 : 0) | opcode, payload.length])
    : Buffer.from([(final ? 0x80 : 0) | opcode, 126, payload.length >> 8, payload.length & 255]);
  return Buffer.concat([header, payload]);
}

test('buffers initial state coalesced with handshake and multiple frames', () => {
  assert.equal(typeof WebSocketResponseParser, 'function');
  const parser = new WebSocketResponseParser(key);
  parser.push(Buffer.concat([handshake(), frame('{"type":"state"}'), frame('next')]));
  assert.equal(parser.accepted, true);
  assert.deepEqual(parser.messages, ['{"type":"state"}', 'next']);
});

test('buffers byte-fragmented extended and continuation frames', () => {
  assert.equal(typeof WebSocketResponseParser, 'function');
  const parser = new WebSocketResponseParser(key);
  const long = 'state'.repeat(40);
  for (const byte of Buffer.concat([handshake(), frame(long), frame('frag', 1, false), frame('mented', 0)])) {
    parser.push(Buffer.from([byte]));
  }
  assert.deepEqual(parser.messages, [long, 'fragmented']);
});

test('rejects bad handshake proof and invalid server frames', () => {
  assert.equal(typeof WebSocketResponseParser, 'function');
  assert.throws(() => new WebSocketResponseParser(key).push(handshake('wrong')), /accept/i);
  assert.throws(() => new WebSocketResponseParser(key).push(Buffer.from('HTTP/1.1 200 OK\r\n\r\n')), /101/);
  const parser = new WebSocketResponseParser(key);
  parser.push(handshake());
  assert.throws(() => parser.push(Buffer.from([0x81, 0x80, 0, 0, 0, 0])), /masked/);
});

test('async API acceptance requires exact 202 and typed accepted action proof', () => {
  assert.equal(typeof isAcceptedResponse, 'function');
  const response = (code, body) => ({ code, json: () => body });
  const accepted = { ok: true, accepted: true, action: 'set_volume' };
  assert.equal(isAcceptedResponse(response(202, accepted), 'set_volume'), true);
  assert.equal(isAcceptedResponse(response(200, accepted), 'set_volume'), false);
  assert.equal(isAcceptedResponse(response(202, { ...accepted, accepted: 'true' }), 'set_volume'), false);
  assert.equal(isAcceptedResponse(response(202, accepted), 'load_preset'), false);
});

test('state broadcast requires an actual state object with core typed fields', () => {
  assert.equal(typeof isStateBroadcast, 'function');
  const data = { plugins: [], volumes: { input: 1, output: 1, monitor: 1 }, active_slot: 0 };
  assert.equal(isStateBroadcast({ type: 'state', data }), true);
  for (const invalid of [null, [], {}, { ...data, plugins: null }, { ...data, volumes: null }, { ...data, active_slot: '0' }]) {
    assert.equal(isStateBroadcast({ type: 'state', data: invalid }), false);
  }
});

test('isolated fixture cannot silently skip lost plugins or settings files', () => {
  assert.equal(typeof canSkipCheck, 'function');
  const isolated = { DIRECTPIPE_TEST_CONFIG_DIR: 'owned-config' };
  assert.equal(canSkipCheck('Plugin bypass', isolated), false);
  assert.equal(canSkipCheck('Settings file check', isolated), false);
  assert.equal(canSkipCheck('Recorded audio sample progression', isolated), false);
  assert.equal(canSkipCheck('Recorded audio sample progression', { ...isolated, DIRECTPIPE_TEST_NO_AUDIO: '1' }), true);
  assert.equal(canSkipCheck('Plugin bypass', {}), true);
});

test('isolated recording proof never falls back to owner profile directories', () => {
  assert.equal(typeof recordingDirectoryCandidates, 'function');
  const isolated = { DIRECTPIPE_TEST_CONFIG_DIR: 'owned-config', APPDATA: 'owner-data', USERPROFILE: 'owner-profile' };
  assert.deepEqual(recordingDirectoryCandidates(isolated), []);
  assert.deepEqual(recordingDirectoryCandidates({ ...isolated, DIRECTPIPE_TEST_RECORDING_DIR: 'owned-recordings' }), ['owned-recordings']);
  assert.ok(recordingDirectoryCandidates({ APPDATA: 'manual-data', USERPROFILE: 'manual-profile' }).length > 0);
});

test('real owned TCP fixture preserves handshake remainder without a later broadcast', { timeout: 3000 }, async (t) => {
  assert.equal(typeof wsConnect, 'function');
  const sockets = new Set();
  const server = net.createServer((socket) => {
    sockets.add(socket);
    socket.once('data', (request) => {
      const actual = /Sec-WebSocket-Key:\s*([^\r\n]+)/i.exec(request.toString())[1].trim();
      const accept = crypto.createHash('sha1').update(actual + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
      socket.write(Buffer.concat([handshake(accept), frame('{"type":"state","data":{}}')]));
    });
  });
  t.after(() => { for (const socket of sockets) socket.destroy(); server.close(); });
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const client = await wsConnect(server.address().port);
  t.after(() => client.close());
  assert.equal(client.accepted, true);
  assert.equal(await client.readFrame(), '{"type":"state","data":{}}');
});
