# Game internals — DDLC TI-Nspire Act 1

This is a reference for continuing development without prior context. It inventories all **story variables and player decisions** implemented by the port, including derived original-script names. Renderer caches, font glyphs and individual animation counters are implementation details, not additional story flags. The release-readiness update adds a first-run notice and pack-owned word scores without changing story state or event positions. See PORT_STATUS for validation evidence and remaining hardware checks.

## 1. How the port runs

The calculator does not run Ren'Py. Python tools on the PC read the original compiled scripts into inert AST objects, then translate supported dialogue, images, conditionals and choices into a compact event stream. The C++ nZATO runtime reads that stream and draws at 320×240 in RGB565.

| Responsibility | Source |
|---|---|
| Safe script decoding; no arbitrary game Python execution | `tools/rpyc_ast.py` |
| Days 1–3; first/second poem rounds; compiler entry point | `tools/compile_first_day.py` |
| Branch-aware script translation; third poem and Day 4 | `tools/compile_day4.py` |
| Sunday visit and confession, Day 5 | `tools/compile_day5.py` |
| Festival morning and ending, Day 6 | `tools/compile_day6.py` |
| Poem extraction and author glyph preparation | `tools/poems.py` |
| Local original word-table extraction | `tools/compile_first_day.py` reads extracted `poemwords.txt` |
| Fresh extraction/conversion entry point | `tools/make_ddlc_pack.py` |
| Original dialogue/choice lookup | `tools/source_text.py` |
| Binary event/asset encoding | `python/src/nzato_converter/compiler.py` |
| Event execution, word randomization, timing | `cpp/src/player.cpp` |
| Menu input, naming, decision recording | `cpp/src/navigation.cpp` |
| Save writing/loading, reconstruction, settings persistence | `cpp/src/session.cpp` |
| State layout, capacities, runtime fields | `cpp/include/nzato/player.hpp` |
| Runtime word scores | `Pack::poem_words()` in `cpp/include/nzato/pack.hpp` |

Each conversion creates a private work folder, printed by the converter. Scripts, images and fonts are under its `raw/` subfolder. The readable **compiled port script** is `ddlc.nzp.events.json` inside that work folder: search its `text_en`, `script`, `line`, state operations and branch targets. It contains repeated branch expansions, so reading straight down does not necessarily follow one playthrough.

Static source calls are expanded into events with private return targets. Supported source conditions become state comparisons/jumps; the calculator has no Python interpreter or Ren'Py call stack. Dynamic calls for Monika's comments and Sunday's visitor become explicit route dispatches. Compiler snapshots of possible sprite expressions are merged at branch joins to generate cleanup valid for every incoming path.

## 2. Timeline and chapter naming

| Player day | Content | Main original chapter |
|---|---|---|
| 1 | Join the club | `ch0` |
| 2 | Poem 1; first personal scene; sharing; disagreement | `ch1` |
| 3 | Poem 2; history-dependent personal scene; sharing; walk-home answer | `ch2` |
| 4 | Poem 3; route history; sharing; festival requests | `ch3` |
| 5 | Sunday; Sayori visit; baking/decorations; confession | `ch4` |
| 6 | Festival morning; final Sayori poem; ending; credits | `ch5` |

Internal label suffixes do not define the player day. In particular, repeat Natsuki/Yuri paths on Day 3 include their `_exclusive_2_ch3` follow-up CG. Day 4 treats that follow-up as already watched if both earlier poems selected that girl. A route selected for the first time on Day 3 gets its first scene only; it must not fall through into its repeat scene. Day 4 can supply the follow-up when that girl reaches her second appeal later.

## 3. Complete stored story-variable inventory

`story_state_` has **16 signed 16-bit slots**. All start at **-1** on New Game. There is no dictionary of original Ren'Py variables at runtime. Slots are deliberately reused after their old meaning is no longer needed. A numeric value has meaning only together with its slot **and story phase**.

Winner codes: **0 Sayori, 1 Natsuki, 2 Yuri**. Opinion codes: **-1 bad, 0 medium, 1 good**. Monika has no poem score or winner code.

