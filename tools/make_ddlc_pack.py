#!/usr/bin/env python3
"""Extract original PC DDLC and build its private TI-Nspire content pack.
Python 3.11+; conversion needs only Pillow and NumPy. Extraction alone uses stdlib.
"""
from __future__ import annotations
import argparse
import hashlib
import io
import json
import pickle
import shutil
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path, PurePosixPath

PROJECT = Path(__file__).resolve().parents[1]
SUPPORTED_SCRIPTS = "da7ba6d3cf9ec1ae666ec29ae07995a65d24cca400cd266e470deb55e03a51d4"

class ConversionError(Exception):
    pass

class IndexReader(pickle.Unpickler):
    def find_class(self, module, name):
        raise ConversionError("Archive index contains executable objects; refusing extraction")


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024*1024), b""):
            h.update(block)
    return h.hexdigest()


def game_directory(path):
    path = path.expanduser().resolve()
    for candidate in (path / "game", path):
        if all((candidate / name).is_file() for name in ("scripts.rpa", "images.rpa", "fonts.rpa")):
            return candidate
    raise ConversionError("Choose the extracted original PC DDLC folder (or its game folder). "
                          "Required: scripts.rpa, images.rpa, fonts.rpa. DDLC Plus is unsupported.")


def extract_archive(archive, target):
    target = target.resolve()
    count = 0
    with archive.open("rb") as f:
        header = f.readline(128).decode("ascii").strip().split()
        if len(header) != 3 or header[0] != "RPA-3.0":
            raise ConversionError(f"{archive.name}: expected original DDLC RPA-3.0 format")
        index_offset, key = int(header[1], 16), int(header[2], 16)
        archive_size = archive.stat().st_size
        if not len(' '.join(header)) < index_offset < archive_size:
            raise ConversionError(f"{archive.name}: invalid index offset")
        f.seek(index_offset)
        inflater = zlib.decompressobj()
        compressed = f.read(32*1024*1024+1)
        if len(compressed) > 32*1024*1024:
            raise ConversionError("Archive index exceeds size limit")
        expanded = inflater.decompress(compressed, 32*1024*1024+1)
        if len(expanded) > 32*1024*1024 or not inflater.eof:
            raise ConversionError("Invalid or oversized compressed archive index")
        index = IndexReader(io.BytesIO(expanded), encoding="latin1").load()
        if not isinstance(index, dict):
            raise ConversionError("Archive index is not a file table")
        for name, chunks in sorted(index.items()):
            if isinstance(name, bytes):
                name = name.decode("utf-8")
            if not isinstance(name, str) or "\\" in name or "\x00" in name:
                raise ConversionError("Unsafe archive filename")
            relative = PurePosixPath(name)
            if (relative.is_absolute() or not relative.parts or
                any(part in (".", "..") or ":" in part for part in relative.parts)):
                raise ConversionError("Unsafe archive path")
            destination = target.joinpath(*relative.parts).resolve()
            if not destination.is_relative_to(target):
                raise ConversionError("Archive path escapes extraction directory")
            if not isinstance(chunks, (list, tuple)) or not chunks:
                raise ConversionError("Invalid archive chunk list")
            if destination.exists():
                raise ConversionError(f"Duplicate extracted path: {relative}")
            destination.parent.mkdir(parents=True, exist_ok=True)
            with destination.open("wb") as out:
                for chunk in chunks:
                    if not isinstance(chunk, (tuple, list)) or len(chunk) not in (2,3):
                        raise ConversionError("Invalid archive chunk")
                    offset, length = chunk[0] ^ key, chunk[1] ^ key
                    prefix = chunk[2] if len(chunk) == 3 else b""
                    if isinstance(prefix, str):
                        prefix = prefix.encode("latin1")
                    if not isinstance(prefix, bytes) or not 0 <= len(prefix) <= length:
                        raise ConversionError("Invalid archive prefix")
                    payload_length = length-len(prefix)
                    if offset < 0 or length < 0 or offset+payload_length > index_offset:
                        raise ConversionError("Archive chunk is outside data area")
                    f.seek(offset)
                    out.write(prefix)
                    remaining = payload_length
                    while remaining:
                        block = f.read(min(remaining, 1024*1024))
                        if not block:
                            raise ConversionError("Truncated archive data")
                        out.write(block)
                        remaining -= len(block)
            count += 1
    return count


def preserve_art_layout(baked, output):
    """Recreate the confirmed resource order/IDs from engineering metadata only."""
    layout = json.loads((PROJECT / "config/ddlc_art_layout.json").read_text(encoding="utf-8"))
    fresh = json.loads((baked / "assets.json").read_text(encoding="utf-8"))
    if fresh["unresolved"] or fresh["resolver_unsupported"]:
        raise ConversionError("Asset bake contains unresolved image references; pack was not built")
    images = {tuple(row["image"]): row for row in fresh["resources"]}
    (output / "img").mkdir(parents=True, exist_ok=True)
    copied = set()
    for row in layout["resources"]:
        new = images.get(tuple(row["image"]))
        if new is None or new["kind"] != row["kind"]:
            raise ConversionError("Original installation does not match the supported art layout")
        if "layers" not in row:
            pairs = [(row, new)]
        else:
            if len(row["layers"]) != len(new["layers"]):
                raise ConversionError("Original sprite layer count has changed")
            pairs = list(zip(row["layers"], new["layers"]))
        for old_part, new_part in pairs:
            for field in ("width", "height", "x_offset", "y_offset", "role"):
                if field in old_part and old_part[field] != new_part.get(field):
                    raise ConversionError(f"Asset {field} differs from supported layout")
            old_file = old_part["file"]
            if old_file not in copied:
                shutil.copy2(baked / "img" / new_part["file"], output / "img" / old_file)
                copied.add(old_file)
    layout["variants"] = fresh.get("variants", {})
    (output / "assets.json").write_text(json.dumps(layout, indent=2), encoding="utf-8")


