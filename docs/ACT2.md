# Act 2 on the TI-Nspire

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
| New Game, playthrough 3/4 | Act 3 is not implemented: a placeholder line and a "Start over from Act 1" choice that resets the persistent values, deletes saves and restores the character files |

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

0 playthrough, 1 yuri_kill, 2 autoload (1-3 `yuri_kill_1..3`, 4 `ch30_main`),
3-5 special poems (1..11, drawn without repetition on the first Act 2 start),
6 seen_eyes, 7 seen_sticker, 8 seen_ghost_menu, 9 deleted_saves, 10 seen_colors_poem.
Settings and the notice flag precede the section unchanged.

## Pack format (DDUI version 3)

- Branch targets (jump, if_state, if_state_gt, choice_option) are 32-bit and live in
  the unused speaker reference; packs may exceed 32,768 events (the full Act 1 + Act 2
  pack has about 52,600).
- New ops: `system` (delete saves / delete character file / restore character files),
  `persistent_load`, `persistent_store`, `effect` (screen tear, invert), `rand_state`.
  `pause` with duration -1 waits for input.
- Footer: start event, the glitch alphabet, and the four original `.chr` files, which
  the launcher writes to `characters/<name>.chr.tns` next to the pack on New Game.
- Dialogue: `{glitch=N}` renders N random glyphs from the alphabet; the "edited" style
  uses VerilySerifMono glyphs in the private bank 0xEA00. Yuri's y2/y3 fonts use banks
  0xEC00/0xEE00; `poem_y1`/`poem_y2` papers ship as extra poem backgrounds.
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
- Act 3 (`ch30`) and Act 4 (`ch40`) are not compiled yet.

## Testing on a PC

`cpp/platform/headless/main.cpp` drives the engine from stdin and writes PPM frames;
`tools/autoplay.py`-style scripts can play the whole act automatically. The Act 2
run reaches the main menu after the weekend with `playthrough = 3` and the two
character files removed.