| Slot | Meanings and possible values during normal play |
|---|---|
| **0** | First poem winner, 0/1/2. Retained throughout Act 1. |
| **1** | Current sharing recipient mask, 0–15. Reset to 0 at each sharing session. Bits: Sayori=1, Natsuki=2, Yuri=4, Monika=8. Selecting a recipient immediately sets her bit, before her dialogue/paper. A refused poem still counts as a visited recipient. |
| **2** | During sharing: flags 0–3, mask 1=`skip_poem`, mask 2=`n_poemearly`. After Day 2 sharing: disagreement answer 0=Natsuki, 1=Yuri, 2=Help Sayori; retained until the second poem's bonus. Reset for later sharing. During festival requests: temporary visible-option value 0–3. During Sunday confession menu: temporary answer 0=love, 1=dearest friend; source branches then write the durable boolean into slot 7. |
| **3** | Current Sayori score: initialized 0, adds 1–3 per chosen word; completed base score 20–60, or up to 65 on poem 2 if she receives the bonus. During poem 3 scoring it is converted in place to the third Sayori opinion -1/0/1. |
| **4** | Current Natsuki score, same lifecycle/ranges as slot 3; later third Natsuki opinion -1/0/1. |
| **5** | Current Yuri score, same lifecycle/ranges as slot 3; later third Yuri opinion -1/0/1. |
| **6** | Second poem winner (0/1/2, with -1 preparation sentinel), then third poem winner (0/1/2). Festival selection overwrites it with final Sunday visitor: 1=Natsuki, 2=Yuri; retained for Days 5–6. |
| **7** | Day 3 walk-home answer: 0=walk with the displayed Natsuki/Yuri, 1=still walk with Sayori. Day 4 sharing reuses it for actual-paper flags: mask 2=Natsuki, mask 4=Yuri, possible 0/2/4/6. After sharing: `help_sayori`, 0/1. Sunday overwrites it with `sayori_confess`: 1=love, 0=dearest friend; retained through Day 6. |
| **8** | First poem Sayori opinion, -1/0/1. Retained. |
| **9** | First poem Natsuki opinion, -1/0/1. Retained. |
| **10** | First poem Yuri opinion, -1/0/1. Retained. |
| **11** | Second poem Sayori opinion, -1/0/1. Retained. |
| **12** | Second poem Natsuki opinion, -1/0/1. Retained. |
| **13** | Second poem Yuri opinion, -1/0/1. Retained. |
| **14** | Second poem winner, 0/1/2. Preserved copy; unlike slot 6, it is not replaced by poem 3 or festival selection. |
| **15** | Every word menu writes chosen visible index 0–9. Later Day 4 dialogue dispatch uses 0=Sayori, 1=Natsuki, 2=Yuri for `currentname`/`unfairto` (initially set to 2 after third opinions). Festival requests overwrite it with `help_monika`, 0/1. |

Raw scores for all three historical poems are **not** retained as separate arrays. Scores 3–5 are reused; history survives in winners and opinion slots. Replaying recorded word decisions can reproduce earlier scores, but there is no permanent three-round score ledger.

### Original-script names and their implementation

