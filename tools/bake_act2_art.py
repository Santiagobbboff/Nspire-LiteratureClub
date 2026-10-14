"""Bake the art referenced by Act 2 (ch10, ch20-ch23, second poem responses,
Yuri/Natsuki exclusives and the special poems).

Backgrounds, CG composites and character expressions go through the same
ImageResolver paths as Act 1. Act 2 additionally shows full-screen effect
layers (noise, vignette, blood, "eyes"), small positioned overlays and the
special poem sheets; those are baked here as `screen_layer`,
`fullscreen_layer` and `fullscreen_background` rows. Animated ATL effects
keep their first frame; procedural particle effects get a static stand-in.
"""
from __future__ import annotations

import random
import re
from pathlib import Path
from typing import Any

from PIL import Image

from ddlc_image_resolve import CG_PREFIXES, ImageResolver
from rpyc_ast import flatten, kind, load_rpyc

CHARACTERS = {"s": "sayori", "n": "natsuki", "y": "yuri", "m": "monika"}
CHARACTER_TAGS = set(CHARACTERS.values())
SAFE = re.compile(r"[^a-zA-Z0-9_-]+")
SCENE_SIZE = (320, 180)
SCREEN_SIZE = (320, 240)

ACT2_LABELS: dict[str, set[str] | None] = {
    "script-ch10.rpyc": {"ch10_main"},
    "script-ch20.rpyc": None,
    "script-ch21.rpyc": None,
    "script-ch22.rpyc": None,
    "script-ch23.rpyc": None,
    "script-poemresponses2.rpyc": None,
    "script-exclusives2-natsuki.rpyc": None,
    "script-exclusives2-yuri.rpyc": None,
    "poems_special.rpyc": None,
    "script-ch30.rpyc": None,
    "script-ch40.rpyc": None,
}
# Credits art shown through expressions the collector cannot follow: baked explicitly.
EXPLICIT_BACKGROUNDS = {
    **{(f"credits_cg{n}",): f"images/cg/credits/{n}.png" for n in range(1, 11)},
    ("notfound",): "images/menu/notfound.png",
    **{(f"end_glitch{n}",): f"images/bg/end-glitch{n}.jpg" for n in range(1, 5)},
}
EXPLICIT_FULLSCREEN = {
    ("credits_logo",): ("gui/logo.png", (0, 0, 0, 255), 0.6),
    ("credits_ts",): ("images/bg/splash-white.png", (0, 0, 0, 255), 0.65),
    ("poem_end",): ("images/poem_special/poem_end.png", (255, 255, 255, 255), 1.0),
    ("poem_end_clearall",): ("images/poem_special/poem_end_clearall.png", (0, 0, 0, 255), 1.0),
}
# Act 3 room: the window layers, the room and its highlight flattened into one background.
COMPOSITES = {
    ("monika_bg",): ("images/cg/monika/mask_3.png", "images/cg/monika/mask_2.png",
                     "images/cg/monika/monika_bg.png", "images/cg/monika/monika_bg_highlight.png"),
    ("monika_room",): ("images/cg/monika/mask_3.png", "images/cg/monika/mask_2.png",
                       "images/cg/monika/monika_room.png", "images/cg/monika/monika_room_highlight.png"),
}
# Response wrappers reused from Act 1 and poems shown with an `img=` pose.
EXTRA_SPRITES = {("yuri", "2s"), ("natsuki", "1c"), ("yuri", "1a"), ("monika", "1a"), ("yuri_half2",)}
# Overlay tags that are not pictures: bare character tags (repositions) and
# the runtime text overlay used for `splash_warning`.
SKIP_OVERLAYS = {("monika",), ("natsuki",), ("yuri",), ("sayori",), ("yuri_half2",),
                 ("mask_2",), ("mask_3",), ("room_mask",), ("room_mask2",), ("m_rectstatic",),
                 ("m_rectstatic2",), ("m_rectstatic3",), ("glitch_color",), ("glitch_color2",)}
EXTRA_OVERLAYS = {("yuri", "eyes"), ("bg", "eyes")}

