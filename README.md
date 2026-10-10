<img width="320" height="240" alt="title-menu" src="https://github.com/user-attachments/assets/05ad69be-6b77-460d-bc49-ea4a069d6927" />
<img width="320" height="240" alt="sayori-overslept" src="https://github.com/user-attachments/assets/be537136-8e8d-4084-9182-f8a762f5c1dd" />
<img width="640" height="480" alt="image 1" src="https://github.com/user-attachments/assets/66c9480a-3463-4c91-aa9a-d5a0e9af8250" />

# Nspire Literature Club 

An unofficial port of DDLC Act 1 for the **TI-Nspire CX II with Ndless**.
Not affiliated with or endorsed by Team Salvato. Please play the original game first!
[ddlc.moe](https://ddlc.moe).

Full Act 1 (days 1-6) is playable and content complete, including route-dependent CGs,
three scored poem rounds, the original poems and an ending.

This version doesn't include any external game mechanics like character deletion from the original yet!

## How to Generate a Content Pack

Only the launcher is provided, to play you need to generate a content pack first!
Provide an **unmodified original DDLC PC installation**, extracted from its
download. Steam and Itch.io both work. DDLC Plus is not currently supported. 
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

## Installing on the calculator

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

## Future Goals and issues
Port the rest of the game
Add animations and transitions
Game sometimes lags when fast forwarding
Saves may be a bit wonky

you tell me!

## Build and contribute

See [DEVELOPMENT.md](docs/DEVELOPMENT.md) for the native Ndless build, optional desktop
preview and project structure. The SDK/toolchain is needed only to build the launcher.

[PORT_STATUS.md](docs/PORT_STATUS.md) records implemented scope and build limitations.
[GAME_INTERNALS.md](docs/GAME_INTERNALS.md) documents all story-state slots, choices,
poem scoring, resource metadata, save replay and ending behavior.

## Credits, licensing & disclaimer

Original DDLC by **Team Salvato**. Runtime/converter derived from **nZATO by zyf722**
(GPLv3). Script reader, image resolver and transform reader from
**ddlc-ti84plusce by koboshchan** (MIT).
Video decoder from **Nspire-VideoPlayer-Ndless by GigaZelensky** and **Xvid**; retained
component licenses apply. Port by **Santiagobbboff**.
See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Original game content is separate from the engine license. 

This project was heavily developed through vibe coding but has been tested from start to finish. The code may not have received a full human review so I can't guarantee perfection,
or that everything works in every scenario. By using any content or build artifact from this project, you acknowledge and accept the associated risks.