| Original/logical name | Port representation |
|---|---|
| `player` | `player_name_`; separate saved string, not a story slot. |
| `poemwinner[0]`, `[1]`, `[2]` | Slots 0, 14, 6 while they still represent poem winners. |
| `s_poemappeal[0..2]` | Slots 8, 11, 3 after opinion conversion. |
| `n_poemappeal[0..2]` | Slots 9, 12, 4 after opinion conversion. |
| `y_poemappeal[0..2]` | Slots 10, 13, 5 after opinion conversion. |
| `s_appeal`, `n_appeal`, `y_appeal` | Derived count of that girl's winners among the relevant first one/two/three rounds; 0–3. Not another saved counter. |
| `poemopinion` | Derived bad/med/good name for the current recipient's current opinion slot. |
| `s_readpoem`, `n_readpoem`, `y_readpoem`, `m_readpoem` | Slot 1 masks 1/2/4/8: recipient visited. |
| `skip_poem` | Slot 2 mask 1. Reset for each recipient. Response can set it to omit the poem-end/paper section. |
| `n_poemearly` | Slot 2 mask 2. Monika has already seen the poem during Natsuki's response; persists across recipients until Monika consumes/clears it. Not equivalent to Natsuki's visited bit. |
| `n_read3`, `y_read3` | Slot 7 masks 2/4 during Day 4 sharing. Separate from visiting: records an actual third poem rather than a refusal. |
| `n_exclusivewatched`, `y_exclusivewatched` | Derived check that slots 0 and 14 both equal that girl. Used to avoid repeating the agreed Day 3 follow-up on Day 4. |
| `currentname`, `unfairto` | Slot 15 name dispatch. Known assignments set 0/1/2; dialogue is compiled into variants selected at runtime. |
| `help_sayori` | Slot 7 during festival requests; known assignment sets 1. |
| `help_monika` | Slot 15 during festival requests; known assignment sets 1. |
| `ch4_scene` | Slot 6 after festival: 1/2 dispatches Natsuki/Yuri. |
| `ch4_name` | Capitalized visitor name derived from slot 6. No saved second string. |
| `sayori_confess` | Slot 7 on Sunday/Monday, 1/0. |
| `ch2_winner` | Walk-home name dispatch from slot 14: Natsuki if 1, otherwise Yuri, including a Sayori winner. No additional slot. |
| `nextscene` | Compiler dispatch into the appropriate Monika winner/appeal comment. No runtime string variable. |
| `chapter` | Compiler context 1/2/3 for poem rounds, not a saved player-day counter. |
| `poemsread`, `menutext` | Replaced by recipient-mask dispatch and compiled first/next prompt. No separate stored counter/string. |
| `currentpos`, `skip_transition` | Presentation handled/approximated by compiler and renderer; not stored story variables. |
| `poem_s1` etc. | Constant poem objects (section 6), not mutable story slots. |

Source `persistent.playthrough`, `persistent.anticheat`, `persistent.clear[...]`, character deletion, `in_sayori_kill` and exception-reporting Python do not implement Act 2 progression or real filesystem tricks here. Act 2-only conditions are excluded; ending error imagery is simulated. The retained nZATO gallery mask is not DDLC's original persistent playthrough state.

## 4. Every player-facing story choice

UI selection positions are zero-based. A choice record stores that position; the selected option may write a different **value** to its state slot.

| Choice | Options / stored consequence |
|---|---|
| Player name before opening | Up to 16 printable ASCII characters (codes 32–126); QWERTY input, deletion supported. Empty confirmation uses `MC`. Stored in `player_name_`; `[player]` uses it. |
| Poem writing, Days 2/3/4 | 20 choices per round, each 10 words. Slot 15 gets visible index 0–9; slots 3–5 receive the chosen word's actual scores. No route-selector menu remains. |
| Poem recipients, Days 2/3/4 | Sayori, Natsuki, Yuri, Monika; visited girls disappear. Slot 1 becomes `old_mask OR recipient_bit`. Every session permits all 24 orders; declined paper still completes that visit. All four bits set (15) ends sharing. |
| Day 2 disagreement: Who should I side with? | Natsuki→slot 2=0; Yuri→1; Help me, Sayori!!→2. Selects original disagreement dialogue and gives that girl +5 on poem 2. |
| Day 3 walk home: How should I answer? | Walk home with Natsuki/Yuri→slot 7=0; still walk with Sayori→1. The displayed girl is Natsuki if the second winner is Natsuki, otherwise Yuri. Both menu and dialogue use that dispatch. Slot 7 is later reused, so this is not a permanent affection counter. |
| Day 4 festival: Who should I help? | Initially Natsuki., Yuri., Monika., Sayori... . Natsuki/Yuri finalize visitor slot 6=1/2. Monika/Sayori are request detours; set help flags and return to a reduced menu. Each can be tried once; ultimately Natsuki or Yuri must be chosen. |
| Day 5 confession: How should I answer Sayori? | I love you.→temporary slot 2=0, durable slot 7=1. You'll always be my dearest friend.→temporary slot 2=1, durable slot 7=0. Changes Sunday outcome and Monday/final monologue, not the overall Act 1 ending. |

