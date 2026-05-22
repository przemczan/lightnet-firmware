#!/usr/bin/env python3
"""
Lightnet sim logger — captures [SIM:*] serial output from initializer_wemos_sim,
decodes raw hex packets into human-readable form, and saves to a timestamped file.

Usage:
    python tools/sim_logger.py <port> [baud]

    port  — serial port, e.g. COM6 or /dev/ttyUSB0
    baud  — optional, default 230400

Output: tools/sim_<timestamp>.txt  (also printed to stdout)

Paste sections of the output file into a chat for analysis.
"""

import sys
import struct
import serial
from datetime import datetime

# ── Protocol constants (mirror of Protocol.hpp + AnimationTypes.hpp) ──────────

PACKET_NAMES = {
    0: 'NOOP', 1: 'ACK', 2: 'INIT_PULL', 3: 'REGISTER_EDGE',
    4: 'TURN_ON_OFF', 5: 'SET_COLOR', 6: 'SET_BRIGHTNESS',
    7: 'SET_COLOR_AND_BRIGHTNESS', 8: 'REGISTER_EDGE_ACK',
    10: 'FETCH_STATE', 11: 'PANEL_CONFIG',
    12: 'ANIM_PREPARE', 13: 'ANIM_START', 14: 'ANIM_CONTROL',
    15: 'FETCH_ANIM_STATE', 16: 'ANIM_UPDATE_PARAMS',
    17: 'SET_PALETTE', 18: 'SET_BASE_COLORS', 19: 'SET_BRIGHTNESS_GLOBAL',
    200: 'RESET_DEVICE', 201: 'ENTER_BOOTLOADER',
}

ANIM_NAMES = {
    0: 'SOLID', 1: 'FADE', 2: 'TRANSITION', 3: 'BREATHE',
    4: 'PULSE', 5: 'BLINK', 6: 'HUE_CYCLE', 7: 'STROBE', 8: 'REACTIVE',
    64: 'RUN_WAVE', 65: 'RUN_RIPPLE', 66: 'RUN_CHASE',
}

CTRL_NAMES = {1: 'STOP', 2: 'PAUSE', 3: 'RESUME', 4: 'CLEAR_QUEUE'}

PARAM_NAMES = {1: 'TRIGGER', 2: 'BRIGHTNESS_MULT', 3: 'SPEED_SCALE'}

FLAG_BITS = [
    (0x01, 'LOOP'), (0x02, 'PINGPONG'),
    (0x08, 'CURR_COLOR_FROM'), (0x10, 'CURR_COLOR_TO'),
    (0x20, 'CURR_BRT_FROM'), (0x40, 'CURR_BRT_TO'),
]


# ── Decode helpers ─────────────────────────────────────────────────────────────

def addr_str(addr):
    return 'ALL' if addr == 0 else f'p={addr}'

def flags_str(f):
    names = [name for mask, name in FLAG_BITS if f & mask]
    return ','.join(names) if names else '-'

def colorref_str(b4):
    kind = b4[0]
    if kind == 0:
        return f'#{b4[1]:02X}{b4[2]:02X}{b4[3]:02X}'
    if kind == 1:
        return f'PAL:{b4[1]}'
    if kind == 2:
        return f'SLOT:{b4[1]}'
    return f'?{b4.hex()}'

