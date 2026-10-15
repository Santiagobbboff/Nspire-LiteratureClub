"""Compile the third poem and original ch3 story into the existing 16-slot VM.

Only explicitly supported Ren'Py expressions/statements are translated. No
game Python is executed. Calls and local/nested labels get private return
targets, so source returns cannot fall through into another response.
"""
from __future__ import annotations

import ast
import copy
import itertools
import random
from pathlib import Path
from typing import Any

from rpyc_ast import flatten, kind, load_rpyc, pycode_source


class Day4Compiler:
    def __init__(self, events, raw_dir, compile_linear, visible, aliases, centers,
                 cg_layers, asset_by_image, character_layers, replacements, report_skip):
        self.raw_dir = raw_dir
        self.events = events
        self.linear = compile_linear
        self.visible = visible
        self.aliases = aliases
        self.centers = centers
        self.cg = cg_layers
        self.assets = asset_by_image
        self.character_layers = character_layers
        self.replacements = replacements
        self.report_skip = report_skip
        self.serial = 0
        self.pending_clear = []
        self.chapter = 3
        self.possible = {k: {v} for k, v in visible.items()}
        self.domains = {0: (0, 1, 2), 14: (0, 1, 2), 6: (0, 1, 2),
                        1: tuple(range(16)), 2: (0, 1, 2, 3), 7: (0, 2, 4, 6),
                        15: (0, 1, 2)}
        self.domains.update({s: (-1, 0, 1) for s in (3, 4, 5, 8, 9, 10, 11, 12, 13)})
        self.labels = {}
        for filename in ("script-ch3.rpyc", "script-poemresponses.rpyc",
                         "script-exclusives-natsuki.rpyc", "script-exclusives-yuri.rpyc"):
            _, top = load_rpyc(raw_dir / filename)
            for node in flatten(top):
                if kind(node) == "Label":
                    self.labels[node.name] = (filename, node)

    def fresh(self, hint):
        self.serial += 1
        return f"day4_{hint}_{self.serial}"

    def label(self, name):
        self.events.append({"op": "label", "name": name})

    def jump(self, target):
        self.events.append({"op": "jump", "target": target})

    def set(self, slot, value):
        self.events.append({"op": "set_state", "state": slot, "value": value})

    def snapshot(self):
        return copy.deepcopy((self.possible, self.visible, self.aliases, self.centers,
                              self.cg, self.domains))

    def restore(self, snap):
        possible, visible, aliases, centers, cg, domains = copy.deepcopy(snap)
        self.possible = possible
        self.domains = domains
        for dest, source in ((self.visible, visible), (self.aliases, aliases),
                             (self.centers, centers)):
            dest.clear()
            dest.update(source)
        self.cg[:] = cg

    def merge(self, snapshots):
        self.restore(snapshots[0])
        for snap in snapshots[1:]:
            for char, images in snap[0].items():
                self.possible.setdefault(char, set()).update(images)
            for slot, domain in snap[5].items():
                self.domains[slot] = tuple(sorted(set(self.domains[slot]) | set(domain)))
        self.visible.clear()
        for char, images in self.possible.items():
            if len(images) == 1:
                self.visible[char] = next(iter(images))

    @staticmethod
    def reference(node):
        if isinstance(node, ast.Name):
            return node.id
        if isinstance(node, ast.Subscript) and isinstance(node.value, ast.Name):
            index = ast.literal_eval(node.slice)
            return f"{node.value.id}[{index}]"
        raise ValueError(f"unsupported condition operand: {ast.dump(node)}")

    def binding(self, name):
        if name.startswith("poemwinner["):
            slot = (0, 14, 6)[int(name[-2])]
            return (slot,), lambda values: ("sayori", "natsuki", "yuri")[values[slot]]
        if "_poemappeal[" in name:
            girl, index = name.split("_poemappeal[")
            slot = {"s": (8, 11, 3), "n": (9, 12, 4), "y": (10, 13, 5)}[girl][int(index[:-1])]
            return (slot,), lambda values: values[slot]
        if name in ("s_appeal", "n_appeal", "y_appeal"):
            route = {"s": 0, "n": 1, "y": 2}[name[0]]
            slots = (0, 14, 6)[:self.chapter]
            return slots, lambda values: sum(values[s] == route for s in slots)
        if name in ("n_exclusivewatched", "y_exclusivewatched"):
            route = 1 if name[0] == "n" else 2
            # The confirmed Day 3 build already shows the follow-up when both
            # earlier poems select this girl. Do not replay it on Day 4.
            return (0, 14), lambda values: values[0] == values[14] == route
        if name in ("s_readpoem", "n_readpoem", "y_readpoem", "m_readpoem"):
            bit = {"s": 1, "n": 2, "y": 4, "m": 8}[name[0]]
            return (1,), lambda values: bool(values[1] & bit)
        if name in ("n_read3", "y_read3"):
            bit = 2 if name[0] == "n" else 4
            return (7,), lambda values: bool(values[7] & bit)
        if name == "poemopinion":
            slot = {"sayori": (8, 11, 3), "natsuki": (9, 12, 4), "yuri": (10, 13, 5)}[self.response_girl][self.chapter - 1]
            return (slot,), lambda values: {-1: "bad", 0: "med", 1: "good"}[values[slot]]
        if name == "skip_poem":
            return (2,), lambda values: bool(values[2] & 1)
        if name == "n_poemearly":
            return (2,), lambda values: bool(values[2] & 2)
        if name in ("help_sayori", "help_monika"):
            slot = 7 if name == "help_sayori" else 15
            return (slot,), lambda values: True if values[slot] else None
        raise ValueError(f"unsupported Day 4 state reference: {name}")

    def condition(self, expression, yes, no):
        if str(expression).startswith("persistent.playthrough == 2"):
            self.jump(no)  # Act 2 Easter egg: outside this Act 1 port.
            return
        node = ast.parse(str(expression), mode="eval").body
        self.condition_node(node, yes, no)

    def condition_node(self, node, yes, no):
        if isinstance(node, ast.Constant):
            self.jump(yes if node.value else no)
            return
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.Not):
            self.condition_node(node.operand, no, yes)
            return
        if isinstance(node, ast.BoolOp):
            for operand in node.values[:-1]:
                onward = self.fresh("condition")
                if isinstance(node.op, ast.And):
                    self.condition_node(operand, onward, no)
                elif isinstance(node.op, ast.Or):
                    self.condition_node(operand, yes, onward)
                else:
                    raise ValueError("unsupported boolean operator")
                self.label(onward)
            self.condition_node(node.values[-1], yes, no)
            return
        refs = {}

        def operand(n, values):
            if isinstance(n, ast.Constant):
                return n.value
            name = self.reference(n)
            if name not in refs:
                refs[name] = self.binding(name)
            return refs[name][1](values)

        # Gather only the operands of the supported comparison/truth test.
        operands = [node]
        if isinstance(node, ast.Compare):
            if len(node.ops) != 1:
                raise ValueError("chained comparisons are unsupported")
            operands = [node.left, node.comparators[0]]
        for n in operands:
            if not isinstance(n, ast.Constant):
                name = self.reference(n)
                refs[name] = self.binding(name)
        slots = sorted({s for dep, _ in refs.values() for s in dep})

        def result(values):
            if not isinstance(node, ast.Compare):
                return bool(operand(node, values))
            left, right = (operand(n, values) for n in operands)
            op = node.ops[0]
            if isinstance(op, ast.Eq): return left == right
            if isinstance(op, ast.NotEq): return left != right
            if isinstance(op, ast.Lt): return left < right
            if isinstance(op, ast.LtE): return left <= right
            if isinstance(op, ast.Gt): return left > right
            if isinstance(op, ast.GtE): return left >= right
            raise ValueError("unsupported comparison operator")

        rows = [(dict(zip(slots, combination)), result(dict(zip(slots, combination))))
                for combination in itertools.product(*(self.domains[s] for s in slots))]

        def emit(subrows, depth):
            outcomes = {outcome for _, outcome in subrows}
            if len(outcomes) == 1:
                self.jump(yes if next(iter(outcomes)) else no)
                return
            slot = slots[depth]
            children = []
            for value in self.domains[slot][:-1]:
                target = self.fresh("test")
                self.events.append({"op": "if_state", "state": slot,
                                    "value": value, "target": target})
                children.append((value, target))
            value = self.domains[slot][-1]
            emit([r for r in subrows if r[0][slot] == value], depth + 1)
            for value, target in children:
                self.label(target)
                emit([r for r in subrows if r[0][slot] == value], depth + 1)

        emit(rows, 0)

    def linear_node(self, node, script):
        k = kind(node)
        spec = getattr(node, "imspec", None)
        image = tuple(spec[0]) if spec and spec[0] else ()
        char = {"sayori": "s", "natsuki": "n", "yuri": "y", "monika": "m"}.get(
            image[0] if image else "", self.aliases.get(image[0] if image else ""))
        if k == "Say" and getattr(node, "attributes", None):
            char = node.who
            image = ({"s": "sayori", "n": "natsuki", "y": "yuri", "m": "monika"}[char],
                     *map(str, node.attributes))
        if ((char and len(image) > 1) or (k == "Scene" and image)) and image not in self.assets:
            # A CG Scene uses its initial layer as the first composite frame.
            raise ValueError(f"missing Day 4 baked art at {script}:{node.linenumber}: {image}")
        if char and k in ("Hide", "Show", "Say"):
            possibilities = self.possible.get(char, set())
            if k == "Show" and len(image) == 1:
                # Let the existing linear renderer resolve the transform for
                # each possible expression, then retain only its show events
                # as moves. The VM ignores atoms absent on the actual path.
                original_visible = self.visible.copy()
                for current in sorted(possibilities):
                    self.visible[char] = current
                    start = len(self.events)
                    self.linear([node], script, "Day 4 sprite reposition")
                    moves = self.events[start:]
                    del self.events[start:]
                    self.events.extend(dict(event, op="move") for event in moves
                                       if event["op"] == "show")
                self.visible.clear()
                self.visible.update(original_visible)
                return
            elif k == "Hide" or len(image) > 1:
                names = {p["name"] for old in possibilities if old != image or k == "Hide"
                         for p in self.assets[old]["layers"]}
                for name in sorted(names, reverse=True):
                    self.events.append({"op": "hide", "resource": name})
                self.visible.pop(char, None)
                self.possible.pop(char, None)
        dynamic = next((token for token in ("currentname", "ch4_name", "unfairto")
                        if f"[{token}]" in getattr(node, "what", "")), None)
        if k == "Say" and dynamic:
            token, slot = dynamic, 6 if dynamic == "ch4_name" else 15
            other, done = self.fresh("name_other"), self.fresh("name_done")
            value, name = (0, "Sayori") if token == "unfairto" else (1, "Natsuki")
            self.events.append({"op": "if_state", "state": slot, "value": value, "target": other})
            for name, target in (("Yuri", None), (name, other)):
                if target: self.label(target)
                clone = copy.copy(node)
                clone.what = node.what.replace(f"[{token}]", name)
                self.linear([clone], script, "Day 4 dynamic dialogue")
                self.jump(done)
            self.label(done)
        else:
            start = len(self.events)
            self.linear([node], script, "Day 4")
            if k == "Say" and self.pending_clear:
                for event in self.events[start:]:
                    if event.get("op") == "say" and "clear_cg" not in event:
                        event["clear_cg"] = self.pending_clear.pop(0)
                        break
        if k == "Scene":
            self.possible.clear()
        elif char and char in self.visible:
            self.possible[char] = {self.visible[char]}

    def update_flag(self, bit, enabled):
        done = self.fresh("flag_done")
        branches = []
        for value in self.domains[2]:
            target = self.fresh("flag_value")
            self.events.append({"op": "if_state", "state": 2, "value": value,
                                "target": target})
            branches.append((value, target))
        self.jump(done)
        for value, target in branches:
            self.label(target)
            self.set(2, value | bit if enabled else value & ~bit)
            self.jump(done)
        self.label(done)

    def python(self, node):
        source = (pycode_source(node.code) or "").strip()
        assignments = {'currentname = "Yuri"': (15, 2), 'currentname = "Natsuki"': (15, 1),
                       'unfairto = "Sayori"': (15, 0), 'unfairto = "Yuri"': (15, 2),
                       "help_sayori = True": (7, 1), "help_monika = True": (15, 1),
                       'ch4_scene = "natsuki"': (6, 1), 'ch4_scene = "yuri"': (6, 2)}
        if source in assignments:
            self.set(*assignments[source])
            if source in ("help_sayori = True", "help_monika = True"):
                self.domains[assignments[source][0]] = (1,)
        elif source in ("skip_poem = True", "skip_poem = False",
                        "n_poemearly = True", "n_poemearly = False"):
            self.update_flag(1 if source.startswith("skip_poem") else 2,
                             source.endswith("True"))
        elif source in ("n_read3 = True", "y_read3 = True"):
            # Each girl is visited once; marking the poem is distinct from
            # the recipient mask because some responses refuse sharing.
            self.events.append({"op": "add_state", "state": 7,
                                "value": 2 if source[0] == "n" else 4})
        elif source in ("ch4_name = ch4_scene.capitalize()", "currentpos = get_pos()") :
            pass  # Substitution is dispatched from the saved festival route.
        elif source.startswith("persistent.clear["):
            self.pending_clear.append(int(source.split("[")[1].split("]")[0]))
        elif source.startswith(('nextscene = "m_"', "renpy.save_persistent()", "pause(")):
            self.report_skip("Day 4 poem dispatch metadata/visual pause")
        else:
            raise ValueError(f"unsupported Day 4 Python at {node.linenumber}: {source}")

    def dynamic_call(self, node, depth):
        if node.label != "nextscene":
            raise ValueError(f"unsupported dynamic call: {node.label}")
        self.monika_comment(depth)

    def menu_prompt(self, node):
        return "Who should I help for the festival?"

    def user_statement(self, node):
        # Music, sound and screen effects are omitted unless a later chapter
        # explicitly implements their presentation.
        pass

    def invoke(self, name, depth=0):
        if depth > 8:
            raise ValueError(f"recursive Day 4 call: {name}")
        if name not in self.labels:
            raise ValueError(f"unknown Day 4 label: {name}")
        done = self.fresh(f"return_{name}")
        ids, emitted, pending, exits = {}, set(), [], []

        def target(label_name):
            if label_name not in ids:
                ids[label_name] = self.fresh(label_name)
            return ids[label_name]

        def nodes(block, script):
            for node in block:
                k = kind(node)
                if k == "Return":
                    exits.append(self.snapshot())
                    self.jump(done)
                    return
                if k == "Jump":
                    if getattr(node, "expression", False):
                        raise ValueError("dynamic Day 4 jump")
                    self.jump(target(node.target))
                    pending.append((node.target, self.snapshot()))
                    return
                if k == "Label":
                    # Defer nested labels until every incoming branch is known.
                    # A later jump may otherwise enter a label whose sprite
                    # cleanup was compiled using only its first predecessor.
                    self.jump(target(node.name))
                    pending.append((node.name, self.snapshot()))
                    return
                elif k == "If":
                    # These conditionals only change music, which this port omits.
                    if all(kind(child) in {"UserStatement", "With", "Pass"}
                           for _, branch in node.entries for child in branch):
                        continue
                    before = self.snapshot()
                    after, end = [], self.fresh("if_end")
                    for cond, branch in node.entries:
                        body, onward = self.fresh("if_body"), self.fresh("if_next")
                        self.condition(cond, body, onward)
                        self.label(body)
                        self.restore(before)
                        nodes(branch, script)
                        after.append(self.snapshot())
                        self.jump(end)
                        self.label(onward)
                    after.append(before)
                    self.label(end)
                    self.merge(after)
                elif k == "Menu":
                    self.menu(node, lambda b: nodes(b, script))
                elif k == "Call":
                    if getattr(node, "expression", False):
                        self.dynamic_call(node, depth + 1)
                    elif node.label == "showpoem":
                        arguments = getattr(node.arguments, "arguments", [])
                        poem_name = next(value for key, value in arguments if key is None)
                        self.events.append({"op": "poem", "poem": poem_name,
                                            "script": script, "line": node.linenumber})
                        # Apply the expression only after the reader dismisses the paper.
                        for argument, value in getattr(node.arguments, "arguments", []):
                            if argument == "img":
                                pose = type("Show", (), {})()
                                pose.imspec = (tuple(ast.literal_eval(value).split()), None,
                                               None, [], None, None, [])
                                pose.linenumber = node.linenumber
                                self.linear_node(pose, script)
                    else:
                        self.invoke(node.label, depth + 1)
                elif k == "Python":
                    self.python(node)
                elif k == "UserStatement":
                    self.user_statement(node)
                elif k in ("With", "Pass"):
                    continue
                elif k in ("Scene", "Show", "Hide", "Say"):
                    self.linear_node(node, script)
                else:
                    raise ValueError(f"unsupported Day 4 node {k} at {script}:{node.linenumber}")

        script, source = self.labels[name]
        emitted.add(name)
        self.label(target(name))
        nodes(source.block, script)
        exits.append(self.snapshot())
        self.jump(done)
        while pending:
            label_name, snap = pending.pop(0)
            if label_name in emitted:
                continue
            if label_name not in self.labels:
                raise ValueError(f"unknown Day 4 jump destination: {label_name}")
            incoming = [s for label, s in pending if label == label_name]
            pending[:] = [(label, s) for label, s in pending if label != label_name]
            self.merge([snap, *incoming])
            script, source = self.labels[label_name]
            emitted.add(label_name)
            self.label(target(label_name))
            nodes(source.block, script)
            exits.append(self.snapshot())
            self.jump(done)
        self.label(done)
        self.merge(exits)

    def menu(self, node, compile_branch):
        options = [(caption, cond, block) for caption, cond, block in node.items if block]
        conditional = [cond for _, cond, _ in options if str(cond) != "True"]
        if len(conditional) > 1:
            raise ValueError("unsupported multiply conditional festival menu")
        before, done, after = self.snapshot(), self.fresh("menu_end"), []

        def emit(selected):
            targets = [self.fresh("festival_option") for _ in selected]
            self.events.append({"op": "choice", "state": 2, "count": len(selected),
                                "text_en": self.menu_prompt(node)})
            for i, ((caption, _, _), target) in enumerate(zip(selected, targets)):
                self.events.append({"op": "choice_option", "value": i,
                                    "text_en": caption, "target": target})
            for (_, _, block), target in zip(selected, targets):
                self.label(target)
                self.restore(before)
                compile_branch(block)
                after.append(self.snapshot())
                self.jump(done)

        fixed = None
        if conditional:
            flag = str(conditional[0]).split()[0]
            slot = {"help_sayori": 7, "help_monika": 15}.get(flag)
            if slot is not None and len(self.domains[slot]) == 1:
                fixed = self.domains[slot][0] == 0
        if fixed is not None:
            emit(options if fixed else [o for o in options if str(o[1]) == "True"])
        elif conditional:
            allowed, denied = self.fresh("menu_allowed"), self.fresh("menu_denied")
            self.condition(conditional[0], allowed, denied)
            self.label(allowed)
            emit(options)
            self.label(denied)
            emit([o for o in options if str(o[1]) == "True"])
        else:
            emit(options)
        self.label(done)
        self.merge(after)

    def monika_comment(self, depth):
        done = self.fresh("monika_comment_end")
        before, after = self.snapshot(), []
        for girl in ("sayori", "natsuki", "yuri"):
            for appeal in range(1, self.chapter + 1):
                body, onward = self.fresh("monika_comment"), self.fresh("monika_next")
                self.condition(f'poemwinner[{self.chapter - 1}] == "{girl}" and {girl[0]}_appeal == {appeal}',
                               body, onward)
                self.label(body)
                self.restore(before)
                self.invoke(f"m_{girl}_{appeal}", depth)
                after.append(self.snapshot())
                self.jump(done)
                self.label(onward)
        self.label(done)
        self.merge(after)


