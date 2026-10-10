"""Read port-required text by source location from the user's local scripts.
No original dialogue corpus or choice-caption fixtures are shipped here.
"""
from functools import lru_cache
from pathlib import Path
from rpyc_ast import flatten, kind, load_rpyc

@lru_cache(maxsize=16)
def _nodes(path):
    _, top = load_rpyc(path)
    return tuple(flatten(top))

class SourceText:
    def __init__(self, raw_dir):
        self.raw_dir = Path(raw_dir).resolve()

    def _at(self, filename, line, node_kind):
        matches = [n for n in _nodes(self.raw_dir / filename)
                   if kind(n) == node_kind and getattr(n, "linenumber", None) == line]
        if len(matches) != 1:
            raise ValueError(f"Expected one {node_kind} at {filename}:{line}, found {len(matches)}")
        return matches[0]

    def say(self, filename, line):
        return self._at(filename, line, "Say").what

    def menu(self, filename, line, option):
        node = self._at(filename, line, "Menu")
        captions = [caption for caption, condition, block in node.items if block]
        if not 0 <= option < len(captions):
            raise ValueError(f"Missing menu option at {filename}:{line}")
        return captions[option]
