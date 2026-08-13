# RAVE Renderer — Evaluation & Correctness Review

**Scope:** `Source/RAVE_VIEW.C`, `Include/RAVE_VIEW.H`, and the RAVE emission taps in
`Source/3D_VIEW.C`, evaluated against the original software renderer
(`3D_VIEW.C` + `3D_ASM.ASM`), the map/texture data model (`3D_IO.C`,
`Include/VIEWFILE.H`, `Include/GRAPHICS.H`), and the palette/lighting system
(`COLOR.C`, `3D_TRIG.C`).

**Context / goal:** the software↔RAVE runtime switch is a comparison harness; the
intended destination is **RAVE as the sole renderer** so both paths don't run at
once. This review is written against that north star: anything the software path
provides "for free" (palette-index shading, palette animation, per-column masking)
must be reproduced *inside* RAVE, not leaned on.

---

## 1. How the RAVE backend currently works

RAVE is a **tap bolted onto the software column/span walk**, not an independent
renderer. The BSP/visibility walk in `View3dDrawView()` runs exactly as before;
at the points where it would rasterize a primitive, it *also* calls
`RaveViewEmitQuad/Sprite/Sky` with the corners it already projected
(`VIEW.C:196-199` brackets the walk with `RaveViewFrameBegin/End`).

- **Walls** — one whole quad per wall piece (main/upper/lower), emitted from
  `IAddWall` (`3D_VIEW.C:3001`), before screen clipping. HW z-buffer does occlusion.
- **Floors/ceilings** — emitted from `IDrawFloorRun` (`3D_VIEW.C:5223`), **one
  1-pixel-tall quad per scanline per span run** (`syT=row`, `syB=row+1`).
- **Sprites** — one billboard quad per object from `IDrawObjectAndWallRuns`
  (`3D_VIEW.C:4602`).
- **Sky** — two far-depth quads before the walk (`3D_VIEW.C:1610/1619`).

Each quad is uploaded as an **ARGB1555 texture** (palette baked in at upload,
`RAVE_VIEW.C:199-211`), drawn as two gouraud-lit triangles with per-vertex light
in `kd_*` under `kQATextureOp_Modulate` (`RAVE_VIEW.C:714-726`). The frame renders
to an offscreen VRAM buffer (`kQATag_DontSwap`), is **read back to system RAM and
converted to RGB555** every frame (`RaveViewFrameEnd`, `RAVE_VIEW.C:586-636`), then
CPU-composited into the SDL display under the UI (`RESSCALE.C:801`).

The single most important structural fact: **RAVE reuses the software renderer's
per-column / per-scanline geometry generation.** That decision is the root of both
the biggest correctness gaps (below) and the biggest performance gaps (§4).

---

## 2. Correctness issues

Severity: 🔴 breaks rendering / wrong output · 🟡 visibly wrong but bounded · 🟢 minor/cosmetic.

### 2.1 🔴 Lighting is linear RGB modulation, not the palette-index shade remap

**This is the central wrong assumption.** A&A does not darken by scaling RGB. It
lights every texel through a **palette-index REMAP**:

```
final_index = P_shadeIndex[light][source_index]   // 3D_VIEW.C:1071, a 64×256 byte table
on_screen   = palette[ final_index ]
```

`P_shadeIndex` is loaded verbatim from `MDAT.RES` (`3D_TRIG.C:445`) and applied to
**walls** (`3D_VIEW.C:7214`, ASM `3D_ASM.ASM:112`), **flats** (`3D_VIEW.C:7666`,
ASM `:900`), and **sprites** (`3D_VIEW.C:7091`, ASM `:501`). It is deliberately
**non-linear and hue-shifting** — as a surface darkens, the walk through palette
space bends toward the palette's dark ramp (e.g. a bright red browns out; A&A's
self-illuminating accent indices stay bright at low light instead of fading).

RAVE instead computes the *light level* faithfully (`IRaveShadeToLight` mirrors
`IDetermineShade`) but then **applies it as `texelRGB × light`** via
`kQATextureOp_Modulate` (`RAVE_VIEW.C:498, 720`). Consequences:

- Dark surfaces are the **wrong hue** — a uniform linear fade toward black instead
  of A&A's palette-ramp walk. This is exactly the mismatch documented for the UDB
  export (linear light cannot reproduce a palette remap).
- **Self-illum / fullbright accent texels darken** when they should stay bright
  (the shade table keeps specific indices bright; a linear multiply can't).
- Lighting is **per-vertex gouraud** (walls: two values, one per wall end; flats
  and sprites: one constant per run/sprite — `3D_VIEW.C:2984, 5211, 4579`), whereas
  the software path re-evaluates `IDetermineShade` **per column / per row**.

**This will never match the software renderer by construction.** It is the item
most worth an explicit product decision (accept the approximation, or reproduce the
table — see §5.2).

### 2.2 🔴 All palette animation is lost under RAVE

RAVE bakes the palette into each texture **once at upload** (`GrGetPalette` in
`IRaveUploadTexture`, `RAVE_VIEW.C:245`) and caches the result keyed by pixel
pointer, flushing only on level reload (`RaveViewFlushTextures`). But A&A animates
the *palette itself* every frame:

- **Damage / pickup / status full-screen tints** — `ColorUpdate` pushes
  `G_rval/G_gval/G_bval` offsets into the whole palette via `GrSetPalette` every
  frame (`COLOR.C:218-304`). Under RAVE these never reach the scene — no red
  damage flash, no pickup flash, no fade-to-black on transitions.
- **Animated "glow" colors** — `ColorGlowUpdate` rewrites palette indices
  **226–254** every frame (`COLOR.C:320-422`): pulsing reds, fire flicker, and the
  oscillating glow colors used by torches/effects (and by imported quest logos).
  In a baked RGB texture these are **frozen**.

Because textures are cached by pointer and only invalidated on level reload, no
palette change of any kind propagates. For a RAVE-only future this must be handled
deliberately (§5.3): global tints are cheap to reapply at composite time; animated
glow indices need a targeted path.

### 2.3 🔴 Non-power-of-two textures are rejected → missing surfaces

`IRaveUploadTexture` returns `NULL` (surface not drawn) for any texture whose width
or height is not a power of two (`RAVE_VIEW.C:236-239`). The comment assumes "AA's
textures already are (64×64, 128×128, …)". **They are not, in general:**

Measured across the 564 wall textures in `TEXTURE1` (from `AmuletsArmor.wad`):

| | count | notes |
|---|---|---|
| Power-of-two (both dims) | 504 (89%) | 64×64 (236), 128×128 (196), 128×256 (52), 256×128 (5)… |
| **Non-power-of-two** | **60 (11%)** | 16×13 (×36), 47×48 (×10), 320×200 (×5), 320×48, 168×104, 126×128, 65×15, 37×38, 29×25… |

Any map surface using one of those 60 textures renders as a **hole** (skipped) under
RAVE. Flats are also fetched through the same POT-gated path (`IRaveGetTexture`), so
a non-POT floor/ceiling texture vanishes too. (The sky is safe — it goes through
`IRaveUploadFlat`, which POT-pads, `RAVE_VIEW.C:753-787`.)

Fix is mechanical: POT-pad these like sprites/flats already do (§5.1).

### 2.4 🟡 Walls that cross the near plane are dropped whole

Walls are emitted as a single quad **before** any near-plane clipping; the only
guard is "skip if either end has `relativeFromZ/ToZ ≤ 0`" (`3D_VIEW.C:2939`). So a
wall you walk up to — one endpoint in front of the eye, one behind — is **discarded
entirely**, popping out of existence as you approach. The software path clips such
walls per column and keeps the visible part. RAVE needs real near-plane clipping of
the quad (split at `z=nearplane`) instead of an all-or-nothing reject.

### 2.5 🟡 Wall vertical texture scale is hard-coded to 1 texel/world-unit

RAVE sets `vTop = offY + (absoluteTop − absoluteBottom)`, `vBot = offY`
(`3D_VIEW.C:2971`) — i.e. exactly one texel per world unit of wall height. The
software path derives the vertical texel step from the wall matrix:
`V0 = −du·top − (u+0x8000) + (offY<<16)`, stepping by `−du`, where
`du = (G_wall.e<<2)/(bottomCalc>>4)` (`3D_VIEW.C:3146`). Wherever A&A's true
texel-per-unit differs from 1, **RAVE walls are vertically stretched/compressed**
relative to software, and the `offY` (`tmYoffset`) anchor is applied at a different
scale. (Note: A&A has **no Doom upper/lower-unpegged flags** — the sidedef carries
only `tmXoffset/tmYoffset`, `VIEWFILE.H:84-91` — so "no pegging" is *correct*; the
scale is the real bug, not pegging.)

### 2.6 🟡 Depth mapping collapses precision near the camera

`IRaveZ(invW) = clamp(1 − invW·16, 0, 1)` (`3D_VIEW.C:2820`) with a magic `16.0f`.
Any geometry closer than depth 16 (`invW > 1/16`) clamps to `z = 0`, so **all near
geometry shares one depth bucket** — z-fighting and wrong occlusion up close, on top
of `kQAContext_DeepZ`. The mapping should be a proper near/far projection of the
actual world depth range, not an ad-hoc linear-in-1/w clamp.

### 2.7 🟡 Sprites: the "~40% crop", colorized objects, and translucency

- **Crop bug (flagged in-code):** `RAVE_VIEW.C:327-343` still carries the `#if 0`
  red-fill diagnostic for "barrel renders as ~40% crop." The column-decode offset
  math itself is correct (it matches the software decode
  `p_picture[offset − start − 4]`, `3D_VIEW.C:4308-4315`), and the clamp
  `if (en >= h) en = h−1` (`RAVE_VIEW.C:355`) will silently drop rows if the passed
  `h` is smaller than the raster's actual `end` values — the most likely crop
  source. Root-cause needs the stubbed diagnostic run on hardware (does the solid
  red fill the full sprite height or ~40%?), then compare `h`/screen-extent to
  `ObjectGetPictureHeight` and the raster's max `end`.
- **Colorized objects not reflected:** the software path can recolor an object via
  `ColorizeMemory` → `G_colorizedObject` before rasterizing (`3D_VIEW.C:4348-4362`)
  — player team colors, status effects. RAVE caches the sprite texture by the
  *original* picture pointer, so **recolored objects show their base colors**.
- **Translucency is approximated:** software translucent objects/walls use a
  palette-space blend LUT `G_translucentTable[p_shade[c]][dst]`
  (`3D_VIEW.C:7098-7127`); RAVE uses a flat hardware `alpha = 0.5` blend
  (`3D_VIEW.C:3001`, `RAVE_VIEW.C:719`). Different result, generally acceptable.

### 2.8 🟢 Horizontal wrap relies on HW tiling (mask dropped)

Software masks the horizontal texel with `& sizeX` (a POT wrap mask,
`3D_VIEW.C:3303`); RAVE drops the mask and relies on HW texture repeat
(`RAVE_VIEW.C:712`). Fine for POT textures with a repeat wrap mode set — but note
the code never explicitly sets a wrap mode (`kQATexture_None`), so tiling correctness
depends on the engine default being *repeat*, not *clamp*. Worth verifying on
hardware; a clamped wall would show one stretched copy instead of tiling.

### 2.9 🟢 Things the port got right (validated)

- **Texture storage order is handled correctly.** A&A textures are column-major
  (`GRAPHICS.H:27`, `PICS.C:397`); `IRaveUploadTexture` reads `p[col*h + row]` and
  transposes to row-major (`RAVE_VIEW.C:247-255`) — matches the data model.
- **Index-0 transparency** is mapped to alpha-0 + alpha-test cutout
  (`RAVE_VIEW.C:203, 505`) — the right idea for masked walls and sprites.
- **Flat world-grid alignment** is correct in principle: floors/ceilings emit
  `u=worldX, v=worldY` (`3D_VIEW.C:5205-5213`), matching Doom's fixed 64-grid flat
  mapping (`(worldX&63)<<6 | (worldY&63)`); floor scroll offsets are applied and
  ceiling offsets skipped, mirroring the software path.
- **Sky panorama** is split into two quads to preserve horizontal wrap.

---

## 3. Summary parity table

| Aspect | Software (ground truth) | RAVE now | Status |
|---|---|---|---|
| Wall U | `(offX − v) & sizeX`, per column | per-endpoint, perspective-correct via invW | ✅ ok (HW tiling) |
| Wall V | `−du·top − (u+0x8000) + offY<<16`, du from `G_wall.e` | `offY + wallHeight` (1 texel/unit) | 🟡 scale mismatch |
| Wall clip | per-column, near-plane clipped | whole quad, dropped if crosses near plane | 🔴 pop-out up close |
| Flat U/V | world grid, per-row perspective | world grid, per-scanline quads | ✅ correct / 🔴 perf |
| Lighting | `palette[P_shadeIndex[light][texel]]` per texel | `texelRGB × light` gouraud | 🔴 wrong hue |
| Palette anim (fades/glow) | live `GrSetPalette` each frame | baked at upload, frozen | 🔴 lost |
| Non-POT textures | drawn | rejected → not drawn | 🔴 holes |
| Sprite transparency | index 0 skipped | alpha-0 + alpha-test | ✅ ok |
| Sprite recolor | `ColorizeMemory` per object | base texture cached | 🟡 lost |
| Translucency | `G_translucentTable` LUT | HW alpha 0.5 | 🟡 approx |
| Depth | true per-column Z | `1 − invW·16` clamp | 🟡 precision loss |

---

## 4. Performance issues (relevant to the "RAVE-only" goal)

The current design will **not** deliver the intended speedup, because it keeps the
software geometry generator on the CPU and adds HW work on top.

1. **Floors/ceilings emit one quad per scanline** (`3D_VIEW.C:5216`). A full-screen
   floor is hundreds of 1-pixel-tall degenerate quads per sector run — thousands of
   `QADrawTriTexture` calls per frame for surfaces that should be a handful of
   polygons. This is the dominant HW-side cost and defeats the point of hardware
   rasterization.
2. **The software walk still runs in full even when RAVE presents.**
   `G_raveSkipSoftwareRaster = RaveViewIsPresenting()` (`3D_VIEW.C:1531`) skips only
   the innermost pixel writes. The BSP traversal, per-column wall projection
   (`IAddWall`), and **per-scanline floor world-coordinate math** (`IDrawFloorRun`)
   all still execute on the G3 to *produce* the quads. Moving "everything to RAVE"
   as currently structured removes pixel-fill but keeps the bulk of the CPU cost.
3. **Per-frame VRAM readback + CPU composite.** `RaveViewFrameEnd` reads the whole
   render target back to RAM and converts it (`RAVE_VIEW.C:586-636`); `RESSCALE.C`
   then scales+overlays it into SDL. That's **two full-view CPU passes every frame**
   over the 3D view — a large cost on a PowerBook G3, and a big part of why running
   both renderers at once is slow. It exists only to composite under the SDL UI.
4. Minor: the texture cache is a linear probe (`IRaveCacheFind`, `RAVE_VIEW.C:377`) —
   fine at a few hundred textures, but it's on the per-surface path.

---

## 5. Recommendations

Ordered so the comparison harness keeps working throughout, ending at a
RAVE-only-capable renderer.

### 5.1 Near-term correctness (keep the current tap architecture)

- **POT-pad wall/floor textures** instead of rejecting them (§2.3). Reuse the
  sprite/flat POT-pad path: allocate `INextPow2(w)×INextPow2(h)`, copy the texels,
  normalize UVs by the POT size. For the few *tiling* non-POT walls the padding edge
  may show; most non-POT textures are non-tiling decals/switches, so this recovers
  nearly all missing surfaces immediately.
- **Near-plane clip walls** (§2.4): split the wall quad at the near plane and emit
  the front part, rather than dropping walls that cross `z≤0`.
- **Fix the depth mapping** (§2.6): project true world depth into `[0,1]` with a
  real near/far, removing the magic `16.0f` clamp.
- **Reapply global palette tints at composite time** (§2.2, cheap win): apply
  `G_rval/G_gval/G_bval` (and any fade) as an add/multiply on the RGB555 readback in
  `ResScaleRaveComposite`. This restores damage/pickup/fade flashes for the whole
  RAVE view with no texture re-upload.
- **Resolve the sprite crop** (§2.7): run the stubbed red-fill diagnostic; if the
  fill is full-height the bug is in the column decode/`h` clamp, else in the
  billboard's vertical screen extent.
- **Set an explicit texture wrap mode** (repeat) rather than relying on the engine
  default (§2.8).

### 5.2 Reproducing the shade table (the fidelity decision)

Linear gouraud will never match `P_shadeIndex`. Options, best-fidelity first:

- **Baked shade levels.** Pre-shade each texture through `P_shadeIndex` at the light
  levels actually used, upload those as separate RGB textures, and select per
  primitive by the surface's discrete light index. This reproduces the *exact*
  palette-ramp look. Cost is VRAM (64 levels of everything is far past ~5 MB), so
  bake **on demand per (texture, lightLevel) pair actually seen**, cache with LRU
  eviction. This keeps per-primitive lighting (matching the software's per-run
  granularity) and the correct hue walk.
- **Accept linear gouraud** as a deliberate approximation and document it. Cheapest;
  the swap harness lets the user judge whether it "reads" acceptably. If kept,
  consider a small correction curve to bias the fade toward the palette's dark ramp.
- **CLUT/dependent-texture** (paletted textures + an animated CLUT) would solve both
  shading *and* palette animation elegantly — but the Rage LT Pro RAVE engine is
  very unlikely to expose indexed textures; verify before pursuing.

### 5.3 Palette animation under RAVE-only

- **Global fades/tints:** the composite-time tint in §5.1 fully covers these.
- **Animated glow indices (226–254):** in RGB textures they're frozen. Practical
  options: (a) accept frozen glow (minor for most content); (b) for surfaces known
  to use glow indices, upload a companion "glow mask" (alpha = glow-index present)
  and blend an animated color at composite/emit time; (c) re-upload only the small
  set of glow-using textures each frame. Start with (a), escalate to (b) if the loss
  is visible on real content.

### 5.4 Architectural (required to make RAVE the sole, faster renderer)

The current tap inherits the software renderer's per-column/per-scanline model. To
actually offload the G3 and be RAVE-only:

- **Emit whole polygons, not software spans.** Tessellate each visible sector
  floor/ceiling into a few triangles (sector polygon triangulation) and emit once,
  instead of a quad per scanline (§4.1). Emit each wall as one clipped quad (already
  close — just add near-plane clipping). This is the single biggest change and the
  one that unlocks the HW win.
- **Decouple geometry generation from the software rasterizer** so that when RAVE
  presents, the CPU does *only* BSP visibility + polygon setup, not per-column
  projection and per-scanline world-coordinate stepping (§4.2). The
  `extract-3d-geo` decoupling this branch forked from is the right seam to push on.
- **Eliminate the per-frame readback** (§4.3): render RAVE to a target that can be
  HW-composited or presented directly, and move the UI overlay into the RAVE frame
  (draw the UI as textured quads, or use a HW blit for the overlay) so the CPU stops
  doing two full-view passes per frame. Until then, the readback is the price of
  compositing under SDL and will cap the speedup.

### 5.5 Keep the swap harness

The runtime software↔RAVE toggle (`RaveViewToggle`, F-key + `resolution.ini`) is the
right tool and should stay through all of the above — it's how each fix gets
validated against ground truth before the software path is retired. Retire the
software renderer only after the parity table in §3 is all ✅ (or consciously
accepted), and after §5.4 removes the software geometry generator from the hot path
(otherwise "RAVE-only" saves little).

---

## 6. Suggested order of work

1. POT-pad textures + near-plane wall clip + depth fix + composite-time tint — these
   turn the RAVE view from "holes, popping, wrong flashes" into "structurally
   correct," cheaply, without touching the architecture. *(§5.1)*
2. Resolve the sprite crop and wrap-mode. *(§5.1)*
3. Decide the shading strategy (baked levels vs accept gouraud) and glow handling —
   the fidelity call. *(§5.2, §5.3)*
4. Tessellate floors/ceilings to whole polygons — correctness-neutral, large HW win.
   *(§5.4)*
5. Decouple geometry emission from the software rasterizer, then remove the readback.
   *(§5.4)*
6. Only then retire the software path. *(§5.5)*
