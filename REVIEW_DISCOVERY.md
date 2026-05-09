# Lightnet — Discovery Phase Review

**Scope:** correctness and reliability of the controller↔panel discovery (initialization) phase, both at the firmware and PCB level. Every "Finding" is independently actionable; severity reflects likely impact on real‑world reliability of discovery.

**Sources reviewed:**
- `src/controller/main.cpp`, `src/panel/main.cpp`, `src/panel/main.hpp`, `src/main.cpp`
- `lib/Lightnet/Controller/PanelsInitializer.{hpp,cpp}`
- `lib/Lightnet/Controller/PanelsController.{hpp,cpp}`
- `lib/Lightnet/Panel/LightnetPanel.{hpp,cpp}`
- `lib/Lightnet/Common/{LightnetBus,LightnetPanelEdge,LightnetPinger,Protocol}.{hpp,cpp}`
- `lib/Lightnet/MessageApi/MessageHandler.cpp`
- `lib/Lightnet/Utils/{List,CircularQueue}.{hpp,cpp}`
- `schematic/Schematic.pdf`, `schematic/Controller.png`, `schematic/Panel.png`
- `platformio.ini`

---

## 1. System recap (verified against code + schematic)

### 1.1 Bus topology

Each edge connector is a **5‑wire link** carrying:

| Wire | Direction | Purpose |
|---|---|---|
| `+24V` | controller → leaves | unregulated panel power (gated on the controller by P‑MOSFET Q1 / IRF4905 driven by NPN Q2 / FMMT491 from `PE`/`PANELS_POWER_PIN`) |
| `GND` | — | reference |
| `HSDA` | bidirectional | I²C data, buffered by P82B96TD on every node |
| `HSCL` | bidirectional | I²C clock, buffered by P82B96TD on every node |
| `PING` | bidirectional, open‑drain | per‑edge handshake line, idle‑high via `INPUT_PULLUP` (and a discrete pull‑up on the bus side) |

So the entire tree is:
- **One shared I²C segment** (HSDA/HSCL across all panels), repeated through a P82B96TD on each node so the bus is electrically long but noise‑resistant.
- **N independent ping lines**, one per edge: each edge has its own physical wire on each side. On the panel that's PB1/PB2/PB3 (Arduino D9/D10/D11 → PCINT1..3); on the controller that's a single edge pin level‑shifted via Q3 (BSS138).

### 1.2 Discovery sequence (controller side)

`PanelsInitializer::start()` (`PanelsInitializer.cpp:26`) runs the master:
1. Configures `intPinNo` as `INPUT`, attaches a `CHANGE` interrupt.
2. Constructs the controller's own `LightnetPanelEdge` on `edgePinNo` (the shared 5th wire of the controller's single edge).
3. Calls `LNBus.begin(sda, scl)` (master mode).
4. `boot()` is called every loop: it drives the `LightnetPanelEdge` state machine and, once the edge is in `STATE_BOOTING`, calls `pull()` every `PULL_INTERVAL_MS = 15 ms` (`PanelsInitializer.hpp:12`).
5. `pull()` sends `PACKET_INITIALIZATION_PULL` to the magic slave address `Protocol::PULLING_ADDRESS = 120 = 0x78`, then immediately requests a `PacketRegisterEdge` back via `Wire.requestFrom`.
6. When a panel answers, the controller increments `currentPanelIndex`, creates `Panel` and `Edge` objects, and links them to `lastActiveEdge` to build the topology.

`isReady()` returns true only when **the controller's own edge** receives a "return ping" — i.e. when the root panel has finished discovering its entire subtree and pulses back.

### 1.3 Discovery sequence (panel side)

`LightnetPanel::run()` (`LightnetPanel.cpp:43`) is a state machine:

```
STATE_IDLE
  → STATE_WAIT_FOR_WELLCOME_PING       — poll any edge for an inbound ping
  → STATE_RESPOND_TO_WELLCOME_PING     — pulse parent edge once, enable WDT(2 s)
  → STATE_REGISTER_EDGES               — sub‑state machine (per edge)
        REGISTER_STATE_BEGIN  → LNBus.begin(sda, scl, 0x78)   slave at 0x78
        REGISTER_STATE_SEND   → wait for controller pull, reply with
                                PacketRegisterEdge(panelIndex, edgeIndex)
        REGISTER_STATE_END    → LNBus.end(); compute new bootTimeoutMs
        REGISTER_STATE_BOOT   → if non‑parent edge: send wellcome ping & wait
        REGISTER_STATE_READY  → all edges done → return to parent
  → STATE_RETURN_TO_PARENT             — pulse parent edge, signaling "done"
  → STATE_READY                        — LNBus.begin(sda, scl, this->index) slave
  → STATE_WORKING                      — wdt_disable; service incoming packets
```

The PCINT0 ISR in `src/panel/main.cpp` calls `LNPanel.updateEdgesStates()` which iterates **all** edges and does a `digitalRead` on each.

### 1.4 Controller orchestration in `setup()`

`src/controller/main.cpp:144` does (simplified):

```cpp
LNPanelsInitializer.configure(...);
LNPanelsInitializer.start();          // master mode + IRQ attach
delay(150);                           // "wait for panels to boot"
panelsController = new PanelsController();
panelsController->resetDevices(50);   // best-effort soft-reset (1..50)
delay(300);                           // panels' 100 ms boot delay + reset slack
```

