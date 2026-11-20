// Generates golden WSC packets with the upstream JS SDK (@asls/wsc-sdk).
//
// Usage:
//   curl -sL https://registry.npmjs.org/@asls/wsc-sdk/-/wsc-sdk-2.2.0.tgz | tar xz -C /tmp
//   node tools/golden/gen_golden.mjs /tmp/package/src tests/golden
//
// Writes one <name>.bin per case plus MANIFEST.json (SDK version, inputs, hex).
// A case the SDK cannot encode is recorded in the manifest with its error
// instead of a .bin file, so SDK gaps stay visible.

import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const [sdkSrc, outDir] = process.argv.slice(2);
if (!sdkSrc || !outDir) {
  console.error('usage: gen_golden.mjs <sdk src dir> <output dir>');
  process.exit(2);
}

const sdk = await import(join(sdkSrc, 'main.js'));
const { WscPacket: P, WscTransport: T, WscFlags: F } = sdk;
const sdkVersion = JSON.parse(readFileSync(join(sdkSrc, '..', 'package.json'), 'utf8')).version;

const ramp = (n, start = 0) => Uint8Array.from({ length: n }, (_, i) => (start + i) & 0xFF);
const flags = ({ tr = false, gw = false, ack = false, mc = false } = {}) => new F(tr, gw, ack, mc);
const viaGateway = { tr: true, gw: true };

