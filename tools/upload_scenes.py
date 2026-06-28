#!/usr/bin/env python3
"""Upload scene JSON files to a live Lightnet controller.

    python upload_scenes.py <ip-or-host> [options]

Reads every *.json file in --dir (default: tools/scenes), looks each one up
by its "name" field against the controller's existing scene library
(GET /api/scenes), and POSTs any that aren't already present. Scenes that
already exist (by name) are skipped by default; pass --update to PATCH them
with the file's current contents instead.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

try:
    import requests
except ImportError:
    sys.exit("This tool requires 'requests'. Install with: pip install requests")


def list_existing_scenes(base_url: str, timeout: float) -> dict[str, str]:
    """Returns {name: id} for every non-hidden scene currently on the device."""
    resp = requests.get(f"{base_url}/api/scenes", timeout=timeout)
    resp.raise_for_status()
    return {entry["name"]: entry["id"] for entry in resp.json()}


def create_scene(base_url: str, body: dict, timeout: float) -> str:
    resp = requests.post(f"{base_url}/api/scenes", json=body, timeout=timeout)
    resp.raise_for_status()
    return resp.json()["id"]


def update_scene(base_url: str, scene_id: str, body: dict, timeout: float) -> str:
    resp = requests.patch(f"{base_url}/api/scenes/{scene_id}", json=body, timeout=timeout)
    resp.raise_for_status()
    return resp.json()["id"]


def main() -> int:
    parser = argparse.ArgumentParser(description="Upload scene JSON files to a Lightnet controller.")
    parser.add_argument("host", help="Controller IP or hostname (e.g. 192.168.1.42 or lightnet-XXXX.local)")
    parser.add_argument("-d", "--dir", default="tools/scenes",
                         help="Directory of scene JSON files (default: tools/scenes)")
    parser.add_argument("--update", action="store_true",
                         help="PATCH scenes that already exist (matched by name) instead of skipping them")
    parser.add_argument("--dry-run", action="store_true",
                         help="Only report what would be created/updated; no requests sent")
    parser.add_argument("-t", "--timeout", type=float, default=10.0, help="Per-request timeout seconds")
    args = parser.parse_args()

    base_url = args.host if args.host.startswith("http") else f"http://{args.host}"
    scenes_dir = Path(args.dir)

    if not scenes_dir.is_dir():
        sys.exit(f"Not a directory: {scenes_dir}")

    files = sorted(scenes_dir.glob("*.json"))

    if not files:
        sys.exit(f"No .json files found in {scenes_dir}")

    print(f"Target: {base_url}  |  {len(files)} scene file(s) in {scenes_dir}")

    try:
        existing = list_existing_scenes(base_url, args.timeout)
    except requests.RequestException as exc:
        sys.exit(f"Failed to list existing scenes: {exc}")

    print(f"{len(existing)} scene(s) already on device.\n")

    created = updated = skipped = failed = 0

    for path in files:
        try:
            body = json.loads(path.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError) as exc:
            print(f"  [FAIL]   {path.name}: {exc}")
            failed += 1
            continue

        name = body.get("name")

        if not name:
            print(f"  [FAIL]   {path.name}: missing \"name\" field")
            failed += 1
            continue

        existing_id = existing.get(name)

        if existing_id and not args.update:
            print(f"  [SKIP]   {name} (already exists as {existing_id})")
            skipped += 1
            continue

        action = "update" if existing_id else "create"

        if args.dry_run:
            print(f"  [DRY-RUN] would {action} {name}")
            continue

        try:
            if existing_id:
                update_scene(base_url, existing_id, body, args.timeout)
                print(f"  [UPDATE] {name} ({existing_id})")
                updated += 1
            else:
                new_id = create_scene(base_url, body, args.timeout)
                print(f"  [CREATE] {name} ({new_id})")
                created += 1
        except requests.RequestException as exc:
            print(f"  [FAIL]   {name}: {exc}")
            failed += 1

    print(f"\n{created} created, {updated} updated, {skipped} skipped, {failed} failed.")

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