# Semi-transparent solids: (r, g, b, a). Ren'Py's own definitions carry the
# alpha for dark/darkred; the two faint-effect Solid() flashes are shown on a
# layer above the stage with the "dizzy" transform, approximated as tints.
SOLID_LAYERS = {
    ("dark",): (0, 0, 0, 0xE4),
    ("darkred",): (0x11, 0, 0, 0xC8),
    ('Solid("ff0000")',): (255, 0, 0, 70),
    ('Solid("#440000")',): (0x44, 0, 0, 90),
}
# Full-screen fills (`show black` / `show white` as layers, not scenes).
FILL_LAYERS = {("black",): (0, 0, 0), ("white",): (255, 255, 255)}
# Small displayables with an explicit ATL placement on the 1280x720 stage:
# (anchor_x, anchor_y, x, y, zoom) with Ren'Py align semantics (x/y <= 1 are fractions).
PLACED = {
    ("y_glitch_head",): (0.0, 0.0, 0, 0, 1.0),
    ("n_moving_mouth",): (0.5, 0.5, 615, 305, 1.0),
    ("natsuki_ghost_blood",): (0.0, 0.0, 620, 320, 0.8),
    ("noface2",): (0.95, 0.47, 0.95, 0.47, 1.0),
}
# Full-screen pictures that replace the stage (shown with `scene` by the compiler).
FULLSCREEN_TAGS = ("poem_special", "bsod", "paper_glitch", "end", "intro", "splash-glitch2")
N_RECTS = {
    ("n_rects_ghost1",): ((580, 270), (20, 25)),
    ("n_rects_ghost2",): ((652, 264), (20, 25)),
    ("n_rects_ghost3",): ((616, 310), (25, 15)),
    ("n_rects_ghost4",): ((735, 310), (25, 20)),
    ("n_rects_ghost5",): ((740, 376), (25, 20)),
}


def image_tuple(node: Any) -> tuple[str, ...] | None:
    spec = getattr(node, "imspec", None)
    if not spec or not spec[0]:
        return None
    return tuple(str(part) for part in spec[0])


def procedural_blood(size: tuple[int, int], seed: int, region: tuple[float, float, float, float],
                     drops: int) -> Image.Image:
    """Transparent canvas with dark red droplets inside `region` (fractions)."""
    rng = random.Random(seed)
    canvas = Image.new("RGBA", size, (0, 0, 0, 0))
    from PIL import ImageDraw
    draw = ImageDraw.Draw(canvas)
    x0, y0, x1, y1 = region
    for _ in range(drops):
        cx = rng.uniform(x0, x1) * size[0]
        cy = rng.uniform(y0, y1) * size[1]
        r = rng.uniform(1.5, 6)
        shade = rng.randint(90, 170)
        draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(shade, 0, 0, 230))
        if rng.random() < 0.5:
            draw.line([cx, cy, cx + rng.uniform(-1, 1), cy + rng.uniform(4, 16)],
                      fill=(shade, 0, 0, 200), width=2)
    return canvas


