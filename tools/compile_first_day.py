#!/usr/bin/env python3
"""Compile the complete Act 1 story and three scored poem rounds.

The runtime randomizes the poem minigame's twenty boards of ten words.
Each pick accumulates the original three character scores and selects
the route and poem-response tier. Ren'Py Python/audio effects are omitted,
and unsupported story control flow fails closed.
"""

from __future__ import annotations

import argparse
import json
import random
import re
import shutil
import sys
from pathlib import Path
from typing import Any

from PIL import Image

from rpyc_ast import flatten, kind, load_rpyc, pycode_source

CHARACTERS = {"s": "sayori", "n": "natsuki", "y": "yuri", "m": "monika"}
SPEAKERS = {
    "s": "Sayori", "n": "Natsuki", "y": "Yuri", "m": "Monika",
    "mc": "{player}", "ny": "Natsuki and Yuri",
}
INLINE_VAR = re.compile(r"\[([A-Za-z_][A-Za-z0-9_]*)\]")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw-dir", type=Path, required=True)
    parser.add_argument("--art-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--font", type=Path, required=True)
    args = parser.parse_args()

    source_root = Path(__file__).resolve().parents[1] / "python" / "src"
    sys.path.insert(0, str(source_root))
    from ddlc_transforms import load_transform_animations
    from ddlc_image_resolve import CG_PREFIXES
    from nzato_converter.compiler import UI_TEXT_KEYS, build_pack

    raw_dir, art_dir = args.raw_dir.resolve(), args.art_dir.resolve()
    from source_text import SourceText
    local_text = SourceText(raw_dir)
    image_manifest = json.loads((art_dir / "assets.json").read_text(encoding="utf-8"))
    asset_by_image = {tuple(row["image"]): row for row in image_manifest["resources"]}
    resource_name: dict[tuple[str, ...], str | list[str]] = {}
    resources: list[dict[str, Any]] = []
    resource_names_seen: set[str] = set()
    for row in image_manifest["resources"]:
        image = tuple(row["image"])
        if row["kind"] == "prescaled_sprite" and row.get("layers"):
            parts = row["layers"]
            resource_name[image] = [part["name"] for part in parts]
            for part in parts:
                if part["name"] not in resource_names_seen:
                    resources.append({"name": part["name"],
                                      "path": f"img/{part['file']}",
                                      "kind": "prescaled_sprite"})
                    resource_names_seen.add(part["name"])
        else:
            prefix = {"background": "bg", "screen_layer": "ovl", "fullscreen_layer": "fsl",
                      "fullscreen_background": "fs"}.get(row["kind"], "sprite")
            name = f"{prefix}_{row['id']:03d}"
            resource_name[image] = name
            entry = {"name": name, "path": f"img/{row['file']}", "kind": row["kind"]}
            if "screen_y" in row:
                entry["screen_x"], entry["screen_y"] = row.get("screen_x", 0), row["screen_y"]
            resources.append(entry)

    # The simplified DDLC title menu intentionally uses a plain black backdrop
    # until the real menu artwork is adapted for the Nspire layout.
    title_bg_path = art_dir / "img" / "simple_title_bg.png"
    black_path = art_dir / "img" / "title_black.png"
    transparent_path = art_dir / "img" / "title_transparent.png"
    Image.new("RGB", (320, 180), (0, 0, 0)).save(black_path)
    shutil.copy2(black_path, title_bg_path)
    Image.new("RGBA", (1, 1), (0, 0, 0, 0)).save(transparent_path)
    resources.append({"name": "__simple_title_bg", "path": "img/simple_title_bg.png",
                      "kind": "background"})
    resources.append({"name": "__title_aux0", "path": "img/title_black.png",
                      "kind": "background"})
    resources.append({"name": "__title_aux1", "path": "img/title_transparent.png",
                      "kind": "ui_overlay"})
    for index in range(2, 5):
        resources.append({"name": f"__title_aux{index}",
                          "path": "img/title_transparent.png", "kind": "particle"})
    resources.extend((
        {"name": "__gallery_overlay", "path": "img/title_transparent.png",
         "kind": "ui_overlay"},
        {"name": "__main_menu_overlay", "path": "img/title_transparent.png",
         "kind": "ui_overlay"},
    ))

    _metadata, top = load_rpyc(raw_dir / "script-ch0.rpyc")
    label = next((node for node in top if kind(node) == "Label" and node.name == "ch0_main"), None)
    if label is None:
        raise SystemExit("script-ch0.rpyc has no ch0_main label")
    transforms = load_transform_animations(raw_dir)
    last_center: dict[str, int] = {}
    visible_image: dict[str, tuple[str, ...]] = {}
    character_layers: dict[str, set[str]] = {key: set() for key in CHARACTERS}
    character_by_name = {name: key for key, name in CHARACTERS.items()}
    for image, packed_names in resource_name.items():
        # Keep old cleanup event counts/save cursors stable. Day 4 resources
        # become eligible for broad cleanup only after the confirmed slice.
        if asset_by_image[image].get("introduced_day", 1) >= 4:
            continue
        character = character_by_name.get(image[0]) if image else None
        if character is None:
            continue
        names = packed_names if isinstance(packed_names, list) else [packed_names]
        character_layers[character].update(names)
    aliases: dict[str, str] = {}
    cg_layers: list[str] = []
    events: list[dict[str, Any]] = []
    skipped: dict[str, int] = {}
    unknown_vars: set[str] = set()
    ended = False
    pending_clear: list[int] = []  # persistent.clear[k] waiting for the next dialogue line

    def report_skip(reason: str) -> None:
        skipped[reason] = skipped.get(reason, 0) + 1

    def show(character: str, image: tuple[str, ...], at_list: Any = None) -> bool:
        row = asset_by_image.get(image)
        if row is None:
            report_skip(f"missing baked sprite: {' '.join(image)}")
            return False
        center = last_center.get(character, 160)
        for transform in at_list or []:
            if transform in transforms:
                canvas_x, _flags = transforms[transform]
                # Same 2-pixel center precision as the DDLC-aware TI-84 compiler.
                center = round(canvas_x / 8) * 2
                break
        last_center[character] = center
        old = visible_image.get(character)
        if old is not None and old != image:
            for old_part in reversed(asset_by_image[old]["layers"]):
                events.append({"op": "hide", "resource": old_part["name"]})
        for part in row["layers"]:
            events.append({
                "op": "show", "resource": part["name"],
                "screen_x": center - part["width"] // 2 + part["x_offset"],
                "screen_y": 185 - part["height"] + part["y_offset"],
            })
        visible_image[character] = image
        return True

    def hide_character(character: str) -> None:
        image = visible_image.pop(character, None)
        if image is None:
            return
        for part in reversed(asset_by_image[image]["layers"]):
            events.append({"op": "hide", "resource": part["name"]})

    def hide_all_character_layers(character: str) -> None:
        # Use at story joins where the runtime may have arrived through a
        # different route branch than the compiler's current image snapshot.
        visible_image.pop(character, None)
        for name in sorted(character_layers[character], reverse=True):
            events.append({"op": "hide", "resource": name})

    def hide_response_layers(resources: set[str]) -> None:
        # Poem entry clears the previous scene. At each response exit,
        # only the expression layers referenced by that response can remain.
        for name in sorted(resources, reverse=True):
            events.append({"op": "hide", "resource": name})

    replacements = {
        "player": "{player}", "s_name": "Sayori", "n_name": "Natsuki",
        "y_name": "Yuri", "m_name": "Monika", "currentname": "{player}",
    }

    def compile_nodes(nodes: list[Any], script_file: str, section: str) -> None:
        """Compile a known-linear block, rejecting control flow we cannot model."""
        for node in nodes:
            node_kind = kind(node)
            if node_kind in {"If", "Menu", "Call", "Jump", "While", "Label"}:
                # Sayori's first exclusive has a music-only conditional. Its
                # block contains no story, visual, or state-changing events.
                if node_kind == "If" and all(
                    kind(child) in {"With", "Python", "UserStatement", "Pass"}
                    for _condition, block in node.entries for child in block
                ):
                    report_skip("music-only conditional")
                    continue
                raise SystemExit(
                    f"Unexpected control flow in {section} at line "
                    f"{node.linenumber}: {node_kind}"
                )
            if node_kind == "Scene":
                spec = getattr(node, "imspec", None)
                image = tuple(spec[0]) if spec and spec[0] else ()
                if image and image[0].startswith(CG_PREFIXES):
                    cg_layers[:] = [image[0]]
                    composite = tuple(cg_layers)
                    if composite in asset_by_image:
                        events.append({"op": "scene", "resource": resource_name[composite]})
                        visible_image.clear()
                        visible_image.clear()
                    else:
                        report_skip(f"missing baked CG frame: {' '.join(composite)}")
                    continue
                cg_layers.clear()
                if image not in asset_by_image:
                    report_skip(f"missing baked scene: {' '.join(image)}")
                    continue
                events.append({"op": "scene", "resource": resource_name[image]})
                visible_image.clear()
            elif node_kind == "Show":
                spec = getattr(node, "imspec", None)
                image = tuple(spec[0]) if spec and spec[0] else ()
                tag = image[0] if image else ""
                if image in asset_by_image and tag == "bg":
                    cg_layers.clear()
                    events.append({"op": "scene", "resource": resource_name[image]})
                    visible_image.clear()
                    continue
                if tag.startswith(CG_PREFIXES):
                    if tag not in cg_layers:
                        cg_layers.append(tag)
                    composite = tuple(cg_layers)
                    if composite in asset_by_image:
                        events.append({"op": "scene", "resource": resource_name[composite]})
                        visible_image.clear()
                    else:
                        report_skip(f"missing baked CG frame: {' '.join(composite)}")
                    continue
                character = next((key for key, value in CHARACTERS.items() if value == tag), None)
                if character is None:
                    report_skip(f"non-character Show: {' '.join(image)}")
                    continue
                if len(spec) > 2 and spec[2]:
                    aliases[str(spec[2])] = character
                if len(image) == 1:
                    image = visible_image.get(character, ())
                if image:
                    show(character, image, spec[3] if len(spec) > 3 else None)
            elif node_kind == "Hide":
                spec = getattr(node, "imspec", None)
                image = tuple(spec[0]) if spec and spec[0] else ()
                tag = image[0] if image else ""
                if tag.startswith(CG_PREFIXES) and tag in cg_layers:
                    cg_layers.remove(tag)
                    composite = tuple(cg_layers)
                    if composite in asset_by_image:
                        events.append({"op": "scene", "resource": resource_name[composite]})
                    else:
                        report_skip(f"missing baked CG frame: {' '.join(composite)}")
                    continue
                character = next((key for key, value in CHARACTERS.items() if value == tag), aliases.get(tag))
                if character:
                    hide_character(character)
            elif node_kind == "Say":
                text = getattr(node, "what", None)
                if not isinstance(text, str):
                    report_skip("non-literal dialogue")
                    continue
                for match in INLINE_VAR.findall(text):
                    if match in replacements:
                        text = text.replace(f"[{match}]", replacements[match])
                    else:
                        unknown_vars.add(match)
                who = getattr(node, "who", None)
                if who in CHARACTERS and getattr(node, "attributes", None):
                    image = (CHARACTERS[who], *(str(value) for value in node.attributes))
                    show(who, image)
                if who is not None:
                    text = f'"{text}"'
                events.append({
                    "op": "say", "speaker_en": SPEAKERS.get(who, ""), "text_en": text,
                    "script": script_file, "line": int(getattr(node, "linenumber", 0)),
                })
                if pending_clear:
                    events[-1]["clear_cg"] = pending_clear.pop(0)
            elif node_kind == "Return":
                return
            elif node_kind == "Python":
                source = (pycode_source(node.code) or "").strip()
                clear = re.fullmatch(r"persistent\.clear\[(\d+)\] = True", source)
                if clear:
                    pending_clear.append(int(clear.group(1)))
                continue
            elif node_kind in {"With", "UserStatement", "Pass"}:
                # Transitions/audio/initialization don't affect this simplified
                # linear playback. Crucially, none of these execute game code.
                continue
            else:
                report_skip(f"unhandled node in {section}: {node_kind}")

    if not label.block or kind(label.block[-1]) != "Return":
        raise SystemExit("ch0_main ended without its Return; refusing a truncated build")
    events.append({"op": "label", "name": "act1_start"})
    compile_nodes(label.block, "script-ch0.rpyc", "ch0_main")

    # Replace the Ren'Py poem UI with a simple 20-round word selector. Each
    # round offers ten candidates, and the original point values are applied
    # to three persistent story-state slots (Sayori=3, Natsuki=4, Yuri=5).
    # This fixed shuffled board is intentionally simple; runtime randomization
    # and the paper-style UI can be added independently.
    _day2_meta, day2_top = load_rpyc(raw_dir / "script-ch1.rpyc")
    day2 = next((node for node in day2_top
                 if kind(node) == "Label" and node.name == "ch1_main"), None)
    if day2 is None or len(day2.block) <= 148 or kind(day2.block[124]) != "Call":
        raise SystemExit("unexpected ch1_main layout; refusing to compile a partial route")
    route_specs = (
        ("Sayori", "sayori", 0, "script-exclusives-sayori.rpyc", "sayori_exclusive_1"),
        ("Natsuki", "natsuki", 1, "script-exclusives-natsuki.rpyc", "natsuki_exclusive_1"),
        ("Yuri", "yuri", 2, "script-exclusives-yuri.rpyc", "yuri_exclusive_1"),
    )
    word_rows = []
    for line in (raw_dir / "poemwords.txt").read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        word, *points = line.split(",")
        values = tuple(int(float(value)) for value in points)
        if len(values) != 3 or any(value not in (1, 2, 3) for value in values):
            raise SystemExit(f"invalid poem word score row: {line}")
        word_rows.append((word, values))
    if len(word_rows) != 228:
        raise SystemExit(f"poemwords.txt has {len(word_rows)} words; expected 228")
    all_word_rows = list(word_rows)
    random.Random("DDLC Nspire poem word board v1").shuffle(word_rows)
    word_rows = word_rows[:200]

    events.append({"op": "set_state", "state": 3, "value": 0})
    events.append({"op": "set_state", "state": 4, "value": 0})
    events.append({"op": "set_state", "state": 5, "value": 0})
    events.append({"op": "say", "speaker_en": "",
                   "text_en": "It's time to write a poem. Choose one word from each set of ten. Your choices will shape the routes and reactions."})
    for round_index in range(20):
        page = word_rows[round_index * 10:(round_index + 1) * 10]
        events.append({"op": "label", "name": f"poem_words_{round_index}"})
        events.append({"op": "choice", "state": 15, "count": 10,
                       "text_en": f"Choose a word ({round_index + 1}/20)."})
        for option_index, (word, _points) in enumerate(page):
            events.append({"op": "choice_option", "value": option_index,
                           "target": f"poem_pick_{round_index}_{option_index}",
                           "text_en": word})
        for option_index, (_word, points) in enumerate(page):
            events.append({"op": "label", "name": f"poem_pick_{round_index}_{option_index}"})
            for slot, points_for_girl in zip((3, 4, 5), points):
                events.append({"op": "add_state", "state": slot,
                               "value": points_for_girl})
            events.append({"op": "jump", "target":
                           f"poem_words_{round_index + 1}" if round_index < 19
                           else "poem_score_winner"})

    # The original sorts Sayori, Natsuki, Yuri scores in that insertion order;
    # Python's stable sort means ties are awarded Yuri, then Natsuki, then Sayori.
    events.extend((
        {"op": "label", "name": "poem_score_winner"},
        {"op": "if_state_gt", "state": 3, "other_state": 4,
         "target": "poem_sayori_beats_natsuki"},
        {"op": "jump", "target": "poem_natsuki_or_yuri"},
        {"op": "label", "name": "poem_sayori_beats_natsuki"},
        {"op": "if_state_gt", "state": 3, "other_state": 5,
         "target": "poem_winner_sayori"},
        {"op": "jump", "target": "poem_winner_yuri"},
        {"op": "label", "name": "poem_natsuki_or_yuri"},
        {"op": "if_state_gt", "state": 4, "other_state": 5,
         "target": "poem_winner_natsuki"},
        {"op": "jump", "target": "poem_winner_yuri"},
    ))
    for value, route_name in ((0, "sayori"), (1, "natsuki"), (2, "yuri")):
        events.extend((
            {"op": "label", "name": f"poem_winner_{route_name}"},
            {"op": "set_state", "state": 0, "value": value},
            {"op": "jump", "target": "day2_shared_opening"},
        ))

    events.append({"op": "label", "name": "day2_shared_opening"})
    compile_nodes(day2.block[:124], "script-ch1.rpyc", "ch1_main shared opening")
    events.extend((
        {"op": "if_state", "state": 0, "value": 0, "target": "day2_sayori"},
        {"op": "if_state", "state": 0, "value": 1, "target": "day2_natsuki"},
        {"op": "jump", "target": "day2_yuri"},
    ))

    for _display_name, route_name, _value, script_name, exclusive_label in route_specs:
        _route_meta, route_top = load_rpyc(raw_dir / script_name)
        route = next((node for node in route_top
                      if kind(node) == "Label" and node.name == exclusive_label), None)
        if route is None or not route.block or kind(route.block[-1]) != "Return":
            raise SystemExit(f"{script_name} is missing the expected {exclusive_label} route")
        events.append({"op": "label", "name": f"day2_{route_name}"})
        compile_nodes(route.block, script_name, exclusive_label)
        events.append({"op": "jump", "target": "day2_route_clear"})

    events.append({"op": "label", "name": "day2_shared_poem_intro"})
    compile_nodes(day2.block[125:148], "script-ch1.rpyc", "ch1_main shared poem intro")

    def store_poem_opinion(score_slot: int, winner_slot: int, winner_value: int,
                           opinion_slot: int, label_prefix: str, next_label: str,
                           max_score: int) -> None:
        events.append({"op": "if_state", "state": winner_slot, "value": winner_value,
                       "target": f"{label_prefix}_winner"})
        for score in range(20, 29):
            events.append({"op": "if_state", "state": score_slot, "value": score,
                           "target": f"{label_prefix}_dislike"})
        for score in range(46, max_score + 1):
            events.append({"op": "if_state", "state": score_slot, "value": score,
                           "target": f"{label_prefix}_like"})
        events.extend((
            {"op": "set_state", "state": opinion_slot, "value": 0},
            {"op": "jump", "target": next_label},
            {"op": "label", "name": f"{label_prefix}_dislike"},
            {"op": "set_state", "state": opinion_slot, "value": -1},
            {"op": "jump", "target": next_label},
            {"op": "label", "name": f"{label_prefix}_like"},
            {"op": "set_state", "state": opinion_slot, "value": 1},
            {"op": "jump", "target": next_label},
            {"op": "label", "name": f"{label_prefix}_winner"},
            {"op": "set_state", "state": opinion_slot, "value": 1},
            {"op": "label", "name": next_label},
        ))

    # Store reaction tiers before the FIRST reading. The route alone does not
    # initialize these saved opinion slots; leaving this until poem two made
    # every first-day reader see the default -1 (bad) opinion.
    for route_name, route_value, score_slot, opinion_slot in (
        ("sayori", 0, 3, 8), ("natsuki", 1, 4, 9), ("yuri", 2, 5, 10),
    ):
        store_poem_opinion(score_slot, 0, route_value, opinion_slot,
                           f"poem0_{route_name}_opinion", f"poem0_next_{route_name}", 60)

    # Preserve original sharing and poem-reading conditions. A
    # four-bit mask mirrors the *_readpoem flags without consuming route slot 0.
    _response_meta, response_top = load_rpyc(raw_dir / "script-poemresponses.rpyc")
    response_labels = {
        node.name: node for node in flatten(response_top) if kind(node) == "Label"
    }
    needed_labels = (
        "ch1_s_bad", "ch1_s_med", "ch1_s_good", "ch1_s_shared",
        "ch1_n_bad", "ch1_n_med", "ch1_n_good",
        "ch1_y_bad", "ch1_y_med", "ch1_y_good", "ch1_y_shared", "ch1_m_start",
        "m_sayori_1", "m_natsuki_1", "m_yuri_1",
        "ch2_s_bad", "ch2_s_shared", "ch2_s_med", "ch2_s_med_shared", "ch2_s_good",
        "ch2_n_bad", "ch2_n_bad_sharedwithch3", "ch2_n_med", "ch2_n_med_shared",
        "ch2_n_good", "ch2_n_good_sharedwithch3", "ch2_y_bad", "ch2_y_shared",
        "ch2_y_med", "ch2_y_good", "ch2_y_good_shared", "ch2_m_start",
        "m_sayori_2", "m_natsuki_2", "m_yuri_2",
    )
    missing_labels = [name for name in needed_labels if name not in response_labels]
    if missing_labels:
        raise SystemExit("missing chapter-one poem response labels: " + ", ".join(missing_labels))
    recipients = (
        ("Sayori", "sayori", 0), ("Natsuki", "natsuki", 1),
        ("Yuri", "yuri", 2), ("Monika", "monika", 3),
    )
    events.append({"op": "set_state", "state": 1, "value": 0})
    events.append({"op": "set_state", "state": 2, "value": 0})
    events.append({"op": "label", "name": "poem_order_clear"})
    # Incoming routes can leave atoms the compiler's last pose never used.
    # Reset the classroom scene to clear ALL live body/face layers before the
    # recipient UI. Replace the former fall-through jump so event IDs stay put.
    # Response exits return here, after initialization, preserving the read mask.
    events.append({"op": "scene", "resource": resource_name[("bg", "club_day")]})
    events.append({"op": "label", "name": "poem_order_dispatch"})
    for mask in range(16):
        events.append({"op": "if_state", "state": 1, "value": mask,
                       "target": "poem_all_read" if mask == 15 else f"poem_order_{mask}"})
    events.append({"op": "jump", "target": "poem_all_read"})

    for mask in range(15):
        remaining = [recipient for recipient in recipients if not mask & (1 << recipient[2])]
        events.append({"op": "label", "name": f"poem_order_{mask}"})
        events.append({
            "op": "choice", "state": 1, "count": len(remaining),
            "text_en": "Who should I show my poem to first?" if mask == 0
                       else "Who should I show my poem to next?",
        })
        for display_name, route_name, bit in remaining:
            events.append({
                "op": "choice_option", "value": mask | (1 << bit),
                "target": f"poem_read_{route_name}", "text_en": display_name,
            })

    first_thoughts = {
        "sayori": (
            local_text.say('script-poemresponses.rpyc', 33),
            local_text.say('script-poemresponses.rpyc', 34),
        ),
        "natsuki": (
            local_text.say('script-poemresponses.rpyc', 39),
            local_text.say('script-poemresponses.rpyc', 40),
        ),
        "yuri": (
            local_text.say('script-poemresponses.rpyc', 45),
            local_text.say('script-poemresponses.rpyc', 46),
        ),
        "monika": (
            local_text.say('script-poemresponses.rpyc', 51),
            local_text.say('script-poemresponses.rpyc', 52),
        ),
    }

    def compile_fragment(nodes: list[Any], script_file: str, section: str,
                         clear_hidden_characters: bool = False) -> None:
        """Compile direct dialogue/art while omitting unsupported control flow."""
        control = {"If", "Label", "Jump", "Return", "Pass", "Python", "With"}
        direct = [node for node in nodes if kind(node) not in control]
        if not clear_hidden_characters:
            compile_nodes(direct, script_file, section)
            return
        for node in direct:
            spec = getattr(node, "imspec", None)
            tag = spec[0][0] if spec and spec[0] else ""
            character = next((key for key, name in CHARACTERS.items() if name == tag), None)
            if kind(node) == "Hide" and character is not None:
                # At a branch join the compiler's last expression can differ
                # from the runtime's. Remove body AND face on every path.
                hide_all_character_layers(character)
            else:
                compile_nodes([node], script_file, section)

    deferred_responses = []

    def compile_original_response(chapter, girl):
        from compile_day4 import Day4Compiler
        compiler = Day4Compiler(events, raw_dir, compile_nodes, visible_image,
                                aliases, last_center, cg_layers, asset_by_image,
                                character_layers, replacements, report_skip)
        compiler.chapter = chapter
        compiler.response_girl = girl
        compiler.serial = len(events) * 100
        compiler.update_flag(1, False)
        script, wrapper = compiler.labels[f"poemresponse_{girl}"]
        for node in wrapper.block[:3]:
            compiler.linear_node(node, script)
        if girl == "monika":
            compiler.invoke(f"ch{chapter}_m_start")
        else:
            slot = {"sayori": (8, 11), "natsuki": (9, 12), "yuri": (10, 13)}[girl][chapter - 1]
            before, after = compiler.snapshot(), []
            done = compiler.fresh("reaction_end")
            for tier, opinion in (("bad", -1), ("med", 0), ("good", 1)):
                body, onward = compiler.fresh("reaction"), compiler.fresh("reaction_next")
                events.append({"op": "if_state", "state": slot, "value": opinion, "target": body})
                compiler.jump(onward)
                compiler.label(body)
                compiler.restore(before)
                compiler.invoke(f"ch{chapter}_{girl[0]}_{tier}")
                after.append(compiler.snapshot())
                compiler.jump(done)
                compiler.label(onward)
            compiler.label(done)
            compiler.merge(after)
        end, skip = compiler.fresh("poem_end"), compiler.fresh("poem_skip")
        compiler.condition("skip_poem", skip, end)
        compiler.label(end)
        end_start = len(events)
        compiler.invoke(f"ch{chapter}_{girl[0]}_end")
        if chapter == 1 and girl == "monika":
            # Ren'Py falls through from ch1_m_end into ch1_m_end2. Append the
            # missing text out of line so existing saves keep their event IDs.
            return_label = events[-1]["name"]
            entry = "day2_monika_after_poem"
            for event in events[end_start:]:
                if event.get("op") == "jump" and event.get("target") == return_label:
                    event["target"] = entry
            deferred_responses.append((entry, return_label,
                                       compiler.labels["ch1_m_end2"]))
        compiler.label(skip)

    for display_name, route_name, bit in recipients:
        response_event_start = len(events)
        events.append({"op": "label", "name": f"poem_read_{route_name}"})
        events.append({"op": "if_state", "state": 1, "value": 1 << bit,
                       "target": f"poem_first_{route_name}"})
        events.append({"op": "jump", "target": f"poem_response_{route_name}"})
        events.append({"op": "label", "name": f"poem_first_{route_name}"})
        for line in first_thoughts[route_name]:
            events.append({"op": "say", "speaker_en": "", "text_en": line,
                           "script": "script-poemresponses.rpyc", "line": 0})
        events.append({"op": "label", "name": f"poem_response_{route_name}"})

        compile_original_response(1, route_name)
        character_code = next(key for key, value in CHARACTERS.items() if value == route_name)
        response_layers = {
            event["resource"] for event in events[response_event_start:]
            if event.get("op") == "show"
            and event.get("resource") in character_layers[character_code]
        }
        events.append({"op": "label", "name": f"poem_read_cleanup_{route_name}"})
        hide_response_layers(response_layers)
        events.append({"op": "jump", "target": "poem_order_clear"})

    events.append({"op": "label", "name": "poem_all_read"})

    # Finish day two with the original disagreement menu and its three
    # linear responses. The selection occupies its own RAM slot for later use.
    conflict = next((node for node in day2_top
                     if kind(node) == "Label" and node.name == "ch1_end"), None)
    conflict_routes = (
        ("Natsuki", "natsuki", 0), ("Yuri", "yuri", 1), (local_text.menu('script-ch1.rpyc', 331, 2), "sayori", 2),
    )
    if conflict is None or len(conflict.block) <= 246 or kind(conflict.block[168]) != "Menu":
        raise SystemExit("unexpected ch1_end layout; refusing to omit the conflict choice")
    compile_nodes(conflict.block[:168], "script-ch1.rpyc", "ch1_end shared conflict setup")
    conflict_visible = visible_image.copy()
    conflict_aliases = aliases.copy()
    conflict_cg_layers = list(cg_layers)
    events.append({"op": "choice", "state": 2, "count": 3,
                   "text_en": "Who should I side with?"})
    for display_name, route_name, value in conflict_routes:
        events.append({"op": "choice_option", "value": value,
                       "target": f"conflict_{route_name}", "text_en": display_name})

    for _display_name, route_name, _value in conflict_routes:
        visible_image.clear()
        visible_image.update(conflict_visible)
        aliases.clear()
        aliases.update(conflict_aliases)
        cg_layers[:] = conflict_cg_layers
        branch_name = f"ch1_end_{route_name}"
        branch = next((node for node in day2_top
                       if kind(node) == "Label" and node.name == branch_name), None)
        if branch is None or not branch.block or kind(branch.block[-1]) != "Return":
            raise SystemExit(f"script-ch1.rpyc is missing the {branch_name} branch")
        events.append({"op": "label", "name": f"conflict_{route_name}"})
        compile_nodes(branch.block, "script-ch1.rpyc", branch_name)
        for character in CHARACTERS:
            hide_character(character)
        events.append({"op": "jump", "target": "ch1_end_shared"})

    events.append({"op": "label", "name": "ch1_end_shared"})
    compile_nodes(conflict.block[169:246], "script-ch1.rpyc", "ch1_end shared ending")
    for character in CHARACTERS:
        hide_character(character)

    # Continue the original chapter flow into the second poem. Keep the two
    # terminal event positions from the previous pack so existing save cursors
    # still land at the start of this new section.
    events.extend((
        {"op": "jump", "target": "poem2_prepare"},
        {"op": "jump", "target": "poem2_prepare"},
    ))
    events.append({"op": "label", "name": "poem2_prepare"})

    events.extend((
        {"op": "set_state", "state": 3, "value": 0},
        {"op": "set_state", "state": 4, "value": 0},
        {"op": "set_state", "state": 5, "value": 0},
        {"op": "set_state", "state": 6, "value": -1},
        {"op": "say", "speaker_en": "",
         "text_en": "Time to write another poem. The choice you made during the disagreement may influence its score."},
    ))

    second_word_rows = list(all_word_rows)
    random.Random("DDLC Nspire poem word board chapter 1").shuffle(second_word_rows)
    second_word_rows = second_word_rows[:200]
    for round_index in range(20):
        page = second_word_rows[round_index * 10:(round_index + 1) * 10]
        events.append({"op": "label", "name": f"poem2_words_{round_index}"})
        events.append({"op": "choice", "state": 15, "count": 10,
                       "text_en": f"Choose a word ({round_index + 1}/20)."})
        for option_index, (word, _points) in enumerate(page):
            events.append({"op": "choice_option", "value": option_index,
                           "target": f"poem2_pick_{round_index}_{option_index}",
                           "text_en": word})
        for option_index, (_word, points) in enumerate(page):
            events.append({"op": "label", "name": f"poem2_pick_{round_index}_{option_index}"})
            for slot, points_for_girl in zip((3, 4, 5), points):
                events.append({"op": "add_state", "state": slot,
                               "value": points_for_girl})
            events.append({"op": "jump", "target":
                           f"poem2_words_{round_index + 1}" if round_index < 19
                           else "poem2_apply_conflict_bonus"})

    # The previous day's Natsuki/Yuri/Sayori disagreement adds five points,
    # exactly as the chapter-one poem script does.
    events.extend((
        {"op": "label", "name": "poem2_apply_conflict_bonus"},
        {"op": "if_state", "state": 2, "value": 0, "target": "poem2_bonus_natsuki"},
        {"op": "if_state", "state": 2, "value": 1, "target": "poem2_bonus_yuri"},
        {"op": "if_state", "state": 2, "value": 2, "target": "poem2_bonus_sayori"},
        {"op": "jump", "target": "poem2_score_winner"},
    ))
    for route_name, score_slot in (("natsuki", 4), ("yuri", 5), ("sayori", 3)):
        events.extend((
            {"op": "label", "name": f"poem2_bonus_{route_name}"},
            {"op": "add_state", "state": score_slot, "value": 5},
            {"op": "jump", "target": "poem2_score_winner"},
        ))
    events.extend((
        {"op": "label", "name": "poem2_score_winner"},
        {"op": "if_state_gt", "state": 3, "other_state": 4,
         "target": "poem2_sayori_beats_natsuki"},
        {"op": "jump", "target": "poem2_natsuki_or_yuri"},
        {"op": "label", "name": "poem2_sayori_beats_natsuki"},
        {"op": "if_state_gt", "state": 3, "other_state": 5,
         "target": "poem2_winner_sayori"},
        {"op": "jump", "target": "poem2_winner_yuri"},
        {"op": "label", "name": "poem2_natsuki_or_yuri"},
        {"op": "if_state_gt", "state": 4, "other_state": 5,
         "target": "poem2_winner_natsuki"},
        {"op": "jump", "target": "poem2_winner_yuri"},
    ))
    for value, route_name in ((0, "sayori"), (1, "natsuki"), (2, "yuri")):
        events.extend((
            {"op": "label", "name": f"poem2_winner_{route_name}"},
            {"op": "set_state", "state": 6, "value": value},
            {"op": "set_state", "state": 14, "value": value},
            {"op": "jump", "target": "poem2_store_opinions"},
        ))
    events.append({"op": "label", "name": "poem2_store_opinions"})
    for route_name, route_value, score_slot, opinion_slot in (
        ("sayori", 0, 3, 11), ("natsuki", 1, 4, 12), ("yuri", 2, 5, 13),
    ):
        store_poem_opinion(score_slot, 6, route_value, opinion_slot,
                           f"poem1_{route_name}_opinion", f"poem1_next_{route_name}", 65)
    events.append({"op": "jump", "target": "day3_shared_opening"})

    # Day-three shared opening, then the player's second route. Repeating the
    # first winner enters that character's follow-up exclusive and CG path.
    day3_meta, day3_top = load_rpyc(raw_dir / "script-ch2.rpyc")
    day3 = next((node for node in day3_top
                 if kind(node) == "Label" and node.name == "ch2_main"), None)
    day3_end = next((node for node in day3_top
                     if kind(node) == "Label" and node.name == "ch2_end"), None)
    if day3 is None or len(day3.block) <= 329 or kind(day3.block[329]) != "Call" or day3_end is None:
        raise SystemExit("unexpected ch2_main/ch2_end layout; refusing a partial day-three build")
    day3_route_files = {
        "sayori": "script-exclusives-sayori.rpyc",
        "natsuki": "script-exclusives-natsuki.rpyc",
        "yuri": "script-exclusives-yuri.rpyc",
    }
    day3_route_sources = {}
    for route_name, script_name in day3_route_files.items():
        _, route_top = load_rpyc(raw_dir / script_name)
        day3_route_sources[route_name] = {
            node.name: node for node in route_top if kind(node) == "Label"
        }
    events.append({"op": "label", "name": "day3_shared_opening"})
    compile_nodes(day3.block[:329], "script-ch2.rpyc", "ch2_main shared opening")
    day3_visible = visible_image.copy()
    day3_aliases = aliases.copy()
    day3_cg_layers = list(cg_layers)
    for route_value, route_name in ((0, "sayori"), (1, "natsuki"), (2, "yuri")):
        events.append({"op": "if_state", "state": 6, "value": route_value,
                       "target": f"day3_choose_{route_name}"})
    events.append({"op": "jump", "target": "day3_choose_yuri"})
    for route_value, route_name in ((0, "sayori"), (1, "natsuki"), (2, "yuri")):
        visible_image.clear()
        visible_image.update(day3_visible)
        aliases.clear()
        aliases.update(day3_aliases)
        cg_layers[:] = day3_cg_layers
        events.append({"op": "label", "name": f"day3_choose_{route_name}"})
        events.append({"op": "if_state", "state": 0, "value": route_value,
                       "target": f"day3_{route_name}_repeat"})
        events.append({"op": "jump", "target": f"day3_{route_name}_first"})
        for ordinal, label_suffix in (("first", 1), ("repeat", 2)):
            events.append({"op": "label", "name": f"day3_{route_name}_{ordinal}"})
            source_name = f"{route_name}_exclusive_{label_suffix}"
            route_node = day3_route_sources[route_name].get(source_name)
            if route_node is None:
                raise SystemExit(f"missing day-three route label {source_name}")
            compile_nodes(route_node.block, day3_route_files[route_name], source_name)
            if ordinal == "repeat" and route_name in {"natsuki", "yuri"}:
                followup_name = f"{route_name}_exclusive_2_ch3"
                followup = day3_route_sources[route_name].get(followup_name)
                if followup is None:
                    raise SystemExit(f"missing day-three CG route label {followup_name}")
                compile_nodes(followup.block, day3_route_files[route_name], followup_name)
            # Each branch ends its own personal scene. A new route must not
            # fall through into the repeat route compiled immediately after it.
            events.append({"op": "jump", "target": "day3_poem_order_start"})

    # Read poem two in any order. State 1 is the read mask; state 6 keeps the
    # current poem winner, while states 8-13 retain both days' opinions.
    events.append({"op": "label", "name": "day3_poem_order_start"})
    for character in CHARACTERS:
        hide_all_character_layers(character)
    events.extend((
        {"op": "set_state", "state": 1, "value": 0},
        {"op": "set_state", "state": 2, "value": 0},
        {"op": "jump", "target": "day3_poem_order_dispatch"},
        {"op": "label", "name": "day3_poem_order_dispatch"},
    ))
    for mask in range(16):
        events.append({"op": "if_state", "state": 1, "value": mask,
                       "target": "day3_poem_all_read" if mask == 15
                       else f"day3_poem_order_{mask}"})
    events.append({"op": "jump", "target": "day3_poem_all_read"})
    for mask in range(15):
        remaining = [(display, route, bit) for display, route, bit in recipients
                     if not mask & (1 << bit)]
        events.append({"op": "label", "name": f"day3_poem_order_{mask}"})
        events.append({"op": "choice", "state": 1, "count": len(remaining),
                       "text_en": "Who should I show my poem to first?" if mask == 0
                       else "Who should I show my poem to next?"})
        for display_name, route_name, bit in remaining:
            events.append({"op": "choice_option", "value": mask | (1 << bit),
                           "target": f"day3_poem_read_{route_name}",
                           "text_en": display_name})

    for display_name, route_name, bit in recipients:
        response_event_start = len(events)
        events.append({"op": "label", "name": f"day3_poem_read_{route_name}"})
        # Keep the old slots/labels for save compatibility, but bypass the
        # first-sharing-only introduction on every later-day recipient.
        events.append({"op": "jump", "target": f"day3_poem_response_{route_name}"})
        events.append({"op": "jump", "target": f"day3_poem_response_{route_name}"})
        events.append({"op": "label", "name": f"day3_poem_first_{route_name}"})
        for line in first_thoughts[route_name]:
            events.append({"op": "say", "speaker_en": "", "text_en": line,
                           "script": "script-poemresponses.rpyc", "line": 0})
        events.append({"op": "label", "name": f"day3_poem_response_{route_name}"})

        compile_original_response(2, route_name)
        character_code = next(key for key, value in CHARACTERS.items() if value == route_name)
        response_layers = {
            event["resource"] for event in events[response_event_start:]
            if event.get("op") == "show"
            and event.get("resource") in character_layers[character_code]
        }
        events.append({"op": "label", "name": f"day3_poem_read_cleanup_{route_name}"})
        hide_response_layers(response_layers)
        events.append({"op": "jump", "target": "day3_poem_order_dispatch"})

    events.append({"op": "label", "name": "day3_poem_all_read"})

    # The final walk-home answer follows the second poem's winner.
    if len(day3_end.block) <= 514 or kind(day3_end.block[511]) != "Menu":
        raise SystemExit("unexpected ch2_end layout; refusing to omit the final choice")
    compile_nodes(day3_end.block[:488], "script-ch2.rpyc", "ch2_end before walk-home scene")
    compile_nodes(day3_end.block[488:490], "script-ch2.rpyc", "ch2_end walk-home transition")
    for state_value, displayed_winner in ((0, "Yuri"), (1, "Natsuki"), (2, "Yuri")):
        events.append({"op": "if_state", "state": 14, "value": state_value,
                       "target": f"day3_walkup_{displayed_winner.lower()}"})
    events.append({"op": "jump", "target": "day3_walkup_yuri"})
    for displayed_winner in ("Natsuki", "Yuri"):
        events.append({"op": "label", "name": f"day3_walkup_{displayed_winner.lower()}"})
        replacements["ch2_winner"] = displayed_winner
        compile_nodes(day3_end.block[492:511], "script-ch2.rpyc",
                      f"ch2_end walk-home setup {displayed_winner}")
        events.append({"op": "jump", "target": f"day3_walkhome_menu_{displayed_winner.lower()}"})
    events.append({"op": "label", "name": "day3_walkhome_menu_natsuki"})
    events.append({"op": "choice", "state": 7, "count": 2,
                   "text_en": "How should I answer?"})
    events.append({"op": "choice_option", "value": 0, "target": "day3_walk_with_winner",
                   "text_en": local_text.menu("script-ch2.rpyc", 858, 0).replace("[ch2_winner]", "Natsuki")})
    events.append({"op": "choice_option", "value": 1, "target": "day3_walk_with_sayori",
                   "text_en": local_text.menu('script-ch2.rpyc', 858, 1)})
    events.append({"op": "label", "name": "day3_walkhome_menu_yuri"})
    events.append({"op": "choice", "state": 7, "count": 2,
                   "text_en": "How should I answer?"})
    events.append({"op": "choice_option", "value": 0, "target": "day3_walk_with_winner",
                   "text_en": local_text.menu("script-ch2.rpyc", 858, 0).replace("[ch2_winner]", "Yuri")})
    events.append({"op": "choice_option", "value": 1, "target": "day3_walk_with_sayori",
                   "text_en": local_text.menu('script-ch2.rpyc', 858, 1)})

    events.extend((
        {"op": "label", "name": "day3_walk_with_winner"},
        {"op": "if_state", "state": 14, "value": 1, "target": "day3_walk_natsuki"},
        {"op": "jump", "target": "day3_walk_yuri"},
    ))
    for route_name in ("natsuki", "yuri"):
        branch = next(node for node in day3_top
                      if kind(node) == "Label" and node.name == f"ch2_end_{route_name}")
        events.append({"op": "label", "name": f"day3_walk_{route_name}"})
        compile_fragment(branch.block, "script-ch2.rpyc", f"ch2_end_{route_name}")
        events.append({"op": "jump", "target": "day3_end_shared"})

    sayori_walk = next(node for node in day3_top
                       if kind(node) == "Label" and node.name == "ch2_end_sayori")
    events.append({"op": "label", "name": "day3_walk_with_sayori"})
    events.extend((
        {"op": "if_state", "state": 14, "value": 1,
         "target": "day3_walk_with_sayori_natsuki"},
        {"op": "jump", "target": "day3_walk_with_sayori_yuri"},
    ))
    for winner in ("Natsuki", "Yuri"):
        events.append({"op": "label", "name": f"day3_walk_with_sayori_{winner.lower()}"})
        replacements["ch2_winner"] = winner
        for node in sayori_walk.block:
            if kind(node) == "If":
                alternative = next((branch for condition, branch in node.entries
                                    if ('== "Natsuki"' in condition) == (winner == "Natsuki")), None)
                if alternative is None:
                    raise SystemExit("unexpected ch2_end_sayori winner conditional")
                compile_fragment(alternative, "script-ch2.rpyc", "ch2_end_sayori conditional",
                                 clear_hidden_characters=True)
            else:
                compile_fragment([node], "script-ch2.rpyc", "ch2_end_sayori",
                                 clear_hidden_characters=True)
        events.append({"op": "jump", "target": "day3_end_after_choice"})

    events.append({"op": "label", "name": "day3_end_shared"})
    shared_end = next(node for node in day3_top
                      if kind(node) == "Label" and node.name == "ch2_end_shared")
    compile_fragment(shared_end.block, "script-ch2.rpyc", "ch2_end_shared",
                     clear_hidden_characters=True)
    events.append({"op": "label", "name": "day3_end_after_choice"})
    compile_nodes(day3_end.block[512:514], "script-ch2.rpyc", "ch2_end after choice")
    for character in CHARACTERS:
        hide_character(character)
    events.append({"op": "jump", "target": "day3_story_end"})
    events.append({"op": "label", "name": "day3_story_end"})
    for character in CHARACTERS:
        hide_all_character_layers(character)
    # Replace the previous ending in place, preserving Day 1-3 event indices.
    events.append({"op": "jump", "target": "poem3_prepare"})
    for image, packed_names in resource_name.items():
        character = character_by_name.get(image[0]) if image else None
        if character is not None:
            character_layers[character].update(
                packed_names if isinstance(packed_names, list) else [packed_names]
            )
    from compile_day4 import extend_day4
    extend_day4(events, raw_dir, all_word_rows, store_poem_opinion, compile_nodes,
                visible_image, aliases, last_center, cg_layers, asset_by_image,
                character_layers, replacements, report_skip)
    for entry, return_label, (script, continuation) in deferred_responses:
        if any(kind(node) not in {"Say", "Return"} for node in continuation.block):
            raise ValueError("Unexpected Monika post-poem continuation structure")
        events.append({"op": "label", "name": entry})
        compile_fragment(continuation.block, script, continuation.name)
        events.append({"op": "jump", "target": return_label})
    # Clear every incoming route's body/head atoms before shared dialogue shows
    # Monika again. Keep this out of line to preserve existing save cursors and
    # the appended Monika continuation's indices. A tag's last compiled pose is
    # insufficient here: the actual route may leave a different pose on screen.
    events.append({"op": "label", "name": "day2_route_clear"})
    events.append({"op": "scene", "resource": resource_name[("bg", "club_day")]})
    events.append({"op": "jump", "target": "day2_shared_poem_intro"})
    manifest = {
        "resources": resources,
        # Preserve the local source order: seeded shuffle/save replay depends on it.
        "poem_words": [{"text": word, "points": points} for word, points in all_word_rows],
        "events": [
            {"op": "title", "resource": "__simple_title_bg",
             "history_background": "__title_aux0", "flags": 0x01},
            *events,
        ],
        "ui": {
            "texts_en": {
                **{key: key.replace("_", " ").title() for key in UI_TEXT_KEYS},
                "title": "Doki Doki Literature Club",
                "start": "Press ENTER to begin",
            },
            "credits_en": "DDLC art belongs to its original creators.",
        },
    }
    from poems import prepare_poems
    prepare_poems(manifest, raw_dir, art_dir, args.font.resolve().parent)
    from prepare_ending import prepare_ending
    prepare_ending(manifest, raw_dir, art_dir, args.font.resolve().parent)
    from prepare_ui_art import prepare_ui_art
    prepare_ui_art(manifest, raw_dir, art_dir)
    position = 0
    label_positions = {}
    for event in manifest["events"]:
        if event["op"] == "label":
            label_positions[event["name"]] = position
        else:
            position += 1
    manifest["ddlc"] = {"playable_days": 6, "minimum_choice_capacity": 128,
                         "labels": label_positions}
    # Act dispatch (compile_act2) decides where a New Game begins; Act 1
    # event indices stay untouched so older DDS saves still replay.
    manifest["start_label"] = "act_dispatch"
    from compile_act2 import GLITCH_ALPHABET
    manifest["glitch_alphabet"] = GLITCH_ALPHABET
    manifest["edited_font"] = str((raw_dir / "gui/font/VerilySerifMono.otf").resolve())
    manifest["character_files"] = [str((raw_dir / f"{name}.chr").resolve())
                                   if (raw_dir / f"{name}.chr").is_file() else None
                                   for name in ("monika", "natsuki", "sayori", "yuri")]
    build_pack(
        manifest,
        args.font.resolve(),
        args.output.resolve(),
        art_dir,
        language="en",
        discover_key_fonts=False,
    )
    # Resolved events are also a readable, source-line-linked debugging aid.
    args.output.with_suffix(".events.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    print(f"Built {len(manifest['events'])} events with {len(resources)} art resources")
    print(f"Pack: {args.output.resolve()} ({args.output.stat().st_size:,} bytes)")
    print(f"Unrecognized inline variables: {', '.join(sorted(unknown_vars)) or 'none'}")
    print(f"Skipped statement summary: {json.dumps(skipped, sort_keys=True)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
