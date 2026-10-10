"""Position/animation table reader adapted from koboshchan's MIT-licensed TI-84 tools.
See licenses/ddlc-ti84plusce-LICENSE.txt for the retained copyright/permission notice.
"""
import re
from pathlib import Path
from rpyc_ast import kind, load_rpyc
_POS_EXPR_RE = re.compile(r"^\w+\((\d+)\)$")
_ANIM_PREFIX_RE = re.compile(r"^([a-z]+?)\d*$")
_ANIM_PREFIX_FLAGS = {"f": 1, "h": 2, "hf": 3, "s": 4}

def load_transform_animations(raw_dir: Path) -> dict[str, tuple[int, int]]:
    """Maps each position-carrying transform's name (e.g. "f21") to its
    (X coordinate in Ren'Py's 1280-wide canvas, OP_SHOW flags bitmask), by
    reading transforms.rpyc. Transforms that aren't a single `func(N)` call
    (e.g. "thide", a hide animation with no position of its own) are simply
    absent from the map."""
    path = raw_dir / "transforms.rpyc"
    if not path.is_file():
        return {}

    _, top = load_rpyc(path)
    animations: dict[str, tuple[int, int]] = {}
    for init_node in top:
        for node in getattr(init_node, "block", None) or []:
            if kind(node) != "Transform":
                continue
            atl = getattr(node, "atl", None)
            statements = getattr(atl, "statements", None) if atl else None
            if not statements:
                continue
            exprs = getattr(statements[0], "expressions", None)
            if not exprs:
                continue
            expr = exprs[0][0]
            m = _POS_EXPR_RE.match(expr.strip()) if isinstance(expr, str) else None
            if not m:
                continue
            prefix_m = _ANIM_PREFIX_RE.match(node.varname)
            prefix = prefix_m.group(1) if prefix_m else ""
            flags = _ANIM_PREFIX_FLAGS.get(prefix, 0)
            animations[node.varname] = (int(m.group(1)), flags)
    return animations