Festival slot 2 stores the **current menu index**, not a permanent girl ID. Four emitted option lists exist:

| Available list | Index/value mapping |
|---|---|
| Both request options available | 0 Natsuki; 1 Yuri; 2 Monika; 3 Sayori |
| Monika already requested | 0 Natsuki; 1 Yuri; 2 Sayori |
| Sayori already requested | 0 Natsuki; 1 Yuri; 2 Monika |
| Both already requested | 0 Natsuki; 1 Yuri |

New Game, Load, Settings, Help, Quit, pause-menu navigation and save-slot/page selection do not create poem/affection variables. Background mode is a separately persisted preference.

## 5. Poem words, scores and routing

### Word generation

The original table contains **228 words**, each with three point values in **Sayori, Natsuki, Yuri** order. Each value is 1–3. Conversion reads the player's local original table into the private manifest and pack footer. `Pack::poem_words()` supplies runtime scores; no original word list is embedded in the public C++ source. The table preserves original row order, so the existing shuffle and DDS replay produce the same boards and scores.

On New Game `poem_seed_` is generated from time, clock and a changing game serial; a nonzero seed is used. For each round the seed is mixed with that round's first word-menu event index. A xorshift32 generator drives a Fisher–Yates shuffle of all 228 word indices. Twenty successive pages show the first 200 entries, ten per page; words do not repeat within that round. Different rounds can reuse words. Twenty of the displayed candidates are chosen.

The board is cached as `word_board_` with `word_board_base_`. `word_menu_page()` recognizes the compiled `Choose a word (x/20).` prompt. Runtime text overrides the fixed placeholder words left in the manifest, and `apply_random_word()` applies the actual shuffled word's points in place of the three compiled placeholder additions, then takes the existing next-page jump. Reading the static event file's placeholder words alone does not tell you a player's randomized board.

Saves keep the seed and each chosen option position, so replay can regenerate exactly the same words and score history. Loading does not draw a new board.

### Winner and reaction tiers

1. Reset scores 3/4/5 to zero and choose 20 words: base totals 20–60.
2. **Poem 2 only:** add 5 to the girl chosen in Day 2's disagreement. Her possible final total is 25–65. Poems 1 and 3 have no such bonus.
3. Highest total wins; tied winners use **Yuri > Natsuki > Sayori** priority.
4. Winner receives a **good** opinion regardless of total. Non-winners use the thresholds below.

| Non-winner score | Opinion |
|---|---|
| 20–28 | -1 bad |
| 29–45 | 0 medium |
| 46+ | 1 good |

First opinions are stored in 8–10 before the first recipient prompt (the earlier all-bad bug was missing this initialization). Second opinions go to 11–13. Third opinions replace totals in 3–5, after saving the third winner in 6.

The route winner selects personal time, but a girl's reaction also depends on current tier, previous tiers and reading order. “Bad/medium/good” is not a promise of only three fixed speeches: historical conditions produce more variants. Monika comments on the selected winner and its appeal count without competing for points.

## 6. Actual poems, special variants and refusals

| Object | Author / title | Normal appearance |
|---|---|---|
| `poem_s1` | Sayori — Dear Sunshine | Day 2 |
| `poem_s2` | Sayori — Bottles | Day 3 |
| `poem_s3` | Sayori — % | Day 6 pamphlet |
| `poem_n1` | Natsuki — Eagles Can Fly | Day 2 |
| `poem_n2` | Natsuki — Amy Likes Spiders | Day 3, if not refused |
| `poem_n3` | Natsuki — I'll Be Your Beach | Day 4 normal ending, if not refused |
| `poem_n3b` | Natsuki — Because You | Day 4 special ending |
| `poem_y1` | Yuri — Ghost Under the Light | Day 2 |
| `poem_y2` | Yuri — The Raccoon | Day 3, if not refused |
| `poem_y3` | Yuri — Beach | Day 4 normal ending, if not refused |
| `poem_y3b` | Yuri — Ghost Under the Light pt. 2 | Day 4 special ending |
| `poem_m1` | Monika — Hole in Wall | Day 2 |
| `poem_m2` | Monika — Save Me | Day 3 |
| `poem_m3` | Monika — The Lady who Knows Everything | Day 4 |

