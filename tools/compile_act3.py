"""Translate DDLC Act 3 (script-ch30: Monika's room, her topics, and her deletion).

Monika's room is a flattened background, her lines are ordinary dialogue, the
topic list lives in four persistent bit masks, and the launcher checks whether
`characters/monika.chr.tns` still exists when the story resumes and before each
new topic. Deleting that file with the calculator's document browser and
relaunching is what moves the story on, as in the original.
"""
from __future__ import annotations

import random
import re

from compile_act2 import (Act2Compiler, AUTOLOAD, PERSIST, SLOT_CHOICE, SLOT_TEMP, SLOT_WORD,
                          speakers_table)
from rpyc_ast import kind, pycode_source

AUTOLOAD.update({"ch30_main2": 5, "ch30_postpoem": 6, "ch30_autoload": 7, "ch30_end": 8,
                 "ch30_end_2": 9, "credits": 10})
PERSIST.update({"monika_reload": 11, "current_monikatopic": 12, "monika_kill": 13,
                "tried_skip": 14, "monika_back": 15, "topics_0": 16, "topics_1": 17,
                "topics_2": 18, "topics_3": 19})
SLOT_RELOAD, SLOT_TOPIC, SLOT_KILL, SLOT_SCRATCH = 7, 11, 12, 3
TOPIC_COUNT = 56
EXCLUDED_TOPICS = (14, 25, 26)
COLORS_TOPIC = 27
GROUP_SIZES = (16, 16, 16, 8)


