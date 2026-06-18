#!/usr/bin/env node
// Dumps MIRROR_BATCH frames from a live controller: per-batch counts of each inner
// I²C packet type, plus the addresses seen for PREPARE/START. Diagnostic for the
// live-preview pipeline. Usage: node mirror-dump.js <controller-ip> [seconds]
//
// Wire layout must match lib/Lightnet/Core/Common/MirrorBatch.h

'use strict';
const WebSocket = require('ws');
const ip = process.argv[2];
const seconds = Number(process.argv[3] ?? 15);
if (!ip) { console.error('Usage: node mirror-dump.js <ip> [seconds]'); process.exit(1); }

const MIRROR_BATCH = 9;
const WS_PACKET_META_SIZE = 13; // WebsocketApi::PacketMeta without payload
const MIRROR_BATCH_HEADER_SIZE = 6;
const MIRROR_RECORD_HEADER_SIZE = 3;
const MIRROR_BATCH_COUNT_OFFSET = 4; // u16 count inside MirrorBatchHeader

const TYPE = { 4:'TURN_ON_OFF',5:'SET_COLOR',12:'PREPARE',13:'START',14:'CONTROL',
               16:'UPDATE_PARAMS',17:'SET_PALETTE',18:'SET_BASE_COLORS',19:'SET_GLOBAL_BRIGHTNESS' };

const totals = {};
let batches = 0;

const ws = new WebSocket(`ws://${ip}/ws`);
ws.on('open', () => {
    console.log(`Connected to ${ip}, listening ${seconds}s for MIRROR_BATCH…\n`);
    setTimeout(() => {
        console.log('\n=== totals over', batches, 'batches ===');
        for (const [t, n] of Object.entries(totals)) console.log(`  ${t}: ${n}`);
        process.exit(0);
    }, seconds * 1000);
});
ws.on('error', (e) => { console.error('WS error:', e.message); process.exit(1); });

ws.on('message', (data) => {
    const buf = Buffer.isBuffer(data) ? data : Buffer.from(data);
    if (buf.length < WS_PACKET_META_SIZE || buf.readUInt8(0) !== MIRROR_BATCH) return;
    const payload = buf.slice(WS_PACKET_META_SIZE);
    const count = payload.readUInt16LE(MIRROR_BATCH_COUNT_OFFSET);
    let off = MIRROR_BATCH_HEADER_SIZE;
    const perBatch = {};
    const prepAddrs = [], startAddrs = [];
    for (let i = 0; i < count && off + MIRROR_RECORD_HEADER_SIZE <= payload.length; i++) {
        const addr = payload.readUInt8(off);
        const type = payload.readUInt8(off + 1);
        const size = payload.readUInt8(off + 2);
        off += MIRROR_RECORD_HEADER_SIZE + size;
        const name = TYPE[type] ?? `?${type}`;
        perBatch[name] = (perBatch[name] || 0) + 1;
        totals[name] = (totals[name] || 0) + 1;
        if (type === 12) prepAddrs.push(addr);
        if (type === 13) startAddrs.push(addr);
    }
    batches++;
    const parts = Object.entries(perBatch).map(([k, v]) => `${k}×${v}`).join(' ');
    let line = `batch ${batches}: ${parts}`;
    if (prepAddrs.length) line += `  PREPARE@[${prepAddrs.join(',')}]`;
        if (startAddrs.length) line += `  START@[${startAddrs.join(',')}]`;
    console.log(line);
});
