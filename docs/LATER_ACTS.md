# Acts 2, 3 and 4 on the TI-Nspire

Updated 2026-10-14. Act 2 is compiled from the original scripts (`script-ch10`,
`script-ch20` to `script-ch23`, `script-poemresponses2`, `script-exclusives2-*`,
`poems_special`) by `tools/compile_act2.py`. No original text, art or character
files are in this repository; everything comes from the player's own installation
through the converter.

## Flow

| Trigger | What runs |
|---|---|
| Act 1 ending (`ch5` line 219) | `persistent.playthrough = 1`, `sayori.chr` deleted (`characters/sayori.chr.tns` beside the pack) |
| New Game, playthrough 1 | `ch10_main` (glitched restart, saves deleted) → `ch20_from_ch10` → `ch20_main2` |
| New Game, playthrough 2 | `ch20_main` |
| Then | poem → `ch21_main` → sharing → `ch21_end` → poem → `ch22_main` → sharing → `ch22_end` → poem → `ch23_main` → sharing → `ch23_end` → `yuri_kill` |
| Weekend | `persistent.yuri_kill` counts every advance to 1440; every launch with `autoload = yuri_kill_2` adds 200 and resumes the corpse scene (as the original does) |
| `yuri_kill_3` | `yuri.chr` and `natsuki.chr` deleted, saves deleted, `playthrough = 3`, `autoload = ch30_main`, back to the main menu |
| New Game, playthrough 3 (or autoload `ch30_*`) | Act 3: Monika's room, her 53 topics in random order (four persistent bit masks), resume speeches on relaunch (`monika_reload`), the poem of broken "Monika" words and `poem_m4` |
| `characters/monika.chr.tns` missing at launch or before a topic | `ch30_end`: the deletion scene, then `playthrough = 4` and the main menu. Quit the game, delete the file in the document browser, relaunch |
| New Game, playthrough 4 | Act 4 (`ch40_main`): the club without Monika; `clearall` (every CG seen, `persistent.clear[0..9]`) picks Sayori's good ending; then the credits |
| Credits | the glitch backdrops, Monika's credit lines, the ten credit cards (the original CG or the "not found" card when that CG was never seen), "made with love by", the engine's scrolling roll, then the program exits |
| Every later launch (`autoload = postcredits_loop`) | the final sheet, the "script file is missing" dialog, and the original's "delete your save data and start over?" choice |

Each converter run keeps Act 1's event indices unchanged (the Act dispatch label is
appended after the Act 1 credits and selected through the pack footer's start event),
so existing DDS saves still replay.

## Story state (fresh 16 slots, all -1 on New Game, zeroed where noted)

| Slot | Meaning |
|---|---|
| 0 / 14 / 6 | `poemwinner[0..2]`: 1 Natsuki, 2 Yuri (Sayori never wins in Act 2) |
| 9 / 12 / 4 | `n_poemappeal[0..2]` (-1 bad, 0 medium, 1 good); 4 is the round 3 score until scored |
| 10 / 13 / 5 | `y_poemappeal[0..2]`; 5 is the round 3 score until scored |
| 3 / 8 / 11 | Sayori score (unused), scratch (`rand`, `menu_clicked`, persistent copies), `y_appeal` |
| 7 | `n_appeal` (wins, not reduced by the ch22/ch23 winner overrides) |
| 1 | sharing recipient mask (Natsuki 2, Yuri 4, Monika 8); word-menu scratch |
| 2 | flags: 1 `skip_poem`, 2 `n_poemearly`, 4 `y_gave`, 8 `natsuki_23`, 16 `faint_effect` |
| 15 | word-menu selection, menu/dialog choices, then `persistent.yuri_kill` during the weekend |

`y_ranaway` is derived: Yuri won poem 1 and Monika has not been read yet in the
chapter 1 sharing. Random Ren'Py draws (`renpy.random.randint`) become `rand_state`
events seeded from the poem seed and the event position, so saves replay identically.

## Persistent values (`ddlc.sav.tns`, `DDP3` section, 16 × int16)

