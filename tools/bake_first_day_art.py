#!/usr/bin/env python3
"""Bake art referenced by the opening, day-two routes, and poem responses.

The output PNGs are already scaled and alpha-cropped for the Nspire's 320x240
screen. ``assets.json`` preserves their DDLC image names and draw offsets for
the story-to-Nspire importer. The nZATO content-pack builder later converts
these images into its RGB565 palette/index representation.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any

from ddlc_image_resolve import CG_PREFIXES, ImageResolver
from rpyc_ast import flatten, kind, load_rpyc

CHARACTERS = {"s": "sayori", "n": "natsuki", "y": "yuri", "m": "monika"}
SAFE = re.compile(r"[^a-zA-Z0-9_-]+")


def image_tuple(node: Any) -> tuple[str, ...] | None:
    spec = getattr(node, "imspec", None)
    if not spec or not spec[0]:
        return None
    return tuple(str(part) for part in spec[0])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw-dir", type=Path, required=True,
                        help="directory containing the extracted scripts and images/")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    raw_dir = args.raw_dir.resolve()
    selected_labels = {
        "script-ch5.rpyc": {"ch5_main"},
        "script-ch4.rpyc": {"ch4_main","ch4_exclusive_natsuki","ch4_exclusive_yuri",
                            "ch4_end","ch4_end_yes","ch4_end_no"},
        "script-ch0.rpyc": {"ch0_main"},
        "script-ch1.rpyc": {"ch1_main", "ch1_end", "ch1_end_natsuki",
                             "ch1_end_yuri", "ch1_end_sayori"},
        "script-ch2.rpyc": {"ch2_main", "ch2_end", "ch2_end_sayori",
                             "ch2_end_natsuki", "ch2_end_yuri", "ch2_end_shared"},
        "script-ch3.rpyc": {"ch3_main", "ch3_start_natsuki", "ch3_start_yuri",
                             "ch3_start_none", "ch3_end", "ch3_end_natsuki",
                             "ch3_end_yuri", "ch3_end_sayori", "ch3_end_monika"},
        "script-exclusives-sayori.rpyc": {"sayori_exclusive_1", "sayori_exclusive_2"},
        "script-exclusives-natsuki.rpyc": {"natsuki_exclusive_1", "natsuki_exclusive_2",
                                            "natsuki_exclusive_2_ch3"},
        "script-exclusives-yuri.rpyc": {"yuri_exclusive_1", "yuri_exclusive_2",
                                        "yuri_exclusive_2_ch3"},
        "script-poemresponses.rpyc": {
            "ch1_s_bad", "ch1_s_med", "ch1_s_good", "ch1_s_shared",
            "ch1_n_bad", "ch1_n_med", "ch1_n_good",
            "ch1_y_bad", "ch1_y_med", "ch1_y_good", "ch1_y_shared", "ch1_m_start",
            "m_sayori_1", "m_natsuki_1", "m_yuri_1",
            "ch2_s_bad", "ch2_s_shared", "ch2_s_med", "ch2_s_med_shared", "ch2_s_good",
            "ch2_n_bad", "ch2_n_bad_sharedwithch3", "ch2_n_med", "ch2_n_med_shared",
            "ch2_n_good", "ch2_n_good_sharedwithch3", "ch2_y_bad", "ch2_y_shared",
            "ch2_y_med", "ch2_y_good", "ch2_y_good_shared", "ch2_m_start",
            "m_sayori_2", "m_natsuki_2", "m_yuri_2",
        },
    }
    # Day 4 can jump to nested/shared response labels from both chapter 2 and 3.
    _, poem_top = load_rpyc(raw_dir / "script-poemresponses.rpyc")
    selected_labels["script-poemresponses.rpyc"].update(
        node.name for node in flatten(poem_top)
        if kind(node) == "Label" and node.name.startswith(("ch3_", "m_"))
    )
    selected_nodes: list[Any] = []
    for script_name, names in selected_labels.items():
        script = raw_dir / script_name
        if not script.is_file():
            raise SystemExit(f"missing compiled chapter script: {script}")
        _metadata, nodes = load_rpyc(script)
        found = {node.name: node for node in flatten(nodes) if kind(node) == "Label"}
        missing = names - found.keys()
        if missing:
            raise SystemExit(f"{script_name} is missing labels: {', '.join(sorted(missing))}")
        selected_nodes.extend(found[name] for name in sorted(names))

    scenes: set[tuple[str, ...]] = set()
    sprites: set[tuple[str, ...]] = set()
    for label in selected_nodes:
        cg_layers: list[str] = []
        for node in flatten(label.block):
            node_kind = kind(node)
            if node_kind == "Scene":
                name = image_tuple(node)
                if name and name[0].startswith(CG_PREFIXES):
                    cg_layers = [name[0]]
                    scenes.add(tuple(cg_layers))
                else:
                    cg_layers = []
                    if name and (name[0] == "bg" or name == ("black",)):
                        scenes.add(name)
            elif node_kind == "Show":
                name = image_tuple(node)
                if name and name[0].startswith(CG_PREFIXES):
                    if name[0] not in cg_layers:
                        cg_layers.append(name[0])
                    scenes.add(tuple(cg_layers))
                elif name and len(name) > 1 and name[0] in set(CHARACTERS.values()):
                    sprites.add(name)
            elif node_kind == "Hide":
                name = image_tuple(node)
                if name and name[0].startswith(CG_PREFIXES) and name[0] in cg_layers:
                    cg_layers.remove(name[0])
                    if cg_layers:
                        scenes.add(tuple(cg_layers))
            elif node_kind == "Say":
                who = getattr(node, "who", None)
                attrs = getattr(node, "attributes", None) or []
                if who in CHARACTERS and attrs:
                    sprites.add((CHARACTERS[who], *(str(a) for a in attrs)))

    args.output_dir.mkdir(parents=True, exist_ok=True)
    resolver = ImageResolver(raw_dir, args.output_dir)
    resources: list[dict[str, Any]] = []
    unresolved: list[dict[str, Any]] = []

    for image_name in sorted(scenes):
        ident = resolver.scene_id(image_name)
        if ident is None:
            unresolved.append({"kind": "background", "image": image_name})
            continue
        entry = resolver.scenes[ident]
        resources.append({
            "kind": "background", "image": list(image_name), "id": ident,
            "file": entry["file"], "width": entry["w"], "height": entry["h"],
        })

    for image_name in sorted(sprites):
        base_id, overlay_id = resolver.sprite_layers(image_name)
        if base_id is None:
            unresolved.append({"kind": "sprite", "image": image_name})
            continue
        layer_ids = [base_id] + ([overlay_id] if overlay_id is not None else [])
        layers = []
        for layer_index, ident in enumerate(layer_ids):
            entry = resolver.sprites[ident]
            layers.append({
                "name": f"sprite_atom_{ident:03d}", "id": ident,
                "role": "base" if layer_index == 0 else "expression",
                "file": entry["file"], "width": entry["w"], "height": entry["h"],
                "x_offset": entry["dx"], "y_offset": entry["dy"],
            })
        resources.append({
            "kind": "prescaled_sprite", "image": list(image_name),
            "id": len([row for row in resources if row["kind"] == "prescaled_sprite"]),
            "layers": layers,
        })

    from bake_act2_art import bake_act2
    variants = bake_act2(raw_dir, resolver, args.output_dir, resources, unresolved)
    summary = {
        "variants": variants,
        "screen": {"width": 320, "height": 240, "color_depth": 16},
        "scaling": "DDLC original 1280x720 scene mapped to 320x180; sprites at 0.80 x 0.25",
        "sprite_anchor": "Ren'Py x-center and bottom anchor, with stored crop offsets",
        "resources": resources,
        "unresolved": unresolved,
        "resolver_unsupported": [
            {"image": list(name), "reason": reason}
            for name, reason in resolver.unsupported
        ],
    }
    (args.output_dir / "assets.json").write_text(
        json.dumps(summary, indent=2), encoding="utf-8"
    )
    print(f"Baked {len(scenes) - sum(x['kind'] == 'background' for x in unresolved)} scene frames")
    print(f"Baked {len(sprites) - sum(x['kind'] == 'sprite' for x in unresolved)} character expressions")
    print(f"Unresolved referenced images: {len(unresolved)}")
    print(f"Manifest: {args.output_dir / 'assets.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