def topic_masks(include_colors):
    masks = [0, 0, 0, 0]
    for topic in EXCLUDED_TOPICS + (() if include_colors else (COLORS_TOPIC,)):
        masks[(topic - 1) // 16] |= 1 << ((topic - 1) % 16)
    return masks


def signed16(value):
    return value - 0x10000 if value >= 0x8000 else value


class Act3Compiler(Act2Compiler):
    def fresh(self, hint):
        self.serial += 1
        return f"act3_{hint}_{self.serial}"

    def binding(self, name):
        if name == "monika_kill":
            return (SLOT_KILL,), lambda values: bool(values[SLOT_KILL])
        if name == "autoload":
            return (SLOT_TEMP,), lambda values: values[SLOT_TEMP]
        if name == "waittime":
            return (SLOT_TEMP,), lambda values: values[SLOT_TEMP]
        if name == "monika_reload":
            return (SLOT_RELOAD,), lambda values: values[SLOT_RELOAD]
        if name == "topic":
            return (SLOT_TOPIC,), lambda values: values[SLOT_TOPIC]
        return super().binding(name)

    def constant_substitutions(self, expression):
        text = super().constant_substitutions(expression)
        text = text.replace("not list(set(process_list).intersection(stream_list))", "False")
        text = text.replace("list(set(process_list).intersection(stream_list))", "False")
        text = re.sub(r'currentuser != "" and currentuser\.lower\(\) != player\.lower\(\)', "False", text)
        text = text.replace("persistent.steam", "False").replace("renpy.macintosh", "False")
        text = text.replace("persistent.monika_kill", "monika_kill")
        text = text.replace("persistent.monika_reload", "monika_reload")
        text = text.replace("persistent.current_monikatopic", "topic")
        text = re.sub(r'persistent\.autoload == "(\w+)"', lambda m: f"autoload == {AUTOLOAD[m.group(1)]}", text)
        return text

    def condition(self, expression, yes, no):
        text = self.constant_substitutions(expression)
        if text.strip() == "waittime > 0":
            self.set(SLOT_SCRATCH, 0)
            self.events.append({"op": "if_state_gt", "state": SLOT_TEMP, "other_state": SLOT_SCRATCH,
                                "target": yes})
            self.jump(no)
            return
        if text.strip().startswith("autoload =="):
            self.domains[SLOT_TEMP] = tuple(range(11))
        if text.strip() == "monika_kill":
            self.domains[SLOT_KILL] = (0, 1)
        super().condition(text, yes, no)

    def store(self, slot, key):
        self.events.append({"op": "persistent_store", "state": slot, "key": PERSIST[key]})

    def load(self, slot, key):
        self.events.append({"op": "persistent_load", "state": slot, "key": PERSIST[key]})

    def check_monika_file(self):
        """Jump to the deletion scene when characters/monika.chr.tns is gone."""
        self.events.append({"op": "system", "action": 4, "arg": SLOT_SCRATCH, "arg2": 0})
        self.events.append({"op": "if_state", "state": SLOT_SCRATCH, "value": 0,
                            "target": "act3_monika_deleted"})

    def reset_topics(self):
        done = self.fresh("topics_ready")
        self.load(SLOT_SCRATCH, "seen_colors_poem")
        with_colors, without = self.fresh("topics_colors"), self.fresh("topics_nocolors")
        self.events.append({"op": "if_state", "state": SLOT_SCRATCH, "value": 1, "target": with_colors})
        self.label(without)
        for group, mask in enumerate(topic_masks(False)):
            self.set(SLOT_SCRATCH, signed16(mask))
            self.store(SLOT_SCRATCH, f"topics_{group}")
        self.jump(done)
        self.label(with_colors)
        for group, mask in enumerate(topic_masks(True)):
            self.set(SLOT_SCRATCH, signed16(mask))
            self.store(SLOT_SCRATCH, f"topics_{group}")
        self.label(done)

    def pick_topic(self):
        """persistent.current_monikatopic = random.choice(remaining topics)."""
        events = self.events
        retry = self.fresh("topic_pick")
        self.label(retry)
        # Exhausted groups form a mask in the scratch slot; all four -> start over.
        self.set(SLOT_SCRATCH, 0)
        for group, size in enumerate(GROUP_SIZES):
            self.load(SLOT_TEMP, f"topics_{group}")
            skip = self.fresh("group_open")
            full = signed16((1 << size) - 1)
            events.append({"op": "if_state", "state": SLOT_TEMP, "value": full, "target": self.fresh("group_full")})
            full_label = events[-1]["target"]
            self.jump(skip)
            self.label(full_label)
            events.append({"op": "add_state", "state": SLOT_SCRATCH, "value": 1 << group})
            self.label(skip)
        refill = self.fresh("topics_refill")
        events.append({"op": "if_state", "state": SLOT_SCRATCH, "value": 15, "target": refill})
        events.append({"op": "rand_state", "state": SLOT_TEMP, "count": 4, "mask_state": SLOT_SCRATCH})
        done = self.fresh("topic_done")
        for group, size in enumerate(GROUP_SIZES):
            in_group = self.fresh("in_group")
            events.append({"op": "if_state", "state": SLOT_TEMP, "value": group, "target": in_group})
            after = self.fresh("after_group")
            self.jump(after)
            self.label(in_group)
            self.load(SLOT_SCRATCH, f"topics_{group}")
            events.append({"op": "rand_state", "state": SLOT_TEMP, "count": size, "mask_state": SLOT_SCRATCH})
            self.store(SLOT_SCRATCH, f"topics_{group}")
            for index in range(size):
                chosen = self.fresh("chosen")
                events.append({"op": "if_state", "state": SLOT_TEMP, "value": index, "target": chosen})
                nxt = self.fresh("not_chosen")
                self.jump(nxt)
                self.label(chosen)
                self.set(SLOT_TOPIC, group * 16 + index + 1)
                self.jump(done)
                self.label(nxt)
            self.jump(done)
            self.label(after)
        self.jump(done)
        self.label(refill)
        self.reset_topics()
        self.jump(retry)
        self.label(done)
        self.store(SLOT_TOPIC, "current_monikatopic")

    def topic_dispatch(self, depth):
        """call expression "ch30_" + str(persistent.current_monikatopic): every
        topic continues with `jump ch30_loop`, so one shared block serves both
        the loop and the launch-resume path."""
        self.jump("act3_topics")
        return

    def emit_topics(self, depth=0):
        done = self.fresh("topic_dispatch_done")
        before, after = self.snapshot(), []
        for topic in range(1, TOPIC_COUNT + 1):
            name = f"ch30_{topic}"
            if name not in self.labels:
                continue
            body, onward = self.fresh("topic"), self.fresh("topic_next")
            self.events.append({"op": "if_state", "state": SLOT_TOPIC, "value": topic, "target": body})
            self.jump(onward)
            self.label(body)
            self.restore(before)
            self.invoke(name, depth)
            after.append(self.snapshot())
            self.jump(done)
            self.label(onward)
        self.jump(done)
        self.label(done)
        self.merge(after)

    def act3_poem(self):
        """Act 3's poem game: every word is a broken "Monika"; nothing is scored."""
        events = self.events
        rng = random.Random("DDLC Nspire act 3 poem")
        from compile_act2 import GLITCH_ALPHABET
        prefix = self.fresh("mpoem")
        events.append({"op": "say", "speaker_en": "", "text_en": "It's time to write a poem."})
        for page in range(20):
            self.label(f"{prefix}_page_{page}")
            events.append({"op": "choice", "state": SLOT_WORD, "count": 10,
                           "text_en": f"Pick a word ({page + 1}/20)."})
            targets = [self.fresh("mword") for _ in range(10)]
            for option, target in enumerate(targets):
                letters = list("Monika")
                for index in range(6):
                    roll = rng.randint(0, 4)
                    if roll == 0:
                        letters[index] = " "
                    elif rng.randint(0, 4) == 0:
                        letters[index] = rng.choice(GLITCH_ALPHABET)
                events.append({"op": "choice_option", "value": option, "text_en": "".join(letters),
                               "target": target})
            for target in targets:
                self.label(target)
                self.jump(f"{prefix}_page_{page + 1}" if page < 19 else f"{prefix}_done")
        self.label(f"{prefix}_done")

    def python(self, node):
        source = (pycode_source(node.code) or "").strip()
        if "\n" in source:
            if 'renpy.file("../characters/monika.chr")' in source:
                self.check_monika_file()
                return
            if "persistent.current_monikatopic = random.choice" in source:
                self.pick_topic()
                return
            if "persistent.monika_back" in source:
                # Act 4: Monika's file came back -> her "stop playing with my heart" dialog.
                self.events.append({"op": "system", "action": 4, "arg": SLOT_SCRATCH, "arg2": 0})
                absent = self.fresh("monika_absent")
                self.events.append({"op": "if_state", "state": SLOT_SCRATCH, "value": 0, "target": absent})
                self.dialog("Please stop playing with my heart.\nI don't want to come back.")
                self.label(absent)
                return
            self.report_skip("multi-line Act 3 Python (platform/stream checks)")
            return
        assignments = {
            "persistent.monika_reload = 0": (SLOT_RELOAD, 0, "monika_reload"),
            "persistent.yuri_kill = 0": (SLOT_SCRATCH, 0, "yuri_kill"),
            "persistent.monika_kill = False": (SLOT_KILL, 0, "monika_kill"),
            "persistent.monika_kill = True": (SLOT_KILL, 1, "monika_kill"),
            "persistent.current_monikatopic = 0": (SLOT_TOPIC, 0, "current_monikatopic"),
            "persistent.current_monikatopic = 1": (SLOT_TOPIC, 1, "current_monikatopic"),
            "persistent.autoload = None": (SLOT_TEMP, 0, "autoload"),
            "persistent.monika_back = True": (SLOT_SCRATCH, 1, "monika_back"),
        }
        if source in assignments:
            slot, value, key = assignments[source]
            self.set(slot, value)
            self.store(slot, key)
            if slot == SLOT_KILL:
                self.domains[SLOT_KILL] = (value,)
            return
        if source == "persistent.monikatopics = []":
            self.reset_topics()
            return
        if source == "persistent.monika_reload += 1":
            self.events.append({"op": "add_state", "state": SLOT_RELOAD, "value": 1})
            self.store(SLOT_RELOAD, "monika_reload")
            return
        if source == "waittime = renpy.random.randint(4, 8)":
            self.events.append({"op": "rand_state", "state": SLOT_TEMP, "count": 5, "mask_state": -1})
            self.events.append({"op": "add_state", "state": SLOT_TEMP, "value": 4})
            return
        if source == "waittime -= 1":
            self.events.append({"op": "add_state", "state": SLOT_TEMP, "value": -1})
            return
        if source == "_window_hide(None)":
            self.events.append({"op": "window", "visible": False})
            return
        if source == "style.say_dialogue = style.default_monika":
            self.edited = False
            return
        if source.startswith(("persistent.tried_skip", "persistent.clear[", "m.display_args",
                              "m.what_args", "style.say_window", "stream_list =",
                              "config.allow_skipping", "persistent.clearall", "lockedtext",
                              "if all(clear")):
            return
        super().python(node)

    def user_statement(self, node):
        line = node.line.strip()
        if line.startswith(("show screen fake_skip", "hide screen fake_skip")):
            return
        super().user_statement(node)

    def dynamic_call(self, node, depth):
        expression = str(node.label)
        if expression.startswith('"ch30_" + str(persistent.current_monikatopic)'):
            self.topic_dispatch(depth)
            return
        if expression.startswith('"ch30_reload_"'):
            done = self.fresh("reload_done")
            before, after = self.snapshot(), []
            for value in range(5):
                body, onward = self.fresh("reload"), self.fresh("reload_next")
                self.events.append({"op": "if_state", "state": SLOT_RELOAD, "value": value, "target": body})
                self.jump(onward)
                self.label(body)
                self.restore(before)
                self.invoke(f"ch30_reload_{value}", depth)
                after.append(self.snapshot())
                self.jump(done)
                self.label(onward)
            self.restore(before)
            self.invoke("ch30_reload_4", depth)  # monika_reload > 4
            after.append(self.snapshot())
            self.jump(done)
            self.label(done)
            self.merge(after)
            return
        super().dynamic_call(node, depth)


def extend_act3(previous):
    compiler = Act3Compiler.__new__(Act3Compiler)
    compiler.__dict__.update(previous.__dict__)
    events = compiler.events
    speakers = speakers_table()
    compiler.replacements["basedir"] = "Games/ddlc"
    compiler.replacements["currentuser"] = "you"
    # Nested labels in ch30 (none) and ch40 are registered by extend_act2's loader.

    def enter(label_name, autoload_code):
        for slot in range(16):
            compiler.set(slot, 0)
        compiler.domains.update({SLOT_KILL: (0, 1), SLOT_RELOAD: tuple(range(6)), SLOT_TEMP: tuple(range(11)),
                                 SLOT_TOPIC: tuple(range(57)), 2: tuple(range(32))})
        compiler.possible.clear()
        compiler.visible.clear()
        compiler.overlay_layers = set()
        compiler.overlay_aliases = {}
        compiler.chapter = 0
        compiler.load(SLOT_RELOAD, "monika_reload")
        compiler.load(SLOT_TOPIC, "current_monikatopic")
        compiler.load(SLOT_KILL, "monika_kill")
        compiler.set(SLOT_TEMP, autoload_code)
        speakers["m"] = "Monika"
        compiler.replacements["m_name"] = "Monika"

    # Entry from New Game (playthrough 3) and from autoload codes 4-9.
    entries = (("act3_start", "ch30_main", 4), ("act3_entry_5", "ch30_main2", 5),
               ("act3_entry_6", "ch30_postpoem", 6), ("act3_entry_7", "ch30_autoload", 7),
               ("act3_entry_8", "ch30_end", 8), ("act3_entry_9", "ch30_end_2", 9))
    for entry, label, code in entries:
        compiler.label(entry)
        enter(label, code)
        if label in ("ch30_main", "ch30_main2", "ch30_postpoem", "ch30_autoload"):
            compiler.check_monika_file()
        compiler.edited = False
        compiler.invoke(label)
        events.append({"op": "end", "flags": 0})

    compiler.label("act3_topics")
    compiler.possible.clear()
    compiler.visible.clear()
    compiler.emit_topics()
    compiler.jump("act3_loop")
    compiler.label("act3_loop")
    compiler.possible.clear()
    compiler.visible.clear()
    compiler.invoke("ch30_loop")
    events.append({"op": "end", "flags": 0})

    compiler.label("act3_monika_deleted")
    compiler.possible.clear()
    compiler.visible.clear()
    compiler.overlay_layers = set()
    compiler.overlay_aliases = {}
    compiler.invoke("ch30_end")
    events.append({"op": "end", "flags": 0})
    speakers["m"] = "Monika"
    compiler.replacements["m_name"] = "Monika"
