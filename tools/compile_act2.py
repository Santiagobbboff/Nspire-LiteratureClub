"""Translate DDLC Act 2 (ch10, ch20-ch23, second poem responses, Yuri/Natsuki
exclusives, special poems and the weekend) into the 16-slot event VM.

Only explicitly supported statements are translated; no game Python runs.
"""
from __future__ import annotations

from compile_day6 import Day6Compiler

# A subset of glitchtext.rpyc's non-ASCII alphabet; the runtime draws
# `{glitch=N}` as N random glyphs from this string.
GLITCH_ALPHABET = ("¡¢£¤¥¦§©«¬®°±µ¶»¿ÀÃÆÇÐÑØÞßãæçðñøþÿĀăĄćČđĒěĞğĦıĳĸŁłŊŒœŞšŦŧŬŵŹž")


class Act2Compiler(Day6Compiler):
    def fresh(self, hint):
        self.serial += 1
        return f"act2_{hint}_{self.serial}"


def extend_act2(previous):
    compiler = Act2Compiler.__new__(Act2Compiler)
    compiler.__dict__.update(previous.__dict__)
    events = compiler.events
    # New Game entry: route by persistent.playthrough (key 0).
    compiler.label("act_dispatch")
    events.append({"op": "system", "action": 3})  # characters/*.chr beside the pack
    events.append({"op": "persistent_load", "state": 15, "key": 0})
    for value, target in ((1, "act2_placeholder"), (2, "act2_placeholder"),
                          (3, "act3_placeholder"), (4, "act3_placeholder")):
        events.append({"op": "if_state", "state": 15, "value": value, "target": target})
    compiler.jump("act1_start")
    compiler.label("act2_placeholder")
    events.append({"op": "say", "speaker_en": "", "text_en": "Act 2 is not compiled into this pack yet. {glitch=24}"})
    events.append({"op": "end", "flags": 0})
    compiler.label("act3_placeholder")
    events.append({"op": "say", "speaker_en": "", "text_en": "Act 3 is not part of this port yet."})
    events.append({"op": "end", "flags": 0})
