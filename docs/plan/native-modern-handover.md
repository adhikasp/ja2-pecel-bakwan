# Native Modern Game — handover (2026-10-02)

Status of [native-modern-game.md](native-modern-game.md) at the end of the first orchestration run.
Decisions and measured numbers are in [native-modern-game-decisions.md](native-modern-game-decisions.md).

## Done (merged to master)

| Phase | PR | Result |
|---|---|---|
| 0 Foundations | #14 | RmlUi chosen; render path decided; world spike 0 px diff; asset tooling (`tools/assets/`, `assets/manifest.json`, 7,615 assets classified); `docs/ui/_template.md` and review checklist |
| 1 Design system | #15, #16 | Owner chose **B · Night Ops**; tokens, fonts (OFL, CJK fetched at configure), 109 icons, components, `ja2.debug("gallery")`. Owner approved the gallery |
| 2 Native UI runtime | #17 | `src/game/NativeUI/` + `src/nativeui/`; view models; `ui_mode` per screen; automation by id; native message box, tooltip, toasts, cursor; native Credits |
| 3 Front-end | #18 | Native main menu, options (5 tabs), save/load (local thumbnails), new game, loading screen (40 tips, EN only) |
| 8 World renderer | #19 | SDL_GPU compute renderer, 0 px vs software in 5 scenes on D3D12 + Vulkan; fractional zoom 1–4x; `gpu` default in windows with software fallback |

## In flight (not merged)

| Phase | Where | State | Next step |
|---|---|---|---|
| 4 Map screen | PR #20, branch `native/phase-4-mapscreen` | Implemented; **not yet reviewed by the orchestrator**. Resolution suite not fully rerun after golden updates | Review code and screenshots, rerun all suites. Gaps listed in the PR body: pre-battle/help/militia redistribution/item description still legacy; contract and move box are menus, not the approved card modals; message log lacks tabs/search; no drag and drop or search/sort/Stack & merge in sector inventory; no view-model unit test; strings EN only; Towns filter off after plotting in tour shots |
| 5 Tactical HUD | branch `native/phase-5-tactical-hud` | M1 spec `docs/ui/tactical.md` + wireframes (`pr-screenshots/native-phase-5-wireframes/`). **Owner approved every state**; message log key = **H**; long guns must not be clipped in the hand slot (recorded in the spec) | Implement (M3/M4). New icons needed: mute, lockpick, crowbar, door explosive, swap hands. Reuse Phase 4 components once #20 merges |
| 6 Laptop | branch `native/phase-6-laptop` | M1 spec `docs/ui/laptop.md` + wireframe mocks committed; **screenshots were never pushed** (agent stopped at the weekly limit) | Regenerate wireframe screenshots, push to `pr-screenshots/native-phase-6-wireframes/`, get owner approval per screen, then implement |

## Not started
- Phase 7 remaining screens (pre-battle, auto-resolve, shopkeeper, hiring/contract flows, end-game; editor decision).
- Phase 9 HD content art (local generation only, never committed).
- Phase 10 legacy removal.

## Known follow-ups from merged phases
- World renderer: 4K at world zoom 1 while scrolling is 57 fps (worst case, cache off); needs strip-based cache reuse across camera moves. MSL shaders generated but never run on a Mac. No sub-tile camera / animated zoom.
- Translations: new UI strings and the 40 loading tips need translator review (marked `_todo`).
- Phase 2: game still uses SDL's default renderer, not SDL's GPU driver, for the UI.

## Working rules (carry forward)
- Owner approves the style and **every screen's wireframes individually** before implementation.
- Item and content art is never stretched: native aspect, integer/uplift scaling, small items centred at natural size.
- Derived or extracted game art never enters git; it is generated on the player's machine.
- Every visual PR carries screenshot proof on `pr-screenshots` (see [AGENTS.md](../../AGENTS.md)); the reviewer opens the images before merging.
- Subagent worktrees under `.claude/worktrees/` hold build dirs; build long paths via a `subst` drive.