**Special Natsuki/Yuri poems:** the third poem-end label selects its special variant when that girl's appeal count is at least 3: she won all three rounds. This is winner history, not simply three good opinions. The check occurs after the response and only if `skip_poem` is false.

**Refusal/absence rules from the translated source:**

- Day 2 opens the normal first poems.
- Day 3 Natsuki/Yuri: a current bad response with a previous bad opinion sets `skip_poem`; their second paper/end segment is skipped.
- Day 4 Natsuki: if both prior opinions were bad, all current reaction tiers lead to the refusal. With just one prior bad, a current bad reaction follows the earlier bad/refusal branch. Otherwise the applicable source response/end proceeds.
- Day 4 Yuri: both prior bad opinions refuse regardless of current tier; a current bad response also refuses when either prior opinion was bad. Other history branches follow the source.
- Sayori does not share a paper on Day 4: medium goes to the bad response, and both bad/good response paths skip the poem end. Her final poem is instead shown on Day 6.
- Monika shares her scheduled poem. `n_poemearly` modifies her introductory dialogue when she already saw the player's poem in Natsuki's response, then clears that flag.
- Actual third-paper flags `n_read3`/`y_read3` feed later dialogue, independently of the recipient-visited mask. A refusal must not set an actual-paper flag.

Response compilation executes the current tier, its nested historical conditions, then the appropriate `_end` unless `skip_poem` is set. The end includes **post-paper dialogue**; paper dismissal returns there rather than jumping straight to the next recipient. Every response clears all character atoms it could display before reopening the recipient UI, including a second girl introduced by a branch.

### Fonts and rendering

Paper is 267×240 centered on the screen, overlaying the existing classroom/character stage. Live text wraps in a 238×198 viewport and scrolls. Up/Down scroll four pixels; Left/Right page with overlap; Enter dismisses. Saving from the poem/pause navigation preserves the poem event but restarts its scroll at the top on load.

| Author | Font source | Raster size / line height | Private glyph bank |
|---|---|---|---|
| Sayori | `s1.ttf` | 18 / 25 px | E200 |
| Natsuki | `n1.ttf` | 15 / 20 px | E400 |
| Yuri | `y1.ttf` | 20 / 28 px | E600 |
| Monika | `m1.TTF` | 18 / 25 px | E800 |

Only the glyphs needed for these poems are shipped, not full TTF files. The large late-game `y3.ttf` corruption font is excluded to save space. The object named `poem_y3` is a normal Act 1 poem using `y1.ttf`; the object name does not imply that font file.

## 7. Saving: files, fields and loading

### Files and slot numbering

There are **4 pages × 6 slots = 24 manual saves**, internally numbered 0–23: page 1=0–5, page 2=6–11, page 3=12–17, page 4=18–23. No auto/quick saving.

Calculator files beside the content pack: `ddlc-0.sav.tns` through `ddlc-23.sav.tns`. Windows runtime files use `.sav` instead. Settings/gallery persistence uses a separate `ddlc.sav.tns` (`ddlc.sav` on Windows). These are binary data files despite the calculator extension. DDLC neither reads nor writes nZATO's filenames; both games can coexist in the same folder.

A saved slot contains: event cursor, all 16 slots, player name, poem seed, choice history and thumbnail. It does not contain the whole content pack or copies of all scene images.

### DDS byte layout

`SaveHead` and `SaveChoice` are packed without padding. Integers use the native little-endian representation on these targets.

