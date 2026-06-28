#!/usr/bin/env python3
"""Measure HTTP API endpoint processing time on a live Lightnet controller.

    python perf_test.py <ip-or-host> [options]

Each endpoint is called repeatedly and the round-trip time is summarised
(mean / median / p95 / min / max). The tool is non-invasive: all persistent
data (appearance, configuration, palettes, scenes) is snapshotted before the
run and restored afterwards, temporary resources are deleted, and transient
playback state is restored on a best-effort basis.

Adding a new endpoint test: append a `self.measure(...)` call to the relevant
phase method below. Read-only GETs go in `_read_only_phase`; data mutations in
`_data_phase`; self-reapplying config mutations in `_config_phase`; playback in
`_playback_phase`. Keep mutations non-invasive (re-apply current values or use
the temp scene/palette) so the restore step has nothing to undo.
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
import time
from dataclasses import dataclass, field
from typing import Any, Optional

try:
    import requests
except ImportError:
    sys.exit("This tool requires 'requests'. Install with: pip install requests")


# Temporary resources created during the run (deleted in teardown). A persistent
# instance feeds the idempotent dependents (GET/PUT/PATCH/play); the "pair" name
# is used by the create+delete loop, which deletes its resource every iteration.
TEMP_PALETTE_NAME = "__perftest__"
TEMP_PALETTE_PAIR_NAME = "__perftest_pair__"
TEMP_SCENE_NAME = "__perftest__"

# A minimal, valid, near-black scene used for playback timing so the run stays
# visually unobtrusive even while it is briefly played.
TEMP_SCENE_BODY = {
    "schemaVersion": 8,
    "name": TEMP_SCENE_NAME,
    "loop": True,
    "layers": [
        {
            "group": "perf",
            "panels": "all",
            "sequence": [
                {
                    "type": "BREATHE",
                    "colorFrom": "#000000",
                    "colorTo": "#080808",
                    "duration": 4000,
                    "loop": True,
                }
            ],
        }
    ],
}

TEMP_PALETTE_BODY = {
    "schemaVersion": 1,
    "name": TEMP_PALETTE_NAME,
    "stops": [[0, "#000000"], [255, "#080808"]],
}


@dataclass
class Measurement:
    label: str
    method: str
    path: str
    samples: list[float] = field(default_factory=list)
    status: Optional[int] = None
    note: Optional[str] = None  # "skipped: ..." or "FAILED: ..."

    @property
    def ok(self) -> bool:
        return bool(self.samples) and not (self.note or "").startswith("FAILED")


class ApiClient:
    def __init__(self, base_url: str, timeout: float):
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout
        self.session = requests.Session()

    def call(self, method: str, path: str, *, body: Any = None, raw: bytes | None = None):
        """Single request. Returns (response, elapsed_ms)."""
        kwargs: dict[str, Any] = {"timeout": self.timeout}
        if body is not None:
            kwargs["json"] = body
        if raw is not None:
            kwargs["data"] = raw
        start = time.perf_counter()
        resp = self.session.request(method, self.base_url + path, **kwargs)
        return resp, (time.perf_counter() - start) * 1000.0


class Benchmark:
    def __init__(self, client: ApiClient, repeats: int, warmup: int):
        self.client = client
        self.repeats = repeats
        self.warmup = warmup
        self.results: list[Measurement] = []
        # Snapshot of original device state, filled by _snapshot().
        self.snap: dict[str, Any] = {}
        self.temp_scene_id: Optional[str] = None
        self.temp_palette: Optional[str] = None
        self._idx = 0  # running endpoint counter, for progress output

    # -- progress output (stderr, so it never mixes with --json on stdout) --

    @staticmethod
    def _log(msg: str = "", end: str = "\n") -> None:
        print(msg, end=end, file=sys.stderr, flush=True)

    def _start(self, label: str, method: str) -> None:
        self._idx += 1
        self._log(f"  [{self._idx:>2}] {label:<30} {method:<6} ", end="")

    def _done(self, m: "Measurement") -> None:
        if m.ok:
            self._log(f"{statistics.mean(m.samples):7.1f} ms  (n={len(m.samples)})")
        else:
            self._log(m.note or "no data")

    # -- measurement core ---------------------------------------------------

    def measure(
        self,
        label: str,
        method: str,
        path: str,
        *,
        body: Any = None,
        raw: bytes | None = None,
        expect: tuple[int, ...] = (200, 202, 204),
        repeats: Optional[int] = None,
    ) -> Optional[requests.Response]:
        """Time `path` and append a Measurement. Returns the last response."""
        m = Measurement(label=label, method=method, path=path)
        self.results.append(m)
        n = repeats if repeats is not None else self.repeats
        last: Optional[requests.Response] = None
        self._start(label, method)
        try:
            for _ in range(self.warmup):
                self.client.call(method, path, body=body, raw=raw)
            for _ in range(n):
                resp, elapsed = self.client.call(method, path, body=body, raw=raw)
                last = resp
                m.status = resp.status_code
                if resp.status_code not in expect:
                    m.note = f"FAILED: status {resp.status_code} (expected {expect})"
                    break
                m.samples.append(elapsed)
        except requests.RequestException as exc:
            m.note = f"FAILED: {exc}"
        self._done(m)
        return last

    def measure_create_delete(
        self,
        *,
        create_label: str,
        delete_label: str,
        create_path: str,
        create_body: Any,
        delete_template: str,
        id_from: Optional[Any] = None,
        create_expect: tuple[int, ...] = (200,),
        delete_expect: tuple[int, ...] = (200,),
    ) -> None:
        """Time a non-idempotent create+delete as matched pairs.

        Each iteration creates a resource and immediately deletes it, so the
        name/id is free for the next iteration and nothing leaks. `delete_template`
        may contain `:id`, substituted from `id_from(response)` (defaults to the
        path as-is for name-keyed resources).
        """
        create_m = Measurement(create_label, "POST", create_path)
        delete_m = Measurement(delete_label, "DELETE", delete_template)
        self.results.extend([create_m, delete_m])
        self._start(f"{create_label} + {delete_label}", "POST")
        try:
            for i in range(self.warmup + self.repeats):
                timed = i >= self.warmup
                resp, elapsed = self.client.call("POST", create_path, body=create_body)
                create_m.status = resp.status_code
                if resp.status_code not in create_expect:
                    create_m.note = f"FAILED: status {resp.status_code} (expected {create_expect})"
                    break
                if timed:
                    create_m.samples.append(elapsed)

                path = delete_template
                if id_from is not None:
                    path = delete_template.replace(":id", str(id_from(resp)))
                resp, elapsed = self.client.call("DELETE", path)
                delete_m.status = resp.status_code
                if resp.status_code not in delete_expect:
                    delete_m.note = f"FAILED: status {resp.status_code} (expected {delete_expect})"
                    break
                if timed:
                    delete_m.samples.append(elapsed)
        except requests.RequestException as exc:
            (delete_m if create_m.samples else create_m).note = f"FAILED: {exc}"
        if create_m.ok and delete_m.ok:
            self._log(f"{statistics.mean(create_m.samples):7.1f} ms create / "
                      f"{statistics.mean(delete_m.samples):.1f} ms delete  (n={len(create_m.samples)})")
        else:
            self._log((create_m.note or delete_m.note) or "no data")

    def skip(self, label: str, method: str, path: str, reason: str) -> None:
        self.results.append(Measurement(label=label, method=method, path=path, note=f"skipped: {reason}"))
        self._idx += 1
        self._log(f"  [{self._idx:>2}] {label:<30} {method:<6} skipped: {reason}")

    # -- phases -------------------------------------------------------------

    def _snapshot(self) -> None:
        """Capture original state so it can be restored after the run."""
        def get(path: str) -> Any:
            try:
                resp, _ = self.client.call("GET", path)
                return resp.json() if resp.status_code == 200 else None
            except (requests.RequestException, ValueError):
                return None

        self.snap["appearance"] = get("/api/appearance")
        self.snap["configuration"] = get("/api/configuration")
        self.snap["state"] = get("/api/state")
        self.snap["panels"] = get("/api/panels")
        self.snap["mqtt"] = get("/api/mqtt")

    def _read_only_phase(self) -> None:
        scenes = []
        try:
            resp, _ = self.client.call("GET", "/api/scenes")
            scenes = resp.json() if resp.status_code == 200 else []
        except (requests.RequestException, ValueError):
            pass

        self.measure("Get appearance", "GET", "/api/appearance")
        self.measure("List palettes", "GET", "/api/palettes")
        self.measure("Get palette by name", "GET", "/api/palettes/Rainbow")
        self.measure("List scenes", "GET", "/api/scenes")
        if scenes:
            self.measure("Get scene by id", "GET", f"/api/scenes/{scenes[0]['id']}")
        self.measure("Get configuration", "GET", "/api/configuration")
        self.measure("Get state", "GET", "/api/state")
        self.measure("List panels", "GET", "/api/panels")
        self.measure("Get panel edges", "GET", "/api/panels/edges")
        self.measure("Get firmware status", "GET", "/api/firmware/status")
        if self.snap.get("mqtt") is not None:
            self.measure("Get MQTT config", "GET", "/api/mqtt")
        else:
            self.skip("Get MQTT config", "GET", "/api/mqtt", "not available (non-ESP32 target)")

    def _create_persistent_resources(self) -> None:
        """Create one palette and one scene (untimed) for the idempotent
        dependents (GET/PUT/PATCH/play). Deleted untimed in teardown."""
        # Clear any leftover from a previously interrupted run, then create.
        try:
            self.client.call("DELETE", f"/api/palettes/{TEMP_PALETTE_NAME}")
            resp, _ = self.client.call("POST", "/api/palettes", body=TEMP_PALETTE_BODY)
            if resp.status_code == 200:
                self.temp_palette = TEMP_PALETTE_NAME
            resp, _ = self.client.call("POST", "/api/scenes", body=TEMP_SCENE_BODY)
            if resp.status_code == 200:
                self.temp_scene_id = resp.json().get("id")
        except (requests.RequestException, ValueError):
            pass

    def _data_phase(self) -> None:
        self._create_persistent_resources()

        # Palette: idempotent dependents on the persistent instance...
        if self.temp_palette:
            self.measure("Get palette by name (temp)", "GET", f"/api/palettes/{TEMP_PALETTE_NAME}")
            self.measure("Update palette stops", "PUT", f"/api/palettes/{TEMP_PALETTE_NAME}", body=TEMP_PALETTE_BODY, expect=(200,))
        else:
            self.skip("Update palette stops", "PUT", "/api/palettes/:name", "create failed")
        # ...and create/delete timed as matched pairs (name-keyed).
        pair_body = {**TEMP_PALETTE_BODY, "name": TEMP_PALETTE_PAIR_NAME}
        self.client.call("DELETE", f"/api/palettes/{TEMP_PALETTE_PAIR_NAME}")  # clear leftover
        self.measure_create_delete(
            create_label="Create palette", delete_label="Delete palette",
            create_path="/api/palettes", create_body=pair_body,
            delete_template=f"/api/palettes/{TEMP_PALETTE_PAIR_NAME}",
            create_expect=(200,), delete_expect=(204,))

        # Scene: idempotent dependents on the persistent instance...
        if self.temp_scene_id:
            self.measure("Get scene by id (temp)", "GET", f"/api/scenes/{self.temp_scene_id}")
            self.measure("Update scene", "PATCH", f"/api/scenes/{self.temp_scene_id}", body=TEMP_SCENE_BODY, expect=(200,))
        else:
            self.skip("Update scene", "PATCH", "/api/scenes/:id", "create failed")
        # ...and create/delete timed as matched pairs (id-keyed).
        self.measure_create_delete(
            create_label="Create scene", delete_label="Delete scene",
            create_path="/api/scenes", create_body=TEMP_SCENE_BODY,
            delete_template="/api/scenes/:id", id_from=lambda r: r.json().get("id", ""),
            create_expect=(200,), delete_expect=(200,))

    def _config_phase(self) -> None:
        """Self-reapplying mutations: re-send current values (no net change)."""
        appearance = self.snap.get("appearance")
        if appearance:
            self.measure("Update appearance", "PATCH", "/api/appearance", body=appearance, expect=(202,))
        config = self.snap.get("configuration")
        if config and "powerStateOnBoot" in config:
            # Re-apply only powerStateOnBoot; patching logicalRoot would restart a playing scene.
            self.measure("Update configuration", "PATCH", "/api/configuration",
                         body={"powerStateOnBoot": config["powerStateOnBoot"]}, expect=(200,))
        mqtt = self.snap.get("mqtt")
        if mqtt is not None:
            # Empty password leaves the stored password unchanged.
            payload = {k: mqtt[k] for k in
                       ("enabled", "brokerDiscovery", "broker", "port", "username", "topicPrefix", "discoveryPrefix")
                       if k in mqtt}
            # Each PATCH triggers an MQTT reconnect — keep the sample count low.
            self.measure("Update MQTT config", "PATCH", "/api/mqtt", body=payload, expect=(200,),
                         repeats=min(self.repeats, 5))
        state = self.snap.get("state")
        if state and "isOn" in state:
            # Re-applying power rebroadcasts appearance to all panels — keep it low.
            self.measure("Set power state", "POST", "/api/state/power", body={"isOn": state["isOn"]}, expect=(202,),
                         repeats=min(self.repeats, 5))

    def _playback_phase(self) -> None:
        state = self.snap.get("state") or {}
        if not state.get("isOn"):
            for label, method, path in (
                ("Play stored scene", "POST", "/api/scenes/:id/play"),
                ("Replay last scene", "POST", "/api/scenes/play"),
                ("Play one-shot scene", "POST", "/api/scenes/play/one-shot"),
                ("Set scene speed", "POST", "/api/scenes/speed"),
                ("Play one-shot animation", "POST", "/api/animations/play"),
                ("Trigger reactive animation", "POST", "/api/animations/trigger"),
                ("Stop scene", "POST", "/api/scenes/stop"),
            ):
                self.skip(label, method, path, "system_off")
            return

        if self.temp_scene_id:
            self.measure("Play stored scene", "POST", f"/api/scenes/{self.temp_scene_id}/play", expect=(202,))
            self.measure("Replay last scene", "POST", "/api/scenes/play", expect=(202, 404))
        else:
            self.skip("Play stored scene", "POST", "/api/scenes/:id/play", "no temp scene")
            self.skip("Replay last scene", "POST", "/api/scenes/play", "no temp scene")

        self.measure("Play one-shot scene", "POST", "/api/scenes/play/one-shot", body=TEMP_SCENE_BODY, expect=(202,))

        speed = (self.snap.get("state") or {}).get("speed", 1.0)
        self.measure("Set scene speed", "POST", "/api/scenes/speed", body={"speed": speed}, expect=(202,))

        # group 250 (free), near-black, brief — overwritten by the next scene frame.
        self.measure("Play one-shot animation", "POST", "/api/animations/play",
                     body={"group": 250, "panels": "all", "type": "PULSE",
                           "colorFrom": "#000000", "colorTo": "#080808", "duration": 200}, expect=(202,))
        self.measure("Trigger reactive animation", "POST", "/api/animations/trigger",
                     body={"group": 250, "value": 0}, expect=(202,))

        self._panel_phase()

        self.measure("Stop scene", "POST", "/api/scenes/stop", expect=(202,))

    def _panel_phase(self) -> None:
        panels = self.snap.get("panels") or []
        if not panels:
            self.skip("Set panel color", "PUT", "/api/panels/:address/color", "no panels discovered")
            self.skip("Set panel on/off", "PUT", "/api/panels/:address/on", "no panels discovered")
            return
        panel = panels[0]
        addr = panel["address"]
        # Re-apply the panel's captured color/on state — overwritten by the next animation frame anyway.
        color = panel.get("color", "#000000")
        on_value = 1 if panel.get("on") else 0
        self.measure("Set panel color", "PUT", f"/api/panels/{addr}/color", body={"color": color}, expect=(202,))
        self.measure("Set panel on/off", "PUT", f"/api/panels/{addr}/on", body={"value": on_value}, expect=(202,))

    def _teardown(self) -> None:
        """Restore original state. Runs in a finally block."""
        def restore(method: str, path: str, body: Any = None) -> None:
            try:
                self.client.call(method, path, body=body)
            except requests.RequestException:
                pass

        # Delete the persistent temp resources (timed deletes were the pair loop).
        if self.temp_scene_id:
            restore("DELETE", f"/api/scenes/{self.temp_scene_id}")
            self.temp_scene_id = None
        if self.temp_palette:
            restore("DELETE", f"/api/palettes/{self.temp_palette}")
            self.temp_palette = None
        restore("DELETE", f"/api/palettes/{TEMP_PALETTE_PAIR_NAME}")  # defensive

        if self.snap.get("appearance"):
            restore("PATCH", "/api/appearance", self.snap["appearance"])
        if self.snap.get("configuration"):
            restore("PATCH", "/api/configuration", self.snap["configuration"])

        state = self.snap.get("state") or {}
        if "speed" in state:
            restore("POST", "/api/scenes/speed", {"speed": state["speed"]})
        # Restore playback to whatever was running before the run.
        if state.get("playing"):
            if state.get("lastPlayedSceneIsStored") and state.get("lastPlayedSceneId"):
                restore("POST", f"/api/scenes/{state['lastPlayedSceneId']}/play")
            else:
                restore("POST", "/api/scenes/play")  # best effort for a one-shot
        else:
            restore("POST", "/api/scenes/stop")
        if "isOn" in state:
            restore("POST", "/api/state/power", {"isOn": state["isOn"]})

    # -- orchestration ------------------------------------------------------

    def run(self, read_only: bool, skip_playback: bool) -> None:
        self._log("Snapshotting current device state...")
        self._snapshot()
        try:
            self._log("\n== Read-only endpoints ==")
            self._read_only_phase()
            if not read_only:
                self._log("\n== Data endpoints (palettes, scenes) ==")
                self._data_phase()
                self._log("\n== Configuration endpoints ==")
                self._config_phase()
                if not skip_playback:
                    self._log("\n== Playback endpoints ==")
                    self._playback_phase()
        finally:
            if not read_only:
                self._log("\nRestoring original device state...")
                self._teardown()
                self._log("Done.")


def percentile(samples: list[float], pct: float) -> float:
    if not samples:
        return 0.0
    ordered = sorted(samples)
    rank = max(0, min(len(ordered) - 1, round(pct / 100 * (len(ordered) - 1))))
    return ordered[rank]


def print_report(results: list[Measurement], repeats: int) -> None:
    header = ("Endpoint", "Method", "n", "mean", "median", "p95", "min", "max")
    rows: list[tuple[str, ...]] = []
    for m in results:
        if m.ok:
            rows.append((
                m.label, m.method, str(len(m.samples)),
                f"{statistics.mean(m.samples):.1f}",
                f"{statistics.median(m.samples):.1f}",
                f"{percentile(m.samples, 95):.1f}",
                f"{min(m.samples):.1f}",
                f"{max(m.samples):.1f}",
            ))
        else:
            rows.append((m.label, m.method, "-", m.note or "no data", "", "", "", ""))

    widths = [max(len(header[i]), *(len(r[i]) for r in rows)) for i in range(len(header))]
    line = "  ".join(h.ljust(widths[i]) for i, h in enumerate(header))
    print("\n" + line)
    print("-" * len(line))
    # Sort timed rows by mean desc; keep skipped/failed at the bottom in original order.
    timed = sorted((m for m in results if m.ok), key=lambda m: statistics.mean(m.samples), reverse=True)
    untimed = [m for m in results if not m.ok]

    def fmt(m: Measurement) -> str:
        if m.ok:
            cells = (m.label, m.method, str(len(m.samples)),
                     f"{statistics.mean(m.samples):.1f}", f"{statistics.median(m.samples):.1f}",
                     f"{percentile(m.samples, 95):.1f}", f"{min(m.samples):.1f}", f"{max(m.samples):.1f}")
            return "  ".join(c.ljust(widths[i]) for i, c in enumerate(cells))
        return f"{m.label.ljust(widths[0])}  {m.method.ljust(widths[1])}  {m.note}"

    for m in timed:
        print(fmt(m))
    for m in untimed:
        print(fmt(m))

    print("-" * len(line))
    print(f"All times in ms, round-trip per request ({repeats} samples each). "
          f"{len(timed)} measured, {len(untimed)} skipped/failed.\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Measure Lightnet HTTP API endpoint processing time.")
    parser.add_argument("host", help="Controller IP or hostname (e.g. 192.168.1.42 or lightnet-XXXX.local)")
    parser.add_argument("-n", "--repeats", type=int, default=20, help="Timed samples per endpoint (default 20)")
    parser.add_argument("-w", "--warmup", type=int, default=1, help="Untimed warmup calls per endpoint (default 1)")
    parser.add_argument("-t", "--timeout", type=float, default=10.0, help="Per-request timeout seconds (default 10)")
    parser.add_argument("--read-only", action="store_true", help="Only test GET endpoints (no mutations)")
    parser.add_argument("--skip-playback", action="store_true", help="Skip play/stop/animation/panel tests")
    parser.add_argument("--json", metavar="FILE", help="Also write raw results to a JSON file")
    args = parser.parse_args()

    base_url = args.host if args.host.startswith("http") else f"http://{args.host}"
    client = ApiClient(base_url, args.timeout)

    print(f"Target: {base_url}  |  repeats={args.repeats}  warmup={args.warmup}")
    bench = Benchmark(client, args.repeats, args.warmup)
    bench.run(read_only=args.read_only, skip_playback=args.skip_playback)
    print_report(bench.results, args.repeats)

    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump([
                {"label": m.label, "method": m.method, "path": m.path,
                 "status": m.status, "note": m.note, "samples_ms": m.samples}
                for m in bench.results
            ], fh, indent=2)
        print(f"Raw results written to {args.json}")

    failed = [m for m in bench.results if (m.note or "").startswith("FAILED")]
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
