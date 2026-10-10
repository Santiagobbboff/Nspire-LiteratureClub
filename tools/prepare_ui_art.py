"""Create pack-owned title/notebook pixels; never emit C++ image headers."""
from pathlib import Path
from PIL import Image, ImageDraw


def prepare_ui_art(manifest, raw_dir, art_dir):
    gui = raw_dir / "gui"
    target = art_dir / "img"
    target.mkdir(parents=True, exist_ok=True)
    canvas = Image.new("RGBA", (320, 240), (255, 255, 255, 255))
    d = ImageDraw.Draw(canvas)
    for row, cy in enumerate(range(-12, 258, 48)):
        for cx in range(24 if row % 2 else 0, 338, 48):
            d.ellipse((cx-13, cy-13, cx+13, cy+13), fill=(255,239,255,255))
    for y in range(240):
        # Flatten the apex: only y=120 otherwise reaches 78, making a lone
        # border pixel stick out beyond the adjacent rows.
        edge = min(77, 60 + 18*(14400-(y-120)**2)//14400)
        d.line((0,y,edge-1,y), fill=(255,230,242,255))
        d.line((edge,y,edge+5,y), fill=(255,188,223,255))
    logo = Image.open(gui / "logo.png").convert("RGBA").resize((104,104), Image.Resampling.LANCZOS)
    canvas.alpha_composite(logo, (-3,-10))
    for tag,cx,cy,zoom in [('y',600,335,.60),('n',750,385,.58),('s',510,500,.68),('m',1000,640,1.)]:
        art = Image.open(gui / f"menu_art_{tag}.png").convert("RGBA")
        size = (round(art.width*zoom/3), round(art.height*zoom/3))
        x,y = round(cx/4-size[0]/2), round((cy-art.height*zoom/2)/3)
        canvas.alpha_composite(art.resize(size, Image.Resampling.LANCZOS), (x,y))
    canvas.convert("RGB").quantize(colors=256, method=Image.Quantize.MEDIANCUT,
                                 dither=Image.Dither.NONE).save(target / "ddlc_title.png")
    notebook = Image.open(raw_dir / "images/bg/notebook.png").convert("RGB")
    notebook = notebook.crop((0,0,960,720)).resize((320,240), Image.Resampling.LANCZOS)
    notebook.quantize(colors=64, method=Image.Quantize.MEDIANCUT,
                      dither=Image.Dither.NONE).save(target / "ddlc_notebook.png")
    # Append after story resources: previous resource/event IDs stay stable.
    for name in ("title", "notebook"):
        manifest["resources"].append({"name": "__ddlc_"+name,
                                      "path": "img/ddlc_"+name+".png",
                                      "kind": "ddlc_ui_image"})
    manifest["ddlc_ui"] = {"title": "__ddlc_title", "notebook": "__ddlc_notebook"}
