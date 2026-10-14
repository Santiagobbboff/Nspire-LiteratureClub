"""Translate DDLC Act 2 (ch10, ch20-ch23, the second poem responses, the Act 2
Yuri/Natsuki exclusives, special poems and the weekend) into the 16-slot VM.

Only explicitly supported statements are translated and no game Python runs.
Act 2 keeps Act 1's slot conventions where the shared response labels need
them (poem winners in 0/14/6, opinions in 9/12/4 and 10/13/5, read mask in 1,
flags in 2) and adds counters for n_appeal (7), y_appeal (11) plus scratch
slots 8 and 15 (15 is also the word-menu slot and the weekend counter).
"""
from __future__ import annotations

import ast
import copy
import random
import re
import sys
from typing import Any

from compile_day6 import Day6Compiler
from rpyc_ast import flatten, kind, load_rpyc, pycode_source

# A subset of glitchtext.rpyc's non-ASCII alphabet; the runtime draws
# `{glitch=N}` as N random glyphs from this string.
GLITCH_ALPHABET = "¡¢£¤¥¦§©«¬®°±µ¶»¿ÀÃÆÇÐÑØÞßãæçðñøþÿĀăĄćČđĒěĞğĦıĳĸŁłŊŒœŞšŦŧŬŵŹž"

ACT2_FILES = ("script-ch10.rpyc", "script-ch20.rpyc", "script-ch21.rpyc", "script-ch22.rpyc",
              "script-ch23.rpyc", "script-poemresponses2.rpyc", "script-exclusives2-natsuki.rpyc",
              "script-exclusives2-yuri.rpyc", "poems_special.rpyc", "script-poemresponses.rpyc",
              "script-ch30.rpyc", "script-ch40.rpyc")
CHARACTER_TAGS = {"sayori": "s", "natsuki": "n", "yuri": "y", "monika": "m"}
PERSIST = {"playthrough": 0, "yuri_kill": 1, "autoload": 2, "special_0": 3, "special_1": 4,
           "special_2": 5, "seen_eyes": 6, "seen_sticker": 7, "seen_ghost_menu": 8,
           "deleted_saves": 9, "seen_colors_poem": 10}
AUTOLOAD = {"yuri_kill_1": 1, "yuri_kill_2": 2, "yuri_kill_3": 3, "ch30_main": 4}
CHARACTER_FILES = {"monika": 0, "natsuki": 1, "sayori": 2, "yuri": 3}
FLAG_SKIP_POEM, FLAG_N_POEMEARLY, FLAG_Y_GAVE, FLAG_NATSUKI_23, FLAG_FAINT = 1, 2, 4, 8, 16
SLOT_N_APPEAL, SLOT_Y_APPEAL, SLOT_TEMP, SLOT_WORD, SLOT_MASK, SLOT_FLAGS = 7, 11, 8, 15, 1, 2
SLOT_YURI_KILL = 15
SLOT_CHOICE = 15  # menus/dialogs; free outside the poem game and the weekend
Y_KILL_THRESHOLDS = ((920, "3a"), (460, "2a"), (None, "1a"))
MUSIC_PYTHON = ("audio.", "audiostart", "renpy.music", "currentpos", "musicpos", "timeleft",
                "track =", "startpos", "mouse_visible", "config.", "quick_menu", "allow_skipping",
                "_history_list", "consolehistory", "srf =", "anticheat", "renpy.save_persistent",
                "persistent.clear[", "persistent.anticheat", "in_yuri_kill", "poemsread",
                "skip_transition", "y_exclusivewatched", "y_ranaway", "persistent.deleted_saves",
                "style.say_dialogue", "del _history_list", "chapter =", "pt =", "menutext =",
                "poemopinion =", "persistent.seen_eyes", "persistent.seen_sticker", "persistent.ghost_menu",
                "gtext = ", "ntext = ", "x = ", "progress", "ac += 0", "starttime")
RANDINT = re.compile(r"renpy\.random\.randint\((\d+)\s*,\s*(\d+)\)")
SPEAKER_VARS = {"s_name": "s", "n_name": "n", "y_name": "y", "m_name": "m"}


def speakers_table():
    """compile_first_day's SPEAKERS dict (the entry point runs as __main__)."""
    main = sys.modules.get("__main__")
    table = getattr(main, "SPEAKERS", None)
    if table is None:
        import compile_first_day
        table = compile_first_day.SPEAKERS
    return table


