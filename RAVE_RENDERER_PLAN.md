# RAVE Renderer — Remediation Plan

Companion to `RAVE_RENDERER_EVALUATION.md`. This plan turns that evaluation's
findings into sequenced, actionable work. It is written for the reality that
**hardware testing happens in a later session** (PowerBook G3 Lombard / Mac OS 9,
ATI Rage LT Pro), so it deliberately separates *code that can be written and
staged now* from *validation that must wait for the machine*.

---

## Guardrails (do not regress these)

Two hard-won facts from prior sessions — violating either wastes a hardware trip:

1. **The sprite vertical flip is CONFIRMED CORRECT — keep it.** Sprites use
   large-v (picHeight) at the screen-TOP corner, matching the wall UV convention.
   It was twice mistaken for the crop and reverted; do not touch it. The crop is a
   *separate*, size-dependent bug.
2. **The depth bias was already proven wrong and removed.** The sprite crop is not
   a depth/z problem. Do not re-add a sprite depth bias to "fix" the crop.

The runtime **software↔RAVE toggle stays through all of this** (F-key +
`resolution.ini`). It is the only ground-truth check we have; the software path is
retired only at the very end (Phase 5), and only after the parity table is all-green
or consciously accepted.

---

## Strategy: two tracks

The evaluation's findings fall into two fundamentally different buckets:

- **Track A — Correctness within the current "tap" architecture.** Mechanical,
  low-risk fixes that make the RAVE view structurally correct (no holes, no
  pop-out, right flashes). These do *not* touch the software-geometry-generator
  seam. Most can be **written and staged now**; each is gated behind a discrete
  hardware check.
- **Track B — Architectural rework for RAVE-as-sole-renderer.** Whole-polygon
  emission, decoupling geometry from the software rasterizer, killing the per-frame
  readback. This is where the actual speedup lives, and it is the larger, riskier
  effort. It should only start after Track A has the view looking right.

Do Track A first. It is cheap, it is validatable one fix at a time, and it makes the
comparison screenshots meaningful for judging Track B later.

---

## Phase 0 — Prep (now, no hardware)

- [ ] **Confirm the build/test loop is documented and current.** CW8 on aa-tiger
      via `cmdide` (foreground only), SIZE re-bake (`aasize.r`), `make_img.sh` →
      USB (HFS+, not FAT), F-key toggle for A/B screenshots. Cross-check against
      the OS9 build memories before the next hardware session so no step is
      rediscovered live.
- [ ] **Stage the sprite-crop diagnostic as the FIRST thing to run on hardware.**
      The solid-red-fill test is already in-code at `RAVE_VIEW.C:327` (`#if 0`).
      Leave it ready; Phase 2 flips it to `#if 1` in the first hardware build.
- [ ] **Note the recurring empty-quest-list build flake.** It has blocked test
      builds twice (last a red diagnostic build that was never evaluated). Before
      shipping the next test img, verify `MAPDESC/` is present in the staged `Run/`.
      Not RAVE-related, but it silently invalidates a hardware trip.

---

## Phase 1 — Structural correctness, low-risk (write now, validate next session)

These are the "turn holes/popping/wrong-flashes into structurally correct" fixes
from evaluation §5.1 / §6.1. All can be coded now against current source.

> **Implementation status — AUTHORED 2026-08-13 (not yet built/tested on hardware).**
> Code for 1.1, 1.3, 1.4, 1.5 is written on `renderer/rave` (uncommitted). 1.2 was
> investigated and found **already handled upstream** — see below. Nothing here has
> been compiled (needs CW8 on aa-tiger) or run on the G3. Files touched:
> `Source/RAVE_VIEW.C`, `Source/3D_VIEW.C`, `Source/COLOR.C`, `Include/COLOR.H`.

### 1.1 POT-pad wall/floor textures instead of rejecting them  🔴 §2.3
- **Problem:** `IRaveUploadTexture` (`RAVE_VIEW.C:236-239`) returns NULL for any
  non-power-of-two texture. 60 of 564 wall textures (11%) are non-POT → those
  surfaces render as **holes**. Flats go through the same gate.
- **Fix:** Reuse the sprite POT-pad approach (`IRaveUploadSprite`,
  `RAVE_VIEW.C:298`, uses `INextPow2`). Allocate `INextPow2(w) × INextPow2(h)`,
  copy texels into a transparent-padded buffer, and have the wall/flat emitters
  normalize U/V by the POT size (the way the sprite emitter already does).
- **Caveat:** POT-padding a *tiling* non-POT wall will show a seam at the pad edge.
  Most non-POT textures are non-tiling decals/switches, so this recovers nearly all
  missing surfaces; accept the seam on the rare tiling case for now.