`PANELS_POWER_PIN` driving is **commented out** (`main.cpp:158–162`); the panels are reset via I²C broadcast only.

---

## 2. Discovery‑phase findings, ranked by reliability impact

Severity legend: **CRIT** (very likely cause of real‑world failures) · **HIGH** (will manifest under stress) · **MED** (latent / corner case) · **LOW** (cosmetic/cleanup).

### 2.1 — **CRIT** — Possible I²C pin mismatch between schematic and firmware (PB only)

According to the panel schematic, the P82B96TD bus extender's MCU side (`ISDA`/`ISCL`) is wired to **pin 3 (PE0/SDA1)** and **pin 6 (PE1/SCL1)** of the ATmega328PB‑AU TQFP‑32. These are the second TWI peripheral.

The firmware in `LightnetBus.cpp` only ever uses the default `Wire` instance:
```cpp
Wire.begin(address);
Wire.setClock(BUS_FREQUENCY);
```
On every Arduino core (including MiniCore) that supports the 328PB, **`Wire` is TWI0 (PC4/PC5)**. To talk on PE0/PE1 you must use `Wire1`.

Implications, if the schematic is the as‑built design:
- **`panel_atmega328pb` build can never communicate.** The TWI peripheral is bit‑banging on PC4/PC5, which on the schematic are unconnected (or used for something else).
- `panel_nanoatmega328` (Arduino Nano, ATmega328P) does not even have TWI1 — so this build can only work if the actual PCB has the bus on PC4/PC5.
- This would explain symptoms like "discovery succeeds on the prototype, fails on the production board" or vice‑versa, depending on which board is paired with which firmware.

**What to verify, in order:**
1. Open the actual PCB / Gerbers (not just the schematic) and trace `HSDA/HSCL` net to the AVR pins.
2. If the production board uses **PC4/PC5**: schematic is stale — update it; firmware is correct; ignore this finding.
3. If it uses **PE0/PE1**: firmware is wrong — replace `Wire` with `Wire1` (the PB Arduino core exposes both). Probably with a compile‑time switch keyed off `__AVR_ATmega328PB__` so the 328P/Nano builds keep using TWI0.

This is the single most likely root cause of intermittent / total discovery failures and should be checked first.

### 2.2 — **CRIT** — Boot timeout dividing by panel index makes deep trees impossible

`LightnetPanel::endEdgeRegistration()` (`LightnetPanel.cpp:147`) sets the per‑edge boot timeout for the panel's children with:

```cpp
edge->setBootTimeout(edge->getBootTimeout() / this->index / this->edges->getSize());
```

With the default `BOOT_TIMEOUT_MILLS = 1000` and `edges = 3`:

| `this->index` | per‑edge boot timeout |
|---|---|
| 1 | 333 ms |
| 2 | 166 ms |
| 3 | 111 ms |
| 5 | 66 ms |
| 10 | 33 ms |
| 30 | 11 ms |

The "boot timeout" is how long a panel waits for its child edge's full subtree to complete and return a ping. So the **deeper a panel sits in the discovery order, the less time it gives its own children to recurse**, even though deeper subtrees don't take less time. A subtree rooted at panel 10 with three‑deep grandchildren simply can't complete inside 33 ms.

When the parent times out, `setNextEdgeToRegister()` rotates on. The eventual return‑ping from the child arrives late and is interpreted as either nothing (if the parent is no longer waiting on that edge) or worse — a *spurious* ping for whatever edge the parent is now polling.

Index is also not depth: it's just the order in which panels happen to be assigned. Two siblings can have very different indices.

**Suggested fix:** drop the division entirely. Use a generous, depth‑independent constant (e.g. 2–5 s). Discovery is one‑shot and only as long as the slowest leaf, so total budget on the root's wait isn't really shortened by being aggressive at intermediate panels. If you want to bound the global wait, do it on the controller, not by squeezing leaves.

### 2.3 — **CRIT** — Watchdog enabled, never reset across the whole subtree

`LightnetPanel::run()` enables a **2‑second** WDT on entry to `STATE_RESPOND_TO_WELLCOME_PING`:

```cpp
case STATE_RESPOND_TO_WELLCOME_PING:
    this->respondToWellcomePing();
    wdt_enable(WDTO_2S);
    break;
```
…and only disables it once `STATE_WORKING` is reached. There is **no `wdt_reset()` anywhere in `registerEdges()` / `bootEdge()` / `setNextEdgeToRegister()`**. The only `wdt_reset()` call in the whole codebase is in `setup()` (`src/panel/main.cpp:11`).

Real consequence: any panel whose subtree takes >2 s to register/boot resets in the middle of discovery. In a moderate tree (say, controller + 6 panels) that's easy to hit — each panel does multiple `LNBus.begin/end` cycles, waits up to `bootTimeoutMs` per child, plus the 15 ms pulling cadence on the controller. The reset then re‑enters `STATE_IDLE` while the controller has already moved on, so the panel becomes orphaned for the rest of the discovery cycle.

Combined with #2.2, this produces a perverse failure mode: low‑index panels have generous timeouts but trip the WDT; high‑index panels finish quickly per‑edge but report "BOOT_TIMEOUT" on every connected child.

**Fix options:**
- Sprinkle `wdt_reset()` at the top of `run()` and inside `bootEdge()`.
- Or: don't enable the WDT at all during discovery; only arm it after `STATE_WORKING`. The original intent looks like "reset if discovery hangs" but the implementation doesn't service it during a phase that *expects* to spend seconds idle waiting on children.