| Byte offset | Length | Meaning |
|---|---|---|
| 0 | 4 | ASCII `DDS` followed by a zero byte (`44 44 53 00` hex) |
| 4 | 4 | uint32 event cursor |
| 8 | 32 | 16 × int16 story slots |
| 40 | 17 | Player name buffer including terminator |
| 57 | 1 | uint8 recorded choice count, up to 128 |
| 58 | 4 | uint32 poem seed |
| 62 | count × 5 | Choice records: uint32 menu-event index + uint8 visible option index |
| 62 + count × 5 | 9,600 | 80×60 RGB565 thumbnail, uint16 pixels |

Total size: **9,662 + 5 × choice_count bytes**; maximum **10,302 bytes** with 128 choices. The name limit is 16 visible characters. A choice record identifies its compiled menu event, not a globally numbered girl or word.

There are 60 word selections over three rounds, plus 12 recipient selections, the disagreement, walk-home answer, festival requests and confession. The 128-entry capacity was raised from 64 to accommodate the full game. Live confirmation updates a previous record for the same menu event or appends a new one while capacity permits.

### How load reconstructs the game

`restore_to()` starts a fresh story, restores name/seed/recorded decisions, and runs events up to the saved cursor in replay mode. Choices take their saved option; randomized boards are regenerated from the saved seed and original menu positions. Branches, scenes, dialogue and relevant flags are rebuilt along that path. After replay, a normal slot load restores the saved 16-slot snapshot.

During replay image events use stable metadata, then only final active images are materialized. This avoids decompressing every past image. Timed pauses/transitions are completed rather than replayed in real time; dialogue is fully revealed. The room-entry skip lock is rebuilt from its marked event. Transient shake is cleared and a short load fade is applied.

The following are **not separately serialized**: exact typewriter progress, poem/dialogue scroll position, current menu highlight, auto-play/skip toggle, animation frame/timer, history scroll or credits elapsed time. Images and dialogue history are reconstructed rather than serialized as a full screen/history snapshot. Background display mode belongs to the separate settings file, so changing settings is not undone by loading an older story save.

### Compatibility and failure handling

Current DDLC saves accept **DDS only**. DDS stands for Doki Doki Save, with no numeric suffix. The bundled upstream nZATO source accepts NZSV, which also cannot be mistaken for DDS.

The latest ending/route corrections preserve existing event/choice positions; the DDS change retains the previous payload layout but uses a new identifier and filenames. Future source edits that insert/reorder events can invalidate cursor and menu indices even when state slots are unchanged. The file has no content-pack hash, checksum or automatic event-index migration; change the identifier or provide a deliberate migration when needed.

Loading checks header/version, choice count and required payload reads before exposing a slot as valid. Saving writes directly to the slot file; it is not an atomic temporary-file replacement. Both choices and cursor depend on this exact compiled story layout. Back up release pairs and relevant saves before incompatible changes.

### Separate persistent settings

`PersistentHead` contains magic `DDGP` and a uint64 generic gallery-unlock mask. It is **not packed** like SaveHead, so native struct padding makes its raw byte size ABI-dependent. The DDLC background-mode byte follows that header: 0=Original, 1=Stretch, 2=Crop. Default is Original. A second byte records notice acknowledgement: 1=seen. Older settings without that byte preserve their background preference and show the notice once. nZATO's NZGP file is not read/imported. No original DDLC Act 2 progression is stored here. New Game resets story state, not this preferences file or manual save files.

## 8. Non-slot state that affects behavior

