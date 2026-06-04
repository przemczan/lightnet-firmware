#!/usr/bin/env node
// Dumps MIRROR_BATCH frames from a live controller: per-batch counts of each inner
// I²C packet type, plus the addresses seen for PREPARE/START. Diagnostic for the
// live-preview pipeline. Usage: node mirror-dump.js <controller-ip> [seconds]

'use strict';
const WebSocket = require('ws');
const ip = process.argv[2];
const seconds = Number(process.argv[3] ?? 15);
if (!ip) { console.error('Usage: node mirror-dump.js <ip> [seconds]'); process.exit(1); }

const MIRROR_BATCH = 9;
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
    if (buf.length < 13 || buf.readUInt8(0) !== MIRROR_BATCH) return;
    const payload = buf.slice(13);
    const count = payload.readUInt16LE(4);
    let off = 6;
    const perBatch = {};
    const prepAddrs = [], startAddrs = [];
    for (let i = 0; i < count && off + 3 <= payload.length; i++) {
        const addr = payload.readUInt8(off);
        const type = payload.readUInt8(off + 1);
        const size = payload.readUInt8(off + 2);
        off += 3 + size;
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