// Each case: [name, note, type, data, opts]. Packet IDs are fixed so output is stable.
const cases = [
  // STREAM_CHANNELS
  ['stream_channels_sacn_full', 'full universe, sACN multicast, PRIORITY param',
    P.Type.STREAM_CHANNELS, { universe: 1, startChannel: 1, values: ramp(512) },
    { id: 1, flags: flags({ ...viaGateway, mc: true }),
      transport: T.udp(T.Protocol.SACN, '239.255.0.1', 5568, new Map([[T.Param.PRIORITY, Uint8Array.of(100)]])) }],
  ['stream_channels_artnet_slice', 'slice 50..124 (partial update)',
    P.Type.STREAM_CHANNELS, { universe: 0, startChannel: 50, values: ramp(75, 50) },
    { id: 2, flags: flags(viaGateway), transport: T.udp(T.Protocol.ARTNET, '192.168.1.100', 6454) }],
  ['stream_channels_dmx_usb', 'USB address (FTDI 0403:6001)',
    P.Type.STREAM_CHANNELS, { universe: 0, startChannel: 1, values: ramp(24) },
    { id: 3, flags: flags(viaGateway), transport: T.usb(T.Protocol.DMX512, 0x0403, 0x6001) }],
  ['stream_channels_dmx_serial', 'serial address + BAUD_RATE param',
    P.Type.STREAM_CHANNELS, { universe: 0, startChannel: 1, values: ramp(8) },
    { id: 4, flags: flags(viaGateway), transport: T.serial(T.Protocol.DMX512, 2, 250000) }],

  // STREAM_TIMECODE
  ['stream_timecode', '01:02:03:04 at 25 fps, no TD',
    P.Type.STREAM_TIMECODE, { hours: 1, minutes: 2, seconds: 3, frames: 4, rate: 25 }, { id: 5 }],

  // CONTROL_CUE
  ['control_cue_ack', 'lighting.cue.42 START, ACK requested',
    P.Type.CONTROL_CUE, { address: 'lighting.cue.42', action: P.CueAction.START }, { id: 6, flags: flags({ ack: true }) }],
  ['control_cue_osc', 'cue via OSC gateway',
    P.Type.CONTROL_CUE, { address: 'lighting.cue.42', action: P.CueAction.START },
    { id: 7, flags: flags(viaGateway), transport: T.udp(T.Protocol.OSC, '10.0.0.5', 8000) }],
  ['control_cue_u16_index', 'index > 255 (two INDEX sentinels)',
    P.Type.CONTROL_CUE, { address: 'lighting.cue.300', action: P.CueAction.STOP }, { id: 8 }],
  ['control_cue_point_cue', 'NOT SPEC-CORRECT INTENT: cue 10.5 encodes exactly like index 2565 (spec 03 ambiguity); SDK decodes 2565',
    P.Type.CONTROL_CUE, { address: 'lighting.cue.10.5', action: P.CueAction.START }, { id: 9 }],
  ['control_cue_custom', 'NOT SPEC-CONFORMANT: SDK adds an empty CUSTOM segment before house_lights_on; decoder robustness only',
    P.Type.CONTROL_CUE, { address: 'lighting.custom.house_lights_on', action: P.CueAction.START }, { id: 10 }],

  // CONTROL_PARAM, one per value type
  ...[
    ['u8', P.ValueType.U8, 200], ['u16', P.ValueType.U16, 0xBEEF], ['u32', P.ValueType.U32, 0xDEADBEEF],
    ['u64', P.ValueType.U64, 0x0102030405060708n], ['i8', P.ValueType.I8, -5], ['i16', P.ValueType.I16, -1234],
    ['i32', P.ValueType.I32, -123456], ['i64', P.ValueType.I64, -5n], ['f32', P.ValueType.F32, 0.75],
    ['f64', P.ValueType.F64, -1.5], ['string', P.ValueType.STRING, 'grün'], ['bool', P.ValueType.BOOL, true],
  ].map(([name, valueType, value], i) => [`control_param_${name}`, `value type ${name}`,
    P.Type.CONTROL_PARAM, { address: 'lighting.layer.2.intensity', valueType, value }, { id: 20 + i }]),
  ['control_param_modbus_tcp', 'TCP address',
    P.Type.CONTROL_PARAM, { address: 'motion.axis.1', valueType: P.ValueType.F32, value: 12.5 },
    { id: 40, flags: flags({ ...viaGateway, ack: true }), transport: T.tcp(T.Protocol.MODBUS, '10.0.0.9', 502) }],
  ['control_param_http_url', 'URL address',
    P.Type.CONTROL_PARAM, { address: 'video.layer.1.opacity', valueType: P.ValueType.F32, value: 0.5 },
    { id: 41, flags: flags(viaGateway), transport: T.http(T.Protocol.HTTP, 'http://10.0.0.7/api') }],

  // TUNNEL_RAW
  ['tunnel_raw_sdk_helper', 'NOT SPEC-CONFORMANT: WscTransport.raw() writes iface 0, not in spec 05; decoder robustness only',
    P.Type.TUNNEL_RAW, { raw: Uint8Array.of(0x01, 0x02, 0xFF) }, { id: 50, flags: flags(viaGateway), transport: T.raw() }],
  ['tunnel_raw_udp', 'RAW over UDP',
    P.Type.TUNNEL_RAW, { raw: Uint8Array.of(0x01, 0x02, 0xFF) },
    { id: 51, flags: flags(viaGateway), transport: new T(T.Protocol.RAW, T.Iface.UDP, T.Addr.IPV4, Uint8Array.of(10, 0, 0, 3, 0x1F, 0x90)) }],

  // STATE_*
  ['state_query_keepalive', 'KEEPALIVE, empty target_id',
    P.Type.STATE_QUERY, { queryType: P.StateQuery.KEEPALIVE }, { id: 60 }],
  ['state_answer_json', 'SUCCESS with JSON data',
    P.Type.STATE_ANSWER, { queryId: 60, status: P.Status.SUCCESS, data: { id: '35797cde-3def-48f8-bb5a-19713477591e' } }, { id: 61 }],
  ['state_answer_raw', 'raw data',
    P.Type.STATE_ANSWER, { queryId: 7, status: P.Status.PARTIAL, data: Uint8Array.of(0xCA, 0xFE) }, { id: 62 }],
  ['state_error', 'NO_ROUTE with context',
    P.Type.STATE_ERROR, { errorCode: P.ErrorCode.NO_ROUTE, message: 'no adapter', context: { protocol: 2 } }, { id: 63 }],

  // Header edge cases
  ['header_max_id', 'id 0xFFFFFFFF',
    P.Type.STATE_QUERY, { queryType: P.StateQuery.KEEPALIVE }, { id: 0xFFFFFFFF }],
];

const toJson = (v) => JSON.stringify(v, (_, x) => {
  if (typeof x === 'bigint') return x.toString();
  if (x instanceof Uint8Array) return Buffer.from(x).toString('hex');
  if (x instanceof Map) return Object.fromEntries(x);
  return x;
}, 2);

mkdirSync(outDir, { recursive: true });
const manifest = { sdk: `@asls/wsc-sdk@${sdkVersion}`, cases: [] };
for (const [name, note, type, data, opts] of cases) {
  const entry = { name, note, type: `0x${type.toString(16)}`, input: data, id: opts.id };
  try {
    const bytes = P.create(type, data, opts).serialize();
    writeFileSync(join(outDir, `${name}.bin`), bytes);
    entry.hex = Buffer.from(bytes).toString('hex');
    try { entry.sdk_decode = P.decode(P.deserialize(bytes)); } catch (e) { entry.sdk_decode_error = e.message; }
  } catch (e) {
    entry.sdk_error = e.message;
  }
  manifest.cases.push(entry);
  console.log(`${entry.sdk_error ? 'ERR ' : 'ok  '} ${name}${entry.sdk_error ? `: ${entry.sdk_error}` : ''}`);
}
writeFileSync(join(outDir, 'MANIFEST.json'), `${toJson(manifest)}\n`);