| Runtime field/group | Meaning and persistence |
|---|---|
| `cursor_` | Position in compiled events; saved. It implicitly locates the day, dialogue and poem page; there is no extra saved day number. |
| `player_name_` | Name string; saved. |
| `poem_seed_` | Randomization identity; saved. |
| `choice_history_`, `choice_history_count_` | Up to 128 menu-event/option records; saved. |
| `word_board_`, `word_board_base_` | Cached shuffled word indices/round identity; regenerated from seed and cursor/menus. |
| `choice_open_`, `choice_first_`, `choice_count_`, `choice_selection_` | Current menu and highlight; reconstructed/transient. |
| `poem_open_`, paper/text/font fields, `poem_scroll_px_` | Current paper and scrolling state; paper reconstructed, scroll resets. |
| `layers_`, background/face references | Active image atoms and scene; reconstructed from events, not serialized as images. |
| `speaker_`, `text_`, reveal/wait/scroll fields | Current dialogue presentation; replay rebuilds content and completes reveal/waits. |
| `history_` | Dialogue log rebuilt by replay, not a separate saved transcript. |
| `background_mode_` | Original/Stretch/Crop; separate persistent preference. |
| `notice_seen_` | First-run acknowledgement; separate persistent preference, not part of DDS story saves. |
| `startup_phase_`, `startup_elapsed_ms_` | Notice/fade lifecycle; transient. |
| `gallery_unlocks_` | Retained generic nZATO gallery mask in preferences file; not a DDLC route/Act 2 variable. |
| `window_visible_`, `manual_ui_hidden_` | Script visibility and H toggle; presentation, not affection/history. |
| `auto_play_`, `skip_indicator_`, timing fields | Playback controls; transient. |
| `cutscene_locked_` | Room-entry lock; reset by New Game, reconstructed during save replay. |
| `ended_`, `credits_roll_active_`, credits timing, `quit_requested_` | Ending lifecycle; event/timing driven, not extra route variables. |

Images use split body/face atoms rather than storing every complete expression as a full sprite. Cleanup must clear both; “hide the last remembered pose” is insufficient when incoming branches use different poses. Render caches are invalidated by relevant image/movement/UI changes and are never save data.

Day 2's `poem_order_clear` resets the classroom scene before recipient dispatch,
clearing all live sprite layers on every route and after each reading. It is after
the initial read-mask reset, so returning here preserves recipient completion.
This scene event replaces a redundant jump; existing event indices stay unchanged.

The earlier route join also clears all layers: each Day 2 exclusive returns to
the appended `day2_route_clear` block (classroom scene reset, then jump back to
`day2_shared_poem_intro`). This removes route-specific Sayori/Monika poses before
the shared Monika pose is shown, rather than waiting until the recipient UI.
The two events are appended after the Monika continuation to keep existing save
cursors stable; replay can follow that backward jump as described in section 12.

Original backgrounds retain 320×180 at y=20. Sprite bottoms are physically aligned at y=200 in Original mode; fill modes use bottom y=240. Stretch fills the screen non-proportionally; Crop fills proportionally and cuts the sides. RGB565 provides 65,536 color codes; pink distinctions and dialogue transparency are implemented by CPU compositing into the framebuffer. No separate GPU shader system is involved.

## 9. Ending and credits

The original room sequence is Ren'Py layered animation, not a supplied movie. This port uses 31 adapted resources: 16 reveal frames, one full scene, four looping close-ups, eight glitch splash frames and two exception frames. Dialogue remains native text. Existing NVP video support remains in nZATO but is not required for this ending; no separate movie file needs transferring.

At original `script-ch5` line 217, the room-entry Say event carries `lock_skip`. Encoding sets mask 2 in the existing Say x operand. Applying it sets `cutscene_locked_`, disables fast-forward and auto-play, and protects timed pauses/inline waits from manual dismissal. Ordinary dialogue remains advanceable; untimed inline waits remain dismissible to avoid a dead end. New Game resets the lock; loading after that event reconstructs it by replay. Changing the room marker must preserve this ordering.

Sayori is composited above both glitch/exception backgrounds. The simulated notice is only `An exception has occurred.` / `See traceback.txt for details.` No traceback file is created/read and the calculator does not actually crash or delete character files.

The compiled `end` with flags=2 starts credits. Internal `ended_` stops story advancement; public `ended()` remains false while the credits roll is active so the application loop continues. Sequence: **5 seconds black → 30 seconds scrolling from below the screen → 5 seconds final card → auto quit**. Act 2 does not start.

## 10. Rules for future changes