def decode_packet(addr, payload):
    """Return a decoded one-line string for a raw packet payload."""
    if len(payload) < 5:
        return f'(too short: {len(payload)}B)'

    ptype = payload[0]
    name  = PACKET_NAMES.get(ptype, f'TYPE_{ptype}')
    dst   = addr_str(addr)
    p     = payload[5:]   # skip 5-byte PacketMeta

    if ptype == 12:   # ANIM_PREPARE
        if len(p) < 18:
            return f'{name} → {dst}  (truncated)'
        anim     = ANIM_NAMES.get(p[0], f'?{p[0]}')
        group    = p[1]
        flgs     = flags_str(p[2])
        trans    = p[3]
        dur      = struct.unpack_from('<H', p, 4)[0]
        cfrom    = colorref_str(p[6:10])
        cto      = colorref_str(p[10:14])
        bfrom    = p[14]; bto = p[15]
        p1, p2   = p[16], p[17]
        extra    = f'  p1={p1} p2={p2}' if p1 or p2 else ''
        return (f'ANIM_PREPARE → {dst}  group={group}  {anim}  '
                f'dur={dur}ms  flags=[{flgs}]  trans={trans}ms  '
                f'bFrom={bfrom}  bTo={bto}  cFrom={cfrom}  cTo={cto}{extra}')

    if ptype == 13:   # ANIM_START
        if len(p) < 2:
            return f'{name} → {dst}  (truncated)'
        return f'ANIM_START → {dst}  group={p[1]}  seq={p[0]}'

    if ptype == 14:   # ANIM_CONTROL
        if len(p) < 1:
            return f'{name} → {dst}  (truncated)'
        return f'ANIM_CONTROL → {dst}  cmd={CTRL_NAMES.get(p[0], f"?{p[0]}")}'

    if ptype == 16:   # ANIM_UPDATE_PARAMS
        if len(p) < 4:
            return f'{name} → {dst}  (truncated)'
        param = PARAM_NAMES.get(p[2], f'?{p[2]}')
        return f'ANIM_UPDATE_PARAMS → {dst}  group={p[1]}  param={param}  val={p[3]}'

    if ptype == 17:   # SET_PALETTE
        if len(p) < 1:
            return f'{name} → {dst}  (truncated)'
        count = p[0]
        stops = []
        for i in range(min(count, 16)):
            if 1 + i * 4 + 3 >= len(p):
                break
            pos = p[1 + i * 4]
            r, g, b = p[2 + i*4], p[3 + i*4], p[4 + i*4]
            stops.append(f'{pos}:#{r:02X}{g:02X}{b:02X}')
        return f'SET_PALETTE → {dst}  stops={count}  [{" ".join(stops)}]'

    if ptype == 18:   # SET_BASE_COLORS
        if len(p) < 9:
            return f'{name} → {dst}  (truncated)'
        pri = f'#{p[0]:02X}{p[1]:02X}{p[2]:02X}'
        sec = f'#{p[3]:02X}{p[4]:02X}{p[5]:02X}'
        ter = f'#{p[6]:02X}{p[7]:02X}{p[8]:02X}'
        return f'SET_BASE_COLORS → {dst}  pri={pri}  sec={sec}  ter={ter}'

    if ptype == 19:   # SET_BRIGHTNESS_GLOBAL
        if len(p) < 1:
            return f'{name} → {dst}  (truncated)'
        return f'SET_BRIGHTNESS_GLOBAL → {dst}  val={p[0]}'

    if ptype == 4:    # TURN_ON_OFF
        if len(p) < 1:
            return f'{name} → {dst}  (truncated)'
        return f'TURN_{"ON" if p[0] else "OFF"} → {dst}'

    if ptype == 5:    # SET_COLOR
        if len(p) < 3:
            return f'{name} → {dst}  (truncated)'
        return f'SET_COLOR → {dst}  #{p[0]:02X}{p[1]:02X}{p[2]:02X}'

    if ptype == 6:    # SET_BRIGHTNESS
        if len(p) < 1:
            return f'{name} → {dst}  (truncated)'
        return f'SET_BRIGHTNESS → {dst}  val={p[0]}'

    if ptype == 7:    # SET_COLOR_AND_BRIGHTNESS
        if len(p) < 4:
            return f'{name} → {dst}  (truncated)'
        return f'SET_COLOR_AND_BRIGHTNESS → {dst}  #{p[0]:02X}{p[1]:02X}{p[2]:02X}  brt={p[3]}'

    return f'{name} → {dst}  ({len(payload)}B)'


# ── Main capture loop ──────────────────────────────────────────────────────────

