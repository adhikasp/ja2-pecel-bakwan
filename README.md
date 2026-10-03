# Jagged Alliance 2 — Stracciatella, Reforged

> **Same game. Same rules. Same saves.** Modernized from the pixels up — presentation, gameplay, AI and tooling.

This is a continuation of the venerable [JA2-Stracciatella](https://github.com/ja2-stracciatella/ja2-stracciatella) project. Upstream made Jagged Alliance 2 run everywhere and fixed its bugs while keeping the 1999 look. This fork keeps all of that, then modernises the game itself — **every aspect of it**: presentation, gameplay, AI and the tooling around them.

The **native modern rebuild comes first** and is already shipping: every screen, the tactical HUD, the strategic map and the world renderer are being rebuilt for modern displays. The **AI**, the **gameplay** (starting with the features worth having from JA2 1.13) and the **test and automation harness** are the tracks that follow. The presentation rebuild changes nothing about the rules — campaign data, mods and save games stay as they are — and any gameplay change after it is deliberate, tracked and measured.

[![CI](https://img.shields.io/github/actions/workflow/status/adhikasp/ja2-stracciatella/github-ci.yml?branch=master&label=CI&logo=github)](https://github.com/adhikasp/ja2-stracciatella/actions/workflows/github-ci.yml)
[![Discord](https://img.shields.io/badge/Discord-The%20Bear%27s%20Pit-5865F2?logo=discord&logoColor=white)](https://discord.com/invite/GqrVZUM)
[![Built by agents](https://img.shields.io/badge/built%20by-agents%20%F0%9F%A4%96-8957e5)](#how-development-works-here)

![The native main menu at 1920x1080](https://raw.githubusercontent.com/adhikasp/ja2-stracciatella/pr-screenshots/docs-readme-rewrite/main-menu.png)

---

## Why this fork exists

Stracciatella's job is a portable, cleaned-up, bug-fixed JA2 that still looks like 1999. That project succeeded — and this fork is built on top of it, not instead of it. The engine work, the portability and the bug fixes all carry over.

What this fork adds is the thing upstream deliberately leaves alone: **it makes JA2 look and feel like a game made for a 1440p/4K monitor today, not a 640x480 game blown up.**

- **Native layouts**, redesigned for 16:9, 16:10 and 21:9 at a 1920x1080 reference, scaling with DPI from 1280x720 up to 4K and 32:9. No centred 640x480 box, no letterbox filler, no "classic coordinates".
- **Native pixels** — outline fonts, vector and high-resolution art, fractional UI scaling (125%, 150%, …), because nothing is pixel-doubled.
- **Modern UX**, where it does not change the rules: readable type, tooltips, hover states, scrolling lists, drag and drop, on-screen keyboard shortcuts.
- **A native world renderer** — GPU tiles and sprites, smooth fractional zoom, and lighting/shadows as shaders, reproducing the original draw order exactly.
- **The same game underneath.** Same numbers, campaign, saves and content data.

This is a full modernization of Jagged Alliance 2 as a product, not a graphics mod. The native rebuild and the agent-driven automation harness are simply the first two pieces — chosen because they are what make everything that follows fast and verifiable.

**Not goals:** pixel-identical 640x480 mode, hand-placed 640x480 coordinates, keeping the old art as the final look, or keeping the old screen code alive after migration.

## The roadmap

The work is organised as four tracks, each with its own GitHub milestone and plan document. Together they are what this fork does that upstream does not — graphics first, then the game behind it.

### 🎨 [Native Modern Graphic](https://github.com/adhikasp/ja2-stracciatella/milestone/9)

The main event, and the one already shipping. Every screen is redesigned for modern displays and rebuilt on [RmlUi](https://github.com/mikke89/RmlUi) (MIT) with real layout, data binding and theming, on top of a GPU compositor.

Highlights so far:

- A **"Night Ops" design system**: tokens, open-licence fonts, 109 vector icons and a full component set, browsable in-game via `ja2.debug("gallery")`.
- A **native UI runtime** with view models that read the existing game state and call the existing game functions, a per-screen `ui_mode` switch, and automation that targets element ids instead of pixel positions.
- **Native front-end** (main menu, options, save/load, new game, loading), **strategic map screen** and **tactical HUD**.
- A **GPU world renderer** (SDL_GPU) that matches the software renderer **pixel for pixel — 0.0000% diff — at 1x** in five test scenes on both D3D12 and Vulkan, with fractional zoom from 1x to 4x. Windows defaults to the GPU path with a software fallback.
- **HD content art** generated on the player's machine from their own game files — derived art is never committed, so the repo stays clean to ship.

**Status:** Phases 0–5 and 8 are merged. Phase 6 (laptop) is in flight; Phases 7, 9 and 10 are open. The full plan, including the per-screen method (audit → design → build → verify) and the parity contract that stops features getting lost, is in [docs/plan/native-modern-game.md](docs/plan/native-modern-game.md).

### 🤖 [AI enhancement](https://github.com/adhikasp/ja2-stracciatella/milestone/6)

Audit and spec the tactical and strategic AI layers, externalise the tunables to `src/externalized/` JSON so behaviour can be changed without a recompile, and build a metrics/evaluation harness so AI changes can be measured instead of argued about.

### 🧪 [Better E2E](https://github.com/adhikasp/ja2-stracciatella/milestone/7)

Deterministic end-to-end tests: tactical battles simulated on a headless virtual clock, a scenario corpus run as AI regression in CI, plus a campaign E2E track that scripts a whole play-through, world map and all, including save/load soak.

### 🔧 [1.13 port](https://github.com/adhikasp/ja2-stracciatella/milestone/8)

An inventory and shortlist of features from JA2 v1.13, each classified as externalized JSON / game code / native UI first, with a licensing and divergence check before anything is ported.

The ordering across all four tracks lives on the [JA2 Modernization Roadmap](https://github.com/users/adhikasp/projects/1) board.

## What it looks like now

Every screenshot below is taken from the real game by the automated test harness, at 1920x1080.

| Strategic map | Tactical HUD |
|---|---|
| ![Native strategic map screen](https://raw.githubusercontent.com/adhikasp/ja2-stracciatella/pr-screenshots/docs-readme-rewrite/strategic-map.png) | ![Native tactical HUD in a firefight](https://raw.githubusercontent.com/adhikasp/ja2-stracciatella/pr-screenshots/docs-readme-rewrite/tactical-hud.png) |
| The full-screen strategic map with dockable side panels, time compression, a message log and sector intel. | The squad bar, per-merc AP/HP/breath/morale, and the native action bar and turn control. |

| Inventory and merc detail | Route plotting |
|---|---|
| ![Native inventory and merc detail panel](https://raw.githubusercontent.com/adhikasp/ja2-stracciatella/pr-screenshots/docs-readme-rewrite/inventory.png) | ![Native route plotting on the strategic map](https://raw.githubusercontent.com/adhikasp/ja2-stracciatella/pr-screenshots/docs-readme-rewrite/route-plotting.png) |
| Drag-and-drop inventory, attributes, attachments and slot-aware item pictures. | Native confirmation modals and shortcuts replace hand-placed popups. |

## Same game underneath

The redesign is presentation only. That promise is enforced, not just stated:

- **Save games** load unchanged across the migration.
- **Mods** keep working: `externalized/` JSON data is untouched, and UI mods become RML/RCSS overrides through the existing VFS layering.
- **Localisation** goes through the existing string tables, with layout audits run per language.
- **No game data ships here.** You need your own copy of Jagged Alliance 2; original art belongs to its rights holders.

## How development works here

This repository is developed **primarily by AI coding agents**, with a human owner setting direction. The owner approves the art direction and every screen's wireframes before implementation — style is a product decision. Everything else, from plan docs to implementation to tests, screenshots and pull requests, is largely written by agents under the rules in [AGENTS.md](AGENTS.md). That harness is what makes modernising the whole game — not just its pixels — practical: every change, in any track, is provable and reversible.

To make that safe, the codebase is built to be **machine-verifiable** — an agent can prove a change works without a human at the keyboard:

- The game runs **headless on a virtual clock** and is driven from the shell or by Lua scripts: `python tools/ja2ctl.py click "New Game"`, `ja2ctl shot`, `ja2ctl state`.
- **Golden screenshots and layout audits** cover every screen at every reference resolution, so a regression shows up as an image diff, not a bug report.
- **Deterministic tests** — unit tests plus `ctest -L e2e` — gate every change in CI.
- **Issues are the unit of work**: one issue per slice, one PR that closes it, and every visual PR carries screenshot proof.

If you want to see how the sausage is made, start with [AGENTS.md](AGENTS.md) and the automation guide in [docs/automation.md](docs/automation.md).

## How to start the game

1. Install the original **Jagged Alliance 2** on your computer. This project uses its data files and ships none of its own.
2. [Compile](COMPILATION.md) it from this repository (or grab a build from CI, if one is available for your platform).

### First start

3. Start the game. On the first run, or whenever the configured game directory cannot be used, the native setup screen opens.
4. Use it to set **"JA2 Data Directory"** to the directory where the original game was installed in step 1 (type the path or use the browse button). It also picks the save game directory, the resource version and mods, and shows the last `ja2.log`. Press **Apply** to write the configuration, then **Start game** to relaunch.
5. If you did not install the English version, select the correct **"Resource version"** (localization), or press **"Guess version"**. Note the two Russian releases: `RUSSIAN` for "BUKA Agonia Vlasty" and `RUSSIAN_GOLD` for "Gold".

The configuration lives in `%USERPROFILE%\Documents\JA2\ja2.json` on Windows, or `~/.ja2/ja2.json` on Unix-like systems. You can also edit `game_dir` by hand, or pass a version explicitly: `ja2.exe -resversion FRENCH`.

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
