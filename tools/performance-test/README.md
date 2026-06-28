# performance-test/

Measures HTTP API endpoint processing time on a **live** Lightnet controller.

Each endpoint is called repeatedly and the round-trip time is summarised
(mean / median / p95 / min / max), so you can spot slow handlers on real hardware.

**Requires:** `pip install requests`

## Usage

```
python perf_test.py <ip-or-host> [options]
```

```
python perf_test.py 192.168.1.42
python perf_test.py lightnet-XXXX.local -n 50
python perf_test.py 192.168.1.42 --read-only        # only GET endpoints
python perf_test.py 192.168.1.42 --json out.json     # also dump raw samples
```

| Option | Default | Description |
|---|---|---|
| `-n, --repeats` | 20 | Timed samples per endpoint |
| `-w, --warmup` | 1 | Untimed warmup calls per endpoint |
| `-t, --timeout` | 10 | Per-request timeout (seconds) |
| `--read-only` | off | Only test GET endpoints (no mutations at all) |
| `--skip-playback` | off | Skip play/stop/animation/per-panel tests |
| `--json FILE` | — | Write raw per-sample results to a JSON file |

Live progress (phase headers + each endpoint's timing as it finishes) is printed
to **stderr** while the run proceeds, so you can see it isn't stuck; the final
summary table goes to **stdout**. Exit code is non-zero if any endpoint returned
an unexpected status.

## Non-invasive by design

The tool restores the device to its original state:

- **Persistent data** (appearance, configuration, palettes, scenes) is snapshotted
  before the run and restored afterwards. Mutation tests either re-apply the
  current value (appearance, configuration, MQTT, power, speed) or operate on a
  throwaway temp scene/palette that is deleted in teardown.
- **Create/Delete** are timed as matched pairs (create→delete each iteration) so
  nothing leaks and repeated calls don't `409`/`404`. A separate persistent temp
  scene/palette feeds the idempotent GET/PUT/PATCH/play tests and is deleted in
  teardown.
- **Transient playback** (which scene is playing, power on/off) is restored on a
  best-effort basis in a `finally` block, so an interrupted run still cleans up.
- Playback tests use a near-black temp scene/animation on a free group, so the
  panels stay visually unobtrusive even while a scene is briefly played.

Best-effort caveat: if a **one-shot** scene was playing before the run, the exact
inline scene cannot be recreated — the tool replays the last-played slot instead.

## Not tested

`POST /api/firmware/panels` is deliberately skipped: it flashes panel firmware
over I²C (slow, destructive, needs a real `.bin`) and is not a meaningful latency
measurement.

## Adding an endpoint

Append a `self.measure(...)` call to the matching phase in `perf_test.py`:
`_read_only_phase` (GETs), `_data_phase` (palette/scene CRUD), `_config_phase`
(self-reapplying config), or `_playback_phase`. Keep mutations non-invasive so the
restore step has nothing to undo.
