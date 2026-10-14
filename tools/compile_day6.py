"""Translate the festival morning and Act 1 finale, including both histories."""
import ast
from rpyc_ast import load_rpyc, flatten, kind, pycode_source
from compile_day5 import Day5Compiler


class Day6Compiler(Day5Compiler):
    def user_statement(self, node):
        if node.line == "window hide(None)":
            self.events.append({"op": "window", "visible": False})
        # `window auto` is already the runtime's normal Say behavior.

    def python(self, node):
        source = (pycode_source(node.code) or "").strip()
        if source.startswith("pause("):
            call = ast.parse(source).body[0].value
            seconds = ast.literal_eval(call.args[0])
            self.events.append({"op": "pause", "duration_ms": round(seconds * 1000)})
        elif source == "persistent.playthrough = 1":
            # Act 1 finale: unlock Act 2 for the next New Game (DDUI v3 packs).
            self.set(8, 1)
            self.events.append({"op": "persistent_store", "state": 8, "key": 0})
        elif source == 'delete_character("sayori")':
            self.events.append({"op": "system", "action": 2, "arg": 2})
        elif source.startswith(("persistent.anticheat =", "delete_character(",
                                "in_sayori_kill =", "try: sys.modules")):
            # No actual crashes; other persistent values are not modelled.
            self.report_skip("Act 1 finale persistent/filesystem simulation")
        else:
            super().python(node)

    def linear_node(self, node, script):
        spec = getattr(node, "imspec", None)
        image = tuple(spec[0]) if spec and spec[0] else ()
        if kind(node) in ("Show", "Hide") and image and image[0] in ENDING_IMAGES:
            # Compose the original layered ATL sequence into background frames.
            # Its dialogue remains normal runtime text over those frames.
            resource = SCENE_AT_LINE.get(node.linenumber)
            if resource:
                self.events.append({"op": "scene", "resource": resource})
            return
        super().linear_node(node, script)
        if kind(node) == "Say" and node.linenumber == 217:
            # Mark the existing room-entry line without shifting save cursors.
            self.events[-1]["lock_skip"] = True


ENDING_IMAGES = {
    "s_kill_bg2", "s_kill2", "s_kill_bg", "s_kill", "s_kill_bg_zoom",
    "s_kill_bg2_zoom", "s_kill_zoom", "s_kill2_zoom", "noise", "vignette",
    "white", "splash_glitch", "splash_glitch2", "splash_glitch_m",
    "splash_glitch_n", "splash_glitch_y", "exception_bg", "fake_exception",
    "fake_exception2",
}
SCENE_AT_LINE = {
    229: "__ending_reveal_00", 232: "__ending_full", 242: "__ending_close_0",
    260: "__ending_splash_0", 275: "__ending_splash_1",
    280: "__ending_close_0", 285: "__ending_exception_0",
    288: "__ending_exception_1", 299: "__ending_close_0",
}


def extend_day6(previous):
    compiler = Day6Compiler.__new__(Day6Compiler)
    compiler.__dict__.update(previous.__dict__)
    _, top = load_rpyc(compiler.raw_dir / "script-ch5.rpyc")
    for node in flatten(top):
        if kind(node) == "Label":
            compiler.labels[node.name] = ("script-ch5.rpyc", node)
    compiler.domains[6] = (1, 2)
    compiler.domains[7] = (0, 1)
    compiler.label("day6_start")
    compiler.invoke("ch5_main")
    compiler.label("act1_credits")
    compiler.events.append({"op": "end", "flags": 2})
    from compile_act2 import extend_act2
    extend_act2(compiler)
