# Open art — a clean-room replacement set

> **Status: decided** (issue [#339](https://github.com/adhikasp/ja2-pecel-bakwan/issues/339)). This sets the
> policy for Phase 9 of [native-modern-game.md](native-modern-game.md). It supersedes the "upscale
> everything" reading of issue #35: upscaling the originals stays, for players who own them
> ([#350](https://github.com/adhikasp/ja2-pecel-bakwan/issues/350)), but it can never be open source.
> The open path is a replacement set made from scratch, the way OpenGFX replaced the original
> sprites of OpenTTD.

## Goal

The game's art is replaced, slot by slot, with art that is **new work**, openly licensed, and not
derived from the original game. In the end:

- every world texture, item picture and generic model is better and more detailed than the
  original, and open source;
- anything new the project adds (an item, a character) ships with open art from day one;
- a player without the original game files gets a complete, legal game's worth of art;
- a player **with** the original files can still play the original game: its characters, maps and
  names come from their own files, and are never distributed by this project.

## What "open" means here

| | Rule |
|---|---|
| **Licence** | CC BY-SA 4.0. Not NC: a non-commercial clause is not open. The engine repo keeps its own licence (see the README); the art does not inherit it |
| **Home** | A separate repository, `pecel-bakwan-gfx`, under the same owner. Released as a *base set* the engine fetches and loads. Binary art does not enter the engine repo's history |
| **Clean room** | No frame of the set is derived from original art (below) |
| **Provenance** | Every asset records who or what made it, how, and under which licence (below) |

The engine's existing decision stands: **derived content (upscaled or repainted originals) is never
committed**, to either repo. Local upscales are generated on the player's machine.

## The clean-room rule

An asset is clean-room when its production never used original art as input. Forbidden:

- tracing, redrawing or repainting an original frame, or working "from memory" of a specific one;
- img2img, inpainting, ControlNet, IP-adapter or any generation conditioned on an original image;
- training or fine-tuning (LoRA, textual inversion, DreamBooth) on original art;
- using original art as a style reference image in a prompt;
- scripts that transform original pixels into the output, other than the local upscale pipeline,
  which writes only to the player's machine.

Allowed: the **slot contract** (names, sizes, frame counts, offsets and the manifest categories)
and the **usage map**. They describe where art goes, not what it looks like.

Looking at the originals to learn what a slot is for is fine, since it is a requirement.
Reproducing how a specific frame looks is not.

### Identity rule (recognisable characters)

Recognisability is an *archetype*, not a depiction. An open character may evoke a role (the grizzled
veteran, the cocky kid, the quiet specialist) but must change **at least two** of: name, face,
silhouette, signature prop. Names, faces, bios, dialogue and voice lines of original characters
are never reused. The original characters, maps and names reach the player only from the player's
own files. Their art there is the local upscale.

## Allowed production methods

| Method | Notes |
|---|---|
| Hand-made | Drawn, painted or modelled by a contributor |
| Procedural | Generated from code or data: tiles, textures, radar and strategic maps. Deterministic |
| 3D renders | Generated or open 3D models rendered into sprites (8 directions, mask pass for recolour). Gives frame stability for free |
| Prompt-only image / 3D generation | Open-weights model, text prompt only, never conditioned on originals. A **human edit or composition pass** is required before an asset counts as shipped |

**Model weights.** Any weights may be used whose licence grants the right to redistribute the
outputs under CC BY-SA 4.0 (a non-commercial or no-redistribution output term disqualifies a
model). This is reviewed per model, not allowlisted in advance, and the review is recorded:
model, version, a link to the licence text and the date it was read. The art repo keeps a model
register so a licence change on a model does not silently invalidate its past output.

**Copyright of raw generation.** The copyright status of unedited generated output is weak or
absent in many jurisdictions, so a CC BY-SA licence on it is only partly meaningful. The human pass
is what turns a draft into authored work and is the reason it is required.

## Provenance

Each asset in the art repo's manifest carries:

| Field | Meaning |
|---|---|
| `slot` | The slot id from the engine manifest it fills |
| `author` | Person or handle responsible |
| `method` | `hand`, `procedural`, `render`, `generated+edited` |
| `tool` | Software and version; for generation, the model, its version, licence link and date read |
| `seed` / `prompt` | For generated and procedural assets, enough to reproduce or audit |
| `licence` | The licence of the asset (CC BY-SA 4.0, or the licence of an incorporated CC0/CC BY-SA input, with its source) |
| `inputs` | Any third-party assets incorporated (e.g. a CC0 texture), with source and licence |

CI rejects an asset with a missing field ([#341](https://github.com/adhikasp/ja2-pecel-bakwan/issues/341)).

## Gates

The existing gates in `tools/assets/gates.py` (exact scale multiple, silhouette/alpha, colour
drift, frame stability, tile seams, size budget) keep checking the **slot geometry**. Two more
are added:

- **Dissimilarity gate** ([#341](https://github.com/adhikasp/ja2-pecel-bakwan/issues/341)): perceptual
  hash and structural similarity against the extracted originals. It fails when an open frame is
  *too close* to an original. This is the inverse of the colour-drift gate and catches accidental
  tracing.
- **Style gate** ([#343](https://github.com/adhikasp/ja2-pecel-bakwan/issues/343)): open frames and
  locally upscaled originals meet the same density, palette range and lighting direction, so both
  can sit side by side in one inventory or one scene.

Contribution is by PR with a checklist line stating that no original art was used as input. There
is no CLA or DCO sign-off; the provenance fields and the dissimilarity gate are the control.

## Who sees what

| Player | Art | Characters, maps, names |
|---|---|---|
| Without original files | Open set (placeholder where a slot is not yet done) | The open campaign, when it exists ([#351](https://github.com/adhikasp/ja2-pecel-bakwan/issues/351)); nothing original |
| With original files | The original game, locally upscaled ([#350](https://github.com/adhikasp/ja2-pecel-bakwan/issues/350)). New content (items, characters we add) uses open art | Arulco, from their files |

Slot resolution order, per slot ([#340](https://github.com/adhikasp/ja2-pecel-bakwan/issues/340)):
**open set**, then **local original or its upscale**, then **placeholder**. The order is
reversed per category if a player with original files asks for the original look everywhere; the
setting is a base-set choice, not a code path.

## Scope

- **In:** art: faces for the open cast, items, world tiles, models and animations, load screens,
  radar and strategic maps, and the loader, provenance and gates that support them.
- **Out:** maps, merc and item data, text, voices and music. A game with zero original assets
  also needs those; that is the open-campaign placeholder
  ([#351](https://github.com/adhikasp/ja2-pecel-bakwan/issues/351)), whose first issue is a plan doc.
  Replacing Arulco's maps makes a **new setting**, not Arulco.
- **Curate.** The open set does not need slot-for-slot parity. Many original slots may map to few
  open tiles. Order of work follows the usage map (`withUsage`).

## Order of work

1. **Foundation:** this policy, the slot contract and loader ([#340](https://github.com/adhikasp/ja2-pecel-bakwan/issues/340)),
   provenance and dissimilarity gates ([#341](https://github.com/adhikasp/ja2-pecel-bakwan/issues/341)),
   style target ([#343](https://github.com/adhikasp/ja2-pecel-bakwan/issues/343)), the art repo and a
   procedural placeholder set so the game renders with no original files
   ([#342](https://github.com/adhikasp/ja2-pecel-bakwan/issues/342)).
2. **Open by construction:** radar and strategic maps ([#344](https://github.com/adhikasp/ja2-pecel-bakwan/issues/344)).
3. **Content in usage order:** items ([#345](https://github.com/adhikasp/ja2-pecel-bakwan/issues/345)),
   models and animations ([#346](https://github.com/adhikasp/ja2-pecel-bakwan/issues/346)),
   world tiles by tileset ([#347](https://github.com/adhikasp/ja2-pecel-bakwan/issues/347)),
   load screens ([#348](https://github.com/adhikasp/ja2-pecel-bakwan/issues/348)).
4. **Keep it clean:** CI rule that new items and characters ship open art
   ([#349](https://github.com/adhikasp/ja2-pecel-bakwan/issues/349)).

Each batch is its own PR with a comparison sheet, a mixed (open beside upscaled original) sheet and
the gate report.

## Risks

| Risk | Mitigation |
|---|---|
| Accidental resemblance to an original | Dissimilarity gate; identity rule (two of four changed); provenance review |
| A model's licence changes or is misread | Per-model review recorded with link and date; model register in the art repo |
| Weak copyright on generated output | Human edit pass required; prefer procedural and rendered methods for volume |
| Two styles look wrong side by side | Style gate and the mixed sheet in every batch |
| Volume (OpenGFX took years) | Usage-first order; curated subset; placeholder set keeps the game complete meanwhile |
| Legal exposure outside our reading | The clean-room rule and gates reduce it; a legal read before the first public release of the base set is recommended and is not claimed done by this document |
