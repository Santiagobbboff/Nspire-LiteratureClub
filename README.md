# DDLC TI-Nspire — unofficial Act 1 port

An unofficial port of DDLC Act 1 for the **TI-Nspire CX II with Ndless**.
Not affiliated with or endorsed by Team Salvato. Please play the original game first:
[ddlc.moe](https://ddlc.moe).

Full Act 1 (days 1-6) is playable and content complete, including route-dependent CGs,
three scored poem rounds, the original poems and an ending.

This version doesn't include any external game mechanics like character deletion from the original yet!

## How to Generate a Content Pack

Only the launcher is provided, to play you need to generate a contente pack first!
Provide an **unmodified original DDLC PC installation**, extracted from its
download. Steam and Itch.io were tested. DDLC Plus is not currently supported. 
The converter only reads your local installation;
it does not alter the game files.

Requirements: **Python 3.11+**, **Pillow**, **NumPy**.
### Windows

Run from this project's folder:

```powershell
py -3 -m pip install -r requirements-converter.txt
py -3 tools/make_ddlc_pack.py "C:\Games\DDLC"
```

Or use `make-pack.cmd "C:\Games\DDLC"` after installing the dependencies.
Replace `C:\Games\DDLC` with your game folder
### Linux

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-converter.txt
python tools/make_ddlc_pack.py "/home/you/game"
```

The wrapper also works: `sh make-pack.sh "/home/you/game"`.

### Options

- `--output /path/to/ddlc.nzp.tns`: destination; default `dist/ndless/ddlc.nzp.tns`.
- `--work-dir /path/to/empty-directory`: a new or empty private work directory;
  default a unique directory under `work/conversions`.
- `--extract-only`: extract the required archives without conversion or external dependencies.

Unsupported script archives and unsafe archive entries are rejected. Compiled
scripts are decoded as inert data, not executed as game Python. Conversion builds
privately before replacing the requested output. The private work folder contains
extracted files, converted art, a readable event script and a conversion report.

**Do not publish generated packs, extracted images/scripts/fonts, readable dialogue
JSON, saves or work folders.** The release source includes no original game archives,
image files, font files or copied narrative dialogue. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Install on the calculator

Download `ddlc.tns` from this repository's **Releases** section. The source-code
download does not include the prebuilt launcher. Generate a pack using the
converter from the same release, then transfer both files into one calculator folder:

- `ddlc.tns` — launcher attached to the GitHub Release, or built from source.
- `ddlc.nzp.tns` — content pack generated from your installation.

Open `ddlc.tns`.

On first launch, read the notice and press ENTER. Acknowledgement is remembered in settings.
DDLC saves use `ddlc-0.sav.tns` through `ddlc-23.sav.tns`; settings use
`ddlc.sav.tns`. These are separate from nZATO's files, so both games can share a 
folder.

See [GAME_INTERNALS.md](docs/GAME_INTERNALS.md) for replay and compatibility.

## Build and contribute

See [DEVELOPMENT.md](docs/DEVELOPMENT.md) for the native Ndless build, optional desktop
preview and project structure. The SDK/toolchain is needed only to build the launcher.

[PORT_STATUS.md](docs/PORT_STATUS.md) records implemented scope and build limitations.
[GAME_INTERNALS.md](docs/GAME_INTERNALS.md) documents all story-state slots, choices,
poem scoring, resource metadata, save replay and ending behavior.
See [RELEASING.md](docs/RELEASING.md) for publishing the source and launcher.

## Credits and licensing

Original DDLC by **Team Salvato**. Runtime/converter derived from **nZATO by zyf722**
(GPLv3). Script reader, image resolver and transform reader from
**ddlc-ti84plusce by koboshchan** (MIT).
Video decoder from **Nspire-VideoPlayer-Ndless by GigaZelensky** and **Xvid**; retained
component licenses apply. Port by **Santiagobbboff and chatgpt-san**.

See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Original game content is separate from the engine license. 