- Give any new story variable a documented slot and lifetime, or deliberately extend the runtime/save format. Do not reuse first/second opinion slots while later source conditions still read them.
- Slot 6 is no longer a third winner after festival selection; slot 7 is no longer a walk-home/reading flag after confession. New late-game conditions cannot assume those earlier meanings survive.
- Winner history and opinion history are different. Use the correct binding for special poems, CGs and refusals.
- Preserve sharing order, early-reading flags and actual-paper flags; these are separate from recipient completion.
- Keep every possible body/head atom in branch cleanup. Preserve call-return and branch-state merges.
- Word menus rely on prompt recognition, 228-word capacity and the target's three score additions plus jump. Update runtime and compiler together if this event structure changes.
- Existing save replay depends on compiled event/menu indices and the random seed. Review compatibility explicitly when shifting those indices.
- Current event limit is 32,768; 31,712 used leaves 1,056. Revisit capacity or inline duplication before Act 2. No Act 2 persistent/reset/deletion system exists yet.
- Current executable/pack are `dist/ndless/ddlc.tns` and `ddlc.nzp.tns`. `tools/make_ddlc_pack.py` accepts a local original game installation and defaults to `dist/ndless/ddlc.nzp.tns`. Generated story/intermediates stay in a private work directory. See README and DEVELOPMENT for build details.

## 11. External UI images, poem data and first launch

Public app/pack names are now `ddlc.tns` and `ddlc.nzp.tns`; internal engine namespaces/magic and save/settings filenames remain as described above. The new fresh-install converter has no dependency on old art folders or a sibling TI-84 checkout. `config/ddlc_art_layout.json` contains resource names/order/dimensions/offset metadata only, preserving the confirmed compilation layout. The copied MIT transform reader and its notice are bundled.

The DDLC footer uses little-endian `<4sHHH>`: `DDUI`, version **2**, title-art
resource ID and notebook resource ID. This is followed by a uint16 word count
(228), then 228 records. Each record has one byte for text length, three bytes
for Sayori/Natsuki/Yuri scores, and the ASCII text bytes. The loader requires
lengths 1–63, printable ASCII, scores 1–3 and complete reads. The compiler also
requires unique words. Version 1 packs request regeneration. Ship the new launcher
and newly generated pack together; DDS story-save bytes and event indices did not change.

On first launch, the white notice waits for ENTER. It identifies the unofficial
port and Team Salvato non-affiliation, recommends completing the original game,
and displays `https://ddlc.moe`. ENTER saves acknowledgement immediately, fades
the text to white over 1,200 ms, then fades the title menu in over 1,200 ms.
Navigation and story advancement are blocked during this sequence; ESC exits
from the notice without acknowledging it. Later launches open the menu directly.
The optional upstream opening movie is disabled for DDLC, so a nearby nZATO movie
cannot run before the notice. No extra video or original artwork is embedded in
the launcher for this screen.

## 12. Poem glyph metrics and first-sharing continuation

Poem glyph keys reserve author banks at E200/E400/E600/E800. Space characters are
now encoded in their author's bank too; newlines remain ordinary newlines. The
raster word-wrap/height functions recognize these private spaces. This avoids
sharing a space advance between Monika and Sayori merely because both use size 18.

The previously spare byte in `<IBBBBH>` glyph headers stores a signed horizontal
bitmap bearing (two's-complement). `Glyph::bearing_x` is applied to the ink origin;
advance still controls pen placement. Ordinary prose glyphs retain zero bearings.
Old packs' zero spare bytes keep their old behavior. A new pack should be used
with the corrected launcher. Monika's poem glyph advances also leave room for their
ink extents at 18px; sizes/line-height settings themselves are unchanged.

`ch1_m_end2` is a source fall-through continuation, not a separate day. It is
compiled into the appended `day2_monika_after_poem` block; the original end-return
jump is redirected there and the block jumps back to its original return label.
Day 3 keeps its old introduction slots unreachable to preserve later event indices.
Save reconstruction consequently uses cursor equality rather than numeric ordering;
a bounded replay budget prevents an invalid/unreachable cursor looping forever.

Glitch splash base resource IDs are unchanged. Three added frames per backdrop
form four-frame loops with the base image at 150ms. Each bake shifts the foreground
layer only; the backdrop remains fixed. The existing scene pointers select the
base images and need no extra show/hide events or new video support.