def bake_act2(raw_dir: Path, resolver: ImageResolver, output_dir: Path,
              resources: list[dict[str, Any]], unresolved: list[dict[str, Any]]) -> dict:
    scenes: set[tuple[str, ...]] = set()
    sprites: set[tuple[str, ...]] = set(EXTRA_SPRITES)
    overlays: set[tuple[str, ...]] = set(EXTRA_OVERLAYS)
    for script_name, names in ACT2_LABELS.items():
        _metadata, nodes = load_rpyc(raw_dir / script_name)
        labels = [node for node in flatten(nodes) if kind(node) == "Label"
                  and (names is None or node.name in names)]
        for label in labels:
            cg_layers: list[str] = []
            for node in flatten(label.block):
                node_kind = kind(node)
                if node_kind in ("Scene", "Show"):
                    name = image_tuple(node)
                    if not name:
                        continue
                    if name[0].startswith(CG_PREFIXES):
                        if node_kind == "Scene":
                            cg_layers = [name[0]]
                        elif name[0] not in cg_layers:
                            cg_layers.append(name[0])
                        scenes.add(tuple(cg_layers))
                        continue
                    if node_kind == "Scene":
                        cg_layers = []
                    if name[0] == "bg" or (node_kind == "Scene" and name in (("black",), ("white",))):
                        scenes.add(name)
                    elif node_kind == "Scene":
                        scenes.add(name)
                    elif name[0] in CHARACTER_TAGS and len(name) > 1:
                        sprites.add(name)
                    else:
                        overlays.add(name)
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
                elif node_kind == "Call" and getattr(node, "label", "") == "showpoem":
                    for key, value in getattr(node.arguments, "arguments", []) or []:
                        if key == "img":
                            sprites.add(tuple(str(value).strip('"').split()))

    existing = {tuple(row["image"]) for row in resources}
    img_dir = output_dir / "img"
    img_dir.mkdir(parents=True, exist_ok=True)

    def add_scene(image_name: tuple[str, ...]) -> None:
        if image_name in existing:
            return
        ident = None
        choice_defn = resolver.table.get(image_name)
        if choice_defn is not None and choice_defn.kind == "choice":
            # A weighted ATL choice (bg club_day2's poster swap): take the first branch.
            choice_ids = resolver.choice_variants(image_name)
            ident = choice_ids[0] if choice_ids else None
        else:
            ident = resolver.scene_id(image_name)
        if ident is None:
            unresolved.append({"kind": "background", "image": list(image_name)})
            return
        entry = resolver.scenes[ident]
        resources.append({"kind": "background", "image": list(image_name), "id": ident,
                          "file": entry["file"], "width": entry["w"], "height": entry["h"],
                          "introduced_act": 2})
        existing.add(image_name)

    for image_name in sorted(scenes):
        if image_name == ("bg", "eyes"):
            overlays.add(image_name)
        elif image_name in (("black",), ("white",)) or image_name[0] == "bg" \
                or image_name[0].startswith(CG_PREFIXES):
            add_scene(image_name)
        else:
            overlays.add(image_name)

    def render(image_name: tuple[str, ...]) -> Image.Image | None:
        defn = resolver.table.get(image_name)
        if defn is None or defn.kind == "unsupported":
            return None
        try:
            return resolver._render_def(defn)
        except Exception:  # noqa: BLE001 - unresolvable art is reported, not fatal
            return None

    # Character expressions; stage-sized "sprites" (yuri eyes) are overlays.
    for image_name in sorted(sprites):
        if image_name in existing:
            continue
        art = render(image_name)
        if art is not None and art.width >= 1200:
            overlays.add(image_name)
            continue
        base_id, overlay_id = resolver.sprite_layers(image_name)
        if base_id is None:
            unresolved.append({"kind": "sprite", "image": list(image_name)})
            continue
        layer_ids = [base_id] + ([overlay_id] if overlay_id is not None else [])
        layers = []
        for layer_index, ident in enumerate(layer_ids):
            entry = resolver.sprites[ident]
            layers.append({"name": f"sprite_atom_{ident:03d}", "id": ident,
                           "role": "base" if layer_index == 0 else "expression",
                           "file": entry["file"], "width": entry["w"], "height": entry["h"],
                           "x_offset": entry["dx"], "y_offset": entry["dy"]})
        resources.append({"kind": "prescaled_sprite", "image": list(image_name),
                          "id": len([row for row in resources if row["kind"] == "prescaled_sprite"]),
                          "layers": layers, "introduced_act": 2})
        existing.add(image_name)

    counters = {"screen_layer": 0, "fullscreen_layer": 0, "fullscreen_background": 0}
    variants: dict[str, list[dict[str, Any]]] = {}

    def add_flat(image_name: tuple[str, ...], image: Image.Image, row_kind: str,
                 screen_y: int = 20, extra: dict | None = None) -> None:
        ident = counters[row_kind]
        counters[row_kind] += 1
        prefix = {"screen_layer": "ovl", "fullscreen_layer": "fsl", "fullscreen_background": "fs"}[row_kind]
        filename = f"{prefix}_{ident:03d}_{SAFE.sub('_', '_'.join(image_name))[:40]}.png"
        image.save(img_dir / filename)
        row = {"kind": row_kind, "image": list(image_name), "id": ident, "file": filename,
               "width": image.width, "height": image.height, "screen_x": 0,
               "screen_y": screen_y, "introduced_act": 2}
        if extra:
            row.update(extra)
        resources.append(row)
        existing.add(image_name)

    def place(art: Image.Image, spec: tuple) -> Image.Image:
        anchor_x, anchor_y, x, y, zoom = spec
        if zoom != 1.0:
            art = art.resize((max(1, round(art.width * zoom)), max(1, round(art.height * zoom))),
                             Image.Resampling.LANCZOS)
        px = x * 1280 if isinstance(x, float) and x <= 1 else x
        py = y * 720 if isinstance(y, float) and y <= 1 else y
        canvas = Image.new("RGBA", (1280, 720), (0, 0, 0, 0))
        canvas.alpha_composite(art.convert("RGBA"),
                               (round(px - anchor_x * art.width), round(py - anchor_y * art.height)))
        return canvas

    for image_name, rel_path in EXPLICIT_BACKGROUNDS.items():
        if image_name in existing:
            continue
        source = Path(raw_dir / rel_path)
        if not source.is_file():
            source = next(Path(raw_dir).glob(rel_path.rsplit(".", 1)[0] + ".*"), None)
        if source is None:
            unresolved.append({"kind": "background", "image": list(image_name)})
            continue
        art = Image.open(source).convert("RGB")
        if art.height > art.width * 9 // 16 + 2:  # the 1280x1280 glitch tiles: keep the top band
            art = art.crop((0, 0, art.width, art.width * 9 // 16))
        flat = art.resize(SCENE_SIZE, Image.Resampling.LANCZOS)
        ident = counters.setdefault("background", 1000)
        counters["background"] += 1
        filename = f"bg_{ident:03d}_{SAFE.sub('_', '_'.join(image_name))}.png"
        flat.save(img_dir / filename)
        resources.append({"kind": "background", "image": list(image_name), "id": ident,
                          "file": filename, "width": flat.width, "height": flat.height,
                          "introduced_act": 4})
        existing.add(image_name)
    for image_name, (rel_path, backdrop, zoom) in EXPLICIT_FULLSCREEN.items():
        if image_name in existing:
            continue
        art = Image.open(raw_dir / rel_path).convert("RGBA")
        canvas = Image.new("RGBA", SCREEN_SIZE, backdrop)
        ratio = min(SCREEN_SIZE[0] / art.width, SCREEN_SIZE[1] / art.height) * zoom
        size = (max(1, round(art.width * ratio)), max(1, round(art.height * ratio)))
        canvas.alpha_composite(art.resize(size, Image.Resampling.LANCZOS),
                               ((SCREEN_SIZE[0] - size[0]) // 2, (SCREEN_SIZE[1] - size[1]) // 2))
        add_flat(image_name, canvas.convert("RGB"), "fullscreen_background", 0)
    for image_name in sorted(overlays):
        if image_name in FILL_LAYERS:
            if (*image_name, "layer") not in existing:
                add_flat((*image_name, "layer"), Image.new("RGBA", (1, 1), (*FILL_LAYERS[image_name], 255)),
                         "fullscreen_layer")
            continue
        if image_name in existing or image_name in SKIP_OVERLAYS or image_name[0] == "splash_warning":
            continue
        if image_name in SOLID_LAYERS:
            r, g, b, a = SOLID_LAYERS[image_name]
            add_flat(image_name, Image.new("RGBA", (1, 1), (r, g, b, 255)), "fullscreen_layer",
                     extra={"opacity": a})
            continue
        if image_name in (("blood",), ("blood_eye",), ("blood_eye2",)):
            region, drops = {("blood",): ((0.25, 0.25, 0.75, 0.95), 60),
                             ("blood_eye",): ((0.42, 0.18, 0.5, 0.45), 14),
                             ("blood_eye2",): ((0.5, 0.18, 0.58, 0.45), 14)}[image_name]
            add_flat(image_name, procedural_blood(SCENE_SIZE, sum(map(ord, "".join(image_name))), region, drops),
                     "screen_layer")
            continue
        if image_name in N_RECTS:
            (px, py), (pw, ph) = N_RECTS[image_name]
            canvas = Image.new("RGBA", SCENE_SIZE, (0, 0, 0, 0))
            from PIL import ImageDraw
            scale = SCENE_SIZE[0] / 1280
            ImageDraw.Draw(canvas).rectangle(
                [round(px * scale), round(py * scale), round((px + pw) * scale), round((py + ph) * scale)],
                fill=(0, 0, 0, 255))
            add_flat(image_name, canvas, "screen_layer")
            continue
        if image_name in COMPOSITES:
            canvas = Image.new("RGBA", (1280, 720), (0, 0, 0, 255))
            for layer in COMPOSITES[image_name]:
                canvas.alpha_composite(Image.open(raw_dir / layer).convert("RGBA"))
            flat = canvas.convert("RGB").resize(SCENE_SIZE, Image.Resampling.LANCZOS)
            ident = counters.setdefault("background", 1000)
            counters["background"] += 1
            filename = f"bg_{ident:03d}_{SAFE.sub('_', '_'.join(image_name))}.png"
            flat.save(img_dir / filename)
            resources.append({"kind": "background", "image": list(image_name), "id": ident,
                              "file": filename, "width": flat.width, "height": flat.height,
                              "introduced_act": 3})
            existing.add(image_name)
            continue
        defn = resolver.table.get(image_name)
        if defn is not None and defn.kind == "condswitch":
            rows = []
            # Keep one picture per decomposition stage (1a/2a/3a) to save pack space.
            kept = {"1a", "2a", "3a"}
            kept_variants = [(c, p) for c, p in defn.variants if Path(p).stem in kept]
            for condition, path in kept_variants:
                variant_name = (image_name[0], Path(path).stem)
                if variant_name not in existing:
                    synth = (f"{image_name[0]}$cond${path}",)
                    resolver.table.setdefault(synth, type(defn)(imgname=synth, kind="path", path=path))
                    ident = resolver.scene_id(synth)
                    if ident is None:
                        continue
                    entry = resolver.scenes[ident]
                    resources.append({"kind": "background", "image": list(variant_name), "id": ident,
                                      "file": entry["file"], "width": entry["w"], "height": entry["h"],
                                      "introduced_act": 2})
                    existing.add(variant_name)
                rows.append({"condition": None if condition.strip() == "True" else condition,
                             "image": list(variant_name)})
            variants[" ".join(image_name)] = rows
            continue
        if image_name == ("intro",):
            canvas = Image.new("RGBA", (1280, 720), (255, 255, 255, 255))
            splash = raw_dir / "images/bg/splash.png"
            if splash.is_file():
                art = Image.open(splash).convert("RGBA")
                canvas.alpha_composite(art, ((1280 - art.width) // 2, (720 - art.height) // 2))
            add_flat(image_name, canvas.resize(SCREEN_SIZE, Image.Resampling.LANCZOS).convert("RGB"),
                     "fullscreen_background", 0)
            continue
        art = render(image_name)
        if art is None:
            unresolved.append({"kind": "overlay", "image": list(image_name)})
            continue
        if image_name in PLACED:
            art = place(art, PLACED[image_name])
        elif image_name == ("noface1",):
            tiled = Image.new("RGBA", (1280, 720), (0, 0, 0, 0))
            for ty in range(0, 720, art.height):
                for tx in range(0, 1280, art.width):
                    tiled.alpha_composite(art, (tx, ty))
            art = tiled
        if image_name[0].startswith(FULLSCREEN_TAGS) or image_name == ("bg", "eyes"):
            backdrop = (255, 255, 255, 255) if image_name[0].startswith("poem_special") else (0, 0, 0, 255)
            canvas = Image.new("RGBA", SCREEN_SIZE, backdrop)
            ratio = min(SCREEN_SIZE[0] / art.width, SCREEN_SIZE[1] / art.height)
            size = (max(1, round(art.width * ratio)), max(1, round(art.height * ratio)))
            canvas.alpha_composite(art.resize(size, Image.Resampling.LANCZOS),
                                   ((SCREEN_SIZE[0] - size[0]) // 2, (SCREEN_SIZE[1] - size[1]) // 2))
            add_flat(image_name, canvas.convert("RGB"), "fullscreen_background", 0)
            continue
        if art.size != (1280, 720):
            canvas = Image.new("RGBA", (1280, 720), (0, 0, 0, 0))
            canvas.alpha_composite(art.convert("RGBA"), ((1280 - art.width) // 2, (720 - art.height) // 2))
            art = canvas
        layer = art.resize(SCENE_SIZE, Image.Resampling.LANCZOS).convert("RGBA")
        if image_name == ("noise",):
            layer.putalpha(90)
        add_flat(image_name, layer, "screen_layer")
    return variants
