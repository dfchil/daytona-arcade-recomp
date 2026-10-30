# Daytona USA for Dreamcast, native

A Dreamcast port of Daytona USA (`daytona93`, Deluxe '93) in which the game is
written again in C for the SH-4, rather than running the recompiled arcade
program.

## How it differs from `platform/dreamcast`

`platform/dreamcast` runs this repository's recompiled i960/TGP code on the
Dreamcast, built from the Revision A set. It reaches about 32 frames/s in
Flycast. This directory takes a different route:

- **Gameplay is reimplemented in C and bit-exact to the arcade.** It covers car
  physics, the 39 opponents, collisions, damage, the race rules, the front end,
  records and the attract mode. Every routine cites the original code it
  reproduces. The recompiled game in this repository (`tools/m2segment`) is
  the reference. The native game is checked against it tick for tick, through
  lockstep traces on the host and checkpoint rows on the console. All three
  circuits match the reference on hardware with 0 differing rows: Beginner 259
  rows, Advanced 320, Expert 345.
- **Rendering is PVR-native.** The arcade decides what the picture contains,
  and PVR techniques decide how it is drawn: prebuilt strip streams, 4-bit
  palette textures, bilinear filtering, super-sampling on the far road and the
  signs, and modifier-volume shadows. The renderer's math goes through SH4ZAM.
  Gameplay arithmetic stays exact.
- **Assets are converted from your own ROM set at build time.** Nothing
  derived from the ROM is in the repository. Only the converters and the
  sha256 of every output are committed.
- **Music and effects run on the AICA through AICAflow.** The soundtrack is
  converted from vgmrips' MultiPCM logs. The effects and the announcer are
  rendered from the ROM set's sound program.
- **It runs at 60 frames/s on a console**: one simulation tick per displayed
  frame at 59.94 Hz.

## Status

Works on hardware:

- all three circuits (Beginner, Advanced, Expert), automatic and manual
  transmission, the views VR1 to VR4 and the HUD;
- the attract mode over all three circuits (cinematic, V.R. demo, title);
- the finish, podium, results and name entry.

Implemented, but HANDOFF.md records no hardware run yet: operator settings
(difficulty, advertise sound) and the VMU save of records and settings.
`make check` tests the save against a fake VMU.

Results on hardware, from [recipes/FULL_COURSE_BENCHMARKS.md](recipes/FULL_COURSE_BENCHMARKS.md)
(23 October 2026, scripted runs, one tick per frame):

| Run | Frames | Late frames | Checkpoints differing | Registration mean/max |
| --- | --- | --- | --- | --- |
| Beginner, 8 laps, podium, results | 12,473 | 3 | 0 of 259 | 7.44 / 15.9 ms |
| Advanced, 3rd place, podium, results | 15,403 | 0 | 0 of 320 | 6.29 / 12.5 ms |
| Expert, 3rd place, podium, results | 16,655 | 1 | 0 of 345 | 6.41 / 14.4 ms |
| Attract, three loops | 8,265 | 0 | – | 6.97 / 14.2 ms |

Known limits:

- The title stalls while the attract loads the next circuit. The longest
  stalls in the run above were 79 and 228 ms. They are not counted as late
  frames.
- Beginner's three late frames come at ticks 4589–4615, where registration
  takes 13–14 ms.
- Not native yet:
  - the transmission screen's two turning cars and the ranking overlay;
  - the radar zoom chord;
  - time attack;
  - the COUNTRY and CABINET settings.

  [HANDOFF.md](HANDOFF.md) lists each item in its "Not done" notes.
- A second game in one session does not yet carry over the player state the
  original keeps (HANDOFF.md, "17 October: front-end flow").
- There is no disc image on this branch. The game loads its assets from a host
  through dcload's `/pc/`.
- The soundtrack is about 8 cents flat of MAME's MultiPCM clock, because of
  the clock the vgmrips logs give (recipes/README.md, "Music").

## Prerequisites

- **This repository's host build** (CMake, a C++20 compiler; `./setup.sh`).
  The recipe builds its own `m2import`, `m2recomp` and `m2segment` from it.
- **KallistiOS** with its SH-4 toolchain at `/opt/toolchains/dc` (`sh-elf`,
  `kos-cc`, `pvrtex`). The port is built and measured with KOS
  `368a2eb9`.
- **enDjinn**, the frontend framework: `platform/dreamcast-native/scripts/fetch_endjinn.sh` (pinned,
  into `extern/endjinn`). `ENDJINN_ROOT` overrides it.
- **SH4ZAM**: `platform/dreamcast-native/scripts/fetch_sh4zam.sh` (pinned v0.9.0, built into
  `extern/sh4zam/lib`).
- **AICAflow and AICAforge**: `platform/dreamcast-native/scripts/fetch_aicaflow.sh` (pinned, into
  `extern/aicaflow`).
- **Your ROM set**: MAME's `daytona93` (`.zip` or `.7z`). The sound ROMs are
  not needed.
- **The soundtrack**: vgmrips' [Daytona USA (Sega Model 2)](https://vgmrips.net/packs/pack/daytona-usa-sega-model-2)
  pack. Download it in a browser, because the site has a bot check. Only the
  archive with the sha256 recorded in `recipes/build_music.sh` is accepted.
- `python3` with numpy and Pillow; zlib headers; `unzip`; `zsh`.

Versions, licences and pins: [../THIRD_PARTY.md](../THIRD_PARTY.md).

## Build from scratch

From the repository root:

```sh
./setup.sh                       # host build of this repository
platform/dreamcast-native/scripts/fetch_endjinn.sh
platform/dreamcast-native/scripts/fetch_sh4zam.sh
platform/dreamcast-native/scripts/fetch_aicaflow.sh
zsh platform/dreamcast-native/recipes/build_assets.sh roms/daytona93.zip build/my-assets \
    --music ~/Downloads/Daytona_USA_\(Sega_Model_2\).zip --elf
```

`build_assets.sh` rebuilds everything from the ROM set and the music pack. That
covers the asset sets, the generated headers, the soundtrack, the effects, the
scripted benchmark races with their expected checkpoint rows, and, with `--elf`,
the ELFs. It compares every output with `recipes/expected.sha256`. It takes
about 10 minutes on an M-series Mac. The recommended asset set is
`build/my-assets/assets-ssfar4f`.
[recipes/README.md](recipes/README.md) documents each step, its options and its
outputs.

Other ELF variants are built against the recipe's outputs:

```sh
source extern/endjinn/environ.sh
make -C platform/dreamcast-native $(grep -v '^#' build/my-assets/make-vars.txt) -j8          # all
make -C platform/dreamcast-native $(grep -v '^#' build/my-assets/make-vars.txt) -j8 check    # + host and ELF checks
```

The build options are documented in `local.cfg.mk`. The defaults are the
recommended build. The ELF's name carries every option that is on.

## Running

On a console with dcload-ip or dcload-serial, use `kos-tool`. enDjinn's
`environ.sh` puts it on `PATH`.

```sh
kos-tool -t CONSOLE_IP -m build/my-assets/assets-ssfar4f -x platform/dreamcast-native/bin/<name>.elf
kos-tool -t /dev/<serial-device> -m build/my-assets/assets-ssfar4f -x platform/dreamcast-native/bin/<name>.elf
```

`-m` maps `/pc/` to the asset set, where the game reads all its files. Pad in
port A. START+A+B+X+Y returns to the loader.

Flycast has not been tested with this port. The default ELF needs dcload's
`/pc/` for its assets. `NATIVE_DISC=1` reads from `/cd/` instead, but this
branch packages no disc image. AICAflow also needs the AICA's SH-4 interrupt,
and whether Flycast delivers it has not been checked.

## How it is verified

- **Host lockstep.** The gameplay sources compile on the host with
  `-fno-fast-math -ffp-contract=off`. Harnesses in `tests/` (`trace_race_native.c`,
  `test_*_lockstep.py`) run the native race, front end, attract, damage, clock
  and sound commands against traces that `m2segment` writes from the recompiled
  game. `tools/compare_race_trace.py` reports the first differing tick and
  field.
- **Console checkpoints.** ELFs built with `NATIVE_RACE_SCRIPT=1` replay a
  recorded input script without a pad and print `CHECKPOINT` rows. A run counts
  when every row equals the host's expected file. The scripts are the
  benchmark runs above.
- **Assets.** Every converter output has its sha256 in `recipes/*.sha256`.
  Some recipe steps also compare the native picture with the original's
  frames from `m2segment`.
- **`make check`.** It runs host tests (renderer, packs, heap budget, save,
  front end), checks the ELF's code generation (`tests/check_build.sh`) and
  checks the hot-code layout (`tools/hot_layout.py`).
- **Frame time.** The drop log and render statistics ELFs
  (`NATIVE_DIAG_DROP_LOG`, `NATIVE_DIAG_RENDER_STATS`) report each late frame
  with its cause: update, registration, PVR or load. Read them with
  `tools/drop_log_report.py`.

## Source layout

| Path | What |
| --- | --- |
| `code/` | Race, contact, road, path and visibility updates, the scene loader, the practice/race loop (`practice.c`), `main.c` |
| `code/gameplay/` | Front-end modes: title, select, settings, results, name entry |
| `code/render/` | PVR renderer: `materials/`, `models/`, `scene/` (world, sky, road surface, shadows), `ui/` (HUD, results, podium, title, select) |
| `code/audio/` | AICAflow host side: music, effects, the gameplay-driven sound commands |
| `code/save/` | VMU save format, store and game state |
| `include/` | Headers. Much of the gameplay is header-only (`drive.h`, `force_math.h`, `tires.h`, `race*.h`, `collision*.h`, ...). |
| `tools/` | Host-side asset converters (`prepare_*.py`, `export_*.hpp` for `m2segment`), checkers, profiling and layout tools |
| `tests/` | Host tests, lockstep harnesses, the PVR stream simulator (`stream_sim/`), the SH-4 simulator harness (`sh4sim/`) |
| `recipes/` | `build_assets.sh` and its parameters, inputs and expected hashes; the benchmark runs |
| `config/` | Fixed placement of hot code (`hot_layout.tsv`) |
| `Makefile`, `local.cfg.mk` | enDjinn build and every build option |
| `HANDOFF.md` | Engineering state, findings (including the ones that turned out wrong) and source facts |

Shared host-side formats live in `../tools/common/dreamcast/`.
