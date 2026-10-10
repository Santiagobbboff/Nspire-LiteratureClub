"""Read the Ren'Py AST out of a compiled .rpyc file.

From koboshchan's MIT-licensed DDLC TI-84 Plus CE tools. See
licenses/ddlc-ti84plusce-LICENSE.txt for the copyright/permission notice.

DDLC's scripts.rpa ships no .rpy sources, only compiled .rpyc bytecode. Each
file is a "RENPY RPC2" container whose slot 1 is a zlib-compressed,
protocol-2 pickle of `(metadata_dict, list_of_top_level_nodes)`.

SAFETY: pickle.Unpickler will run arbitrary code if allowed to instantiate
arbitrary classes/functions named in the stream (via GLOBAL/REDUCE). This
module never does that: find_class() is an ALLOWLIST that returns only inert
Stub subclasses (which just capture __dict__, no side effects) plus a small
set of harmless real builtins. Nothing from the pickle stream is ever called
as a function. This holds even though the input is the user's own legally
obtained copy of the game -- unpickling untrusted-shaped data safely is good
practice regardless of who owns the file.

Usage:
    from rpyc_ast import load_rpyc, flatten, kind

    meta, top_nodes = load_rpyc(path)
    for node in flatten(top_nodes):
        if kind(node) == "Say":
            print(node.who, node.what)
"""

from __future__ import annotations

import pickle
import struct
import zlib
from pathlib import Path
from typing import Any, Iterable, Iterator, Optional


class RpycFormatError(Exception):
    pass


# --- stub classes ------------------------------------------------------------

class Stub:
    """Inert stand-in for any renpy.* class. Captures __dict__, does nothing else."""

    def __init__(self, *args, **kwargs) -> None:
        # Reached for classes pickled via REDUCE with constructor args we
        # don't care about (their real state arrives through __setstate__).
        pass

    def __setstate__(self, state: Any) -> None:
        # Protocol-2 objects commonly arrive as (None, {...}) or a plain
        # dict; a few (PyCode) use a plain tuple, which we keep as _state.
        if isinstance(state, tuple) and len(state) == 2 and isinstance(state[1], dict):
            state = state[1]
        if isinstance(state, dict):
            self.__dict__.update(state)
        else:
            self.__dict__["_state"] = state

    def __repr__(self) -> str:
        fields = {k: v for k, v in self.__dict__.items() if not k.startswith("_")}
        return f"{type(self).__name__}{fields!r}"


class PyExprStr(str):
    """Real renpy.ast.PyExpr: a str subclass carrying source-location metadata.

    We only need the string payload -- the literal Python source text of an
    inline expression, e.g. the right-hand side of `image bg foo = "..."`.
    """

    def __new__(cls, s: str = "", filename: str | None = None,
                linenumber: int | None = None, py: int | None = None):
        return str.__new__(cls, s)

    def __init__(self, *args, **kwargs) -> None:
        pass


# Real, side-effect-free builtins some fields legitimately use.
_SAFE_BUILTINS = {
    ("__builtin__", "set"): set,
    ("builtins", "set"): set,
    ("__builtin__", "frozenset"): frozenset,
    ("builtins", "frozenset"): frozenset,
}


class _StubUnpickler(pickle.Unpickler):
    def find_class(self, module: str, name: str):  # noqa: D102
        if module == "renpy.ast" and name == "PyExpr":
            return PyExprStr
        safe = _SAFE_BUILTINS.get((module, name))
        if safe is not None:
            return safe
        # A fresh Stub subclass per (module, name) so repr()/kind() show the
        # real Ren'Py class name without ever touching the real class.
        return type(name, (Stub,), {})


# --- container parsing --------------------------------------------------------

_MAGIC = b"RENPY RPC2"
_SLOT_HEADER = struct.Struct("<III")  # slot, offset, length


def _read_slots(blob: bytes) -> dict[int, bytes]:
    if blob[:len(_MAGIC)] != _MAGIC:
        raise RpycFormatError(f"not a RENPY RPC2 file (got {blob[:10]!r})")

    slots: dict[int, bytes] = {}
    pos = len(_MAGIC)
    while True:
        slot, offset, length = _SLOT_HEADER.unpack_from(blob, pos)
        pos += _SLOT_HEADER.size
        if slot == 0:
            break
        slots[slot] = blob[offset:offset + length]
    return slots


def load_rpyc(source: str | Path | bytes) -> tuple[Any, list[Any]]:
    """Load a .rpyc file (path or raw bytes) and return (metadata, top_level_nodes)."""
    blob = source if isinstance(source, bytes) else Path(source).read_bytes()

    slots = _read_slots(blob)
    if 1 not in slots:
        raise RpycFormatError("no AST slot (1) in this .rpyc")

    data = zlib.decompress(slots[1])
    meta, nodes = _StubUnpickler(__import__("io").BytesIO(data)).load()
    return meta, nodes


# --- tree flattening -----------------------------------------------------------

def kind(node: Any) -> str:
    """The node's real Ren'Py class name, e.g. 'Say', 'Label', 'Image'."""
    return type(node).__name__


def pycode_source(code_obj: Any) -> Optional[str]:
    """Pull the PyExpr source text out of a stubbed PyCode's captured state.

    PyCode.__setstate__ receives a plain tuple `(mode, PyExpr_source, loc, ...)`
    rather than a dict, so Stub stores it verbatim as `_state` (see Stub above).
    """
    state = getattr(code_obj, "_state", None)
    if isinstance(state, tuple) and len(state) >= 2 and isinstance(state[1], str):
        return state[1]
    return None


def _children(node: Any) -> Iterator[list]:
    """Yield every nested statement list a node can hold."""
    d = getattr(node, "__dict__", None)
    if not d:
        return

    block = d.get("block")
    if isinstance(block, list):
        yield block

    # Menu.items: [(caption, condition, block), ...]
    items = d.get("items")
    if isinstance(items, list):
        for item in items:
            if isinstance(item, tuple) and item and isinstance(item[-1], list):
                yield item[-1]

    # If.entries: [(condition_str, block), ...]
    entries = d.get("entries")
    if isinstance(entries, list):
        for entry in entries:
            if isinstance(entry, tuple) and len(entry) >= 2 and isinstance(entry[-1], list):
                yield entry[-1]


def flatten(nodes: Iterable[Any]) -> list[Any]:
    """Depth-first flatten of a node list, descending into every nested block."""
    out: list[Any] = []
    stack = list(reversed(list(nodes)))

    while stack:
        node = stack.pop()
        out.append(node)
        # Push nested blocks so they're visited immediately after this node,
        # preserving source order (matters for compile_script.py's linear
        # "last shown sprite per character" tracking).
        nested = list(_children(node))
        for block in reversed(nested):
            stack.extend(reversed(block))

    return out