0 playthrough, 1 yuri_kill, 2 autoload (1-3 `yuri_kill_1..3`, 4 `ch30_main`, 5 `ch30_main2`,
6 `ch30_postpoem`, 7 `ch30_autoload`, 8 `ch30_end`, 9 `ch30_end_2`, 10 `credits`,
11 `postcredits_loop`), 3-5 special poems (1..11, drawn without repetition on the first
Act 2 start), 6 seen_eyes, 7 seen_sticker, 8 seen_ghost_menu, 9 deleted_saves,
10 seen_colors_poem, 11 monika_reload, 12 current_monikatopic, 13 monika_kill,
14 tried_skip, 15 monika_back, 16-19 used-topic masks (`DDP4`, 32 values; `DDP3`
files with 16 values are still read). Settings and the notice flag precede the section
unchanged. CG unlocks (`persistent.clear[k]`) ride on the next dialogue event's `x`
bits 2-5 and land in the existing gallery mask, so Act 1 event indices do not move.

## Pack format (DDUI version 3)

- Branch targets (jump, if_state, if_state_gt, choice_option) are 32-bit and live in
  the unused speaker reference; packs may exceed 32,768 events (the full Act 1 + Act 2
  pack has about 52,600).
- New ops: `system` (1 delete saves, 2 delete a character file, 3 restore character
  files by mask, 4 does a character file exist, 5 are all CGs seen, 6 is one CG seen),
  `persistent_load`, `persistent_store`, `effect` (screen tear, invert), `rand_state`
  (seeded from the poem seed, the event index and the story slots, so replays match
  while loops vary). `pause` with duration -1 waits for input.
- Footer: start event, the glitch alphabet, and the four original `.chr` files, which
  the launcher writes to `characters/<name>.chr.tns` next to the pack on New Game.
- Dialogue: `{glitch=N}` renders N random glyphs from the alphabet; the "edited" style
  uses VerilySerifMono glyphs in the private bank 0xEA00. Yuri's y2/y3 fonts use banks
  0xEC00/0xEE00; `poem_y1`/`poem_y2` papers ship as extra poem backgrounds.
- The converter splits packs above 9.5 MB into `ddlc.nzp.tns`, `ddlc.nzp2.tns`, ...
  (`--split-mb`); the launcher opens consecutive parts as one file.
- Resource kinds: `screen_layer` (320×180 RGBA overlays such as noise, vignette, blood,
  the eyes), `fullscreen_layer` (black/white fills shown as layers), and
  `fullscreen_background` (320×240 special poem sheets, `bsod`, `paper_glitch`, intro).

## Approximations and known gaps

- Audio is absent, as in Act 1. Screen effects: `tear` and `invert` are real post
  effects; `show layer master at ...` (dizzy, rewind, heartbeat, zooms) is skipped.
  The faint-effect `Solid()` flashes are static tints; `noise` is a static 35 % layer.
- Animated ATL sprites (Yuri's glitch frames, `y_glitch_head`, Natsuki's ghost zoom,
  the moving mouth) keep their first frame. `blood` and the ghost specks are
  procedural stand-ins. Monika's fade-in during Yuri's confession is stepped
  through the compiled `malpha` values.
- Special poems are the original sheets scaled to 320×240 (the small handwriting is
  tight but legible). The poem game's rare word-glitch and Yuri sticker easter eggs
  are omitted; the one-time "eyes" glimpse after a poem is kept.
- Decoy menus (`renpy.display_menu(... interact=False)`) are not drawn. The rigged
  festival choice is an ordinary three-option menu. Menus with more than ten
  options show the first ten.
- The `.chr` files are written beside the pack so the player can inspect or delete
  them with the document browser; the game only reads back whether `monika.chr`
  exists once Act 3 is ported.
- Act 3: the window/room layers are one flattened background (no scrolling masks),
  Monika's glitch frames are static, the RectStatic/colour-wash effects are skipped,
  the waiting between topics keeps the original 20-40 s, and Monika's slow unskippable
  text is ordinary dialogue. The "fake skip" scene is omitted.
- Act 4 credits: no song; the glitch backdrops and credit cards are timed scenes with
  text overlays instead of scrolling layers; the "deleted" CG variants are replaced by
  the "not found" card. The Act 1 ending now shows the END card and returns to the
  menu (the previous build rolled credits there).

## Testing on a PC

`cpp/platform/headless/main.cpp` drives the engine from stdin and writes PPM frames;
an autoplay script can play every act unattended. Act 2 reaches the main menu after
the weekend with `playthrough = 3` and two character files removed; Act 3 loops
topics until `monika.chr.tns` is deleted and the game relaunched; Act 4 ends in the
credit roll and the program exits.