class Act2Compiler(Day6Compiler):
    def fresh(self, hint):
        self.serial += 1
        return f"act2_{hint}_{self.serial}"

    # -- state bindings ----------------------------------------------------
    def binding(self, name):
        if name in ("n_appeal", "y_appeal"):
            slot = SLOT_N_APPEAL if name[0] == "n" else SLOT_Y_APPEAL
            return (slot,), lambda values: values[slot]
        if name == "y_gave":
            return (SLOT_FLAGS,), lambda values: bool(values[SLOT_FLAGS] & FLAG_Y_GAVE)
        if name == "natsuki_23":
            return (SLOT_FLAGS,), lambda values: bool(values[SLOT_FLAGS] & FLAG_NATSUKI_23)
        if name == "faint_effect":
            return (SLOT_FLAGS,), lambda values: bool(values[SLOT_FLAGS] & FLAG_FAINT)
        if name == "y_ranaway":
            # Set after yuri_exclusive2_1, cleared by Monika's m2_yuri_1 comment.
            return (0, SLOT_MASK), lambda values: values[0] == 2 and not (values[SLOT_MASK] & 8)
        if name in ("rand", "menu_clicked"):
            return (SLOT_TEMP,), lambda values: values[SLOT_TEMP]
        if name in ("_return", "madechoice"):
            return (SLOT_CHOICE,), lambda values: values[SLOT_CHOICE]
        if name in ("n_exclusivewatched", "y_exclusivewatched"):
            return (0,), lambda values: False
        return super().binding(name)

    def constant_substitutions(self, expression):
        text = str(expression)
        text = text.replace("persistent.playthrough", "2").replace("persistent.demo", "False")
        text = text.replace("config.skipping", "False").replace("seen_eyes_this_chapter", "False")
        text = text.replace("config.developer", "False")
        text = text.replace("renpy.windows and renpy.game.preferences.fullscreen", "False")
        text = re.sub(r"\bchapter\b", str(self.chapter), text)
        return text

    def condition(self, expression, yes, no):
        text = self.constant_substitutions(expression)
        kill = re.fullmatch(r"\s*persistent\.yuri_kill\s*(<|>=)\s*(\d+)\s*", text)
        if kill:
            # Weekend counter (slot 15) compared with a constant via a scratch slot.
            operator, value = kill.group(1), int(kill.group(2))
            self.set(SLOT_TEMP, value - 1)
            above = self.fresh("kill_above")
            self.events.append({"op": "if_state_gt", "state": SLOT_YURI_KILL,
                                "other_state": SLOT_TEMP, "target": above})
            self.jump(no if operator == ">=" else yes)
            self.label(above)
            self.jump(yes if operator == ">=" else no)
            return
        match = RANDINT.search(text)
        if match:
            low, high = int(match.group(1)), int(match.group(2))
            count = high - low + 1
            self.events.append({"op": "rand_state", "state": SLOT_TEMP, "count": min(16, count),
                                "mask_state": -1})
            self.domains[SLOT_TEMP] = tuple(range(min(16, count)))
            text = RANDINT.sub("rand", text)
            if low:
                text = text.replace("rand", f"(rand + {low})")
        if text.strip() in ("_return", "madechoice != \"monika\"", "menu_clicked < 9"):
            if text.strip() == "_return":
                self.domains[SLOT_CHOICE] = (0, 1)
            elif text.strip().startswith("madechoice"):
                self.domains[SLOT_CHOICE] = (0, 1, 2)
                text = "madechoice != 2"
            else:
                self.domains[SLOT_TEMP] = tuple(range(10))
        node = ast.parse(text, mode="eval").body
        self.condition_node(node, yes, no)

    # -- statements ----------------------------------------------------------
    def set_flag(self, bit, enabled):
        self.update_flag(bit, enabled)

    def python(self, node):
        source = (pycode_source(node.code) or "").strip()
        events = self.events
        if source in ("style.say_dialogue = style.edited", "style.say_dialogue = style.normal"):
            self.edited = source.endswith("edited")
            return
        glitch = re.fullmatch(r"(\w+) = glitchtext\((\d+|renpy\.random\.randint\(\d+, ?\d+\))\)", source)
        if glitch:
            name, size = glitch.group(1), glitch.group(2)
            count = int(size) if size.isdigit() else 40
            if name in SPEAKER_VARS:
                speakers_table()[SPEAKER_VARS[name]] = f"{{glitch={count}}}"
            self.replacements[name] = f"{{glitch={count}}}"
            return
        speaker = re.fullmatch(r"(\w_name) = ['\"](.+?)['\"]", source)
        if speaker:
            speakers_table()[SPEAKER_VARS[speaker.group(1)]] = speaker.group(2)
            self.replacements[speaker.group(1)] = speaker.group(2)
            return
        if source.startswith("pause("):
            self.compile_pause(source)
            return
        if source == "starttime = datetime.datetime.now()":
            self.stab_clock = 0.0
            return
        simple = {
            "y_appeal = 1": (SLOT_Y_APPEAL, 1), "y_appeal = 2": (SLOT_Y_APPEAL, 2),
            'poemwinner[1] = "yuri"': (14, 2), 'poemwinner[2] = "yuri"': (6, 2),
            "n_poemappeal[1] = 0": (12, 0), "menu_clicked = 0": (SLOT_TEMP, 0),
            "menu_clicked = 8": (SLOT_TEMP, 8), "persistent.yuri_kill = 1": (SLOT_YURI_KILL, 1),
            "ad = 40.0": None, "ac = 1.0": None,
        }
        if source in simple:
            if simple[source] is not None:
                self.set(*simple[source])
                if source == "y_appeal = 1":
                    self.domains[SLOT_Y_APPEAL] = (1,)
                elif source == "y_appeal = 2":
                    self.domains[SLOT_Y_APPEAL] = (2,)
                elif source.startswith("poemwinner[1]"):
                    self.domains[14] = (2,)
                elif source.startswith("poemwinner[2]"):
                    self.domains[6] = (2,)
                elif source == "persistent.yuri_kill = 1":
                    events.append({"op": "persistent_store", "state": SLOT_YURI_KILL,
                                   "key": PERSIST["yuri_kill"]})
            if source == "ac = 1.0":
                self.monika_alpha = 1.0
            return
        if source in ("y_appeal += 1", "menu_clicked += 1", "persistent.yuri_kill += 1"):
            slot = {"y": SLOT_Y_APPEAL, "m": SLOT_TEMP, "p": SLOT_YURI_KILL}[source[0]]
            events.append({"op": "add_state", "state": slot, "value": 1})
            if slot == SLOT_Y_APPEAL:
                self.domains[slot] = tuple(sorted({v + 1 for v in self.domains[slot]} & {0, 1, 2, 3}))
            elif slot == SLOT_YURI_KILL:
                events.append({"op": "persistent_store", "state": slot, "key": PERSIST["yuri_kill"]})
            return
        if source.startswith("ac += "):
            self.monika_alpha += float(source[6:])
            return
        if source in ("y_gave = True", "y_gave = False"):
            self.set_flag(FLAG_Y_GAVE, source.endswith("True"))
            return
        if source == "natsuki_23 = True":
            self.set_flag(FLAG_NATSUKI_23, True)
            return
        if source in ("faint_effect = True", "faint_effect = None"):
            self.set_flag(FLAG_FAINT, source.endswith("True"))
            return
        if source in ("skip_poem = True", "skip_poem = False"):
            self.set_flag(FLAG_SKIP_POEM, source.endswith("True"))
            return
        if source.startswith("persistent.autoload = "):
            name = source.split('"')[1]
            self.set(SLOT_TEMP, AUTOLOAD[name])
            events.append({"op": "persistent_store", "state": SLOT_TEMP, "key": PERSIST["autoload"]})
            return
        if source.startswith("persistent.playthrough = "):
            self.set(SLOT_TEMP, int(source.rsplit(" ", 1)[1]))
            events.append({"op": "persistent_store", "state": SLOT_TEMP, "key": PERSIST["playthrough"]})
            return
        if source == "persistent.seen_colors_poem = True":
            self.set(SLOT_TEMP, 1)
            events.append({"op": "persistent_store", "state": SLOT_TEMP, "key": PERSIST["seen_colors_poem"]})
            return
        if source == "delete_all_saves()":
            events.append({"op": "system", "action": 1})
            return
        delete = re.fullmatch(r'delete_character\("(\w+)"\)', source)
        if delete:
            events.append({"op": "system", "action": 2, "arg": CHARACTER_FILES[delete.group(1)]})
            return
        if source.startswith("nextscene = "):
            self.nextscene = source[len("nextscene = "):]
            return
        if source.startswith("madechoice = renpy.display_menu("):
            self.events.append({"op": "choice", "state": SLOT_CHOICE, "count": 3, "text_en": ""})
            targets = [self.fresh("rigged") for _ in range(3)]
            for value, (caption, target) in enumerate(zip(("Natsuki.", "Yuri.", "Monika."), targets)):
                self.events.append({"op": "choice_option", "value": value, "text_en": caption,
                                    "target": target})
            done = self.fresh("rigged_done")
            for target in targets:
                self.label(target)
                self.jump(done)
            self.label(done)
            self.domains[SLOT_CHOICE] = (0, 1, 2)
            return
        if source.startswith("renpy.display_menu("):
            return  # non-interactive decoy menu while Monika talks over it
        if source.startswith('renpy.call_screen("dialog"'):
            self.dialog(ast.literal_eval(source.split(",", 1)[1].rsplit(",", 1)[0].strip()))
            return
        if source.startswith("renpy.full_restart("):
            self.events.append({"op": "end", "flags": 0})
            return
        if source.startswith("renpy.file(") or source.startswith("try:") or source.startswith("os.remove("):
            return
        if source.startswith(MUSIC_PYTHON) or any(source.startswith(prefix) for prefix in MUSIC_PYTHON):
            return
        if "\n" in source:
            self.report_skip("multi-line Act 2 Python (audio/filesystem)")
            return
        super().python(node)

    def compile_pause(self, source):
        call = ast.parse(source).body[0].value
        if not call.args:
            # `$ pause()` with the quick menu off: hide the dialogue window, wait for input.
            self.events.append({"op": "window", "visible": False})
            self.events.append({"op": "pause", "duration_ms": -1})
            return
        argument = call.args[0]
        if isinstance(argument, ast.Constant):
            self.events.append({"op": "pause", "duration_ms": round(float(argument.value) * 1000)})
            return
        # pause(T - (datetime.now() - starttime).total_seconds()): absolute T.
        if isinstance(argument, ast.BinOp) and isinstance(argument.left, ast.Constant):
            absolute = float(argument.left.value)
            delta = max(0.0, absolute - getattr(self, "stab_clock", 0.0))
            self.stab_clock = absolute
            self.events.append({"op": "pause", "duration_ms": round(delta * 1000)})
            return
        raise ValueError(f"unsupported pause: {source}")

    def dialog(self, text, options=("OK",)):
        self.events.append({"op": "choice", "state": SLOT_CHOICE, "count": len(options),
                            "text_en": text})
        targets = [self.fresh("dialog") for _ in options]
        for value, (caption, target) in enumerate(zip(options, targets)):
            self.events.append({"op": "choice_option", "value": value, "text_en": caption,
                                "target": target})
        done = self.fresh("dialog_done")
        for target in targets:
            self.label(target)
            self.jump(done)
        self.label(done)

    def user_statement(self, node):
        line = node.line.strip()
        if line.startswith("show screen tear("):
            arguments = line[len("show screen tear("):-1].split(",")
            pieces = int(arguments[0])
            offset = 12
            if len(arguments) >= 5 and arguments[4].strip().isdigit():
                offset = max(2, int(arguments[4]) // 4)
            self.events.append({"op": "effect", "kind": 1, "duration_ms": 0,
                                "param": pieces | (offset << 8)})
        elif line == "hide screen tear":
            self.events.append({"op": "effect", "kind": 0})
        elif line.startswith("show screen invert("):
            self.events.append({"op": "effect", "kind": 2, "duration_ms": 300})
        elif line in ("window hide(None)", "window hide", "window hide(config.window_hide_transition)"):
            self.events.append({"op": "window", "visible": False})
        elif line == "window show(None)":
            self.events.append({"op": "window", "visible": True})
        elif line.startswith("call screen confirm("):
            message = ast.literal_eval(line[len("call screen confirm("):].split(", Return")[0])
            if "Return(False)" in line:
                self.dialog(message, ("Yes", "No"))
                # Yes is option 0; `_return` reads 1 for yes.
                yes, done = self.fresh("confirm_yes"), self.fresh("confirm_done")
                self.events.append({"op": "if_state", "state": SLOT_CHOICE, "value": 0, "target": yes})
                self.set(SLOT_CHOICE, 0)
                self.jump(done)
                self.label(yes)
                self.set(SLOT_CHOICE, 1)
                self.label(done)
            else:
                self.set(SLOT_CHOICE, 1)
            self.domains[SLOT_CHOICE] = (0, 1)
        elif line.startswith("call screen dialog("):
            self.dialog(ast.literal_eval(line[len("call screen dialog("):].split(", ok_action")[0]))
        elif line.startswith("pause"):
            self.compile_pause(line if line.startswith("pause(") else f"pause({line[5:].strip() or ''})")
        elif line.startswith(("show layer", "hide layer", "play ", "stop ", "queue ", "window auto")):
            return
        else:
            self.report_skip(f"Act 2 user statement: {line.split('(')[0]}")

    # -- images ---------------------------------------------------------------
    def resource_for(self, image):
        row = self.assets.get(image)
        if row is None or row.get("kind") not in ("screen_layer", "fullscreen_layer",
                                                  "fullscreen_background", "background"):
            return None, None
        prefix = {"background": "bg", "screen_layer": "ovl", "fullscreen_layer": "fsl",
                  "fullscreen_background": "fs"}[row["kind"]]
        return f"{prefix}_{row['id']:03d}", row

    def show_overlay(self, image, alias=None):
        name, row = self.resource_for(image)
        if name is None:
            self.report_skip(f"missing Act 2 overlay art: {' '.join(image)}")
            return
        if row["kind"] in ("fullscreen_background", "background"):
            self.events.append({"op": "scene", "resource": name})
            self.possible.clear()
            self.visible.clear()
            self.overlay_layers = set()
            return
        self.events.append({"op": "show", "resource": name, "screen_x": row.get("screen_x", 0),
                            "screen_y": row.get("screen_y", 20)})
        if row.get("opacity") is not None:
            self.events.append({"op": "transform", "resource": name, "transform_options": 0x1000,
                                "flags": int(row["opacity"])})
        self.overlay_layers.add(name)
        if alias:
            self.overlay_aliases[alias] = name

    def hide_overlay(self, image):
        tag = image[0]
        name = self.overlay_aliases.pop(tag, None)
        if name is None:
            name, _row = self.resource_for(image)
        if name is None:
            return
        self.events.append({"op": "hide", "resource": name})
        self.overlay_layers.discard(name)

    def show_y_kill(self):
        done = self.fresh("ykill_done")
        for threshold, variant in Y_KILL_THRESHOLDS:
            name, _row = self.resource_for(("y_kill", variant))
            if name is None:
                continue
            if threshold is None:
                self.events.append({"op": "scene", "resource": name})
                break
            below = self.fresh("ykill_below")
            self.set(SLOT_TEMP, threshold - 1)
            above = self.fresh("ykill_above")
            self.events.append({"op": "if_state_gt", "state": SLOT_YURI_KILL,
                                "other_state": SLOT_TEMP, "target": above})
            self.jump(below)
            self.label(above)
            self.events.append({"op": "scene", "resource": name})
            self.jump(done)
            self.label(below)
        self.label(done)
        self.possible.clear()
        self.visible.clear()

    def linear_node(self, node, script):
        node_kind = kind(node)
        spec = getattr(node, "imspec", None)
        image = tuple(spec[0]) if spec and spec[0] else ()
        alias = str(spec[2]) if spec and len(spec) > 2 and spec[2] else None
        at_list = list(spec[3]) if spec and len(spec) > 3 and spec[3] else []
        if node_kind in ("Show", "Scene") and image:
            if image[0] == "splash_warning":
                self.events.append({"op": "overlay", "text_en": image[1].strip('"'), "exclusive": True})
                return
            if image == ("y_kill",):
                self.show_y_kill()
                return
            if image[0] == "bg" and image not in self.assets and image[1] == "club_day2":
                image = ("bg", "club_day")
            row = self.assets.get(image)
            if image in (("black",), ("white",)) and node_kind == "Show":
                self.show_overlay((*image, "layer"), alias)
                return
            if row is not None and row.get("kind") in ("screen_layer", "fullscreen_layer",
                                                       "fullscreen_background"):
                self.show_overlay(image, alias)
                return
            if row is not None and row.get("kind") == "background" \
                    and image[0] != "bg" and not image[0].startswith(("n_cg", "y_cg", "s_cg")):
                # Flattened rooms (Act 3) and other full pictures shown with `show`.
                self.show_overlay(image)
                return
            if image[0] not in CHARACTER_TAGS and image[0] != "bg" and node_kind == "Show" \
                    and not image[0].startswith(("n_cg", "y_cg", "s_cg")):
                if image == ("yuri_half2",):
                    pass  # baked as a sprite; falls through to the sprite path below
                elif row is None:
                    self.report_skip(f"missing Act 2 overlay art: {' '.join(image)}")
                    return
            if node_kind == "Scene":
                self.events.append({"op": "overlay", "text_en": ""})
                self.overlay_layers = set()
        if node_kind == "Hide" and image and image[0] not in self.aliases \
                and not image[0].startswith(("n_cg", "y_cg", "s_cg")):
            if image[0] in self.overlay_aliases or image in (("black",), ("white",)) \
                    or (image[0] not in CHARACTER_TAGS and image not in self.assets) \
                    or (image in self.assets and self.assets[image].get("kind") in
                        ("screen_layer", "fullscreen_layer")):
                target = (*image, "layer") if image in (("black",), ("white",)) else image
                self.hide_overlay(target)
                return
        if node_kind == "Show" and image == ("yuri_half2",):
            start = len(self.events)
            clone = copy.copy(node)
            clone.imspec = (("yuri", "half2"), *node.imspec[1:])
            self.sprite_alias(clone, "yuri", ("yuri_half2",), script, x_shift=-72)
            return
        if node_kind == "Say" and isinstance(getattr(node, "what", None), str):
            node = copy.copy(node)
            node.what = re.sub(r"\{cps=\*(\d+)\}", lambda m: f"{{cps={55 * int(m.group(1))}}}", node.what)
        start = len(self.events)
        super().linear_node(node, script)
        if node_kind == "Say" and self.edited:
            for event in self.events[start:]:
                if event.get("op") == "say":
                    event["edited"] = True
        if node_kind == "Show" and image[:2] == ("monika", "1") and any("malpha" in str(t) for t in at_list):
            alpha = max(0, min(255, round(255 * self.monika_alpha / 40.0)))
            for event in self.events[start:]:
                if event.get("op") == "show":
                    self.events.append({"op": "transform", "resource": event["resource"],
                                        "transform_options": 0x1000, "flags": alpha})

    def sprite_alias(self, node, character, image, script, x_shift=0):
        """Show a non-character sprite resource (yuri_half2) through the sprite path."""
        row = self.assets.get(image)
        if row is None:
            self.report_skip(f"missing Act 2 sprite art: {' '.join(image)}")
            return
        center = self.centers.get(character, 160)
        for part in row["layers"]:
            self.events.append({"op": "show", "resource": part["name"],
                                "screen_x": center - part["width"] // 2 + part["x_offset"] + x_shift,
                                "screen_y": 185 - part["height"] + part["y_offset"]})
        self.overlay_layers.update(part["name"] for part in row["layers"])
        self.overlay_aliases["yuri_half2"] = row["layers"][0]["name"]

    # -- calls ----------------------------------------------------------------
    def dynamic_call(self, node, depth):
        expression = str(node.label)
        if expression.startswith('"poem_special_"'):
            index = int(expression.split("special_poems[")[1][0])
            self.special_poem(index, depth)
            return
        if expression != "nextscene":
            raise ValueError(f"unsupported Act 2 dynamic call: {expression}")
        source = getattr(self, "nextscene", "")
        done = self.fresh("dyn_done")
        before, after = self.snapshot(), []
        if source.startswith('poemwinner[0] + "_exclusive2_"'):
            branches = [('poemwinner[0] == "natsuki"', "natsuki_exclusive2_1"),
                        ('poemwinner[0] == "yuri"', "yuri_exclusive2_1")]
        elif source.startswith('"yuri_exclusive2_"'):
            branches = [(f"y_appeal == {n}", f"yuri_exclusive2_{n}_ch22") for n in (1, 2)]
        elif source.startswith('"m2_" + poemwinner[0]'):
            branches = [('poemwinner[0] == "natsuki"', "m2_natsuki_1"),
                        ('poemwinner[0] == "yuri"', "m2_yuri_1")]
        elif source.startswith('"m2_yuri_"'):
            branches = [(f"y_appeal == {n}", f"m2_yuri_{n}") for n in (1, 2, 3)]
        else:
            raise ValueError(f"unsupported nextscene: {source}")
        for condition, label in branches:
            body, onward = self.fresh("dyn_body"), self.fresh("dyn_next")
            self.condition(condition, body, onward)
            self.label(body)
            self.restore(before)
            self.invoke(label, depth)
            after.append(self.snapshot())
            self.jump(done)
            self.label(onward)
        self.jump(done)
        self.label(done)
        self.merge(after)

    def special_poem(self, index, depth):
        done = self.fresh("special_done")
        before, after = self.snapshot(), []
        self.events.append({"op": "persistent_load", "state": SLOT_TEMP, "key": PERSIST["special_0"] + index})
        for poem in range(1, 12):
            body, onward = self.fresh("special"), self.fresh("special_next")
            self.events.append({"op": "if_state", "state": SLOT_TEMP, "value": poem, "target": body})
            self.jump(onward)
            self.label(body)
            self.restore(before)
            self.invoke(f"poem_special_{poem}", depth)
            after.append(self.snapshot())
            self.jump(done)
            self.label(onward)
        self.jump(done)
        self.label(done)
        self.merge(after)

    def menu_prompt(self, node):
        return ""

    def menu(self, node, compile_branch):
        options = [(caption, cond, block) for caption, cond, block in node.items if block]
        if any(str(cond) != "True" for _, cond, _ in options):
            raise ValueError("conditional Act 2 menu")
        options = options[:10]
        before, done, after = self.snapshot(), self.fresh("menu_end"), []
        targets = [self.fresh("option") for _ in options]
        self.events.append({"op": "choice", "state": SLOT_CHOICE, "count": len(options),
                            "text_en": self.menu_prompt(node)})
        for i, ((caption, _, _), target) in enumerate(zip(options, targets)):
            self.events.append({"op": "choice_option", "value": i, "text_en": caption,
                                "target": target})
        for (_, _, block), target in zip(options, targets):
            self.label(target)
            self.restore(before)
            compile_branch(block)
            after.append(self.snapshot())
            self.jump(done)
        self.label(done)
        self.merge(after)

    def music_only(self, block):
        for child in block:
            child_kind = kind(child)
            if child_kind in ("UserStatement", "With", "Pass"):
                continue
            if child_kind == "Python" and (pycode_source(child.code) or "").strip().startswith(MUSIC_PYTHON):
                continue
            return False
        return True

    def invoke(self, name, depth=0):
        """Day 4's label expansion plus Ren'Py fall-through semantics: a nested
        label continues with its parent's remaining statements and a top-level
        label without a terminal return/jump continues with the next label."""
        if depth > 10:
            raise ValueError(f"recursive Act 2 call: {name}")
        if name not in self.labels:
            raise ValueError(f"unknown Act 2 label: {name}")
        done = self.fresh(f"return_{name}")
        ids, emitted, pending, exits = {}, set(), [], []

        def target(label_name):
            if label_name not in ids:
                ids[label_name] = self.fresh(label_name)
            return ids[label_name]

        def nodes(block, script, continuation):
            for index, node in enumerate(block):
                k = kind(node)
                if k == "Return":
                    exits.append(self.snapshot())
                    self.jump(done)
                    return True
                if k == "Jump":
                    if getattr(node, "expression", False):
                        raise ValueError("dynamic Act 2 jump")
                    self.jump(target(node.target))
                    pending.append((node.target, self.snapshot(), None))
                    return True
                if k == "Label":
                    rest = block[index + 1:]
                    self.jump(target(node.name))
                    pending.append((node.name, self.snapshot(), (rest, script, continuation)))
                    return True
                if k == "If":
                    if all(self.music_only(branch) for _, branch in node.entries):
                        continue
                    before = self.snapshot()
                    after, end = [], self.fresh("if_end")
                    for cond, branch in node.entries:
                        body, onward = self.fresh("if_body"), self.fresh("if_next")
                        self.condition(cond, body, onward)
                        self.label(body)
                        self.restore(before)
                        if not nodes(branch, script, continuation):
                            after.append(self.snapshot())
                            self.jump(end)
                        self.label(onward)
                    after.append(before)
                    self.label(end)
                    self.merge(after)
                elif k == "Menu":
                    self.menu(node, lambda b: nodes(b, script, continuation))
                elif k == "Call":
                    if getattr(node, "expression", False):
                        self.dynamic_call(node, depth + 1)
                    elif node.label == "showpoem":
                        arguments = getattr(node.arguments, "arguments", [])
                        poem_name = next(value for key, value in arguments if key is None)
                        event = {"op": "poem", "poem": poem_name, "script": script,
                                 "line": node.linenumber}
                        for key, value in arguments:
                            if key == "paper":
                                event["paper"] = ast.literal_eval(value).rsplit("/", 1)[-1].split(".")[0]
                        self.events.append(event)
                        for argument, value in arguments:
                            if argument == "img":
                                pose = type("Show", (), {})()
                                pose.imspec = (tuple(ast.literal_eval(value).split()), None, None, [], None, None, [])
                                pose.linenumber = node.linenumber
                                self.linear_node(pose, script)
                    elif node.label in ("updateconsole", "hideconsole", "updateconsole_clearall"):
                        continue
                    elif node.label == "poem":
                        self.act3_poem()
                    else:
                        self.invoke(node.label, depth + 1)
                elif k == "Python":
                    self.python(node)
                elif k == "UserStatement":
                    self.user_statement(node)
                elif k in ("With", "Pass", "Init", "Image", "Default"):
                    continue
                elif k in ("Scene", "Show", "Hide", "Say", "ShowLayer"):
                    if k != "ShowLayer":
                        self.linear_node(node, script)
                else:
                    raise ValueError(f"unsupported Act 2 node {k} at {script}:{node.linenumber}")
            return False

        def finish(label_name, script, continuation):
            # Block ended without return/jump: nested labels resume the parent,
            # top-level labels fall through to the next label in the file.
            if continuation is not None:
                rest, parent_script, outer = continuation
                if nodes(rest, parent_script, outer):
                    return
                finish(label_name, parent_script, outer)
                return
            following = self.next_label.get((script, label_name))
            if following is not None:
                self.jump(target(following))
                pending.append((following, self.snapshot(), None))
                return
            exits.append(self.snapshot())
            self.jump(done)

        script, source = self.labels[name]
        emitted.add(name)
        self.label(target(name))
        if not nodes(source.block, script, None):
            finish(name, script, None)
        while pending:
            label_name, snap, continuation = pending.pop(0)
            if label_name in emitted:
                continue
            if label_name not in self.labels:
                raise ValueError(f"unknown Act 2 jump destination: {label_name}")
            incoming = [s for label, s, _c in pending if label == label_name]
            pending[:] = [entry for entry in pending if entry[0] != label_name]
            self.merge([snap, *incoming])
            script, source = self.labels[label_name]
            emitted.add(label_name)
            self.label(target(label_name))
            if label_name == "yuri_kill_loop":
                self.show_y_kill()  # ConditionSwitch re-evaluates live in Ren'Py
            if not nodes(source.block, script, continuation):
                finish(label_name, script, continuation)
        self.label(done)
        if exits:
            self.merge(exits)  # a label that only ever jumps away (Act 3's loop) has no exit


# -- Act 2 assembly ------------------------------------------------------------

def act2_poem_round(compiler, chapter, word_rows, store_opinion):
    """Twenty word pages; Natsuki wins ties against Yuri only on a strictly
    higher score (`if nPointTotal > yPointTotal`), Sayori is absent."""
    events = compiler.events
    prefix = f"act2_poem{chapter}"
    compiler.label(f"{prefix}_prepare")
    for slot in (3, 4, 5):
        compiler.set(slot, 0)
    events.append({"op": "say", "speaker_en": "",
                   "text_en": "It's time to write a poem." if chapter == 0 else "It's time to write another poem."})
    words = list(word_rows)
    random.Random(f"DDLC Nspire poem word board act2 chapter {chapter}").shuffle(words)
    for round_index in range(20):
        compiler.label(f"{prefix}_words_{round_index}")
        page = words[round_index * 10:(round_index + 1) * 10]
        events.append({"op": "choice", "state": SLOT_WORD, "count": 10,
                       "text_en": f"Choose a word ({round_index + 1}/20)."})
        for option, (word, _) in enumerate(page):
            events.append({"op": "choice_option", "value": option, "text_en": word,
                           "target": f"{prefix}_pick_{round_index}_{option}"})
        for option, (_, points) in enumerate(page):
            compiler.label(f"{prefix}_pick_{round_index}_{option}")
            for slot, score in zip((3, 4, 5), points):
                events.append({"op": "add_state", "state": slot, "value": score})
            compiler.jump(f"{prefix}_words_{round_index + 1}" if round_index < 19 else f"{prefix}_winner")
    compiler.label(f"{prefix}_winner")
    winner_slot = (0, 14, 6)[chapter]
    events.append({"op": "if_state_gt", "state": 4, "other_state": 5, "target": f"{prefix}_natsuki"})
    compiler.set(winner_slot, 2)
    events.append({"op": "add_state", "state": SLOT_Y_APPEAL, "value": 1})
    compiler.jump(f"{prefix}_opinions")
    compiler.label(f"{prefix}_natsuki")
    compiler.set(winner_slot, 1)
    events.append({"op": "add_state", "state": SLOT_N_APPEAL, "value": 1})
    compiler.label(f"{prefix}_opinions")
    for value, girl, score_slot, opinion_slot in ((1, "natsuki", 4, (9, 12, 4)[chapter]),
                                                  (2, "yuri", 5, (10, 13, 5)[chapter])):
        store_opinion(score_slot, winner_slot, value, opinion_slot,
                      f"{prefix}_{girl}_opinion", f"{prefix}_next_{girl}", 60)
    # Poem-game easter egg: once per persistent file, a 1-in-6 glimpse of the eyes.
    eyes, _row = compiler.resource_for(("bg", "eyes"))
    if eyes:
        skip = f"{prefix}_no_eyes"
        events.append({"op": "persistent_load", "state": SLOT_TEMP, "key": PERSIST["seen_eyes"]})
        events.append({"op": "if_state", "state": SLOT_TEMP, "value": 1, "target": skip})
        events.append({"op": "rand_state", "state": SLOT_TEMP, "count": 6, "mask_state": -1})
        events.append({"op": "if_state", "state": SLOT_TEMP, "value": 0, "target": f"{prefix}_eyes"})
        compiler.jump(skip)
        compiler.label(f"{prefix}_eyes")
        compiler.set(SLOT_TEMP, 1)
        events.append({"op": "persistent_store", "state": SLOT_TEMP, "key": PERSIST["seen_eyes"]})
        events.append({"op": "scene", "resource": eyes})
        events.append({"op": "pause", "duration_ms": 1200})
        black, _row = compiler.resource_for(("black",))
        events.append({"op": "scene", "resource": black})
        events.append({"op": "pause", "duration_ms": 500})
        events.append({"op": "scene", "resource": eyes})
        events.append({"op": "pause", "duration_ms": 1250})
        events.append({"op": "scene", "resource": black})
        compiler.label(skip)
    compiler.domains[winner_slot] = (1, 2)
    compiler.domains[(9, 12, 4)[chapter]] = compiler.domains[(10, 13, 5)[chapter]] = (-1, 0, 1)


def act2_sharing(compiler, chapter, assets):
    """Act 2 poem sharing: Natsuki, Yuri (unless she ran away) and Monika, three reads."""
    events = compiler.events
    prefix = f"act2_share{chapter}"
    compiler.chapter = chapter
    club_day = next(name for name, row in assets.items() if name == ("bg", "club_day"))
    compiler.label(f"{prefix}_start")
    compiler.set(SLOT_MASK, 0)
    compiler.update_flag(FLAG_SKIP_POEM, False)
    compiler.jump(f"{prefix}_dispatch")
    compiler.label(f"{prefix}_dispatch")
    compiler.linear_node(type("Scene", (), {"imspec": (club_day,), "linenumber": 0})(),
                         "script-poemresponses.rpyc")
    compiler.possible.clear()
    compiler.visible.clear()
    for mask in range(16):
        complete = bin(mask & 14).count("1") >= 3
        events.append({"op": "if_state", "state": SLOT_MASK, "value": mask,
                       "target": f"{prefix}_done" if complete else f"{prefix}_order_{mask}"})
    compiler.jump(f"{prefix}_done")
    recipients = (("Natsuki", "natsuki", 1), ("Yuri", "yuri", 2), ("Monika", "monika", 3))
    for mask in range(16):
        if bin(mask & 14).count("1") >= 3:
            continue
        compiler.label(f"{prefix}_order_{mask}")
        remaining = [r for r in recipients if not mask & (1 << r[2])]
        prompt = "Who should I show my poem to first?" if not mask & 14 else "Who should I show my poem to next?"

        def emit_menu(options):
            events.append({"op": "choice", "state": SLOT_MASK, "count": len(options), "text_en": prompt})
            for display, girl, bit in options:
                events.append({"op": "choice_option", "value": mask | (1 << bit),
                               "text_en": display, "target": f"{prefix}_read_{girl}"})
        if chapter == 1 and not mask & 8 and any(r[1] == "yuri" for r in remaining):
            # Yuri ran away in her Act 2 exclusive until Monika's comment; she
            # cannot be picked before Monika when she won the first poem.
            ran = f"{prefix}_order_{mask}_ranaway"
            events.append({"op": "if_state", "state": 0, "value": 2, "target": ran})
            emit_menu(remaining)
            compiler.label(ran)
            emit_menu([r for r in remaining if r[1] != "yuri"])
        else:
            emit_menu(remaining)
    for display, girl, bit in recipients:
        compiler.label(f"{prefix}_read_{girl}")
        compiler.possible.clear()
        compiler.visible.clear()
        compiler.update_flag(FLAG_SKIP_POEM, False)
        start = len(events)
        script, wrapper = compiler.labels[f"poemresponse_{girl}"]
        for node in wrapper.block[:3]:
            compiler.linear_node(node, script)
        if chapter == 1:
            first = f"{prefix}_first_{girl}"
            events.append({"op": "if_state", "state": SLOT_MASK, "value": 1 << bit, "target": first})
            compiler.jump(f"{prefix}_response_{girl}")
            compiler.label(first)
            thoughts = {"natsuki": (39, 40), "yuri": (45, 46), "monika": (51, 52)}[girl]
            from source_text import SourceText
            local_text = SourceText(compiler.raw_dir)
            for line in thoughts:
                events.append({"op": "say", "speaker_en": "",
                               "text_en": local_text.say("script-poemresponses.rpyc", line),
                               "script": "script-poemresponses.rpyc", "line": line})
            compiler.label(f"{prefix}_response_{girl}")
        compiler.response_girl = girl
        if girl == "monika":
            compiler.invoke(f"ch2{chapter}_m_start")
        else:
            slot = {"natsuki": (9, 12, 4), "yuri": (10, 13, 5)}[girl][chapter - 1]
            before, after = compiler.snapshot(), []
            done = compiler.fresh("reaction_end")
            for tier, opinion in (("bad", -1), ("med", 0), ("good", 1)):
                if f"ch2{chapter}_{girl[0]}_{tier}" not in compiler.labels:
                    continue  # ch22_main2 downgrades Natsuki's "good" to "med" beforehand
                body, onward = compiler.fresh("reaction"), compiler.fresh("reaction_next")
                events.append({"op": "if_state", "state": slot, "value": opinion, "target": body})
                compiler.jump(onward)
                compiler.label(body)
                compiler.restore(before)
                compiler.invoke(f"ch2{chapter}_{girl[0]}_{tier}")
                after.append(compiler.snapshot())
                compiler.jump(done)
                compiler.label(onward)
            compiler.jump(done)
            compiler.label(done)
            compiler.merge(after)
        end, skip = compiler.fresh("poem_end"), compiler.fresh("poem_skip")
        compiler.condition("skip_poem", skip, end)
        compiler.label(end)
        compiler.invoke(f"ch2{chapter}_{girl[0]}_end")
        compiler.label(skip)
        layers = {e["resource"] for e in events[start:] if e.get("op") == "show"}
        for image_set in compiler.possible.values():
            for image in image_set:
                layers.update(p["name"] for p in assets[image]["layers"]) if image in assets else None
        for name in sorted(layers, reverse=True):
            events.append({"op": "hide", "resource": name})
        compiler.possible.clear()
        compiler.visible.clear()
        compiler.overlay_layers = set()
        compiler.jump(f"{prefix}_dispatch")
    compiler.label(f"{prefix}_done")
    compiler.set(SLOT_MASK, 0)


def extend_act2(previous):
    compiler = Act2Compiler.__new__(Act2Compiler)
    compiler.__dict__.update(previous.__dict__)
    compiler.edited = False
    compiler.overlay_layers = set()
    compiler.overlay_aliases = {}
    compiler.monika_alpha = 1.0
    compiler.next_label = {}
    for filename in ACT2_FILES:
        _, top = load_rpyc(compiler.raw_dir / filename)
        previous_name = None
        for node in flatten(top):
            if kind(node) == "Label":
                compiler.labels[node.name] = (filename, node)
        for node in top:
            if kind(node) == "Label":
                if previous_name is not None:
                    compiler.next_label[(filename, previous_name)] = node.name
                previous_name = node.name
    events = compiler.events
    assets = compiler.assets
    store_opinion = previous.store_opinion
    word_rows = previous.word_rows
    speakers = speakers_table()

    def reset_slots():
        for slot in range(16):
            compiler.set(slot, 0 if slot in (SLOT_MASK, SLOT_FLAGS, SLOT_N_APPEAL, SLOT_Y_APPEAL, SLOT_TEMP) else -1)
        compiler.domains.update({0: (1, 2), 14: (1, 2), 6: (1, 2), SLOT_N_APPEAL: (0, 1, 2, 3),
                                 SLOT_Y_APPEAL: (0, 1, 2, 3), SLOT_FLAGS: tuple(range(32)),
                                 SLOT_MASK: tuple(range(16)), SLOT_TEMP: (0, 1)})
        compiler.domains.update({s: (-1, 0, 1) for s in (3, 4, 5, 8, 9, 10, 11, 12, 13)})
        compiler.domains[SLOT_Y_APPEAL] = (0, 1, 2, 3)
        compiler.domains[SLOT_TEMP] = (0, 1)
        compiler.possible.clear()
        compiler.visible.clear()
        compiler.overlay_layers = set()
        compiler.overlay_aliases = {}

    def choose_special_poems():
        done = compiler.fresh("specials_ready")
        events.append({"op": "persistent_load", "state": SLOT_TEMP, "key": PERSIST["special_0"]})
        for value in range(1, 12):
            events.append({"op": "if_state", "state": SLOT_TEMP, "value": value, "target": done})
        compiler.set(SLOT_WORD, 0)
        for index in range(3):
            events.append({"op": "rand_state", "state": SLOT_TEMP, "count": 11, "mask_state": SLOT_WORD})
            events.append({"op": "add_state", "state": SLOT_TEMP, "value": 1})
            events.append({"op": "persistent_store", "state": SLOT_TEMP, "key": PERSIST["special_0"] + index})
        compiler.label(done)

    # -- New Game entry ---------------------------------------------------------
    compiler.label("act_dispatch")
    # splash.rpyc restores character files per playthrough: monika 1, natsuki 2, sayori 4, yuri 8.
    events.append({"op": "persistent_load", "state": SLOT_TEMP, "key": PERSIST["playthrough"]})
    for value, mask in ((0, 15), (1, 11), (2, 1), (3, 0), (4, 14)):
        skip = compiler.fresh("restore_next")
        hit = compiler.fresh("restore")
        events.append({"op": "if_state", "state": SLOT_TEMP, "value": value, "target": hit})
        compiler.jump(skip)
        compiler.label(hit)
        if mask:
            events.append({"op": "system", "action": 3, "arg": mask})
        compiler.jump("act_dispatch_autoload")
        compiler.label(skip)
    compiler.label("act_dispatch_autoload")
    events.append({"op": "persistent_load", "state": SLOT_TEMP, "key": PERSIST["autoload"]})
    for value, target in ((1, "act2_autoload_kill1"), (2, "act2_autoload_kill2"),
                          (3, "act2_autoload_kill3"), (4, "act3_start"), (5, "act3_entry_5"),
                          (6, "act3_entry_6"), (7, "act3_entry_7"), (8, "act3_entry_8"),
                          (9, "act3_entry_9"), (10, "act4_credits"), (11, "act4_postcredits")):
        events.append({"op": "if_state", "state": SLOT_TEMP, "value": value, "target": target})
    events.append({"op": "persistent_load", "state": SLOT_TEMP, "key": PERSIST["playthrough"]})
    for value, target in ((1, "act2_ch10"), (2, "act2_ch20"), (3, "act3_start"),
                          (4, "act4_start")):
        events.append({"op": "if_state", "state": SLOT_TEMP, "value": value, "target": target})
    compiler.jump("act1_start")

    # -- Act 2 start (ch10 glitched restart or ch20 from the menu) -----------
    for entry, label in (("act2_ch10", "ch10_main"), ("act2_ch20", "ch20_main")):
        compiler.label(entry)
        reset_slots()
        choose_special_poems()
        speakers.update({"s": "???", "n": "Girl 2", "y": "Girl 1", "m": "???"})
        compiler.replacements.update({"s_name": "???", "n_name": "Girl 2", "y_name": "Girl 1", "m_name": "???"})
        compiler.chapter = 0
        compiler.invoke(label)
        speakers.update({"s": "Sayori", "n": "Natsuki", "y": "Yuri", "m": "Monika"})
        compiler.replacements.update({"s_name": "Sayori", "n_name": "Natsuki", "y_name": "Yuri", "m_name": "Monika"})
        compiler.jump("act2_after_ch20")

    # -- The club week ------------------------------------------------------------
    compiler.label("act2_after_ch20")
    for names in compiler.character_layers.values():
        for name in sorted(names, reverse=True):
            events.append({"op": "hide", "resource": name})
    act2_poem_round(compiler, 0, word_rows, store_opinion)
    compiler.chapter = 1
    compiler.invoke("ch21_main")
    act2_sharing(compiler, 1, assets)
    compiler.chapter = 1
    compiler.invoke("ch21_end")
    act2_poem_round(compiler, 1, word_rows, store_opinion)
    compiler.chapter = 2
    compiler.invoke("ch22_main")
    act2_sharing(compiler, 2, assets)
    compiler.chapter = 2
    compiler.invoke("ch22_end")
    act2_poem_round(compiler, 2, word_rows, store_opinion)
    compiler.chapter = 3
    compiler.invoke("ch23_main")
    act2_sharing(compiler, 3, assets)
    compiler.chapter = 3
    compiler.invoke("ch23_end")
    events.append({"op": "end", "flags": 0})

    # -- Weekend autoload entries ------------------------------------------------
    for value, label in ((1, "yuri_kill_1"), (2, "yuri_kill_2"), (3, "yuri_kill_3")):
        compiler.label(f"act2_autoload_kill{value}")
        reset_slots()
        compiler.chapter = 3
        events.append({"op": "persistent_load", "state": SLOT_YURI_KILL, "key": PERSIST["yuri_kill"]})
        if value == 2:
            events.append({"op": "add_state", "state": SLOT_YURI_KILL, "value": 200})
            events.append({"op": "persistent_store", "state": SLOT_YURI_KILL, "key": PERSIST["yuri_kill"]})
        compiler.invoke(label)
        events.append({"op": "end", "flags": 0})
    from compile_act3 import extend_act3
    extend_act3(compiler)