def extend_day4(events, raw_dir, word_rows, store_opinion, compile_linear,
                visible, aliases, centers, cg_layers, assets, character_layers,
                replacements, report_skip):
    compiler = Day4Compiler(events, raw_dir, compile_linear, visible, aliases, centers,
                            cg_layers, assets, character_layers, replacements, report_skip)
    compiler.store_opinion = store_opinion
    compiler.word_rows = word_rows
    compiler.label("poem3_prepare")
    for slot in (3, 4, 5):
        compiler.set(slot, 0)
    events.append({"op": "say", "speaker_en": "", "text_en": "It's time to write another poem."})
    words = list(word_rows)
    random.Random("DDLC Nspire poem word board chapter 2").shuffle(words)
    for round_index in range(20):
        compiler.label(f"poem3_words_{round_index}")
        page = words[round_index * 10:(round_index + 1) * 10]
        events.append({"op": "choice", "state": 15, "count": 10,
                       "text_en": f"Choose a word ({round_index + 1}/20)."})
        for option, (word, _) in enumerate(page):
            events.append({"op": "choice_option", "value": option,
                           "text_en": word, "target": f"poem3_pick_{round_index}_{option}"})
        for option, (_, points) in enumerate(page):
            compiler.label(f"poem3_pick_{round_index}_{option}")
            for slot, score in zip((3, 4, 5), points):
                events.append({"op": "add_state", "state": slot, "value": score})
            compiler.jump(f"poem3_words_{round_index + 1}" if round_index < 19
                          else "poem3_score_winner")
    compiler.label("poem3_score_winner")
    events.extend((
        {"op": "if_state_gt", "state": 3, "other_state": 4, "target": "poem3_s_beats_n"},
        {"op": "if_state_gt", "state": 4, "other_state": 5, "target": "poem3_winner_n"},
        {"op": "jump", "target": "poem3_winner_y"},
        {"op": "label", "name": "poem3_s_beats_n"},
        {"op": "if_state_gt", "state": 3, "other_state": 5, "target": "poem3_winner_s"},
        {"op": "jump", "target": "poem3_winner_y"},
    ))
    for value, girl in enumerate(("s", "n", "y")):
        compiler.label(f"poem3_winner_{girl}")
        compiler.set(6, value)
        compiler.jump("poem3_opinions")
    compiler.label("poem3_opinions")
    # Each score is consumed only after the winner is known. Slots 8-13 and
    # 0/14 retain both earlier poems; no save-format/state-count change is needed.
    for value, girl, slot in ((0, "sayori", 3), (1, "natsuki", 4), (2, "yuri", 5)):
        store_opinion(slot, 6, value, slot, f"poem3_{girl}_opinion",
                      f"poem3_next_{girl}", 60)
    for slot in (2, 7): compiler.set(slot, 0)
    compiler.set(15, 2)
    compiler.invoke("ch3_main")
    # Restore the shared club background after any exclusive CG and clear all
    # possible characters before the poem-order UI.
    background = next(name for name, row in assets.items() if name == ("bg", "club_day"))
    compiler.linear_node(type("Scene", (), {"imspec": (background,), "linenumber": 0})(),
                         "script-poemresponses.rpyc")
    compiler.set(1, 0)
    compiler.jump("day4_poem_order_dispatch")
    compiler.label("day4_poem_order_dispatch")
    for mask in range(15):
        events.append({"op": "if_state", "state": 1, "value": mask,
                       "target": f"day4_poem_order_{mask}"})
    compiler.jump("day4_poem_all_read")
    recipients = (("Sayori", "sayori", 0), ("Natsuki", "natsuki", 1),
                  ("Yuri", "yuri", 2), ("Monika", "monika", 3))
    for mask in range(15):
        remaining = [r for r in recipients if not mask & (1 << r[2])]
        compiler.label(f"day4_poem_order_{mask}")
        events.append({"op": "choice", "state": 1, "count": len(remaining),
                       "text_en": "Who should I show my poem to first?" if mask == 0
                                  else "Who should I show my poem to next?"})
        for display, girl, bit in remaining:
            events.append({"op": "choice_option", "value": mask | (1 << bit),
                           "text_en": display, "target": f"day4_poem_read_{girl}"})
    for _, girl, _ in recipients:
        compiler.label(f"day4_poem_read_{girl}")
        compiler.possible.clear()
        visible.clear()
        compiler.update_flag(1, False)
        start = len(events)
        # Original response wrappers set the background and initial expression.
        script, wrapper = compiler.labels[f"poemresponse_{girl}"]
        for node in wrapper.block[:3]: compiler.linear_node(node, script)
        if girl == "monika":
            compiler.invoke("ch3_m_start")
            compiler.invoke("ch3_m_end")
        else:
            slot = {"sayori": 3, "natsuki": 4, "yuri": 5}[girl]
            done = compiler.fresh("reaction_end")
            before = compiler.snapshot()
            after = []
            for tier, opinion in (("bad", -1), ("med", 0), ("good", 1)):
                body, onward = compiler.fresh("reaction"), compiler.fresh("reaction_next")
                events.append({"op": "if_state", "state": slot, "value": opinion, "target": body})
                compiler.jump(onward)
                compiler.label(body)
                compiler.restore(before)
                compiler.invoke(f"ch3_{girl[0]}_{tier}")
                after.append(compiler.snapshot())
                compiler.jump(done)
                compiler.label(onward)
            compiler.label(done)
            compiler.merge(after)
            end, skip = compiler.fresh("poem_end"), compiler.fresh("poem_skip")
            compiler.condition("skip_poem", skip, end)
            compiler.label(end)
            compiler.invoke(f"ch3_{girl[0]}_end")
            compiler.label(skip)
        # A response may show a second girl. Clear every character layer that
        # this compiled response can show before returning to the recipient UI.
        layers = {e["resource"] for e in events[start:] if e["op"] == "show"}
        layers.update(p["name"] for image in compiler.possible.get(girl[0], set())
                      for p in assets[image]["layers"])
        for name in sorted(layers, reverse=True):
            events.append({"op": "hide", "resource": name})
        compiler.possible.clear()
        visible.clear()
        compiler.jump("day4_poem_order_dispatch")
    compiler.label("day4_poem_all_read")
    # Poem-end flags are now dead; reuse these two slots for festival requests.
    compiler.set(7, 0)
    compiler.set(15, 0)
    compiler.domains[7] = compiler.domains[15] = (0,)
    compiler.invoke("ch3_end")
    for names in character_layers.values():
        for name in sorted(names, reverse=True):
            events.append({"op": "hide", "resource": name})
    from compile_day5 import extend_day5
    extend_day5(compiler)
