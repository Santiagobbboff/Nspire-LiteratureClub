#!/usr/bin/env python3
"""Merge a fresh art bake into config/ddlc_art_layout.json.

Existing rows keep their resource names, ids and files (so Act 1 packs and
saves stay stable); rows new in the bake are appended with fresh ids.
Usage: update_art_layout.py <bake assets.json>
"""
import json
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
LAYOUT = PROJECT / "config/ddlc_art_layout.json"


def main() -> int:
    fresh = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    layout = json.loads(LAYOUT.read_text(encoding="utf-8"))
    # Rows added for Act 2+ may be dropped or change kind between bakes; Act 1
    # rows (no introduced_act) are never touched.
    fresh_kinds = {tuple(row["image"]): row["kind"] for row in fresh["resources"]}
    pruned = [row for row in layout["resources"]
              if not row.get("introduced_act")
              or fresh_kinds.get(tuple(row["image"])) == row["kind"]]
    removed = len(layout["resources"]) - len(pruned)
    layout["resources"] = pruned
    by_image = {tuple(row["image"]): row for row in layout["resources"]}
    # Fresh atom id -> existing atom name, learned from rows present in both.
    atom_names: dict[int, dict] = {}
    for row in fresh["resources"]:
        old = by_image.get(tuple(row["image"]))
        if old is None or "layers" not in row or "layers" not in old:
            continue
        for new_part, old_part in zip(row["layers"], old["layers"]):
            atom_names.setdefault(new_part["id"], old_part)
    next_atom = 1 + max((part["id"] for row in layout["resources"] for part in row.get("layers", [])),
                        default=-1)
    next_id = {}
    for row in layout["resources"]:
        if "layers" not in row:
            next_id[row["kind"]] = max(next_id.get(row["kind"], -1), row["id"])
        else:
            next_id["prescaled_sprite"] = max(next_id.get("prescaled_sprite", -1), row["id"])
    added = 0
    for row in fresh["resources"]:
        image = tuple(row["image"])
        if image in by_image:
            continue
        row = json.loads(json.dumps(row))
        if "layers" in row:
            next_id["prescaled_sprite"] = next_id.get("prescaled_sprite", -1) + 1
            row["id"] = next_id["prescaled_sprite"]
            for part in row["layers"]:
                known = atom_names.get(part["id"])
                if known is None:
                    known = {"name": f"sprite_atom_{next_atom:03d}", "id": next_atom,
                             "file": f"sprite_{next_atom:03d}_{part['file'].split('_', 2)[-1]}"}
                    next_atom += 1
                    atom_names[part["id"]] = known
                    known["_fresh_file"] = part["file"]
                part.update(name=known["name"], id=known["id"], file=known["file"])
        else:
            next_id[row["kind"]] = next_id.get(row["kind"], -1) + 1
            fresh_file = row["file"]
            row["id"] = next_id[row["kind"]]
            prefix = {"background": "bg", "screen_layer": "ovl", "fullscreen_layer": "fsl",
                      "fullscreen_background": "fs"}[row["kind"]]
            row["file"] = f"{prefix}_{row['id']:03d}_{fresh_file.split('_', 2)[-1]}"
        layout["resources"].append(row)
        by_image[image] = row
        added += 1
    layout["variants"] = fresh.get("variants", {})
    LAYOUT.write_text(json.dumps(layout, indent=2), encoding="utf-8")
    print(f"layout now has {len(layout['resources'])} resources ({added} added, {removed} removed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
