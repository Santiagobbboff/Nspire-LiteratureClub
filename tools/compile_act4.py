"""Translate DDLC Act 4 (script-ch40: the club without Monika) and the credits.

ch40_main is compiled like the other chapters. The credits are rebuilt from the
original data (the glitch backdrops, Monika's lines from the credits ATL, the
credit cards with their locked/unlocked CG pictures and the final sheet) as a
timed sequence followed by the engine's scrolling credit roll.
"""
from __future__ import annotations

import re

from compile_act3 import Act3Compiler, SLOT_KILL, SLOT_SCRATCH
from compile_act2 import AUTOLOAD, PERSIST, SLOT_CHOICE, SLOT_TEMP, speakers_table
from rpyc_ast import flatten, kind, load_rpyc

AUTOLOAD.update({"postcredits_loop": 11})
# credits_cgN pairs with persistent.clear[N-1]; the clearall endings use the plain picture.
CREDIT_CARDS = [
    ("Concept & Game Design", "Dan Salvato"), ("Character Art", "Satchely"),
    ("Background Art", "Velinquent"), ("Writing", "Dan Salvato"), ("Music", "Dan Salvato"),
    ("Vocals", "Jillian Ashcraft"), ("Special Thanks", "Masha Gutin\nKagefumi"),
    ("Special Thanks", "David Evelyn\nCorey Shin"),
    ("Special Thanks", "Alecia Bardachino\nMatt Naples"), ("Special Thanks", "Monika\n{player}"),
]


def credits_lines(raw_dir):
    """Monika's credit lines, in order, from the mcredits_* ATL definitions."""
    _, top = load_rpyc(raw_dir / "credits.rpyc")
    lines = []
    for node in flatten(top):
        if kind(node) != "Image" or not str(node.imgname[0]).startswith("mcredits_"):
            continue
        texts = re.findall(r'Text\("([^"]*)"', repr(getattr(node, "atl", "")))
        lines.append((str(node.imgname[0]), texts))
    lines.sort(key=lambda item: (int(re.search(r"\d+", item[0]).group()), item[0]))
    return [text for _name, texts in lines for text in texts]


class Act4Compiler(Act3Compiler):
    def fresh(self, hint):
        self.serial += 1
        return f"act4_{hint}_{self.serial}"

    def binding(self, name):
        if name == "clearall":
            return (SLOT_KILL,), lambda values: bool(values[SLOT_KILL])
        return super().binding(name)

    def constant_substitutions(self, expression):
        text = super().constant_substitutions(expression)
        return text.replace("persistent.clearall", "clearall")

    def condition(self, expression, yes, no):
        text = self.constant_substitutions(expression)
        if text.strip() == "clearall":
            self.domains[SLOT_KILL] = (0, 1)
        super().condition(text, yes, no)