### 2.4 — **HIGH** — Pinger pin‑mode reconfiguration creates glitches on the shared edge wire

`LightnetPinger::ping()` (`LightnetPinger.cpp:26`) does, in order:

```cpp
busIsDisabled = true;
pinMode(pinNo, OUTPUT);          // (1) was INPUT_PULLUP; now driven push-pull
digitalWrite(pinNo, LOW);
delayMicroseconds(100);          // PING_DURATION_US
digitalWrite(pinNo, HIGH);       // (2) actively drives HIGH for ~ a few µs
pingSentAt = millis();
busIsDisabled = false;           // (3) interrupts again sense the line
pinMode(pinNo, INPUT_PULLUP);    // (4) finally release; now pulled up via ~30k internal pull-up
```

Three problems on a *shared bidirectional* wire:

1. **Active push‑pull HIGH for ~µs after the pulse.** Between (2) and (4) the AVR/ESP is *driving* the line high. If the other end is simultaneously in `OUTPUT LOW` (e.g. the panel pinging back early, or any glitch / EMI), the two GPIO drivers fight and one of them may latch up, draw high current, or emit a brief out‑of‑spec voltage. The line is supposed to be open‑drain‑ish (idle high via pull‑up, pulled low to assert).
2. **`busIsDisabled = false` happens *before* `pinMode(INPUT_PULLUP)`.** The window is a few µs, but in it the IRQ will fire on the rising edge from (2) → (4) and read whatever `digitalRead` returns at that moment. With a strong push‑pull HIGH that read is HIGH so `hasPing` is set spuriously. With CHANGE interrupts on both ends, every `ping()` you do, you also self‑detect a "ping received". That's then consumed correctly by `getAndResetPingStatus` because of the `!busState && state` edge guard — *but* the race is real on the very first call where `busState` was initialised to `true` (line 9: `volatile bool busState = true;`) and the first `digitalRead` after a low → high is the only way `hasPing` becomes true.
3. **`pingSentAt` is sampled after the pulse, not before it.** All timeouts (`WELLCOME_RESPONSE_TIMEOUT_MILLS = 3 ms`, `BOOT_TIMEOUT_MILLS = 1000 ms`) are measured from this sample. With a 100 µs pulse this is harmless, but it ties timeout semantics to "time since last‑pulse‑finished" rather than "time since I asked the question", which is subtly wrong if the pulse ever stretches.

**Suggested fix** for the pin handling: never go `OUTPUT HIGH`. Use the open‑drain idiom available on AVR/ESP:

```cpp
// idle: pinMode INPUT_PULLUP (line floats high via pull-ups)
// ping: pinMode OUTPUT, digitalWrite LOW, delay, pinMode INPUT_PULLUP (release)
busIsDisabled = true;
pinMode(pinNo, OUTPUT);
digitalWrite(pinNo, LOW);
pingSentAt = millis();           // sample at ping-start
delayMicroseconds(PING_DURATION_US);
pinMode(pinNo, INPUT_PULLUP);    // release first…
busIsDisabled = false;           // …then re‑enable IRQ sensing
```

This guarantees the line is open‑drain, eliminates the contention window, and tightens the semantics of `pingSentAt`.

### 2.5 — **HIGH** — Volatile/atomicity hazards on the panel ISR path

`LightnetPinger.cpp:11` runs in the PCINT context (`LNPanel.updateEdgesStates()` → `readBusState()` → `onBusStateChanged()`):

```cpp
void LightnetPinger::onBusStateChanged() {
    if (this->busIsDisabled) return;
    uint8_t state = digitalRead(this->pinNo);
    if (!this->busState && state) this->hasPing = true;
    this->busState = state;
}
```

`busIsDisabled`, `busState`, `hasPing` are declared `volatile` (good). However:

- **`getAndResetPingStatus()`** runs in the main loop and does a *non‑atomic read‑then‑clear* of `hasPing`. Between `bool state = this->hasPing;` and `this->hasPing = false;` an interrupt can fire and set it again, which we then drop. For "did we get a ping?" semantics that's tolerable (you re‑poll next loop), but it does mean a ping that arrives within a few µs of the consumer can be silently lost.
- More importantly, `pingSentAt` is `unsigned long` (4 bytes on AVR), **not volatile**, written by `ping()` (which is called from main) and never read in the ISR — so it's fine. But it's also written *after* re‑enabling interrupts, see #2.4.
- The PCINT0 vector uses no `ATOMIC_BLOCK`, but every variable it touches is either volatile or locally allocated. The bigger problem is **duration**: a comment in `src/panel/main.cpp:11` claims ~27 µs per IRQ. The PCINT0 vector calls `updateEdgesStates`, which calls **3** virtual `digitalRead`s and 3 list lookups. On a 16 MHz AVR that is plausibly 30–50 µs. **PCINT only flags on changes** — it doesn't latch multiple changes. If the line goes low → high → low while we're inside the ISR (e.g. on a 100 µs pulse where another panel pings while we're processing), the second falling edge **is lost**.

**Mitigations:**
- Switch the panel edges from PCINT0 (vector‑level demultiplex) to an asm‑short ISR that just records the port snapshot into a small ring buffer, and demux in the main loop. The current code already snapshots `busState` per edge but does it *via* `digitalRead` rather than a single `PINB` read.
- Or, since you only actually care about *rising* edges in `onBusStateChanged`, latch them in the ISR with `(PINB & 0x0E) ^ lastSnapshot` and store edge bits, instead of three sequential `digitalRead` round‑trips through Arduino HAL.
- Inline `digitalRead` (or use `direct port reads`); with the savings you can reasonably keep PCINT.

### 2.6 — **HIGH** — Two masters can briefly coexist on the bus, and pull spam continues throughout discovery

While a deeply nested panel is in `REGISTER_STATE_BEGIN` (slave at 0x78), `PanelsInitializer::boot()` keeps firing `pull()` every 15 ms because the controller's own edge has not yet returned to `STATE_READY`. That's fine — **only the controller is master**. Panels are *only* slaves; the parent never plays master to its children, it just pulses the ping line and the global controller polls 0x78.

There are still two real problems:
- **Address‑78 reuse races.** When panel A finishes registering and `LNBus.end()`s, panel A's child B may already have responded to the ping and will `LNBus.begin(slave 0x78)` very shortly after. There is a window (microseconds) where neither A nor B is on 0x78 and the controller's pull NACKs. There is also a window where A's `Wire.end()` and B's `Wire.begin(0x78)` interleave in time relative to the controller's `Wire.beginTransmission(0x78); Wire.write(...); Wire.endTransmission(false); Wire.requestFrom(0x78,...)`. If timing aligns badly (the controller starts a transaction *during* the handover), the byte sent is consumed by neither slave or by whichever happens to acknowledge first. The Wire driver returns success but the transaction is effectively dropped.
- **Pull spam at 15 ms.** During the entire subtree discovery, the controller is hammering 0x78. Every NACK adds bus traffic. With tens of panels, that's tens of thousands of failing transactions over a few seconds. On ESP8266's bit‑banged Wire, each NACK transaction is ~0.5–1 ms; the controller spends a substantial fraction of its CPU on this, *and* every transaction blocks any panel that is mid‑state‑transition (because they're all sharing the bus electrically, even if they aren't slaves yet).

**Suggested fixes:**
- Slow `PULL_INTERVAL_MS` to ≥50 ms once the first panel registers; or arm the next pull only after the previous one NACK'd N times, scaling the interval up.
- Have each panel hold the slave‑0x78 "lock" longer than just a single registration: register **all** of its edges in one slave session. This requires a small protocol change (controller pulls until panel responds with "no more edges") but eliminates the rapid begin/end churn that's at the heart of the timing race.

### 2.7 — **HIGH** — Panel parent edge can be misidentified by spurious pings

`checkForWellcomePing()` (`LightnetPanel.cpp:80`) walks edges and takes the **first** one whose `getAndResetPingStatus()` is true:

```cpp
while (index--) {
    if (edges->get(index)->getAndResetPingStatus()) {
        setState(STATE_RESPOND_TO_WELLCOME_PING);
        parentEdgeIndex = nextEdgeToRegister = index;
        return;
    }
}
```

There is no validation that this edge is actually connected (e.g. by waiting for a confirm pulse, or by sampling the bus voltage). On power‑up:
- The 100 µs pulse is short. The line on a *disconnected* edge is held high by the internal `INPUT_PULLUP` (~30–50 kΩ) — enough that EMI or even crosstalk to a sibling edge could glitch it low briefly.
- The detection logic only looks for `!busState && state`, i.e. a low → high transition. A single‑sample dropout is enough.
- After power‑on, all panels enter `STATE_WAIT_FOR_WELLCOME_PING` with `busState = true`. Any spurious low → high transition during the first ~150 ms (`delay(150)` in controller setup) could be latched as `hasPing = true` and consumed at the start of `run()`.

The result: the panel adopts a wrong `parentEdgeIndex`, sends its return‑ping to a disconnected (or wrong‑neighbor) edge, never registers, and sits in `STATE_REGISTER_EDGES` until its WDT fires (#2.3).

**Suggested fix:** require *two* pulses on the same edge within a window (e.g. controller sends two short pulses ~5 ms apart) or use a longer pulse (1–5 ms) and sample mid‑pulse to confirm a deliberate assertion. Either change is small and dramatically improves noise immunity.

### 2.8 — **HIGH** — `Wire.endTransmission(end=false)` then `requestFrom(... true)` assumes a clean repeated‑start, and ESP8266 software I²C doesn't always provide one

`LightnetBus::sendPacketWithResponse` (`LightnetBus.cpp:133`):
```cpp
sendPacket(addr, packet, sz, type, /*end=*/false);   // STOP suppressed
delayMicroseconds(3);
requestPacket(addr, buf, rspSize);                   // implicit START
```

On AVR's hardware Wire this is a textbook repeated‑start. On the ESP8266 software Wire (`Wire.cpp` in `Esp8266WiFi`/Wire library), the `endTransmission(false)` path is supposed to leave SCL high and SDA in last‑bit state; `requestFrom` then issues a repeated‑start. **In practice the ESP8266 implementation has historically been buggy around repeated‑start with clock stretching enabled** (`setClockStretchLimit(1500)`), and on long bus runs (which yours are — that's the point of the P82B96 buffers), the slack between the two transactions occasionally exceeds the bit‑bang assumptions.

This is exactly the kind of issue that is "rarely an issue at the bench, hard to reproduce in the field". Combined with the 15 ms pull spam (#2.6), every flaky transaction extends discovery.

**Suggested fix:** on ESP8266 builds, fall back to a **STOP + START** pattern (`end=true; delayMicroseconds(50); requestFrom`). It's slightly slower per request but is the path the bit‑banged ESP8266 Wire is actually known‑good on. ESP32 hardware Wire has no such issue and can keep the repeated‑start.

### 2.9 — **MED** — Reset broadcast in `setup()` runs before any panel is a slave

`controller/main.cpp:171`:
```cpp
panelsController->resetDevices(50);
delay(300);
```
walks addresses 50 → 0 sending `PACKET_RESET_DEVICE`. At this point in the boot sequence, the panels have just been powered on and are in `STATE_IDLE`/`WAIT_FOR_WELLCOME_PING`. **No panel is a slave at any address yet.** All 51 transactions NACK out. The 300 ms delay lets panels finish booting.

So the broadcast only does anything if a panel was already running with an assigned address from a *previous* run (i.e. controller rebooted but panels stayed up). This is a valid soft‑reset strategy, but `delay(300)` after it doesn't help fresh boot — and the firmware also `//digitalWrite(PANELS_POWER_PIN, LOW); //digitalWrite(PANELS_POWER_PIN, HIGH);` is commented out.

Implication: if the controller is reset while panels are mid‑use, the only thing reliably resetting them is the I²C broadcast — but `resetDevices(50)` only walks addresses 1..50; **a panel previously assigned index ≥51 will not be reset and will keep its old slave address.** During the next discovery, the controller will collide with this stale slave when it reaches that address.

**Suggested fix:**
- Re‑enable the `PANELS_POWER_PIN` cycle. The hardware exists for exactly this purpose; if it was disabled because of an electrical glitch (inrush, noise) that's a separate fix worth doing.
- Or extend `resetDevices` to cover the full 1..127 range (skipping 0x78); the cost is ~50 ms more boot time.
- Or send a single broadcast reset at address 0x00 (general call). Whether the panel firmware's slave is bound to general call needs to be checked — `Wire.begin(addr)` does not enable general call by default on AVR.

### 2.10 — **MED** — `currentPanelIndex` never reused, can grow unbounded across reboots

`PanelsInitializer::registerPanel` increments `currentPanelIndex` per *new* panel. If a partial discovery succeeds (assigns indices 1..3) then the controller resets, the next discovery starts at `currentPanelIndex = 1` again — but those panels remember their old index. So:
- Old panel @5 stays at 5.
- Controller starts assigning 1, 2, 3, …
- When controller reaches 5, panel @5 is *already* a slave at 5 from before, so `Wire.beginTransmission(5)` ACKs but the payload is interpreted by the old panel as a regular command rather than a "you've been assigned index 5" pull. Mostly harmless because pulls go to 0x78 only, but state on panel 5 (`this->index` is non‑zero, so it skips index assignment in `onPacketReceived`) is now divergent from the controller's view.
- Worse, the *new* panel that the controller was actually trying to assign index 5 to never gets one — both think they have index 5.

`onPacketReceived` does try to defend with `if (!this->index)` — but since the old panel never lost its index, it silently keeps the old one and the controller thinks it just registered a new device at the same index.

**Suggested fix:** at the very start of discovery, send a **broadcast "reset state"** that all panels (in any state) honor, clearing `this->index` and returning them to `STATE_IDLE`. This is what the (currently disabled) panel‑power cycle would do for free.

### 2.11 — **MED** — Deep‑tree topology link can be wrong if a child registration packet is dropped

The controller uses `lastActiveEdge` to remember which edge the *just registered* device sat on. When a new panel registers, it links `lastActiveEdge->connectedEdge = newPanelEdge`. This works only if **registration packets arrive in strict tree‑DFS order**, which the discovery does enforce by serialising children. But:

- Any dropped or NACK'd registration packet leaves `lastActiveEdge` pointing to the *previous* registration. The next successful registration then attaches the new panel to whichever edge happened to be last in the controller's view.
- Specifically, if a parent panel E re‑registers (its second/third edge) while a child panel C is mid‑registration (rare but possible during the begin/end churn — see #2.6), `lastActiveEdge` flips to E and a subsequent grandchild D registers and gets linked under E instead of C.

This is mostly a topology‑display issue (the GUI shows a wrong wiring), not a control issue (commands are addressed by `panelIndex`, not by topology). But it can confound debugging because the visualisation will not match the physical layout.

**Suggested fix:** make registration self‑describing. Add `parentPanelIndex` and `parentEdgeIndex` to `PacketRegisterEdge`; the panel knows both because it set `parentEdgeIndex` in `checkForWellcomePing()` and the parent told it its own index in the *previous* pull cycle's "I'm pinging you" handshake. This eliminates the `lastActiveEdge` global state entirely.

### 2.12 — **MED** — `LNBus.begin/end` cycles on the panel are heavyweight

Every edge causes the panel to call `LNBus.begin(sda, scl, 0x78)` then `LNBus.end()`. On the AVR Wire library:

- `begin` programs the TWI hardware and registers ISR.
- `end` (via `twi_stop` on ESP8266 or `Wire.end()` on AVR) tears it down.

Each cycle is ~µs of MCU time but, more importantly, **toggles the SDA/SCL pin direction** on the AVR. Between `Wire.end()` and the next `Wire.begin(0x78)` the lines are in INPUT/INPUT_PULLUP, which on a *long* bus with parasitic capacitance can produce a brief `LOW` glitch (RC charge curve below logic threshold). The P82B96 buffer should suppress most of this, but it's cleaner to never tear down: do `Wire.begin(0x78)` once at `STATE_RESPOND_TO_WELLCOME_PING` and just stay in slave mode until `STATE_READY` (where you re‑bind to `this->index`).

Doing so also fixes #2.6's handover race because there's no longer an "out of slave" gap.

### 2.13 — **MED** — `Wire.requestFrom(addr, maxSize, true)`'s return value is the number of bytes the controller *requested*, not received

`LightnetBus.cpp:184`:
```cpp
uint8_t receivedSize = Wire.requestFrom(address, maxSize, (uint8_t)true);
if (receivedSize > maxSize) { return 0; }
Wire.readBytes((uint8_t *)buffer, receivedSize);
return receivedSize;
```

On AVR's TWI library this returns the number of bytes the slave actually sent. On **ESP8266 software Wire**, depending on version, it returns the requested size if the transaction succeeded, or 0 on NACK — it does not differentiate "slave returned fewer bytes than asked". So a slave that under‑responds will be incorrectly treated as a full response on ESP8266; `Protocol::validatePacket` will then run on garbage tail bytes and likely fail the CRC check. That CRC failure is a *pull* failure handled silently in `pull()`; the controller just logs `[PULL] error N` and tries again 15 ms later. So discovery gets stuck retrying the same slot until the panel happens to send a complete packet (eventually it does, because it's slow but consistent on the panel side).

**Suggested fix:** on ESP8266, set a watchdog at the byte‑receive level. Or always read 32 bytes (`MAX_PACKET_SIZE`) and let the CRC catch malformed packets — *but* then panels need to pad responses to 32 bytes, which they currently don't.

### 2.14 — **MED** — `delayMicroseconds(3)` in `sendPacketWithResponse` is fragile

`LightnetBus.cpp:113, 146`:
```cpp
delayMicroseconds(3);
Wire.beginTransmission(...);
...
delayMicroseconds(3);
if (this->requestPacket(...) != 0) ...
```
3 µs is below the resolution of `delayMicroseconds` on the ESP8266 (its loop overhead is ~1 µs per call) and is essentially a no‑op there. On AVR it's a few cycles. Whatever it was meant to fix — bus settling, slave processing time — would be better expressed as either a real wait (≥50 µs) or omitted entirely. As written, it adds ambiguity without protection.

### 2.15 — **MED** — `Wire.write(buffer, size)` on AVR truncates silently at 32 bytes

The AVR Wire library has a 32‑byte TX buffer. If a packet ever exceeds 32 bytes the trailing bytes are dropped without indication. Current packet sizes are well below 32 (max appears to be `PacketPanelConfiguration` ≈ 14 bytes), but `MAX_PACKET_SIZE = 32` advertises the whole envelope as legal. If anyone adds a field this becomes a silent data‑corruption bug. Either lower `MAX_PACKET_SIZE` to a safer value (28?) leaving headroom for future fields, or add `static_assert(sizeof(...) <= 32, ...)` on every packet struct in `Protocol.hpp` so the build catches accidental growth.

### 2.16 — **MED** — `attachInterrupt(... CHANGE)` on ESP8266 GPIO12/13 is fine, but with hardware I²C plans for ESP32 there's a conflict

Controller pinout (`controller/main.cpp:5–18`):

| | ESP8266 | ESP32 |
|---|---|---|
| Edge ping out | GPIO13 | GPIO12 |
| Edge IRQ in | GPIO12 | GPIO13 |

On ESP8266, GPIO12/13 are HSPI MISO/MOSI; fine for general I/O.
On ESP32, GPIO12 is a **strapping pin** (MTDI; affects flash voltage at boot). Putting a pull‑up or pull‑down on it matters for boot stability. The ping line idles high through the BSS138 path, and GPIO12 is configured as `INPUT_PULLUP`‑equivalent here as the *output* of the ping (so it's driven actively). That's likely *okay* once the ESP32 has booted, but during boot the pin is undriven and the line state depends on the bus pull‑ups and any panel that might already be powered. If a panel asserts the line LOW at power‑on, MTDI=LOW selects 1.8 V flash, the ESP32 fails to boot, and the controller stays dead.

**Suggested fix:** move the ESP32 pinout off strapping pins (use GPIO 32–33 for ping/IRQ; they have no boot side‑effects and can do interrupts). ALSO: the `PANELS_POWER_PIN` on ESP32 is GPIO 21, which is fine, but the panel‑power should be *off* by default and only enabled after the ESP32 finishes booting — that gives the strapping pins a clean state at boot, even if you don't move them.

### 2.17 — **LOW** — `delay(150)` for "panels to boot" is shorter than the panels' actual boot time

Comment in `controller/main.cpp:163–165`:
```cpp
PRINTLN("waiting for panels to boot");
delay(150);
PRINTLN("Initializing...");
```

The 150 ms accounts for the panels' own internal startup. With WDT pre‑arm, the bootloader (if any), and the 16 MHz AVR's startup fuse settings (default SUT bits = ~65 ms slow rising), real boot can be 80–120 ms, leaving very little margin. If the panels happen to take longer (e.g. cold boot at ‑10 °C), the controller starts pulling 0x78 before any panel is in a state to respond.

**Suggested fix:** raise to 300–500 ms. Cheap.

### 2.18 — **LOW** — `LightnetBus::onReceive` allocates a VLA buffer per receive

`LightnetBus.cpp:29`:
```cpp
uint8_t buffer[size];                  // VLA on the AVR stack
Wire.readBytes(&buffer[0], size);
this->onPacketReceivedCallback(...);
```
On AVR with limited RAM (2 KB on the 328P/328PB), VLAs in IRQ context are risky if `size` is ever attacker‑controlled. Bus framing means `size` is bounded to whatever Wire received in one call (≤32 bytes), so this is safe today, but a `static uint8_t buffer[Protocol::MAX_PACKET_SIZE]` is cheaper and removes the latent VLA.

### 2.19 — **LOW** — `Wire.readBytes` in receive ISR is not async‑safe across all cores

The `onReceive` handler runs in TWI ISR context on AVR. `Wire.readBytes` calls back into the same Wire instance whose state machine just notified you. On AVR's reference implementation that's intentional (the bytes are already in the rx ring buffer; `readBytes` is a memcpy out). On ESP8266 software Wire there is no `onReceive`/`onRequest` callback for slave mode — the panel firmware that uses `LNBus.setOnPacketReceived` only works on AVR (`LightnetBus.cpp:5–7` only calls `Wire.onReceive(...)` if **not** ESP). So this isn't a bug, but it does mean the panel code path is AVR‑only, while the controller code path is ESP‑only — by construction, not by accident.

### 2.20 — **LOW** — Memory & destructor habits

- `PanelsInitializer::~PanelsInitializer` only calls `delete this->panels` on the outer list; the `Panel*` and `Edge*` items leak. (Not a runtime issue because the controller never destructs `LNPanelsInitializer`, it's a global.)
- `List<T>::clear()` sets `items = NULL` even though `items` was just freed; followed by `realloc(NULL, ...)`, which is `malloc`. Fine, but fragile.

### 2.21 — **LOW** — `setNextEdgeToRegister` index comparisons

```cpp
this->nextEdgeToRegister++;
if (!this->edges->get(this->nextEdgeToRegister)) {
    this->nextEdgeToRegister = 0;
}
```
`List::get(i)` returns `T()` (i.e. `nullptr` for pointer types) when out‑of‑range; relies on `T = LightnetPanelEdge*` being a pointer. Fine, but if the list type ever changes this silently breaks. Replace with `if (this->nextEdgeToRegister >= this->edges->getSize())`.

---

## 3. Schematic review (electrical)

### 3.1 Bus

The choice of **P82B96TD** as a bidirectional buffer/repeater on every node is the right tool for this job. It supports up to 400 kHz I²C over long, capacitive lines, makes the bus tolerant to mixed supply rails (panel side at +5 V, controller MCU at +3.3 V), and has hysteresis on inputs. On the bus side (HSDA/HSCL across all panels), the P82B96 datasheet recommends pull‑ups on the order of **~1 kΩ** at 5 V for fast bus‑side rise; the panel uses **R2 = R3 = 4.7 kΩ** and the controller uses **R11 = R12 = 3.3 kΩ**. That mismatch is harmless but the 4.7 kΩ on the panel will be the bottleneck on rise time. On long cables with significant capacitance, drop the panel pull‑ups to 1–2.2 kΩ. (Noting that *every* panel contributes its R2/R3 in parallel — five panels at 4.7 kΩ ≈ 940 Ω, so it self‑corrects with scale; but a single‑panel install runs slowly.)

The MCU‑side P82B96 inputs (ISDA/ISCL) need their own pull‑ups; those should be sized for the local MCU, not the bus. They look present (the schematic has more 4.7 kΩ resistors on the panel side near the MCU), but **on the controller** the MCU‑side inputs to U7 only have the 3.3 kΩ to +3.3 V — that's combined "MCU‑side pull‑up + level shifter pull‑up". OK in principle, but verify there's no fight with the ESP's internal pull‑ups (which are weakly enabled by `Wire.begin()` on some cores).

### 3.2 Ping line

`Q3 BSS138` for level‑shifting the ping signal between the ESP (3.3 V) and the panel‑side bus (5 V) is the standard MOSFET‑level‑shifter trick and only works correctly if **both sides have pull‑ups to their respective rails**. Schematic shows R6 = R7 = 10 kΩ, one each side — correct. But 10 kΩ on a long, capacitive ping wire gives slow rise (RC could reach 50–100 µs on a multi‑metre cable), which interacts badly with the 100 µs `PING_DURATION_US`. If you're seeing edge‑detection misses on long runs, drop these to 2.2 kΩ.

The panel side of the ping line (PING1/PING2/PING3) goes directly to PB1/PB2/PB3 with **no series resistance and no clamp diode**. If two panels assert their ping outputs simultaneously (rare but possible during a glitch), you have direct GPIO‑to‑GPIO contention. Adding **220–470 Ω** in series at each edge connector on the panel side limits fault current to a safe value (≤10 mA) without affecting timing meaningfully.

### 3.3 Power gating (controller)

The PE → Q2 → Q1 chain on the controller (`+24V` switched to panel rails) is fine in principle. R5 = 2.2 kΩ on Q2's base, R4 = 10 kΩ pull‑down on PE — looks correct, with PE LOW at boot keeping panels off. The firmware **disables this gate** (commented out) — that's almost certainly a workaround for an earlier symptom (inrush? pop?) and should be re‑investigated. With a 24 V rail and tens of panels each drawing a few hundred mA worst case, soft‑starting via gate ramp on Q1 (add ~10 nF gate–source on Q1 to slow turn‑on) is recommended. Without that, the ESP regulators may brown out at the moment power is enabled.

### 3.4 Panel power chain

`+24 V → 78L12 (U6) → LM340‑5.0 (U2) → +5 V`. The 78L12 is a 100 mA part. The LM340‑5.0 (TO‑263) is rated up to 1 A. Driving a single WS2812 + the AVR @ 5 V, +5 V rail draw is ≪100 mA; +12 V is unused except as a regulator intermediate. The 78L12's ~12 V drop from 24 V at low current dissipates ~1.2 W in the 78L12 (12 V × 0.1 A) — a TO‑92 78L12 is rated to about 0.8 W in still air; this is **right at the thermal limit** when all 100 mA is being used. If the panel ever draws >50 mA at +5 V (e.g. a brighter LED), the 78L12 will run hot. Replace with a switching pre‑regulator (LM2596‑12) or a higher‑current linear (78M12 in TO‑220).

### 3.5 Crystal & decoupling

X2 = 16 MHz with C6 = C7 = 22 pF — standard. AVCC has its own pin (18) — make sure there's a 100 nF + ferrite from VCC nearby (the schematic shows it via the standard layout). AREF should have a 100 nF to GND if any ADC use is planned (none currently).

### 3.6 Reset / RESET pull‑up

The 6‑pin PROG header carries RESET. There should be a **10 kΩ pull‑up on PC6/RESET** plus a small (100 nF) AC coupling to a programmer header for auto‑reset (optional). The schematic text doesn't make this clear — verify on the actual layout, because a missing RESET pull‑up shows up as random WDT‑like resets under EMI.

### 3.7 Indicator LEDs

`ILED` (red, on R9 = 1 kΩ from +5 V) and `PLED` (cyan?, on R10 = 1 kΩ to PD?) — the firmware blinks PD6 in `PCINT0_vect` and during `PACKET_RESET_DEVICE`. Not a discovery issue; just note it for when reading the firmware: PD6 is the indicator and is otherwise unused.

---

## 4. Concrete prioritised work plan

If reliability of discovery is the immediate goal, do these in order:

1. **Resolve #2.1**: trace `HSDA/HSCL` on the actual PCB. If wired to PE0/PE1 (ATmega328PB SDA1/SCL1), switch the panel firmware to `Wire1` (with a compile‑time switch so the 328P/Nano builds keep `Wire`). This is the single most likely root cause.
2. **Fix #2.2 + #2.3 together**: drop the `/index/edges` division on the boot timeout; either remove the WDT during discovery or sprinkle `wdt_reset()`. These changes are independent of the rest and remove a whole category of "deeper than three panels = fails" symptoms.
3. **Fix #2.4**: change `LightnetPinger::ping()` to never drive HIGH (open‑drain idiom). Move `pinMode(INPUT_PULLUP)` before `busIsDisabled = false`. Sample `pingSentAt` *before* the pulse.
4. **Fix #2.7**: lengthen the ping pulse to 1–2 ms and require two pulses (or a mid‑pulse re‑sample) to confirm a "wellcome", to make the parent‑edge detection robust to glitches. Mirror the timing on the controller side.
5. **Fix #2.6 + #2.12 together**: register all of a panel's edges in *one* slave session. Eliminates the 0x78 handover race.
6. **Re‑enable `PANELS_POWER_PIN`** (with soft‑start cap on the gate of Q1) so a controller reset is also a panels reset (#2.9, #2.10).
7. **#2.16**: move ESP32 ping pin off GPIO12 (strapping). Even on a one‑off prototype it costs you nothing.
8. **#2.17, #2.13, #2.14, #2.15**: housekeeping; harmless individually, but they collectively reduce the "rare flake" set.

Schematic side, only the panel I²C pin question is critical. The ping pull‑down → pull‑up resistor sweet‑spot (R6/R7 = 2.2 kΩ instead of 10 kΩ) and the 78L12 thermal point are good to revisit on the next board spin but won't change discovery success rate today.

---

## 5. Open questions / follow‑ups for next pass

- Confirm on the PCB whether the panel I²C is on PC4/PC5 or PE0/PE1 (this drops or escalates #2.1).
- Re‑measure the actual PCINT0 ISR duration; the comment says ~27 µs but the implementation walks a list and does three `digitalRead`s — likely 35–60 µs. If real, #2.5 deserves more attention.
- Capture a logic‑analyzer trace of one full discovery on a 4‑panel tree:
  - Time from controller's first wellcome pulse to panel A's pong.
  - Time from `PULL` to first `PacketRegisterEdge` byte.
  - Bus idle gaps between consecutive `LNBus.begin/end` cycles on each panel.
- Confirm whether the controller ever sees `[PULL] error N` log spam in the field — a quick metric to gauge how often #2.13/#2.6 triggers.
- Review what happens if a panel resets *during* `STATE_READY`: it goes back through the whole discovery handshake, but the controller still has it indexed. Does the rest of the controller (MessageHandler, command paths) tolerate a panel that becomes a slave at index N a second time?
