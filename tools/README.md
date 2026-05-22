# tools/

Developer utilities for building, flashing, and testing Lightnet firmware.

---

## sim_logger.py

Captures animation simulation output from the `initializer_wemos_sim` firmware
environment, decodes raw I²C packet bytes into human-readable form, and saves a
timestamped log file for protocol verification.

**Requires:** `pip install pyserial`

**Usage:**
```
python tools/sim_logger.py <port> [baud]
```

| Argument | Default | Description |
|---|---|---|
| `port` | — | Serial port, e.g. `COM6` or `/dev/ttyUSB0` |
| `baud` | 230400 | Must match `SIM_SERIAL_BAUD` in platformio.ini |

**Workflow:**
1. Flash the sim firmware: `pio run -e initializer_wemos_sim -t upload`
2. Run the logger: `python tools/sim_logger.py COM6`
3. The script waits silently until it sees `[SIM:DEMO] start` on serial
4. Captures all `[SIM:*]` lines until `[SIM:DEMO] end`, then saves and exits
5. Output file: `tools/sim_<timestamp>.txt`

Ctrl+C stops capture early and saves whatever was collected.

**Log line types:**

| Prefix | Content |
|---|---|
| `[SIM:SEND]` | Every I²C packet the controller sent — timestamp, destination address, full packet decoded (animation type, group, colors, durations, etc.) |
| `[SIM:LED]` | LED output computed by the panel-side `AnimationPlayer` — panel index, RGB, brightness, effective brightness (after global brightness multiply). Only emitted on change. |

Scenes are delimited by `ANIM_CONTROL → ALL  cmd=CLEAR_QUEUE` markers (which `ScenePlayer::loadAndPlay` broadcasts before each scene).

**Sim environment build flags** (in `platformio.ini`):

| Flag | Purpose |
|---|---|
| `SIM_MODE` | Activates sim bus + sim initializer; excludes real Wire code |
| `SIM_PANELS_COUNT=N` | Number of virtual panels to register on boot |
| `SIM_SERIAL_BAUD=230400` | Higher baud to handle 60fps LED output without blocking |

---

## flash_panels_serial.py

Uploads a compiled panel firmware binary to all discovered panels via the
controller's serial port. The controller receives the file, stores it to SPIFFS,
then flashes each panel over I²C using twiboot.

**Requires:** `pip install pyserial`

Used automatically by the `panel_atmega328p_via_controller` PlatformIO environment
(see `upload_command` in `platformio.ini`). Can also be run manually:

```
python tools/flash_panels_serial.py <port> <firmware.bin> [--baud BAUD]
```

**Examples:**
```
python tools/flash_panels_serial.py COM6 .pio/build/panel_atmega328p/firmware.bin
python tools/flash_panels_serial.py /dev/ttyUSB0 firmware.bin --baud 115200
```

**Protocol:** sends a 4-byte magic header (`LNFW`) + firmware length + CRC16, then
the binary payload. The controller responds `READY`, `OK`, or `ERR:...`.

---

## generate_bin.py

PlatformIO post-build script — not run directly. Registered as `extra_scripts` in
all panel environments. After the `.elf` is linked, it runs `objcopy` to strip out
the `.text` + `.data` sections into a raw `.bin` file that `flash_panels_serial.py`
and the OTA flasher can consume.

---

## api-shell/

Interactive Node.js shell for the controller's binary WebSocket API (`/ws`).
Lets you send commands to individual panels and inspect responses without writing
any code — useful for manual testing and debugging.

**Requires:** Node.js + `npm install` (inside `tools/api-shell/`)

**Usage:**
```
cd tools/api-shell
npm install          # once
node api-shell.js <controller-ip>
```

**Example:**
```
node api-shell.js 192.168.1.42
# or using mDNS:
node api-shell.js lightnet-XXXX.local
```

**Available commands** (type `help` once connected):

| Command | Effect |
|---|---|
| `on <panel>` | Turn panel on |
| `off <panel>` | Turn panel off |
| `brightness <panel> <0-255>` | Set panel brightness |
| `color <panel> <r> <g> <b>` | Set panel RGB color |
| `states` | Query and print all panel states (index, on/off, color, brightness) |
| `edges` | Query and print the panel edge topology |

**Note:** This shell uses the binary `MessageApi` WebSocket protocol, which is
separate from the HTTP REST API documented in `docs/api.md`. The HTTP API is
preferred for scene and animation control; this shell is for low-level panel
inspection.