def parse_led(parts):
    """Return (ts_ms, panel, r, g, b, brt, eff) from a split [SIM:LED] line."""
    if parts[0] == '[SIM:LED]':
        ts, panel = int(parts[1]), int(parts[2])
        r, g, b = int(parts[3], 16), int(parts[4], 16), int(parts[5], 16)
        brt, eff = int(parts[6], 16), int(parts[7], 16)
    else:  # old '[SIM:LED ]' — ']' is parts[1]
        ts, panel = int(parts[2]), int(parts[3])
        r, g, b = int(parts[4], 16), int(parts[5], 16), int(parts[6], 16)
        brt, eff = int(parts[7], 16), int(parts[8], 16)
    return ts, panel, r, g, b, brt, eff


def run(port, baud=230400):
    stamp    = datetime.now().strftime('%Y%m%d_%H%M%S')
    out_path = f'tools/sim_{stamp}.txt'
    first_ts = None
    scene_n  = 0
    capturing = False

    print(f'Waiting for [SIM:DEMO] start on {port} at {baud} baud...')
    print('(Reset the board if it already booted, or wait for the next cycle)')

    with open(out_path, 'w', encoding='utf-8') as f, serial.Serial(port, baud, timeout=1) as ser:

        def emit(line):
            print(line)
            f.write(line + '\n')
            f.flush()

        try:
            while True:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode('utf-8', errors='replace').strip()
                if not line:
                    continue

                # ── Demo lifecycle markers ─────────────────────────────────
                if line == '[SIM:DEMO] start':
                    capturing = True
                    first_ts  = None
                    scene_n   = 0
                    header = (f'=== Lightnet Sim Log  {datetime.now().strftime("%Y-%m-%d %H:%M:%S")} ===\n'
                              f'    port={port}  baud={baud}\n')
                    emit(header)
                    print('  Capturing...')
                    continue

                if line == '[SIM:DEMO] end':
                    emit(f'\n=== END ({scene_n} scene(s)) ===')
                    break

                if not capturing:
                    continue

                # ── [SIM] info lines ───────────────────────────────────────
                if line.startswith('[SIM] '):
                    emit(f'  # {line[6:]}')
                    continue

                if not line.startswith('[SIM:'):
                    continue

                parts = line.split()

                # ── [SIM:SEND] ─────────────────────────────────────────────
                if line.startswith('[SIM:SEND]'):
                    try:
                        ts_ms   = int(parts[1])
                        addr    = int(parts[2])
                        payload = bytes(int(x, 16) for x in parts[3:])
                    except (IndexError, ValueError) as e:
                        emit(f'  # parse error: {e}  line={line}')
                        continue

                    if first_ts is None:
                        first_ts = ts_ms
                    t = (ts_ms - first_ts) / 1000.0

                    ptype = payload[0] if payload else -1

                    if ptype == 14 and len(payload) >= 6 and payload[5] == 4:
                        scene_n += 1
                        emit(f'\n{"-" * 60}')
                        emit(f'  SCENE #{scene_n}')
                        emit(f'{"-" * 60}')

                    emit(f'  {t:8.3f}  {decode_packet(addr, payload)}')

                # ── [SIM:LED] or legacy [SIM:LED ] ────────────────────────
                elif line.startswith('[SIM:LED'):
                    try:
                        ts_ms, panel, r, g, b, brt, eff = parse_led(parts)
                    except (IndexError, ValueError) as e:
                        emit(f'  # LED parse error: {e}  line={line}')
                        continue

                    if first_ts is None:
                        first_ts = ts_ms
                    t = (ts_ms - first_ts) / 1000.0

                    emit(f'  {t:8.3f}  LED p={panel}  #{r:02X}{g:02X}{b:02X}  brt={brt}  eff={eff}')

        except KeyboardInterrupt:
            dur = f'{t:.1f}s' if first_ts is not None else '0s'
            emit(f'\n=== INTERRUPTED ({dur}, {scene_n} scene(s)) ===')

    print(f'\n-> Saved to {out_path}')


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    port = sys.argv[1]
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 230400
    run(port, baud)
