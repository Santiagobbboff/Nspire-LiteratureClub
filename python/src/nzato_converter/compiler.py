"""DDLC pack/image/font encoder derived from zyf722's GPLv3 nZATO converter.
The unused ZATO script frontend and its game-specific configuration were removed.
"""
from __future__ import annotations
import difflib
import os
import re
import shutil
import struct
from pathlib import Path
import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFont


TAG = re.compile(r"\{[^{}]*\}")

PROSE_FONT_SIZE = 12

UI_TEXT_KEYS = (
    "start",
    "log",
    "save",
    "load",
    "gallery",
    "title",
    "credits",
    "controls",
    "quit",
    "return",
    "yes",
    "no",
    "no_signal",
    "page_pattern",
    "auto_saves",
    "quick_saves",
    "confirm_quit",
    "confirm_main_menu",
    "confirm_overwrite",
    "confirm_load",
    "confirm_quick_save",
)

_ASSET_INDEX: dict[Path, tuple[dict[str, list[Path]], list[Path]]] = {}

_IMAGE_RECORD_CACHE: dict[
    tuple[int, Path],
    tuple[list[bytes], dict[str, tuple[int, int]], dict[str, str], dict],
] = {}

def resize_rgba(
    src: Image.Image, size: tuple[int, int], resample=Image.Resampling.LANCZOS
) -> Image.Image:
    r, g, b, a = src.split()
    premul = Image.merge(
        "RGBA",
        (
            ImageChops.multiply(r, a),
            ImageChops.multiply(g, a),
            ImageChops.multiply(b, a),
            a,
        ),
    ).resize(size, resample)
    pixels = []
    for pr, pg, pb, alpha in premul.get_flattened_data():
        pixels.append(
            (
                min(255, (pr * 255 + alpha // 2) // alpha),
                min(255, (pg * 255 + alpha // 2) // alpha),
                min(255, (pb * 255 + alpha // 2) // alpha),
                alpha,
            )
            if alpha
            else (0, 0, 0, 0)
        )
    out = Image.new("RGBA", size)
    out.putdata(pixels)
    return out

def find_asset(root: Path, relative: str) -> Path:
    relative = relative.replace("side as whoa", "side as woah")
    requested = Path(relative)
    if requested.is_absolute() and requested.exists():
        return requested
    for direct in (
        root / requested,
        root / "images" / requested,
        root / "gui" / requested,
    ):
        if direct.exists():
            return direct
    key = root.resolve()
    if key not in _ASSET_INDEX:
        files = [
            path
            for path in root.rglob("*")
            if path.is_file() and path.suffix.lower() in (".png", ".jpg", ".jpeg")
        ]
        by_stem: dict[str, list[Path]] = {}
        for path in files:
            by_stem.setdefault(path.stem.casefold(), []).append(path)
        _ASSET_INDEX[key] = by_stem, files
    by_stem, _ = _ASSET_INDEX[key]
    stem = Path(relative).stem.casefold()
    matches = by_stem.get(stem, [])
    if not matches:
        close = difflib.get_close_matches(stem, by_stem, n=1, cutoff=0.94)
        if close:
            return by_stem[close[0]][0]
        raise FileNotFoundError(f"asset not found: {relative}")
    return matches[0]

IMAGE_CORE_FORMAT = "<HHHHBBIHH"

IMAGE_COMPOSITE_FORMAT = "<HhhHH"

def prepare_sprite_rgba(path: Path) -> Image.Image:
    src = Image.open(path).convert("RGBA")
    # Ren'Py positions the complete 450x720 sprite canvas, not its alpha bbox.
    # Preserve that transparent margin through scaling; image_record() crops it
    # afterwards and folds the offset into the packed event coordinates.
    target_height = 226
    scale = target_height / src.height
    scaled = resize_rgba(src, (max(1, round(src.width * scale)), target_height))
    # Extend the normally covered lower rows to the physical bottom of the
    # 240-pixel display. Layered and monolithic sprites use identical geometry.
    lower_start = 180
    lower = scaled.crop((0, lower_start, scaled.width, scaled.height)).resize(
        (scaled.width, scaled.height - lower_start + 15), Image.Resampling.BILINEAR
    )
    result = Image.new("RGBA", (scaled.width, target_height + 15))
    result.paste(scaled.crop((0, 0, scaled.width, lower_start)), (0, 0))
    result.paste(lower, (0, lower_start))
    return result

def encode_rgba_record(
    ident: int,
    rgba: Image.Image,
    flags: int,
    animation_next: int = 0,
    animation_ms: int = 0,
    coverage_override: bytes | None = None,
) -> bytes:
    quantized = rgba.convert("RGB").quantize(
        colors=256, method=Image.Quantize.MEDIANCUT
    )
    indices = bytes(quantized.get_flattened_data())
    raw = quantized.getpalette()[:768]
    colors = [tuple(raw[i : i + 3]) for i in range(0, len(raw), 3)]
    palette = b"".join(
        struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
        for r, g, b in colors
    )
    count = len(colors) if len(colors) < 256 else 0
    coverage = (
        coverage_override
        if coverage_override is not None
        else rgba.getchannel("A").tobytes()
        if flags & 2
        else b""
    )
    return (
        struct.pack(
            IMAGE_CORE_FORMAT,
            ident,
            rgba.width,
            rgba.height,
            count,
            flags,
            0,
            len(indices),
            animation_next,
            min(65535, animation_ms),
        )
        + palette
        + indices
        + coverage
    )

def complete_image_record(
    record: bytes,
    composite_base: int = 0,
    patch_x: int = 0,
    patch_y: int = 0,
    logical_size: tuple[int, int] | None = None,
) -> bytes:
    head_size = struct.calcsize(IMAGE_CORE_FORMAT)
    head = list(struct.unpack(IMAGE_CORE_FORMAT, record[:head_size]))
    patch_width, patch_height = head[1], head[2]
    if logical_size:
        head[1], head[2] = logical_size
    return (
        struct.pack(IMAGE_CORE_FORMAT, *head)
        + struct.pack(
            IMAGE_COMPOSITE_FORMAT,
            composite_base,
            patch_x,
            patch_y,
            patch_width,
            patch_height,
        )
        + record[head_size:]
    )

def alias_image_record(
    ident: int, target: int, size: tuple[int, int] = (320, 180)
) -> bytes:
    """Create a virtual image entry whose pixels live in another NZP entry."""
    return struct.pack(
        IMAGE_CORE_FORMAT,
        ident,
        size[0],
        size[1],
        0,
        0x80,
        0,
        0,
        0,
        0,
    ) + struct.pack(IMAGE_COMPOSITE_FORMAT, target, 0, 0, 0, 0)

def crop_alpha(rgba: Image.Image) -> tuple[Image.Image, int, int]:
    box = rgba.getchannel("A").getbbox()
    if not box:
        return Image.new("RGBA", (1, 1)), 0, 0
    return rgba.crop(box), box[0], box[1]

def plan_layered_sprites(
    resources: list[dict],
    paths: dict[str, Path],
    kinds: dict[str, str],
    first_base_id: int,
) -> tuple[
    dict[str, tuple[Image.Image, int, int, int, tuple[int, int]]],
    list[tuple[int, Image.Image]],
    dict,
]:
    groups: dict[
        tuple[str, tuple[int, int], tuple[int, int, int, int] | None],
        list[tuple[str, Image.Image]],
    ] = {}
    for resource in resources:
        name = resource["name"]
        if kinds[name] != "sprite":
            continue
        rgba = prepare_sprite_rgba(paths[name])
        # A directory can contain genuinely different poses (for example
        # Garin's regular uniform, panic pose and monochrome CG). Do not let
        # those outliers destroy the stable-body mask for normal expressions.
        key = (
            paths[name].parent.as_posix().casefold(),
            rgba.size,
            rgba.getchannel("A").getbbox(),
        )
        groups.setdefault(key, []).append((name, rgba))

    variants: dict[str, tuple[Image.Image, int, int, int, tuple[int, int]]] = {}
    bases: list[tuple[int, Image.Image]] = []
    stats = {"groups": 0, "variants": 0, "source_bytes": 0, "layered_bytes": 0}
    next_base_id = first_base_id
    for items in groups.values():
        if len(items) < 2:
            continue
        arrays = np.stack([np.asarray(rgba, dtype=np.uint8) for _, rgba in items])
        stable = np.all(arrays == arrays[0], axis=(0, 3))
        visible_union = np.any(arrays[:, :, :, 3] != 0, axis=0)
        stable_visible = stable & (arrays[0, :, :, 3] != 0)
        reuse = np.count_nonzero(stable_visible) / max(
            1, np.count_nonzero(visible_union)
        )
        if reuse < 0.75:
            continue

        common_array = arrays[0].copy()
        common_array[~stable] = 0
        common = Image.fromarray(common_array, "RGBA")
        candidate_variants = []
        source_bytes = 0
        layered_bytes = len(encode_rgba_record(next_base_id, common, 2))
        changed = ~stable
        if not np.any(changed):
            continue
        for name, rgba in items:
            cropped_source, _, _ = crop_alpha(rgba)
            source_bytes += len(encode_rgba_record(1, cropped_source, 2))
            # The rectangle contains the complete variant pixels, including
            # unchanged and fully transparent ones. This makes plain overwrite
            # reconstruction unambiguous: transparent means "clear", not
            # "leave the shared base untouched".
            rgba_array = np.asarray(rgba, dtype=np.uint8)
            write_mask = changed & (rgba_array[:, :, 3] != 0)
            patch_y_values, patch_x_values = np.nonzero(write_mask)
            if len(patch_x_values):
                patch_box = (
                    int(patch_x_values.min()),
                    int(patch_y_values.min()),
                    int(patch_x_values.max()) + 1,
                    int(patch_y_values.max()) + 1,
                )
                overlay = rgba.crop(patch_box)
                patch_x, patch_y = patch_box[:2]
            else:
                overlay, patch_x, patch_y = Image.new("RGBA", (1, 1)), 0, 0
            layered_bytes += len(encode_rgba_record(1, overlay, 2))
            candidate_variants.append((name, overlay, patch_x, patch_y, rgba.size))
        # Metadata and direct-RGB reconstruction are only worthwhile when the
        # stored sprite payload falls by a meaningful margin.
        if layered_bytes * 10 >= source_bytes * 9:
            continue

        base_id = next_base_id
        next_base_id += 1
        bases.append((base_id, common))
        for name, overlay, patch_x, patch_y, logical_size in candidate_variants:
            variants[name] = (overlay, patch_x, patch_y, base_id, logical_size)
        stats["groups"] += 1
        stats["variants"] += len(items)
        stats["source_bytes"] += source_bytes
        stats["layered_bytes"] += layered_bytes
    return variants, bases, stats

def image_record(
    ident: int,
    path: Path,
    kind: str,
    base_layer: bool = False,
    animation_next: int = 0,
    animation_ms: int = 0,
    visible_positions: set[str] | None = None,
) -> tuple[bytes, int, int]:
    src = Image.open(path).convert("RGBA")
    crop_x = crop_y = 0
    if kind in ("poem_paper", "ddlc_ui_image"):
        rgba = src
        coverage, flags = b"", 0
    elif kind in ("background", "fullscreen_background"):
        target = (320, 180) if kind == "background" else (320, 240)
        rgba = resize_rgba(src, target, Image.Resampling.BOX)
        flat = Image.new("RGB", rgba.size)
        flat.paste(rgba.convert("RGB"), mask=rgba.getchannel("A"))
        rgba = flat.convert("RGBA")
        coverage, flags = b"", 8
    elif kind == "prescaled_sprite":
        # DDLC's image resolver can bake an expression at its true Ren'Py
        # scale (0.80 * 0.25) and crop it to its visible pixels. Keep that
        # 320x240-stage-sized result intact here; passing it through
        # prepare_sprite_rgba() would scale it a second time and lose the
        # resolver's carefully measured canvas offsets.
        rgba = src
        box = rgba.getchannel("A").getbbox()
        if box:
            crop_x, crop_y = box[:2]
            rgba = rgba.crop(box)
        if rgba.width > 255 or rgba.height > 255:
            raise ValueError(
                f"prescaled sprite {path} is {rgba.width}x{rgba.height}; "
                "the Nspire sprite record limit is 255x255"
            )
        coverage, flags = rgba.getchannel("A").tobytes(), 2
    elif kind in ("screen_layer", "fullscreen_layer"):
        # Ren'Py frequently uses 1280x720 `show` images as transparent or
        # blend-mode stage layers.  Treating them as sprites changes their
        # scale/position; flattening them as backgrounds destroys their alpha.
        alpha_box = src.getchannel("A").getbbox()
        thin = bool(
            alpha_box
            and (alpha_box[2] - alpha_box[0] < 48 or alpha_box[3] - alpha_box[1] < 48)
        )
        horror_line_art = path.stem.startswith(
            ("hands", "ghost", "tface", "def", "crack")
        )
        if kind == "fullscreen_layer":
            # Solid black/white masks cover the physical 320x240 display. A
            # single source pixel is sufficient because the renderer expands
            # full-screen layers with fill_rect_alpha().
            rgba = resize_rgba(src, (1, 1), Image.Resampling.BOX)
        elif horror_line_art and src.size == (1280, 720):
            # These animation frames contain one-pixel red/black line art.
            # A 4:1 BOX average erases most of it before RGB565 quantization,
            # which made the rapid eye frames look like a static, zoomed
            # carfix_red background. Max-pooling preserves the strongest line
            # sample in each calculator pixel without adding runtime work.
            source = np.asarray(src, dtype=np.uint8)
            blocks = source.reshape(180, 4, 320, 4, 4).transpose(0, 2, 1, 3, 4)
            if path.stem.startswith("hands"):
                # The eye frames are opaque black with thin red strokes.
                # Channel maxima retain those strokes within each 4x4 cell.
                reduced = blocks.max(axis=(2, 3))
            else:
                # Transparent PNGs commonly keep white RGB in zero-alpha
                # pixels. Independent channel maxima would turn red lettering
                # white, so select the most opaque sample as one RGBA tuple.
                flat = blocks.reshape(180, 320, 16, 4)
                scores = flat[..., 3].astype(np.uint16) * 256 + flat[..., 0]
                selected = scores.argmax(axis=2)
                reduced = np.take_along_axis(flat, selected[..., None, None], axis=2)[
                    ..., 0, :
                ]
            rgba = Image.fromarray(reduced.astype(np.uint8), "RGBA")
        else:
            # The animated hand-drawn thank-you card is subsequently shown at
            # 0.75x. Preserve its pencil edges before that runtime transform.
            resampling = (
                Image.Resampling.LANCZOS
                if thin or path.stem.startswith("bye")
                else Image.Resampling.BOX
            )
            rgba = resize_rgba(src, (320, 180), resampling)
        # tface is made from one-pixel red lettering on a 1280-wide canvas.
        # A mathematically correct 4:1 reduction leaves its strongest target
        # pixels below 40% opacity, effectively invisible on the calculator's
        # LCD. Preserve the intended legibility after downsampling.
        if path.stem.startswith("tface"):
            rgba.putalpha(rgba.getchannel("A").point(lambda value: min(255, value * 3)))
        rgba, crop_x, crop_y = crop_alpha(rgba)
        coverage, flags = rgba.getchannel("A").tobytes(), 2 | 32
        if kind == "fullscreen_layer":
            flags |= 64
    elif kind == "overlay":
        # The source logo is screen-blended over an opaque black 1280x720
        # canvas. Crop its lit pixels first, then preserve them as alpha.
        luminance = src.convert("RGB").convert("L")
        box = luminance.point(lambda value: 255 if value > 6 else 0).getbbox()
        if box:
            src = src.crop(box)
        scale = min(150 / src.width, 58 / src.height)
        rgba = resize_rgba(
            src,
            (round(src.width * scale), round(src.height * scale)),
            Image.Resampling.BOX,
        )
        # The original title logo is stored on an opaque black canvas and is
        # screen-blended by Ren'Py.  Luminance-key it for the small renderer.
        coverage = (
            rgba.convert("RGB")
            .convert("L")
            .point(lambda value: min(255, round(255 * (value / 255) ** 0.65)))
            .tobytes()
        )
        flags = 2
    elif kind == "ui_overlay":
        rgba = resize_rgba(src, (320, 180), Image.Resampling.BOX)
        coverage, flags = rgba.getchannel("A").tobytes(), 2
    elif kind == "gallery_thumb":
        # The source screen uses 300x169 thumbnails. Pre-filter once during
        # packing so the calculator only performs a native-size blit.
        rgba = resize_rgba(src, (65, 37), Image.Resampling.LANCZOS)
        coverage, flags = rgba.getchannel("A").tobytes(), 2
    elif kind == "particle":
        rgba = resize_rgba(src, (14, 14), Image.Resampling.LANCZOS)
        coverage, flags = rgba.getchannel("A").tobytes(), 2 | 4
    else:
        if kind == "face":
            box = src.getchannel("A").getbbox()
            if box:
                src = src.crop(box)
            size = (68, 68)
            rgba = resize_rgba(src, size)
        else:
            rgba = prepare_sprite_rgba(path)
            size = rgba.size
        box = rgba.getchannel("A").getbbox()
        if box:
            crop_x, crop_y = box[:2]
            rgba = rgba.crop(box)
        # A wide sprite can still contain opaque columns which are outside the
        # 320-pixel viewport at every place where the script uses that image.
        # Remove those columns from the pack and fold the trim into the event
        # coordinates.  Taking the union over all scripted positions also
        # preserves left/right reuse and the complete path of monotonic moves.
        if kind == "sprite" and visible_positions:
            visible_left, visible_top = rgba.width, rgba.height
            visible_right = visible_bottom = 0
            for position in visible_positions:
                base_x = (
                    0
                    if position == "left"
                    else 320 - size[0]
                    if position == "right"
                    else (320 - size[0]) // 2
                )
                placed_x, placed_y = base_x + crop_x, -1 + crop_y
                visible_left = min(visible_left, max(0, -placed_x))
                visible_top = min(visible_top, max(0, -placed_y))
                visible_right = max(visible_right, min(rgba.width, 320 - placed_x))
                visible_bottom = max(visible_bottom, min(rgba.height, 240 - placed_y))
            if visible_left < visible_right and visible_top < visible_bottom:
                rgba = rgba.crop(
                    (visible_left, visible_top, visible_right, visible_bottom)
                )
                crop_x += visible_left
                crop_y += visible_top
        coverage, flags = rgba.getchannel("A").tobytes(), 2
    if base_layer:
        flags |= 16
    return (
        encode_rgba_record(
            ident, rgba, flags, animation_next, animation_ms, coverage_override=coverage
        ),
        crop_x,
        crop_y,
    )

EDITED_BANK = 0xEA00


def edited_bank_text(text: str) -> str:
    """Map Act 2 'edited' dialogue into the VerilySerifMono glyph bank.
    Text inside {tags} (including {player}/{glitch=N}) stays in the prose font."""
    out = []
    depth = 0
    for char in text:
        if char == '{':
            depth += 1
        if depth or char in ' \n\r\t' or ord(char) >= 0x200:
            out.append(char)  # ASCII spaces keep the prose word-wrap working
        else:
            out.append(chr(EDITED_BANK + ord(char)))
        if char == '}' and depth:
            depth -= 1
    return ''.join(out)


def bank_glyph_records(font_path, base, size, chars, gamma=0.64):
    """Rasterize `chars` from `font_path` at `size` into the private bank `base`."""
    records = []
    font = ImageFont.truetype(str(font_path), size)
    ascent, descent = font.getmetrics()
    for char in sorted(set(chars) - set('\n\r\t')):
        box = font.getbbox(char, anchor='ls')
        left = max(0, -box[0])
        advance = max(1, min(30, round(font.getlength(char))))
        width, height = max(1, min(30, max(advance, box[2] + left))), min(24, ascent + descent)
        canvas = Image.new('L', (width, height))
        ImageDraw.Draw(canvas).text((left, ascent), char, font=font, fill=255, anchor='ls')
        canvas = canvas.point([round(255 * (v / 255) ** gamma) for v in range(256)])
        pixels = canvas.tobytes()
        records.append(struct.pack('<IBBBBH', (base + ord(char)) | (size << 24),
                                   width, height, advance, (-left) & 0xFF, len(pixels)) + pixels)
    return records


def poem_glyph_records(fonts):
    """Package only the used poem glyphs, without shipping full font files."""
    records = []
    for spec in fonts:
        size, base = spec['size'], spec['base']
        font = ImageFont.truetype(spec['path'], size)
        ascent, descent = font.getmetrics()
        for char in sorted(set(''.join(spec['texts'])) - set('\n\r\t')):
            if ord(char) >= 0x200:
                raise ValueError(f'Poem character outside reserved font bank: {char!r}')
            box = font.getbbox(char, anchor='ls')
            left = max(0, -box[0])
            advance = max(1, round(font.getlength(char)))
            if base == 0xE800 and not char.isspace():
                # Monika's compact cursive joins letters at 18px. Leave room
                # for each ink extent so o/l/d and t/e/l stay distinguishable
                # on the calculator, without increasing the font size.
                advance = max(advance, box[2] + 1)
            width, height = max(1, advance, box[2] + left), ascent + descent
            canvas = Image.new('L', (width, height))
            ImageDraw.Draw(canvas).text((left, ascent), char, font=font, fill=255, anchor='ls')
            canvas = canvas.point([round(255 * (v / 255) ** 0.64) for v in range(256)])
            pixels = canvas.tobytes()
            records.append(struct.pack('<IBBBBH', (base + ord(char)) | (size << 24),
                                       width, height, advance, (-left) & 0xFF, len(pixels)) + pixels)
    return records

def glyph_records(
    font_path: Path,
    texts: list[str],
    key_fonts: tuple[Path, Path] | None = None,
    *,
    chinese_font: bool = False,
) -> list[bytes]:
    chars = sorted(set("? " + TAG.sub("", "".join(texts))))
    records = []
    narrow = set(".,'\"`!:;|ijlI[]()")
    for size in (8, 9, 10, 11, 12, 13, 14):
        font = ImageFont.truetype(str(font_path), size)
        ascent, descent = font.getmetrics()
        for char in chars:
            box = font.getbbox(char, anchor="ls")
            advance = round(font.getlength(char))
            if char in narrow:
                advance = min(advance, max(2, box[2] - box[0] + 1))
            elif ord(char) < 128:
                # Latin glyphs in the Chinese face are visibly cramped when
                # mixed with CJK. Preserve the font's native advance there;
                # the dedicated English face keeps its tighter game spacing.
                advance = max(3, advance)
            advance = max(2, min(18, advance))
            left = max(0, -box[0])
            width, height = (
                max(1, min(20, max(advance, box[2] + left))),
                min(20, ascent + descent),
            )
            canvas = Image.new("L", (width, height))
            ImageDraw.Draw(canvas).text(
                (left, ascent), char, font=font, fill=255, anchor="ls"
            )
            # Glyphs are rasterized directly at their delivery size, not scaled
            # by the runtime. Raise low-coverage antialiasing samples so thin
            # strokes survive RGB565 blending on the calculator LCD without
            # changing extents, advances, wrapping, or line layout.
            # At 8--10 px, LCD/RGB565 quantization can erase the faint edge
            # samples that visually complete a one-pixel stem. Use a stronger
            # coverage curve only for those small strikes, then taper it at
            # prose sizes. This improves clarity without dilating the bitmap,
            # changing advances, or invalidating wrapping calculations.
            if chinese_font:
                coverage_gamma = 0.68 if size <= 10 else 0.73 if size == 11 else 0.78
            else:
                coverage_gamma = 0.52 if size <= 10 else 0.58 if size == 11 else 0.64
            contrast = [
                round(255 * ((value / 255) ** coverage_gamma)) for value in range(256)
            ]
            canvas = canvas.point(contrast)
            pixels = canvas.tobytes()
            key = ord(char) | (size << 24)
            records.append(
                struct.pack("<IBBBBH", key, width, height, advance, 0, len(pixels))
                + pixels
            )
    if key_fonts:
        # The key fonts use printable ASCII as their source encoding. Store those
        # bitmaps in the private-use area so they can coexist with the prose font.
        key_sources = "@yd/_AHSQLON24568\u0063\u0065\u00a1\u00a2\u00a3\u00a4\u00b7"
        mappings = (
            (key_fonts[0], 0xE000, key_sources),
            (key_fonts[1], 0xE100, key_sources),
        )
        for key_path, private_base, sources in mappings:
            for size in (8, 9, 10, 11, 12, 13, 14):
                font = ImageFont.truetype(str(key_path), size)
                ascent, descent = font.getmetrics()
                for source in sources:
                    box = font.getbbox(source, anchor="ls")
                    natural_advance = round(font.getlength(source))
                    if source in "\u00a1\u00a2\u00a3\u00a4":
                        # The arrow artwork is narrower than the numeric keycaps.
                        # Give each arrow the same cell as a numeric key and
                        # center its pixels so both four-key rows align exactly.
                        advance = max(2, min(28, round(font.getlength("2"))))
                        ink_width = max(1, box[2] - box[0])
                        left = max(0, (advance - ink_width) // 2 - box[0])
                    else:
                        advance = max(2, min(28, natural_advance))
                        left = max(0, -box[0])
                    width = max(1, min(30, max(advance, box[2] + left)))
                    height = min(24, ascent + descent)
                    canvas = Image.new("L", (width, height))
                    ImageDraw.Draw(canvas).text(
                        (left, ascent), source, font=font, fill=255, anchor="ls"
                    )
                    pixels = canvas.tobytes()
                    codepoint = private_base + ord(source)
                    key = codepoint | (size << 24)
                    records.append(
                        struct.pack(
                            "<IBBBBH", key, width, height, advance, 0, len(pixels)
                        )
                        + pixels
                    )
    records.sort(key=lambda record: struct.unpack_from("<I", record)[0])
    return records

def build_pack(
    manifest: dict,
    font: Path,
    output: Path,
    assets_root: Path,
    language: str = "en",
    key_fonts: tuple[Path, Path] | None = None,
    *,
    discover_key_fonts: bool = True,
) -> None:
    # Labels are authoring-time markers, not runtime events. Resolve them after
    # removing the markers so jump destinations match the compact disk stream.
    source_events = manifest["events"]
    labels: dict[str, int] = {}
    runtime_events: list[dict] = []
    for event in source_events:
        if event.get("op") == "label":
            label = str(event.get("name", ""))
            if not label or label in labels:
                raise ValueError(f"invalid or duplicate story label: {label!r}")
            labels[label] = len(runtime_events)
        else:
            runtime_events.append(event)
    for event in runtime_events:
        op = event.get("op")
        if op in {"choice_option", "jump", "if_state", "if_state_gt"}:
            target = event.get("target")
            if isinstance(target, str):
                if target not in labels:
                    raise ValueError(f"unknown story label {target!r} in {op}")
                target = labels[target]
            target = int(target)
            if not 0 <= target < len(runtime_events):
                raise ValueError(f"story target out of range in {op}: {target}")
            event["target_index"] = target
    if len(runtime_events) > 0x7FFFFFFF:
        raise ValueError("story event stream exceeds the 32-bit branch target limit")
    start_label = manifest.get("start_label")
    start_event = 0
    if start_label:
        if start_label not in labels:
            raise ValueError(f"unknown start label {start_label!r}")
        start_event = labels[start_label]
    option_indices: set[int] = set()
    for index, event in enumerate(runtime_events):
        op = event.get("op")
        if op == "choice":
            count = int(event.get("count", 0))
            state = int(event.get("state", 0))
            if not 1 <= count <= 10 or not 0 <= state < 16:
                raise ValueError(f"invalid choice count/state at event {index}")
            if index + count >= len(runtime_events):
                raise ValueError(f"choice at event {index} is missing its options")
            for option_index in range(index + 1, index + count + 1):
                if runtime_events[option_index].get("op") != "choice_option":
                    raise ValueError(f"choice at event {index} has a non-option in its option block")
                option_indices.add(option_index)
        elif op == "choice_option" and index not in option_indices:
            raise ValueError(f"orphan choice option at event {index}")
        elif op in {"set_state", "add_state", "if_state", "if_state_gt", "persistent_load",
                    "persistent_store", "rand_state"} and not 0 <= int(event.get("state", 0)) < 16:
            raise ValueError(f"invalid state slot at event {index}")
        if op in {"persistent_load", "persistent_store"} and not 0 <= int(event.get("key", 0)) < 32:
            raise ValueError(f"invalid persistent key at event {index}")
        if op == "rand_state" and not 1 <= int(event.get("count", 0)) <= 16:
            raise ValueError(f"invalid random range at event {index}")
        if op == "if_state_gt" and not 0 <= int(event.get("other_state", 0)) < 16:
            raise ValueError(f"invalid comparison state slot at event {index}")
    manifest["events"] = runtime_events

    assets = assets_root
    resource_ids = {r["name"]: i + 1 for i, r in enumerate(manifest["resources"])}
    image_cache_key = id(manifest), assets.resolve()
    cached_images = _IMAGE_RECORD_CACHE.get(image_cache_key)
    if cached_images:
        records, offsets, actual_kinds, layer_stats = cached_images
    else:
        visible_positions: dict[str, set[str]] = {}
        for event in manifest["events"]:
            if event.get("op") == "show" and event.get("resource"):
                visible_positions.setdefault(event["resource"], set()).add(
                    event.get("position", "")
                )
        paths: dict[str, Path] = {}
        actual_kinds: dict[str, str] = {}
        for resource in manifest["resources"]:
            path = find_asset(assets, resource["path"])
            kind = resource["kind"]
            if kind == "auto":
                normalized = path.as_posix().lower()
                kind = "sprite" if "/sprites/" in normalized else "screen_layer"
            if resource["name"] in {"black", "black1", "white"}:
                kind = "fullscreen_layer"
            paths[resource["name"]] = path
            actual_kinds[resource["name"]] = kind

        # Most gallery CGs are the exact opaque files already used by the
        # story. Keep their independent virtual IDs, but point them at the
        # canonical story entry instead of encoding a second pixel payload.
        canonical_by_path = {
            path.resolve(): name
            for name, path in paths.items()
            if not name.startswith("__gallery_")
        }
        gallery_aliases = {
            name: canonical_by_path[path.resolve()]
            for name, path in paths.items()
            if name.startswith("__gallery_full_")
            and path.resolve() in canonical_by_path
        }

        layered, layer_bases, layer_stats = plan_layered_sprites(
            manifest["resources"], paths, actual_kinds, len(resource_ids) + 1
        )
        records, offsets = [], {}
        for resource in manifest["resources"]:
            name = resource["name"]
            if name in gallery_aliases:
                records.append(
                    alias_image_record(
                        resource_ids[name], resource_ids[gallery_aliases[name]]
                    )
                )
                offsets[name] = (0, 0)
                continue
            animation_next = resource_ids.get(resource.get("animation_next", ""), 0)
            animation_ms = int(resource.get("animation_duration_ms", 0))
            if name in layered:
                overlay, patch_x, patch_y, base_id, logical_size = layered[name]
                record = encode_rgba_record(
                    resource_ids[name], overlay, 2, animation_next, animation_ms
                )
                records.append(
                    complete_image_record(
                        record, base_id, patch_x, patch_y, logical_size
                    )
                )
                offsets[name] = (0, 0)
                continue
            record, crop_x, crop_y = image_record(
                resource_ids[name],
                paths[name],
                actual_kinds[name],
                name == "black",
                animation_next,
                animation_ms,
                visible_positions.get(name),
            )
            records.append(
                complete_image_record(record, 0, crop_x, crop_y)
                if actual_kinds[name] in ("screen_layer", "fullscreen_layer")
                else complete_image_record(record)
            )
            offsets[name] = (crop_x, crop_y)
        for base_id, base in layer_bases:
            records.append(complete_image_record(encode_rgba_record(base_id, base, 2)))
        _IMAGE_RECORD_CACHE[image_cache_key] = (
            records,
            offsets,
            actual_kinds,
            layer_stats,
        )
    manifest["sprite_layers"] = layer_stats
    suffix = "zh" if language == "zh" else "en"
    compiled_texts: dict[int, str] = {}
    for event_index, event in enumerate(manifest["events"]):
        text = str(event.get(f"text_{suffix}", ""))
        if event.get('op') == 'poem':
            base = {'sayori': 0xE200, 'natsuki': 0xE400, 'yuri': 0xE600, 'monika': 0xE800,
                    'yuri2': 0xEC00, 'yuri3': 0xEE00}[event['author']]
            text = ''.join(c if c in '\n\r\t' else chr(base + ord(c)) for c in text)
        elif event.get('op') == 'say' and event.get('edited'):
            text = edited_bank_text(text)
        compiled_texts[event_index] = text
    ui_texts = manifest.get("ui", {}).get(f"texts_{suffix}", {})
    if set(ui_texts) != set(UI_TEXT_KEYS):
        missing = sorted(set(UI_TEXT_KEYS) - set(ui_texts))
        extra = sorted(set(ui_texts) - set(UI_TEXT_KEYS))
        raise ValueError(
            f"invalid {suffix} UI text table: missing={missing}, extra={extra}"
        )
    ui_values = [str(ui_texts[key]) for key in UI_TEXT_KEYS]
    # Calculator-specific control descriptions stay in the runtime. Include
    # their glyphs without duplicating the game/menu wording in C++.
    shared_info = [
        "Advance dialogue activate interface Navigate Arrow keys Show history Game menu back > "
        "Hold to skip Toggle skipping automatic playback Hide the user interface Quick save "
        "load exit game enter space esc ctrl tab on s/l ↑↓←→ "
        "DDLC TI-Nspire Unofficial fan port This game is not affiliated in any way with Team Salvato. "
        "Please complete the original Doki Doki Literature Club before playing. "
        "Download the original game: https://ddlc.moe ENTER to continue ESC to quit "
        "推进对话并激活界面。方向键 导航界面。显示历史记录。"
        "游戏菜单／返回。按住时快进对话。切换快进。切换自动播放。"
        "隐藏用户界面。快速保存／快速读取。快速退出游戏。",
    ]
    ui = [*ui_values, "AQ0123456789", *shared_info]
    credit_text = str(manifest.get("ui", {}).get(f"credits_{suffix}", ""))
    if not credit_text:
        raise ValueError(f"manifest has no {suffix} credits text")
    texts = (
        ui
        + [credit_text]
        + [
            value
            for event_index, event in enumerate(manifest["events"])
            for value in (
                str(event.get(f"speaker_{suffix}", "")),
                compiled_texts[event_index],
            )
        ]
    )
    effective_key_fonts = key_fonts
    if effective_key_fonts is None and discover_key_fonts:
        effective_key_fonts = ensure_key_fonts(output.parent)
    glyphs = glyph_records(
        font,
        texts,
        effective_key_fonts,
        chinese_font=language == "zh",
    )
    glyphs = [g for g in glyphs
              if not 0xE200 <= (struct.unpack_from('<I', g)[0] & 0xFFFFFF) < 0xF200]
    glyphs.extend(poem_glyph_records(manifest.get('poem_fonts', [])))
    glitch_alphabet = str(manifest.get("glitch_alphabet", ""))
    if glitch_alphabet:
        for size in (PROSE_FONT_SIZE, PROSE_FONT_SIZE - 1):
            glyphs.extend(bank_glyph_records(font, 0, size, glitch_alphabet, 0.58))
    edited_font = manifest.get("edited_font")
    if edited_font:
        edited_chars = ''.join(chr(ord(c) - EDITED_BANK) for text in compiled_texts.values()
                               for c in text if EDITED_BANK <= ord(c) < EDITED_BANK + 0x100)
        glyphs.extend(bank_glyph_records(edited_font, EDITED_BANK, PROSE_FONT_SIZE,
                                         edited_chars + ' ', 0.58))
    # Poem font sizes above the prose range need whitespace metrics too.
    space_sizes = set()
    for spec in manifest.get('poem_fonts', []):
        if spec['size'] > 14 and spec['size'] not in space_sizes:
            space_sizes.add(spec['size'])
            poem_font = ImageFont.truetype(spec['path'], spec['size'])
            advance = max(1, round(poem_font.getlength(' ')))
            glyphs.append(struct.pack('<IBBBBH', ord(' ') | (spec['size'] << 24),
                                      1, 1, advance, 0, 1) + b'\0')
    glyphs.sort(key=lambda g: struct.unpack_from('<I', g)[0])
    strings = bytearray()

    string_cache: dict[bytes, tuple[int, int]] = {}

    def ref(value: str) -> tuple[int, int]:
        data = value.encode("utf-8")
        cached = string_cache.get(data)
        if cached is not None:
            return cached
        offset = len(strings)
        strings.extend(data)
        string_cache[data] = (offset, len(data))
        return offset, len(data)

    credits_ref = ref(credit_text)
    ui_refs = [ref(value) for value in ui_values]
    packed_events = []
    ops = {
        "scene": 1,
        "show": 2,
        "hide": 3,
        "say": 4,
        "end": 5,
        "title": 6,
        "pause": 7,
        "window": 8,
        "overlay": 9,
        "nvl": 10,
        "transform": 11,
        "persistent": 12,
        "choice": 13,
        "choice_option": 14,
        "jump": 15,
        "set_state": 16,
        "if_state": 17,
        "add_state": 18,
        "if_state_gt": 19,
        "move": 20,
        "poem": 21,
        "system": 22,
        "persistent_load": 23,
        "persistent_store": 24,
        "effect": 25,
        "rand_state": 26,
    }
    say_sizes: dict[int, int] = {}
    index = 0
    while index < len(manifest["events"]):
        event = manifest["events"][index]
        if event.get("op") != "say" or event.get("extend"):
            index += 1
            continue
        end = index + 1
        while (
            end < len(manifest["events"])
            and manifest["events"][end].get("op") == "say"
            and manifest["events"][end].get("extend")
        ):
            end += 1
        base_size = PROSE_FONT_SIZE
        if event.get("nvl"):
            for member in range(index, end):
                say_sizes[member] = base_size
            index = end
            continue
        # Ordinary ADV dialogue keeps its standard body size. Long statements
        # are handled by the player's in-box scrollbar rather than automatic
        # font shrinking.
        chosen = base_size
        for member in range(index, end):
            say_sizes[member] = chosen
        index = end

    for event_index, event in enumerate(manifest["events"]):
        name = event.get("resource", "")
        resource = resource_ids.get(name, 0)
        x = y = 0
        if event["op"] == "say":
            x = 1 if event.get("centered") else 0
            if event.get("lock_skip"):
                x |= 2  # Room-entry control bit; y still stores the font size.
            y = say_sizes.get(event_index, 12 if language == "zh" else 11)
        elif event["op"] in ("scene", "show", "move") and name:
            image_resource = next(r for r in manifest["resources"] if r["name"] == name)
            if event.get("particle_up"):
                x = -30001 - (int(name[-1]) - 1)
            elif actual_kinds[name] == "background":
                x = 0
                y = 30 if name in {"zatotitle", "ep1", "ep2", "ep3"} else 20
            elif actual_kinds[name] == "fullscreen_background":
                x, y = 0, 0
            elif actual_kinds[name] in ("screen_layer", "fullscreen_layer"):
                x = int(event.get("screen_x", 0))
                y = int(event.get("screen_y", 20))
            elif actual_kinds[name] == "prescaled_sprite":
                x = int(event.get("screen_x", 0))
                y = int(event.get("screen_y", 0))
            else:
                source = Image.open(find_asset(assets, image_resource["path"])).convert(
                    "RGBA"
                )
                # Match prepare_sprite_rgba(): xalign applies to the complete
                # transparent canvas. offsets[name] then places its alpha bbox.
                scaled_width = round(source.width * 226 / source.height)
                position = event.get("position", "")
                # The complete game never has more than two distinct
                # characters on screen. Bring the overwhelmingly common
                # left/right pair inward instead of reserving three wide slots.
                sprite_inset = 24
                x = (
                    sprite_inset
                    if position == "left"
                    else 320 - scaled_width - sprite_inset
                    if position == "right"
                    else (320 - scaled_width) // 2
                )
                x += offsets[name][0]
                y = -1 + offsets[name][1]
                if position == "shaking":
                    # Keep the ATL shaking marker disjoint from legitimate
                    # negative sprite coordinates (sprites are intentionally
                    # placed at y=-1 on the 240-pixel stage).
                    y = -20000 - y
        elif event["op"] == "poem":
            x = event['font_size']
            y = event['line_height']
        elif event["op"] == "pause":
            x = max(-1, min(32767, int(event.get("duration_ms", 0))))
        elif event["op"] == "system":
            resource = int(event.get("action", 0))
            x = int(event.get("arg", 0))
            y = int(event.get("arg2", 0))
        elif event["op"] in ("persistent_load", "persistent_store"):
            resource = int(event.get("state", 0))
            x = int(event.get("key", 0))
        elif event["op"] == "effect":
            resource = int(event.get("kind", 0))
            x = min(32767, int(event.get("duration_ms", 0)))
            y = int(event.get("param", 0))
        elif event["op"] == "rand_state":
            resource = int(event.get("state", 0))
            x = int(event.get("count", 1))
            y = int(event.get("mask_state", -1))
        elif event["op"] == "window":
            x = 1 if event.get("visible") else 0
        elif event["op"] == "nvl":
            x = {"show": 1, "hide": 2, "clear": 3}[event["action"]]
        elif event["op"] == "overlay":
            if event.get("positioned"):
                x, y = -int(event.get("x", 0)) - 1, int(event.get("y", 0))
            else:
                x = 1 if event.get("exclusive") else 0
        elif event["op"] == "transform":
            packed_pivot = (int(event.get("pivot_y", 110)) << 8) | int(
                event.get("pivot_x", 160)
            )
            packed_options = int(event.get("transform_options", 0))
            x = packed_pivot - 0x10000 if packed_pivot >= 0x8000 else packed_pivot
            y = packed_options - 0x10000 if packed_options >= 0x8000 else packed_options
        elif event["op"] == "title":
            y = resource_ids[event["history_background"]]
        elif event["op"] == "persistent":
            resource = int(event.get("resource", 0))
        elif event["op"] == "choice":
            resource = int(event.get("count", 0))
            x = int(event.get("state", 0))
        elif event["op"] == "choice_option":
            resource = int(event.get("value", 0))
        elif event["op"] == "set_state":
            resource = int(event.get("state", 0))
            x = int(event.get("value", 0))
        elif event["op"] == "if_state":
            resource = int(event.get("state", 0))
            x = int(event.get("value", 0))
        elif event["op"] == "add_state":
            resource = int(event.get("state", 0))
            x = int(event.get("value", 0))
        elif event["op"] == "if_state_gt":
            resource = int(event.get("state", 0))
            x = int(event.get("other_state", 0))
        speaker_ref = ref(str(event.get(f"speaker_{suffix}", "")))
        if event["op"] in ("choice_option", "jump", "if_state", "if_state_gt"):
            # 32-bit branch targets live in the unused speaker reference.
            speaker_ref = (int(event["target_index"]), 0)
        if event["op"] in ("show", "scene") and event.get("behind"):
            speaker_ref = (
                resource_ids.get(str(event["behind"]), 0),
                int(event.get("transition_direction", 0)),
            )
        elif event["op"] in ("show", "scene", "hide"):
            speaker_ref = (0, int(event.get("transition_direction", 0)))
        elif event["op"] == "transform":
            speaker_ref = (int(event.get("duration_ms", 0)), 0)
        elif event["op"] == "title":
            speaker_ref = (
                resource_ids["__gallery_overlay"],
                resource_ids["__main_menu_overlay"],
            )
        refs = (speaker_ref, ref(compiled_texts[event_index]))
        flat_refs = tuple(value for pair in refs for value in pair)
        flags = (
            event.get("flags", 0)
            | (0x80 if event.get("extend") else 0)
            | (0x40 if event.get("op") == "say" and event.get("nvl") else 0)
        )
        packed_events.append(
            struct.pack(
                "<BBHhhIIII", ops[event["op"]], flags, resource, x, y, *flat_refs
            )
        )
    header = struct.pack(
        "<4sHIIII",
        b"NZPK",
        (1 if language == "zh" else 0)
        | (2 if language == "zh" and manifest.get("chinese_source") == "loose" else 0)
        | 4 | (8 if manifest.get("ddlc_ui") else 0),
        len(records),
        len(glyphs),
        len(packed_events),
        len(strings),
    )
    # All fixed-size metadata remains together. The target can load
    # the complete index with a handful of sequential reads, then seek into
    # image/glyph payloads on demand.  This avoids tens of thousands of tiny
    # seeks when a Chinese font contributes many glyph records.
    image_head_size = struct.calcsize(IMAGE_CORE_FORMAT) + struct.calcsize(
        IMAGE_COMPOSITE_FORMAT
    )
    glyph_head_size = struct.calcsize("<IBBBBH")
    image_heads = b"".join(record[:image_head_size] for record in records)
    image_payloads = b"".join(record[image_head_size:] for record in records)
    glyph_heads = b"".join(record[:glyph_head_size] for record in glyphs)
    glyph_payloads = b"".join(record[glyph_head_size:] for record in glyphs)
    ddlc_footer = b""
    if manifest.get("ddlc_ui"):
        ddlc_footer = struct.pack("<4sHHH", b"DDUI", 3,
                                 resource_ids[manifest["ddlc_ui"]["title"]],
                                 resource_ids[manifest["ddlc_ui"]["notebook"]])
        words = manifest.get("poem_words", [])
        if len(words) != 228 or len({row["text"] for row in words}) != 228:
            raise ValueError("DDLC requires all 228 unique local poem words")
        ddlc_footer += struct.pack("<H", len(words))
        for row in words:
            word = row["text"].encode("ascii")
            points = row["points"]
            if (not 1 <= len(word) <= 63 or any(c < 32 or c > 126 for c in word)
                    or len(points) != 3 or any(type(p) is not int or p not in (1, 2, 3) for p in points)):
                raise ValueError("Invalid local poem word or scores")
            ddlc_footer += struct.pack("<BBBB", len(word), *points) + word
        ddlc_footer += struct.pack("<I", start_event)
        alphabet = [ord(c) for c in str(manifest.get("glitch_alphabet", ""))]
        ddlc_footer += struct.pack("<H", len(alphabet)) + b"".join(struct.pack("<I", cp) for cp in alphabet)
        for path in manifest.get("character_files", [None] * 4)[:4] + [None] * (4 - len(manifest.get("character_files", []))):
            blob = Path(path).read_bytes() if path else b""
            ddlc_footer += struct.pack("<I", len(blob)) + blob
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(
        header
        + image_heads
        + glyph_heads
        + b"".join(packed_events)
        + strings
        + image_payloads
        + glyph_payloads
        + struct.pack(
            "<4sII" + "II" * len(ui_refs),
            b"NZUI",
            *credits_ref,
            *(value for pair in ui_refs for value in pair),
        )
        + ddlc_footer
    )

def ensure_key_fonts(
    output_dir: Path, system_font_dir: Path | None = None
) -> tuple[Path, Path] | None:
    if system_font_dir is None:
        windows_directory = os.environ.get("WINDIR")
        system_font_dir = (
            Path(windows_directory) / "Fonts" if windows_directory else None
        )
    cache = output_dir / "font-cache"
    cache.mkdir(parents=True, exist_ok=True)
    english_target = cache / "TINspireKeysCX.ttf"
    chinese_target = cache / "TINspireKeysChinese.ttf"
    english_source = english_target if english_target.is_file() else None
    if english_source is None and system_font_dir is not None:
        candidate = system_font_dir / "TINspireKeysCX.ttf"
        english_source = candidate if candidate.is_file() else None
    if english_source is None:
        return None
    chinese_source = chinese_target if chinese_target.is_file() else None
    if chinese_source is None and system_font_dir is not None:
        candidate = system_font_dir / "TINspireKeysChinese.ttf"
        chinese_source = candidate if candidate.is_file() else None
    if chinese_source is None:
        chinese_source = english_source
    if not english_target.is_file():
        shutil.copy2(english_source, english_target)
    if not chinese_target.is_file():
        shutil.copy2(chinese_source, chinese_target)
    return english_target, chinese_target