def extend_act4(previous):
    compiler = Act4Compiler.__new__(Act4Compiler)
    compiler.__dict__.update(previous.__dict__)
    events = compiler.events
    speakers = speakers_table()
    _, top = load_rpyc(compiler.raw_dir / "credits.rpyc")
    for node in flatten(top):
        if kind(node) == "Label":
            compiler.labels[node.name] = ("credits.rpyc", node)

    def scene_named(name):
        resource, _row = compiler.resource_for(name)
        if resource is None:
            compiler.report_skip(f"missing credits art: {' '.join(name)}")
            return
        events.append({"op": "scene", "resource": resource})
        compiler.possible.clear()
        compiler.visible.clear()

    def pause(ms):
        events.append({"op": "pause", "duration_ms": ms})

    def overlay(text, x=None, y=None):
        event = {"op": "overlay", "text_en": text, "exclusive": True}
        if x is not None:
            event.update(positioned=True, x=x, y=y)
        events.append(event)

    def clear_overlay():
        events.append({"op": "overlay", "text_en": ""})

    # -- Act 4 proper -------------------------------------------------------------
    compiler.label("act4_start")
    for slot in range(16):
        compiler.set(slot, 0)
    compiler.domains.update({SLOT_KILL: (0, 1), SLOT_TEMP: tuple(range(12)), 2: tuple(range(32))})
    compiler.possible.clear()
    compiler.visible.clear()
    compiler.overlay_layers = set()
    compiler.overlay_aliases = {}
    compiler.chapter = 0
    compiler.edited = False
    speakers.update({"s": "Sayori", "n": "Natsuki", "y": "Yuri", "m": "Monika"})
    compiler.replacements.update({"s_name": "Sayori", "n_name": "Natsuki", "y_name": "Yuri", "m_name": "Monika"})
    # persistent.clearall: every CG (clear[0..9]) seen across the playthroughs.
    events.append({"op": "system", "action": 5, "arg": SLOT_KILL, "arg2": 10})
    compiler.invoke("ch40_main")
    compiler.jump("act4_credits")

    # -- Credits -------------------------------------------------------------------
    compiler.label("act4_credits")
    compiler.set(SLOT_TEMP, AUTOLOAD["credits"])
    events.append({"op": "persistent_store", "state": SLOT_TEMP, "key": PERSIST["autoload"]})
    events.append({"op": "system", "action": 5, "arg": SLOT_KILL, "arg2": 10})
    events.append({"op": "window", "visible": False})
    black, _row = compiler.resource_for(("black",))
    events.append({"op": "scene", "resource": black})
    # "Your Reality" plays over 41 seconds of glitch backdrops in the original.
    for n, ms in ((1, 10000), (2, 12000), (3, 10000), (4, 9000)):
        scene_named((f"end_glitch{n}",))
        pause(ms)
    events.append({"op": "scene", "resource": black})
    pause(1500)
    lines = credits_lines(compiler.raw_dir)
    per_line = max(2500, 50000 // max(1, len(lines)))
    for line in lines:
        overlay(line)
        pause(per_line)
    clear_overlay()
    events.append({"op": "scene", "resource": black})
    pause(880)
    scene_named(("credits_logo",))
    pause(9120)
    for index, (header, name) in enumerate(CREDIT_CARDS):
        seen = compiler.fresh("cg_seen")
        done = compiler.fresh("cg_done")
        events.append({"op": "system", "action": 6, "arg": SLOT_SCRATCH, "arg2": index})
        events.append({"op": "if_state", "state": SLOT_KILL, "value": 1, "target": seen})
        events.append({"op": "if_state", "state": SLOT_SCRATCH, "value": 1, "target": seen})
        scene_named(("notfound",))  # unseen CG: the "not found" card stands in for the locked picture
        compiler.jump(done)
        compiler.label(seen)
        scene_named((f"credits_cg{index + 1}",))
        compiler.label(done)
        overlay(f"{header}\n{name}", 6, 190)
        pause(8600)
        clear_overlay()
        if index < len(CREDIT_CARDS) - 1:
            scene_named(("notfound",))
            pause(500)
    scene_named(("credits_ts",))
    overlay("made with love by", 6, 150)
    pause(9300)
    clear_overlay()
    # The engine's scrolling roll (full credits text in the pack) ends the program.
    compiler.set(SLOT_TEMP, AUTOLOAD["postcredits_loop"])
    events.append({"op": "persistent_store", "state": SLOT_TEMP, "key": PERSIST["autoload"]})
    events.append({"op": "end", "flags": 2})

    # -- After the credits: the final sheet, every launch -----------------------------
    compiler.label("act4_postcredits")
    compiler.set(SLOT_TEMP, AUTOLOAD["postcredits_loop"])
    events.append({"op": "persistent_store", "state": SLOT_TEMP, "key": PERSIST["autoload"]})
    events.append({"op": "system", "action": 5, "arg": SLOT_KILL, "arg2": 10})
    events.append({"op": "window", "visible": False})
    normal, all_clear = compiler.fresh("poem_end"), compiler.fresh("poem_end_clearall")
    events.append({"op": "if_state", "state": SLOT_KILL, "value": 1, "target": all_clear})
    compiler.label(normal)
    scene_named(("poem_end",))
    compiler.jump(compiler.fresh("poem_end_shown"))
    shown = events[-1]["target"]
    compiler.label(all_clear)
    scene_named(("poem_end_clearall",))
    compiler.label(shown)
    pause(-1)
    compiler.dialog("Error: Script file is missing or corrupt.\nPlease reinstall the game.", ("OK",))
    events.append({"op": "choice", "state": SLOT_CHOICE, "count": 2,
                   "text_en": "Delete all save data and start over from Act 1?"})
    events.append({"op": "choice_option", "value": 0, "text_en": "Yes, delete my existing data.",
                   "target": "act4_reset"})
    events.append({"op": "choice_option", "value": 1, "text_en": "No, continue where I left off.",
                   "target": "act4_back"})
    compiler.label("act4_reset")
    compiler.set(SLOT_TEMP, 0)
    for key in range(32):
        events.append({"op": "persistent_store", "state": SLOT_TEMP, "key": key})
    events.append({"op": "system", "action": 1})
    events.append({"op": "system", "action": 3, "arg": 15})
    compiler.label("act4_back")
    events.append({"op": "end", "flags": 0})