def split_pack(output, split_mb):
    """Cut the pack into consecutive parts the launcher reads as one file."""
    limit = int(split_mb * 1024 * 1024)
    at = output.name.rfind(".nzp")
    if limit <= 0 or at < 0:
        return [output]
    for index in range(2, 10):  # stale parts from a previous, larger pack
        stale = output.with_name(output.name[:at + 4] + str(index) + output.name[at + 4:])
        if stale.exists():
            stale.unlink()
    data = output.read_bytes()
    if len(data) <= limit:
        return [output]
    parts = [output]
    output.write_bytes(data[:limit])
    for index, start in enumerate(range(limit, len(data), limit), start=2):
        part = output.with_name(output.name[:at + 4] + str(index) + output.name[at + 4:])
        part.write_bytes(data[start:start + limit])
        parts.append(part)
    return parts


def run_stage(*args):
    subprocess.run([sys.executable, *map(str, args)], check=True)


def build_from_raw(raw, work, output):
    try:
        import PIL
        import numpy
    except ImportError as exc:
        raise ConversionError("Install converter dependencies with: python -m pip install "
                              "-r requirements-converter.txt") from exc
    baked, art = work / "art-bake", work / "art"
    print("Converting backgrounds, sprite layers and CGs...", flush=True)
    run_stage(PROJECT / "tools/bake_first_day_art.py", "--raw-dir", raw, "--output-dir", baked)
    preserve_art_layout(baked, art)
    print("Compiling Act 1, poem fonts, title/notebook and ending...", flush=True)
    # Build privately first. Failed conversion cannot truncate an existing output pack.
    generated = work / "ddlc.nzp.tns"
    run_stage(PROJECT / "tools/compile_first_day.py", "--raw-dir", raw, "--art-dir", art,
              "--output", generated, "--font", raw / "gui/font/Aller_Rg.ttf")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".pending")
    shutil.copyfile(generated, temporary)
    temporary.replace(output)
    return generated


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path, help="Original PC DDLC installation or game folder")
    parser.add_argument("--output", type=Path, default=PROJECT/"dist/ndless/ddlc.nzp.tns",
                        help="Destination pack file (default: dist/ndless/ddlc.nzp.tns)")
    parser.add_argument("--work-dir", type=Path, help="New or empty private conversion directory")
    parser.add_argument("--extract-only", action="store_true", help="Extract only; no Pillow/NumPy needed")
    parser.add_argument("--split-mb", type=float, default=9.5,
                        help="Split the pack into ddlc.nzp.tns, ddlc.nzp2.tns, ... parts of at most this "
                             "many MB (transfers above about 10 MB to the calculator stall); 0 = one file")
    args = parser.parse_args()
    if sys.version_info < (3,11):
        parser.error("Python 3.11 or newer is required")
    try:
        game = game_directory(args.game)
        scripts_hash = digest(game/"scripts.rpa")
        if scripts_hash != SUPPORTED_SCRIPTS:
            raise ConversionError("Unsupported or modified scripts.rpa. Use the original, unmodified "
                                  "DDLC 1.1.1 PC release; the converter is bound to that script layout.")
        output = args.output.expanduser().resolve()
        if output.is_relative_to(game) or (game.name == "game" and output.is_relative_to(game.parent)):
            raise ConversionError("Output must be outside the original game installation")
        if args.work_dir:
            work = args.work_dir.expanduser().resolve()
            if work.exists() and (not work.is_dir() or any(work.iterdir())):
                raise ConversionError("--work-dir must be a new or empty directory")
            if work.is_relative_to(game) or (game.name == "game" and work.is_relative_to(game.parent)):
                raise ConversionError("Work directory must be outside the original game installation")
            work.mkdir(parents=True, exist_ok=True)
        else:
            private = PROJECT / "work/conversions"
            private.mkdir(parents=True, exist_ok=True)
            work = Path(tempfile.mkdtemp(prefix="ddlc-", dir=private))
        raw = work / "raw"
        raw.mkdir()
        print("Extracting local game files (no downloads or game Python execution)...", flush=True)
        files = {}
        for archive_name in ("scripts.rpa", "images.rpa", "fonts.rpa"):
            archive = game / archive_name
            files[archive_name] = {"sha256": digest(archive),
                                   "extracted_files": extract_archive(archive, raw)}
            print(f"  {archive_name}: {files[archive_name]['extracted_files']} files", flush=True)
        report = {"source_archives": files, "work_directory": str(work),
                  "private_content": True, "dependencies": ["Pillow", "NumPy"]}
        if not args.extract_only:
            build_from_raw(raw, work, output)
            report.update(output=str(output), output_bytes=output.stat().st_size,
                          output_sha256=digest(output), save_format="DDS")
            print(f"Pack ready: {output} ({output.stat().st_size:,} bytes)", flush=True)
            parts = split_pack(output, args.split_mb)
            if len(parts) > 1:
                report["parts"] = [str(part) for part in parts]
                print("Transfer every part together: " + ", ".join(part.name for part in parts), flush=True)
        (work / "conversion-report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"Private conversion files retained in: {work}", flush=True)
        print("Do not publish the generated pack, extracted assets or readable game script.")
        return 0
    except (ConversionError, OSError, ValueError, TypeError, EOFError, pickle.UnpicklingError,
            zlib.error, subprocess.CalledProcessError) as exc:
        print(f"Conversion stopped: {exc}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    raise SystemExit(main())
