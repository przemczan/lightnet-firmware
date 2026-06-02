#!/usr/bin/env node
// Lightnet controller WebSocket API connector
// Usage: node api-connector.js <controller-ip>
// Requires: npm install ws

'use strict';

const WebSocket = require('ws');
const readline = require('readline');

const ip = process.argv[2];
if (!ip) {
    console.error('Usage: node api-connector.js <controller-ip>');
    process.exit(1);
}

const WS_URL = `ws://${ip}/ws`;

// ─── Protocol constants ───────────────────────────────────────────────────────

const VERSION = 0x01;

const PacketType = {
    TOGGLE:           1,
    SET_BRIGHTNESS:   2,
    SET_COLOR:        3,
    GET_EDGES_LIST:   4,
    GET_PANELS_STATES: 5,
    PANELS_STATES:    6,
    EDGES_LIST:       7,
};

const PacketTypeName = Object.fromEntries(Object.entries(PacketType).map(([k, v]) => [v, k]));

// ─── CRC-16 (matches firmware Crc.cpp) ───────────────────────────────────────

function crc16(buf) {
    let crc = 0xFFFF;
    for (const byte of buf) {
        crc ^= byte;
        for (let i = 0; i < 8; i++) {
            crc = (crc & 1) ? ((crc >>> 1) ^ 0xA001) : (crc >>> 1);
        }
    }
    return crc & 0xFFFF;
}

// ─── Packet builder ───────────────────────────────────────────────────────────
// PacketMeta layout (13 bytes + payload):
//   [0]     type        u8
//   [1-2]   version     u16le
//   [3-6]   nonce       u32le
//   [7-8]   headerCrc   u16le  — CRC16 over bytes 0-6
//   [9-10]  payloadCrc  u16le  — CRC16 over payload bytes
//   [11-12] payloadSize u16le
//   [13+]   payload     bytes

function buildPacket(type, payload = Buffer.alloc(0)) {
    const header = Buffer.alloc(7);
    header.writeUInt8(type, 0);
    header.writeUInt16LE(VERSION, 1);
    header.writeUInt32LE(Date.now() & 0xFFFFFFFF, 3);

    const headerCrc  = crc16(header);
    const payloadCrc = crc16(payload);

    const out = Buffer.alloc(13 + payload.length);
    header.copy(out, 0);
    out.writeUInt16LE(headerCrc,       7);
    out.writeUInt16LE(payloadCrc,      9);
    out.writeUInt16LE(payload.length, 11);
    payload.copy(out, 13);
    return out;
}

// ─── Command builders ─────────────────────────────────────────────────────────

const cmd = {
    toggle(address, on) {
        return buildPacket(PacketType.TOGGLE, Buffer.from([address & 0xFF, on ? 1 : 0]));
    },
    setBrightness(address, brightness) {
        return buildPacket(PacketType.SET_BRIGHTNESS, Buffer.from([address & 0xFF, brightness & 0xFF]));
    },
    setColor(address, r, g, b) {
        return buildPacket(PacketType.SET_COLOR, Buffer.from([address & 0xFF, r & 0xFF, g & 0xFF, b & 0xFF]));
    },
    getPanelsStates() {
        return buildPacket(PacketType.GET_PANELS_STATES);
    },
    getEdgesList() {
        return buildPacket(PacketType.GET_EDGES_LIST);
    },
};

// ─── Response parser ──────────────────────────────────────────────────────────

function parseResponse(buf) {
    if (buf.length < 13) return { error: 'too short', raw: buf.toString('hex') };

    const type        = buf.readUInt8(0);
    const headerCrc   = buf.readUInt16LE(7);
    const payloadCrc  = buf.readUInt16LE(9);
    const payloadSize = buf.readUInt16LE(11);

    // Verify header CRC
    if (crc16(buf.slice(0, 7)) !== headerCrc) {
        return { error: 'header CRC mismatch', type, raw: buf.toString('hex') };
    }

    const payload = buf.slice(13, 13 + payloadSize);

    if (crc16(payload) !== payloadCrc) {
        return { error: 'payload CRC mismatch', type, raw: buf.toString('hex') };
    }

    if (type === PacketType.PANELS_STATES) {
        if (payload.length < 2) return { error: 'PANELS_STATES payload too short' };
        const count  = payload.readUInt16LE(0);
        const panels = [];
        for (let i = 0; i < count; i++) {
            const off = 2 + i * 6;
            if (off + 6 > payload.length) break;
            panels.push({
                index: payload.readUInt16LE(off),
                on:    payload.readUInt8(off + 2) !== 0,
                r:     payload.readUInt8(off + 3),
                g:     payload.readUInt8(off + 4),
                b:     payload.readUInt8(off + 5),
            });
        }
        return { type: 'PANELS_STATES', panels };
    }

    if (type === PacketType.EDGES_LIST) {
        if (payload.length < 2) return { error: 'EDGES_LIST payload too short' };
        const count = payload.readUInt16LE(0);
        const edges = [];
        for (let i = 0; i < count; i++) {
            const off = 2 + i * 8;
            if (off + 8 > payload.length) break;
            edges.push({
                panel:          payload.readUInt16LE(off),
                edge:           payload.readUInt16LE(off + 2),
                connectedPanel: payload.readUInt16LE(off + 4),
                connectedEdge:  payload.readUInt16LE(off + 6),
            });
        }
        return { type: 'EDGES_LIST', edges };
    }

    return { type: PacketTypeName[type] ?? `UNKNOWN(${type})`, payloadHex: payload.toString('hex') };
}