- **Done:** `IRaveUploadTexture` (`RAVE_VIEW.C`) now pads to `INextPow2(w)×INextPow2(h)`
  with a transparent border (index-0 transparency preserved), placing real texels at
  `[0..w)×[0..h)`; `RaveViewEmitQuad` normalizes U/V by the POT size to match.
  `INextPow2` forward-declared for use before its definition.
- **Validate:** load a map known to use a non-POT texture (e.g. the 320×200 or
  16×13 sets); the previously-black surface now draws.

### 1.2 Near-plane clip walls — ❌ NOT NEEDED (evaluation §2.4 is incorrect)
- **Finding:** The evaluation claimed a wall crossing the near plane is dropped
  whole by the `relativeFromZ/ToZ > 0` guard at `3D_VIEW.C:2939`. **It is not.**
  Near-plane clipping already happens upstream in `IIsSegmentGood`
  (`3D_VIEW.C:2201-2222`): when either end has `Z < ZMIN`, it interpolates that
  end's X along the tangent line and clamps its Z to `ZMIN` (`ZMIN = 10<<16`,
  `3D_VIEW.C:809`) **before** any of the three `IAddWall` callers run. So the Z
  values reaching the RAVE guard are always ≥ 10, the guard never rejects a
  visible wall, and the emitted quad corners are already the near-plane-clipped
  endpoints (screen X and texture U are recomputed from the clipped values). This
  is exactly what the software path does — there is no pop-out and no whole-wall
  drop from this mechanism.
- **Action taken:** No clipping code added (it would duplicate `IIsSegmentGood`
  and risk regressions). Added a clarifying comment at the guard so this isn't
  re-derived. If a *different* pop-out is ever observed on hardware, revisit — but
  the mechanism described in §2.4 is not present.

### 1.3 Fix depth mapping (remove the magic 16.0f)  🟡 §2.6
- **Problem:** `IRaveZ(invW) = clamp(1 − invW·16, 0, 1)` (`3D_VIEW.C:2820`) collapses
  everything nearer than depth 16 into one z-bucket → z-fighting / wrong occlusion up
  close.
- **Fix:** Map true world depth into `[0,1]` with a real near/far projection instead
  of the ad-hoc linear-in-1/w clamp. Derive near/far from the engine's actual view
  range. Keep `kQAContext_DeepZ`.
