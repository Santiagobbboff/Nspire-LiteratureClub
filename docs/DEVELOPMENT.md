# Development guide

Run commands from the project root (`ddlc-nspire-port`), not this `docs` folder.

## Build the launcher

Use Linux or WSL with a native Ndless SDK and ARM toolchain:

```sh
export NDLESS_SDK="$HOME/Ndless/ndless-sdk"
bash tools/build_ddlc_wsl.sh
```

Set `NDLESS_TOOLCHAIN_PATH` if the compiler is installed elsewhere. The default is
`$NDLESS_SDK/toolchain/install/bin`. No Docker image is used. The helper compiles
in a temporary home-directory path, avoiding toolchain issues with spaces, and
writes `dist/ndless/ddlc.tns`. Logs are retained in `work/ddlc-launcher-build`.
The build helper does not need a content pack and does not run tests.

With a suitable SDK already on PATH, the direct target is:

```sh
cd cpp/platform/ndless
make -j4 ../../../dist/ndless/ddlc.tns
```

## Generate content

Follow [README](../README.md) using an original unmodified PC installation. 
Python dependencies are listed in `requirements-converter.txt`. 
The converter prints its private work directory. Generated output includes:

- `raw/`: locally extracted scripts, images and fonts.
- `art-bake/`: newly rendered source art.
- `art/`: converted art with stable resource names/order.
- `ddlc.nzp.tns`: generated pack.
- `ddlc.nzp.events.json`: readable compiled event stream with source references.
- `conversion-report.json`: input/output hashes and build summary.

These files are private and excluded from Git. Delete them before preparing a
public source archive; `.gitignore` does not filter a manually zipped folder.

## Source map

| Path | Purpose |
|---|---|
| `cpp/src`, `cpp/include/nzato` | Runtime, rendering, input/state, menus and saves |
| `cpp/platform/ndless` | Calculator backend and native Makefile |
| `cpp/platform/win32` | Optional Windows desktop preview |
| `cpp/third_party/nvp` | Retained video decoder and component licenses |
| `tools/make_ddlc_pack.py` | Local archive extraction and full conversion |
| `tools/rpyc_ast.py`, `source_text.py` | Inert AST decoding and local-source text lookups |
| `tools/bake_first_day_art.py`, `ddlc_image_resolve.py` | Referenced image/layer conversion |
| `tools/ddlc_transforms.py` | MIT transform reader from TI-84 reference tools |
| `tools/compile_first_day.py` | Entry point; Days 1–3 and initial poem logic |
| `tools/compile_day4.py` through `compile_day6.py` | Later Act 1 branches and finale |
| `tools/poems.py`, `prepare_ending.py`, `prepare_ui_art.py` | Paper/fonts, ending frames and pack UI art |
| `python/src/nzato_converter/compiler.py` | Binary pack, image and glyph encoding |
| `config/ddlc_art_layout.json` | Engineering metadata preserving resource layout |

Despite the first-day filenames, the entry point builds the complete supported Act 1.
The layout JSON contains names, IDs, dimensions and offsets, not pixels or dialogue.
Do not reorder resources casually: event indices/save replay depend on stable builds.

## Optional Windows preview

With CMake 3.20+ and a C/C++ compiler installed on Windows:

```powershell
cmake -S . -B build-desktop
cmake --build build-desktop --config Release --target ddlc
```

Generate `dist/ndless/ddlc.nzp.tns` before building; CMake copies it alongside the
executable as `ddlc.nzp`. The preview runs the runtime at 320×240; it is not an
Ndless emulator and cannot establish calculator performance or hardware behavior.

## Changing content and saves

Consult [GAME_INTERNALS.md](GAME_INTERNALS.md) before changing state slots, menu/event order, poem
bindings or renderer cleanup. Saves reconstruct playback from a cursor, decisions,
random seed and story-state snapshot. Renumbering events may invalidate saves even
without changing the save version. Current capacity: 32,768 events; 31,712 used.