// ─── Pretty printers ──────────────────────────────────────────────────────────

function printResponse(parsed) {
    if (parsed.error) {
        console.error(`  ✗ Parse error: ${parsed.error}`);
        return;
    }
    if (parsed.type === 'PANELS_STATES') {
        console.log(`  Panels (${parsed.panels.length}):`);
        for (const p of parsed.panels) {
            const rgb = `rgb(${p.r},${p.g},${p.b})`;
            console.log(`    [${p.index}] ${p.on ? 'ON ' : 'OFF'} ${rgb}`);
        }
        return;
    }
    if (parsed.type === 'EDGES_LIST') {
        console.log(`  Edges (${parsed.edges.length}):`);
        for (const e of parsed.edges) {
            const conn = e.connectedPanel
                ? `→ panel ${e.connectedPanel} edge ${e.connectedEdge}`
                : '→ (unconnected)';
            console.log(`    panel ${e.panel} edge ${e.edge}  ${conn}`);
        }
        return;
    }
    console.log(`  Response: ${JSON.stringify(parsed)}`);
}

// ─── WebSocket connection ─────────────────────────────────────────────────────

let ws = null;
let connected = false;

function connect() {
    console.log(`Connecting to ${WS_URL} …`);
    ws = new WebSocket(WS_URL);

    ws.on('open', () => {
        connected = true;
        console.log('Connected. Type "help" for commands.\n');
        rl.prompt();
    });

    ws.on('message', (data) => {
        const buf = Buffer.isBuffer(data) ? data : Buffer.from(data);
        const parsed = parseResponse(buf);
        process.stdout.write('\r');
        printResponse(parsed);
        rl.prompt();
    });

    ws.on('close', () => {
        connected = false;
        console.log('\nDisconnected. Reconnecting in 3 s…');
        setTimeout(connect, 3000);
    });

    ws.on('error', (err) => {
        console.error(`WS error: ${err.message}`);
    });
}

function send(buf) {
    if (!connected) { console.log('  Not connected.'); return; }
    ws.send(buf);
}

// ─── CLI ──────────────────────────────────────────────────────────────────────

const HELP = `
Commands:
  toggle <addr> <on|off>             Turn panel on or off
  brightness <addr> <0-255>          Set brightness
  color <addr> <r> <g> <b>          Set RGB color (0-255 each)
  states                             Query all panel states
  edges                              Query edge topology
  help                               Show this message
  exit                               Quit

Examples:
  toggle 1 on
  color 1 255 0 0
  brightness 2 128
  states
`;

const rl = readline.createInterface({ input: process.stdin, output: process.stdout, prompt: 'lightnet> ' });

rl.on('line', (line) => {
    const parts = line.trim().split(/\s+/);
    const c = parts[0]?.toLowerCase();

    switch (c) {
        case 'toggle': {
            const addr = parseInt(parts[1]);
            const on   = parts[2] === 'on' || parts[2] === '1';
            if (isNaN(addr)) { console.log('  Usage: toggle <addr> <on|off>'); break; }
            send(cmd.toggle(addr, on));
            break;
        }
        case 'brightness': {
            const addr = parseInt(parts[1]);
            const br   = parseInt(parts[2]);
            if (isNaN(addr) || isNaN(br)) { console.log('  Usage: brightness <addr> <0-255>'); break; }
            send(cmd.setBrightness(addr, Math.max(0, Math.min(255, br))));
            break;
        }
        case 'color': {
            const addr = parseInt(parts[1]);
            const r    = parseInt(parts[2]);
            const g    = parseInt(parts[3]);
            const b    = parseInt(parts[4]);
            if ([addr, r, g, b].some(isNaN)) { console.log('  Usage: color <addr> <r> <g> <b>'); break; }
            send(cmd.setColor(addr, r, g, b));
            break;
        }
        case 'states':
            send(cmd.getPanelsStates());
            break;
        case 'edges':
            send(cmd.getEdgesList());
            break;
        case 'help':
            console.log(HELP);
            break;
        case 'exit':
        case 'quit':
            process.exit(0);
            break;
        case '':
            break;
        default:
            console.log(`  Unknown command: ${c}. Type "help".`);
    }

    rl.prompt();
});

rl.on('close', () => process.exit(0));

connect();