- **Done:** `IRaveZ` (`3D_VIEW.C`) now returns `1.0001 * (1 - invW)` — the perspective
  mapping for near=1, far=9999 (the engine's max-depth clamp), i.e. depth 1→z 0,
  depth 9999→z ~1, monotonic, spreading near geometry across the range. Walls,
  sprites and floors all route through this one function; the sky's hardcoded far
  `z=0.9999` stays behind all real geometry (only walls past depth ~5000 approach it).
- **Validate:** stand where two surfaces are near and nearly co-planar; the flicker
  should resolve into stable ordering.

### 1.4 Reapply global palette tints at composite time  🔴 §2.2 (partial), cheap win
- **Problem:** Damage/pickup/status full-screen tints and fades (`ColorUpdate` →
  `GrSetPalette`, `COLOR.C:218-304`) never reach the baked RAVE textures → no red
  damage flash, no fade-to-black.
- **Fix:** Apply the current `G_rval/G_gval/G_bval` offset (and any active fade) as
  an add/multiply on the RGB555 readback — either in `RaveViewFrameEnd`
  (`RAVE_VIEW.C:597-632`, per pixel during conversion, essentially free) or in
  `ResScaleRaveComposite` (`RESSCALE.C`). No texture re-upload needed.
- **Done:** new `ColorGetTintDelta(r,g,b)` (`COLOR.C`/`COLOR.H`) exposes the dynamic
  6-bit `(G_rval+G_rfilt …)` offset (0 when `OPTIONS_COLORON` is off; gamma excluded
  because it's already baked at upload). `RaveViewFrameEnd` adds it (>>1 to 5-bit,
  clamped) to the RGB555 read-back in a pass that is **skipped entirely when the tint
  is zero**, so the no-flash case costs nothing.
- **Known limitation:** a texture that first *uploads* mid-flash bakes that tint (then
  gets it again at composite → slight double-tint). Rare (textures upload at load when
  tint≈0); revisit only if visible. This is the global-tint half of §2.2; the animated
  glow indices 226–254 remain frozen (Phase 4).
- **Validate:** take damage / pick up an item / trigger a level fade; the whole RAVE
  view should tint, matching software.

### 1.5 Set an explicit texture wrap mode (repeat)  🟢 §2.8
- **Problem:** Wall tiling relies on the engine default being *repeat*; the code
  never sets a wrap mode (`kQATexture_None`). If the default is *clamp*, tiling walls
  show one stretched copy.
- **Fix:** Set repeat wrap explicitly at texture/submit time.
- **Done (documented, not code-enforced):** RAVE *repeats by default*; clamp is opt-in
  via the `kQATextureOp_Clamp_U/_V` bits, which the render state deliberately does not
  set. Since `RAVE.h` isn't in-repo (Mac SDK on aa-tiger only), introducing that
  constant blind would risk a build break for no behavior change — so this is captured
  as a comment at the `kQATag_TextureOp` set, flagged for hardware verification. If a
  tiling wall shows one stretched copy instead of repeats on hardware, that's where to
  add the explicit repeat (and confirm the exact constant against the SDK).
- **Validate:** a wall that should tile horizontally shows repeats, not a stretch.

**Phase 1 exit criteria (hardware):** no missing surfaces, no wall pop-out, stable
near-depth ordering, damage/fade flashes present, tiling walls tile. Software A/B
screenshots differ mainly in *shading hue* (Phase 3), not in *structure*.

---

## Phase 2 — Sprite crop + wrap resolution (needs hardware to root-cause)  🟡 §2.7

This one **cannot be fully closed without the machine** — it needs the diagnostic
run. Everything up to that point is staged now.

- **Known facts:** barrel = 38w×36h, decodes full (col-19 opaque st=0..en=35),
  screen extent ~44px, but only ~40% draws. Size-dependent: tall close sprites
  (elven archers) survive; short/distant ones vanish. Ruled out: depth occlusion,
  real inversion, index-0-as-black, UV sampling range.
- **Step 1 (hardware):** flip `RAVE_VIEW.C:327` to `#if 1`, rebuild, deploy, and
  actually evaluate it this time.
  - **Full-height red box** ⇒ crop is in the **column decode / `h` clamp**
    (`RAVE_VIEW.C:346-358`; `if (en >= h) en = h−1` silently drops rows if `h` is
    smaller than the raster's real `end`). Then: compare passed `h` against
    `ObjectGetPictureHeight` and the raster's max `end`.
  - **Still-short red box** ⇒ crop is in the **billboard geometry / vertical screen
    extent** (`3D_VIEW.C:4602` sprite emit). Then: audit top/bottom corner Y and the
    POT-size UV normalization.
- **Step 2:** apply the fix indicated by Step 1; re-shoot A/B against software.
- **Cleanup:** once solved, remove the temp diagnostics noted in the state memory
  (object-0 dist/z/extent + sky readout in `3D_VIEW.C`; sky-tex-NULL log +
  `MESSAGE.H`/`stdio.h` includes in `RAVE_VIEW.C`) and the `#if 0` red-fill block.

**Also open here: the sky quad is still black** (per state memory, not fully covered
in the evaluation). Texture uploads fine (`sky=1`, no NULL log); suspect the backdrop
quad orientation/depth (`View3dDrawView` ~line 1590). Treat as a sibling to the crop:
diagnose on the same hardware trip.

---

## Phase 3 — The shading fidelity decision  🔴 §2.1 / §5.2  ⟵ NEEDS A PRODUCT CALL

This is the single most important correctness gap and a genuine fork in the road.
A&A does **not** darken by scaling RGB; it remaps through a palette-index shade
table (`P_shadeIndex[light][index]`, 64×256). RAVE's linear `texelRGB × light`
gouraud **can never match this by construction** — dark surfaces get the wrong hue
and self-illuminating accent indices wrongly fade.

**Options (evaluation §5.2), best-fidelity first:**

| Option | Fidelity | Cost | Notes |
|---|---|---|---|
| **A. Baked shade levels** | Exact palette-ramp look | VRAM + upload churn | Pre-shade each texture through `P_shadeIndex` at the light levels *actually seen*; upload per (texture, lightLevel) pair, LRU-evict. Keeps per-primitive light granularity. |
| **B. Accept linear gouraud** | Approximation | ~free | Document it; optionally add a correction curve biasing the fade toward the palette's dark ramp. The swap harness lets us judge if it "reads" acceptably. |
| **C. Paletted texture + animated CLUT** | Solves shading *and* palette anim | Depends on HW | Elegant, but the Rage LT Pro RAVE engine almost certainly does not expose indexed textures. **Verify feasibility before pursuing.** |

**Recommendation:** decide *after* Phases 1–2 make the view otherwise correct, so
the shading difference can be judged in isolation on real A/B screenshots. My default
lean is **B first (cheap, ships, judged on hardware), escalate to A** only if the hue
loss reads as unacceptable on real content. C is contingent on a HW capability check
that's worth doing opportunistically (it would also solve Phase 4's palette-anim
problem).

This is flagged as a **decision the user should make** once Phase 1–2 screenshots
exist. It gates how much work Phase 3 actually is (near-zero for B, substantial for A).

---

## Phase 4 — Remaining palette animation  🔴 §2.2 / §5.3

Global tints/fades are already handled by 1.4. What remains is the **animated glow
indices 226–254** (`ColorGlowUpdate`, `COLOR.C:320-422`) — torches, fire flicker,
pulsing effect colors — which are frozen in baked RGB textures.

Escalation ladder (start low, climb only if the loss is visible on real content):
- **(a) Accept frozen glow.** Minor for most content. Default.
- **(b) Companion glow-mask.** For surfaces known to use glow indices, upload a
  glow-present alpha mask and blend an animated color at composite/emit time.
- **(c) Re-upload only glow-using textures each frame.** Small set, targeted.

If Phase 3 lands on Option C (CLUT), this problem largely dissolves — another reason
to run the CLUT-capability check early.

---

## Phase 5 — Architectural rework for RAVE-as-sole-renderer  §4 / §5.4

Only start after Phases 1–3 make the RAVE view visually acceptable. This is where the
actual speedup is — the current design is *slower* than software on this hardware
because it keeps the CPU geometry generator AND adds HW work AND does a per-frame
readback.

### 5.1 Tessellate floors/ceilings to whole polygons  §4.1 — biggest HW win
- **Problem:** floors/ceilings emit **one 1-pixel-tall quad per scanline**
  (`3D_VIEW.C:5216`) → thousands of degenerate `QADrawTriTexture` calls/frame.
- **Fix:** triangulate each visible sector floor/ceiling into a few triangles and
  emit once. Correctness-neutral (world-grid UV mapping is already right, §2.9),
  large HW-cost reduction.

### 5.2 Decouple geometry generation from the software rasterizer  §4.2
- **Problem:** `G_raveSkipSoftwareRaster` (`3D_VIEW.C:1531`) skips only pixel writes;
  the BSP walk, per-column wall projection (`IAddWall`), and per-scanline floor
  world-coordinate math still run on the G3 to *produce* quads.
- **Fix:** push on the `extract-3d-geo` seam this branch forked from so that when
  RAVE presents, the CPU does only BSP visibility + polygon setup — not per-column /
  per-scanline stepping. See `[[project_renderer_decouple_branching]]`.

### 5.3 Eliminate the per-frame VRAM readback + CPU composite  §4.3
- **Problem:** `RaveViewFrameEnd` reads the whole target back to RAM and converts it
  (`RAVE_VIEW.C:586-636`); `RESSCALE.C` then scales+overlays into SDL — **two full
  view passes on the CPU every frame**, purely to composite under the SDL UI.
- **Fix:** render RAVE to a directly-presentable/HW-compositable target and move the
  UI overlay into the RAVE frame (UI as textured quads, or a HW blit). This is the
  cap on any speedup and the reason running both renderers at once is slow.
- **Note:** the composite-time tint from 1.4 must move with this (it currently rides
  the readback pass); fold it into whatever replaces the readback.

### 5.4 Minor: texture cache is a linear probe  §4.4
- `IRaveCacheFind` (`RAVE_VIEW.C:377`) is O(n) on the per-surface path. Fine at a few
  hundred textures; revisit only if profiling flags it after 5.1–5.3.

---

## Phase 6 — Retire the software path  §5.5

Only after the §3 parity table is all-green or consciously accepted, AND Phase 5 has
removed the software geometry generator from the hot path (otherwise "RAVE-only" saves
little). Keep the toggle until this point.

---

## Sequencing summary

| Order | Work | Hardware needed? | Risk |
|---|---|---|---|
| 0 | Prep: build loop, stage diagnostic, guard the quest-list flake | no | — |
| 1 | POT-pad, near-plane clip, depth fix, composite tint, wrap mode | write now / validate next | low–med |
| 2 | Sprite crop diagnostic + fix; sky quad | **yes (diagnostic)** | med |
| 3 | Shading fidelity decision (B default, A/C contingent) | judge on HW | low (B) / high (A) |
| 4 | Glow-index animation (accept → mask → re-upload ladder) | judge on HW | low–med |
| 5 | Tessellate flats → decouple geometry → kill readback | yes | high |
| 6 | Retire software path | yes | gated |

**What can be authored before the next hardware session:** all of Phase 1, the
Phase 2 diagnostic staging, and the mechanical parts of 5.1 (flat tessellation is
largely CPU-side and testable in software-comparison terms). **What is blocked on
the machine:** the sprite-crop root cause, the sky quad, every A/B fidelity judgment,
and the CLUT-capability check that gates Phases 3C/4.

## Open decisions for the user
1. **Shading strategy (Phase 3):** accept linear gouraud (B) as the shipping default,
   or commit to baked shade levels (A) up front? Recommend deciding after Phase 1–2
   screenshots exist.
2. **How far to push RAVE-only (Phase 5+):** is the goal "visually correct RAVE view
   that can be toggled" (stop after Phase 3/4), or "RAVE is the sole, faster renderer"
   (do Phase 5–6)? Phase 5 is the large, risky effort and only pays off if the answer
   is the latter.
