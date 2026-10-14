# Port status

Updated 2026-10-10.

## Implemented

- Complete adapted Act 1, Days 1–6; Natsuki/Yuri/Sayori routes, CGs, history-dependent
  poem responses/refusals, festival selection and Sunday confession branches.
- Three scored 20-word poem rounds, randomized notebook words and author-font poems.
- Player naming, main/pause/settings menus, history, hide-text and skip controls.
- 24 manual saves across four pages; DDS includes route/poem state and replay seed.
- Layered character assets, translucent pink dialogue UI, original/stretch/crop backgrounds.
- Adapted animated ending with skip lock, credits and auto-quit.
- Fresh local pack generator for original DDLC 1.1.1, using Python 3.11+, Pillow and NumPy.
- First-run unofficial-port notice, remembered acknowledgement and two slow white fades.
- Original poem words/scores supplied by the player's generated pack, not embedded in source.

- Acts 2-4 (2026-10-14): the glitched restart, days ch20-ch23 with the second poem
  responses and exclusives, special poems, Yuri's death and the weekend loop with
  its launch-resume behaviour, Monika's room with all her topics and the character
  file deletion that ends it, the club epilogue with both endings, and the credits.
  Character files are real files next to the pack. See [ACT2](ACT2.md).

Audio, full Ren'Py transition fidelity, random Easter eggs and poem chibi feedback
animations are not implemented.

## Release folder

The source tree retains the launcher/build source, converter, engineering metadata,
licenses and current documentation. Private work, generated packs, extracted game
content, backups, obsolete upstream tools and old development documents were removed.
`dist/ndless/ddlc.tns` is the launcher release artifact; no content pack is bundled.

A locally generated full pack (Acts 1-4) contains about 61,200 events and about 390
art resources, and is about 16.7 MB. Branch targets are 32-bit since the DDUI version 3
format, so the former 32,768-event ceiling no longer applies. DDLC save/settings
filenames are `ddlc-0.sav.tns` through `ddlc-23.sav.tns` and `ddlc.sav.tns`; the
character files are written to `characters/<name>.chr.tns` beside the pack.

## Build evidence and limits

The completed gameplay/ending baseline were confirmed on a real
calculator. The latest native WSL launcher build succeeded;
the launcher is 319,299 bytes. A fresh Windows pack conversion also succeeded.

See [README](../README.md) for installation, [DEVELOPMENT](DEVELOPMENT.md) for builds
and [GAME_INTERNALS](GAME_INTERNALS.md)
for state/save details.
