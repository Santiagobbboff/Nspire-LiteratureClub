"""Bake the Act 1 ATL finale into short native image sequences (not a movie).

The original uses image layers, zooms, noise and a simulated exception screen.
Only the used 320x180 frames are shipped; dialogue is rendered by the engine.
"""
from pathlib import Path
import random
from PIL import Image, ImageDraw, ImageFont
from ddlc_image_resolve import ImageResolver

CREDITS = "\n".join((
    "Doki Doki Literature Club!", "", "Concept & Game Design", "Dan Salvato", "",
    "Character Art", "Satchely", "", "Background Art", "Velinquent", "",
    "Writing", "Dan Salvato", "", "Music", "Dan Salvato", "", "Vocals", "Jillian Ashcraft", "",
    "Special Thanks", "Masha Gutin", "Kagefumi", "David Evelyn", "Corey Shin",
    "Alecia Bardachino", "Matt Naples", "", "made with love by", "Team Salvato", "",
    "nZATO engine", "by zyf722", "", "DDLC TI-84 Plus CE", "by koboshchan", "",
    "DDLC TI-Nspire port by", "Santiagobbboff and Chatgpt-san", "", "Thank you for playing.",
))


def prepare_ending(manifest, raw_dir, art_dir, font_dir):
    resolver = ImageResolver(raw_dir, art_dir / "ending-resolver")
    target = art_dir / "img"
    target.mkdir(parents=True, exist_ok=True)
    background = Image.open(raw_dir / "images/cg/s_kill_bg.png").convert("RGBA")
    foreground = Image.open(raw_dir / "images/cg/s_kill.png").convert("RGBA")

    def composite(bg_zoom, girl_zoom, xalign, yalign, yoffset=0):
        canvas = Image.new("RGBA", (1280, 720), "black")
        bg = background.resize((round(1280 * bg_zoom), round(720 * bg_zoom)), Image.Resampling.LANCZOS)
        canvas.alpha_composite(bg, ((1280-bg.width)//2, (720-bg.height)//2))
        girl = foreground.resize((round(foreground.width*girl_zoom), round(foreground.height*girl_zoom)), Image.Resampling.LANCZOS)
        canvas.alpha_composite(girl, (round((1280-girl.width)*xalign), round((720-girl.height)*yalign+yoffset)))
        return canvas.convert("RGB").resize((320, 180), Image.Resampling.LANCZOS)

    def add(name, image, next_name=None, duration=0):
        filename = name + ".png"
        image.save(target / filename)
        row = {"name": name, "path": "img/"+filename, "kind": "background"}
        if next_name:
            row.update(animation_next=next_name, animation_duration_ms=duration)
        manifest["resources"].append(row)

    for i in range(16):
        t = i/15
        add(f"__ending_reveal_{i:02}", composite(1.1-.1*t, .8-.05*t, .3+.015*t, .25, 10*t),
            f"__ending_reveal_{i+1:02}" if i < 15 else None, 250)
    add("__ending_full", composite(1, .75, .315, .25, 10))
    for i in range(4):
        image = composite(2, 2, .5, .05)
        image = image.transform(image.size, Image.Transform.AFFINE,
                                (1, 0, (-1, 1, 0, -2)[i], 0, 1, (0, 1, -1, 0)[i]))
        # A small repeating noise/vignette sequence approximates the original
        # dizzy/alpha ATL without per-frame calculator effects or a large movie.
        rng = random.Random(100+i)
        px = image.load()
        for y in range(180):
            for x in range(320):
                edge = max(abs(x-160)/160, abs(y-90)/90)
                factor = 1-.3*edge*edge
                noise = rng.randrange(-9, 10)
                px[x,y] = tuple(max(0,min(255,round(c*factor)+noise)) for c in px[x,y])
        add(f"__ending_close_{i}", image, f"__ending_close_{(i+1)%4}", 150)
    for i, name in enumerate(("splash_glitch", "splash_glitch2")):
        image = resolver._render_ref(name)
        gui = raw_dir / "gui"
        if image is None and i:
            image = Image.open(gui / "menu_bg.png").convert("RGBA")
        if image is None:
            raise ValueError(f"cannot render ending image {name}")
        canvas = Image.new("RGBA", image.size, "white")
        canvas.alpha_composite(image.convert("RGBA"))
        if i:
            for girl in ("m", "n", "y"):
                overlay = resolver._render_ref("splash_glitch_"+girl)
                if overlay is None:
                    overlay = Image.open(gui / ("menu_art_"+girl+".png")).convert("RGBA")
                if overlay is None:
                    raise ValueError(f"cannot render ending splash girl {girl}")
                # These are individual title figures, not full-screen layers.
                # Use the source glitch ATL's half-size figures and one of its
                # visible positions; full sprite teleports remain an approximation.
                overlay = overlay.convert("RGBA")
                overlay = overlay.resize((round(overlay.width*.5), round(overlay.height*.5)), Image.Resampling.LANCZOS)
                x, y = {"m": (.3, .1), "n": (.4, .2), "y": (.81, 1.3)}[girl]
                canvas.alpha_composite(overlay, (round(canvas.width*x), round(canvas.height*y)))
        add(f"__ending_splash_{i}", canvas.convert("RGB").resize((320,180),Image.Resampling.LANCZOS))
    font = ImageFont.truetype(str(font_dir / "Aller_Rg.ttf"), 11)
    for i in range(2):
        image = Image.new("RGB", (320,180), "white")
        draw = ImageDraw.Draw(image)
        draw.text((8,8), "An exception has occurred.", font=font, fill="black")
        draw.text((8,24), "See traceback.txt for details.", font=font, fill="black")
        add(f"__ending_exception_{i}", image)
    # The source keeps the zoomed Sayori layer ABOVE the splash/exception
    # screens. Compose her there rather than replacing the entire stage.
    girl = foreground.resize((round(foreground.width*2), round(foreground.height*2)), Image.Resampling.LANCZOS)
    layer = Image.new("RGBA", (1280,720))
    layer.alpha_composite(girl, ((1280-girl.width)//2, round((720-girl.height)*.05)))
    layer = layer.resize((320,180), Image.Resampling.LANCZOS)
    for name in ("__ending_splash_0", "__ending_splash_1", "__ending_exception_0", "__ending_exception_1"):
        backdrop = Image.open(target / (name+".png")).convert("RGBA")
        if "splash" not in name:
            image = backdrop.copy()
            image.alpha_composite(layer)
            image.convert("RGB").save(target / (name+".png"))
            continue
        # Keep both glitch backdrops still and move only the foreground girl.
        # The existing four-frame image player needs no additional runtime
        # effects. Append new resources after the old ones, preserving IDs.
        names = [name] + [f"{name}_move_{i}" for i in range(1, 4)]
        for i, frame_name in enumerate(names):
            moved = layer.transform(layer.size, Image.Transform.AFFINE,
                                    (1, 0, (-1, 1, 0, -2)[i],
                                     0, 1, (0, 1, -1, 0)[i]))
            image = backdrop.copy()
            image.alpha_composite(moved)
            if i == 0:
                image.convert("RGB").save(target / (frame_name+".png"))
                row = next(r for r in manifest["resources"] if r["name"] == name)
                row.update(animation_next=names[1], animation_duration_ms=150)
            else:
                add(frame_name, image.convert("RGB"), names[(i+1) % 4], 150)
    manifest["ui"]["credits_en"] = CREDITS
