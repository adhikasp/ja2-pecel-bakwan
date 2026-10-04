# Jagged Alliance 2 — Stracciatella, Reforged

> **JA2 rebuilt as the game it always wanted to be.** A native modern front end, a smarter Queen, deeper tactics, a living world — written by AI agents, proven by tests that play the game.

This is a fork of [JA2-Stracciatella](https://github.com/ja2-stracciatella/ja2-stracciatella). Upstream made Jagged Alliance 2 run everywhere and fixed its bugs while keeping the 1999 game intact. This fork starts from that foundation and goes the other way: **it is not a preservation project.** It is a modern reimplementation of JA2 with a richer tactical and strategic game, and the revamp *is* the game: new mechanics ship on by default, with no "vanilla mode" and no obligation to keep old saves loading.

[![CI](https://img.shields.io/github/actions/workflow/status/adhikasp/ja2-stracciatella/github-ci.yml?branch=master&label=CI&logo=github)](https://github.com/adhikasp/ja2-stracciatella/actions/workflows/github-ci.yml)
[![Discord](https://img.shields.io/badge/Discord-The%20Bear%27s%20Pit-5865F2?logo=discord&logoColor=white)](https://discord.com/invite/GqrVZUM)
[![Built by agents](https://img.shields.io/badge/built%20by-agents%20%F0%9F%A4%96-8957e5)](#how-its-built-agents-with-a-test-harness)

![The native main menu at 1920x1080](https://raw.githubusercontent.com/adhikasp/ja2-stracciatella/pr-screenshots/docs-readme-rewrite/main-menu.png)

---

## What it looks like now

Every screen is being redesigned for modern displays (16:9, 16:10, 21:9; 1280x720 up to 4K) instead of stretching a 640x480 box. Every screenshot below is taken from the real game by the automated harness, at 1920x1080.

| Strategic map | Tactical HUD |
|---|---|
| ![Native strategic map screen](https://raw.githubusercontent.com/adhikasp/ja2-stracciatella/pr-screenshots/docs-readme-rewrite/strategic-map.png) | ![Native tactical HUD in a firefight](https://raw.githubusercontent.com/adhikasp/ja2-stracciatella/pr-screenshots/docs-readme-rewrite/tactical-hud.png) |
| Full-screen map with dockable panels, time compression, message log and sector intel. | Squad bar, per-merc AP/HP/breath/morale, native action bar and turn control. |

| Inventory and merc detail | Route plotting |
|---|---|
| ![Native inventory and merc detail panel](https://raw.githubusercontent.com/adhikasp/ja2-stracciatella/pr-screenshots/docs-readme-rewrite/inventory.png) | ![Native route plotting on the strategic map](https://raw.githubusercontent.com/adhikasp/ja2-stracciatella/pr-screenshots/docs-readme-rewrite/route-plotting.png) |
| Drag-and-drop inventory, attributes, attachments, slot-aware item pictures. | Native confirmation modals and shortcuts replace hand-placed popups. |

## The pitch

- **Native, not upscaled.** Screens are rebuilt on [RmlUi](https://github.com/mikke89/RmlUi) with a "Night Ops" design system: outline fonts, 109 vector icons, fractional DPI scaling. Main menu, options, save/load, new game, strategic map, tactical HUD, laptop, shopkeeper, pre-battle, auto-resolve and the setup screen are native today.
- **A GPU world renderer.** SDL_GPU tiles and sprites with smooth zoom, matching the software renderer pixel for pixel (0.0000% diff at 1x on D3D12 and Vulkan).
- **Equipment is a system; combat is a simulation.** Attachments, ammo, condition, NCTH, suppression and morale, stances and detection, medical, melee, breaching.
- **The world remembers.** A deed ledger tracks whether you are the savior or the next dictator; towns, factions and NPCs react.
- **The Queen has a mind.** A native emotional drive state and policy layer drive her strategic moves; an optional LLM gives her a voice and reads your free text into a bounded set of intents. The LLM never writes game state, and headless runs use a deterministic stub.
- **Moddable.** VFS layering and the Lua scripting API stay the mod surface.

## Roadmap

The work ships as milestones on GitHub, ordered across tracks on the [JA2 Modernization Roadmap](https://github.com/users/adhikasp/projects/1) board. Plans and decisions live in [docs/plan/](docs/plan) and [docs/ui/](docs/ui).

| Track | Milestones | State |
|---|---|---|
| **Modernize graphics** | [Native Modern Graphic](https://github.com/adhikasp/ja2-stracciatella/milestone/9) | Foundations, design system, UI runtime, front end, map, HUD, GPU renderer, laptop, shopkeeper and pre-battle are merged. Left: remaining screens, HD content art, legacy removal. Plan: [native-modern-game.md](docs/plan/native-modern-game.md). |
| **E2E: play the game in tests** | [Testable Gameplay](https://github.com/adhikasp/ja2-stracciatella/milestone/7) | Deterministic battle harness and save-compatible campaign state authoring landed. Next: assertions (LOS, cover, AP, morale), scenario corpus as AI regression in CI, full-arc campaign and save/load soak. |
| **Modernize AI** | [AI enhancement](https://github.com/adhikasp/ja2-stracciatella/milestone/6) | Audit and plan; strategic patrols and garrisons, civilian AI, and enemy use of the new equipment systems. |
| **1.13-inspired tactics** (ideas reimplemented, never ported code) | [Tactical revamp systems](https://github.com/adhikasp/ja2-stracciatella/milestone/10) · [Tactical depth](https://github.com/adhikasp/ja2-stracciatella/milestone/12) · [Combined arms](https://github.com/adhikasp/ja2-stracciatella/milestone/15) | Ammo, attachments, condition, traits/IMP; NCTH, suppression, explosives, medical, melee, mines, weather; tanks, artillery, air strikes. |
| **Living world** | [NPCs](https://github.com/adhikasp/ja2-stracciatella/milestone/11) · [Reputation & alignment](https://github.com/adhikasp/ja2-stracciatella/milestone/14) · [Strategic management](https://github.com/adhikasp/ja2-stracciatella/milestone/16) | Named NPCs with memory, the deed ledger, militia, facilities, economy, merc lifecycle, sector inventory. |
| **The Queen** | [Queen's Mind](https://github.com/adhikasp/ja2-stracciatella/milestone/13) | Drive state, policy layer, LLM voice behind a `Brain` interface, negotiation and psyops. |
| **Content & tooling** | [Content & tooling](https://github.com/adhikasp/ja2-stracciatella/milestone/17) · [CI & Build tooling](https://github.com/adhikasp/ja2-stracciatella/milestone/18) | Map editor, mod API, localization, difficulty presets; faster builds and e2e. |

## Design principles

These break ties when a plan and a PR disagree (full text in [AGENTS.md](AGENTS.md#game-design-principles)):

1. **The revamp is the game.** On by default, no vanilla mode, no save-format compatibility.
2. **Native-first.** Rules live in C++, not runtime JSON. `src/externalized/` is for the data the original game shipped with.
3. **Testable by construction.** Every system has a unit-test surface and a deterministic headless driving surface. If it cannot be asserted headless, it is not done.
4. **Curate.** Ship a small, balanced set with unit-tested invariants, not a catalog.
5. **Reimplement ideas, never port code.** Other JA2 projects inspire; their code and data are not imported.

## How it's built: agents with a test harness

This repository is developed **primarily by AI coding agents**; a human owner sets direction, approves the art style and each screen's wireframes, and reviews. Plans, code, tests, screenshots and PRs are mostly written by agents under the rules in [AGENTS.md](AGENTS.md). What makes that safe is that the game can verify itself without a person at the keyboard:

- **Headless and scriptable.** The game runs on a virtual clock and is driven from the shell or Lua: `python tools/ja2ctl.py click "New Game"`, `ja2ctl shot`, `ja2ctl state`. Native UI is addressed by element id, not pixel position. See [docs/automation.md](docs/automation.md).
- **Agents look at the result.** Golden screenshots and layout audits cover every screen at every reference resolution, and every PR that changes visuals carries real-game screenshots at 640x480 and widescreen that a reviewer opens.
- **Tests play the game.** Unit tests (`ja2 -unittests`) plus `ctest -L e2e`: per-screen parity tours, deterministic tactical battles, and campaign state authoring that jumps straight to any point in a campaign.
- **Issues are the unit of work.** One issue per slice, one PR that closes it, a board for ordering, milestones for shippable increments. Plans stay in `docs/plan/`.
- **The owner's gate is taste.** Style and wireframes are product decisions; everything else has to prove itself.

Want to contribute or watch how it works? Start with [AGENTS.md](AGENTS.md) and [docs/automation.md](docs/automation.md).

## How to start the game

1. Install the original **Jagged Alliance 2** on your computer. This project uses its data files and ships none of its own.
2. [Compile](COMPILATION.md) it from this repository (or grab a build from CI, if one is available for your platform).

### First start

3. Start the game. On the first run, or whenever the configured game directory cannot be used, the native setup screen opens.
4. Use it to set **"JA2 Data Directory"** to the directory where the original game was installed in step 1 (type the path or use the browse button). It also picks the save game directory, the resource version and mods, and shows the last `ja2.log`. Press **Apply** to write the configuration, then **Start game** to relaunch.
5. If you did not install the English version, select the correct **"Resource version"** (localization), or press **"Guess version"**. Note the two Russian releases: `RUSSIAN` for "BUKA Agonia Vlasty" and `RUSSIAN_GOLD` for "Gold".

The native setup screen replaced the old standalone launcher. The configuration lives in `%USERPROFILE%\Documents\JA2\ja2.json` on Windows, or `~/.ja2/ja2.json` on Unix-like systems. You can also edit `game_dir` by hand, or pass a version explicitly: `ja2.exe -resversion FRENCH`.

Supported localizations: `DUTCH`, `ENGLISH`, `FRENCH`, `GERMAN`, `ITALIAN`, `POLISH`, `RUSSIAN`, `RUSSIAN_GOLD`. Run `ja2.exe -help` for the full list of options.

## Building from source

See [COMPILATION.md](COMPILATION.md) for toolchains and IDE setup. The short version:

- **macOS:** `cmake -S . -B build && cmake --build build -- -j$(sysctl -n hw.logicalcpu)`.
- **Windows (MSYS2 MinGW64):** configure with the `MSYS Makefiles` generator into `_bin`, then `make -j$(nproc)`.
- **Tests:** `./ja2 -unittests`, and `ctest -L e2e` from the build directory.

Per-machine commands, the automation workflow and the repo layout are documented in [AGENTS.md](AGENTS.md).

## History of the project

The original project was run by Tron from 2006. He cleaned up the JA2 sources and made them portable — over *7000 commits* in the original SVN repository at `svn://tron.homeunix.org/ja2/trunk`. Work ceased in 2010. The [original project homepage](http://tron.homeunix.org/ja2) is gone; some history survives in the [JA2-Stracciatella Q&A](http://thepit.ja-galaxy-forum.com/index.php?t=msg&th=13222) and the [Wayback Machine](https://web.archive.org/web/20140204204243/http://tron.homeunix.org/ja2).

The community then revived the project at [ja2-stracciatella/ja2-stracciatella](https://github.com/ja2-stracciatella/ja2-stracciatella), which this fork continues.

## License

Unless specified explicitly in the commit message, all changes since `commit 8287b98` are released to the public domain. All libraries in `dependencies/lib-*` have their own licenses.

It is not known under which license Tron released his changes; all we know is that the source codes were publicly available in his SVN repository.

The original Jagged Alliance source code was released by Strategy First Inc. in 2004 under the Source Code License Agreement ("SFI-SCLA"). The license is in [SFI Source Code license agreement.txt](SFI%20Source%20Code%20license%20agreement.txt).
