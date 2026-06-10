# Renderer Improvement Plan 2

Last updated: 2026-06-10 (PERF-001/002/004/005/006/007/008 fixed; PERF-003 fixed on Metal, deferred on OpenGL)

## Goal

Improve the performance of both the raster renderer and the ray tracing (RT)
renderer under `src/render`, **without sacrificing any visualization quality**.
Every item below is a scheduling, bandwidth, or redundant-work fix: the
rendered image must remain pixel-equivalent (or converge to the same image)
before and after each change.

This file is the working checklist for that effort. Problems were identified
during a full code review of `src/render` (both backends) and the viewport
layer that drives the renderers. Items are fixed one by one, in the suggested
order at the bottom, and each fix is verified by building and running the app
before moving to the next.

Relationship to the previous plan (`archived/renderer_improvement_plan.md`):

- PERF-004 supersedes RT-003 (finer dirty tracking, extended to appearance-only updates).
- PERF-005 covers the parallelization stage of RT-010.
- The remaining old items (RT-004..RT-009) are not duplicated here and stay tracked in the archived plan.

## Implementation Progress

| ID | Status | Summary |
| --- | --- | --- |
| PERF-001 | Fixed | Remove per-frame `waitUntilCompleted` from the Metal raster renderer. |
| PERF-002 | Fixed | Render on demand in raster mode instead of repainting every vsync. |
| PERF-003 | Partial | Batch multiple RT accumulation samples per command buffer (Metal done; OpenGL deferred). |
| PERF-004 | Fixed | Split geometry-dirty vs. appearance-dirty so color/selection changes skip BVH rebuild. |
| PERF-005 | Fixed | Parallelize BVH construction across subtrees. |
| PERF-006 | Fixed | Skip per-primitive alpha fetches in RT traversal when the scene is fully opaque. |
| PERF-007 | Fixed | Reuse GPU scene buffers instead of reallocating on every upload. |
| PERF-008 | Fixed | Skip the 4x MSAA display target when no overlay is drawn (RT display pass). |
| PERF-009 | Not fixed | Restore early-Z for sphere impostors via a `[[depth(less)]]` pipeline variant. |
| PERF-010 | Not fixed | Hoist per-instance bond math out of the per-vertex shader path. |
| PERF-011 | Not fixed | Guard per-frame `sampleCountChanged` signal emission in the viewport. |

Status meanings:

- `Not fixed`: no implementation work has been completed yet.
- `Partial`: some infrastructure exists, but the item is not complete.
- `Fixed`: implemented and verified.

## Work Items

### PERF-001: Metal raster renderer blocks the CPU every frame

Problem:

- `MetalRenderer::render()` ends with `[cmdBuffer waitUntilCompleted]`
  (`src/render/metal/MetalRenderer.mm`, end of `render()`).
- This fully serializes CPU and GPU: the Qt render thread stalls for the whole
  GPU frame, then the GPU idles while the next frame is encoded. Frame time
  becomes CPU + GPU instead of max(CPU, GPU).

Why it matters:

- This is the highest-impact raster-path problem, and the cost grows with
  scene size.
- The RT renderer already solved the same problem (old plan RT-001) with a
  3-slot output texture ring plus command-buffer completion handlers — the
  raster renderer never received that fix.

Proposed solution:

- Adopt the RT renderer's pattern: a small ring of output color textures
  (plus matching MSAA/depth targets), slot states driven by
  `addCompletedHandler`, and `colorTexture()` returning the latest completed
  slot.
- The wait currently exists so Qt never samples a half-rendered texture;
  slot ownership provides the same guarantee without stalling.
- Keep at most one frame in flight initially (matching the RT renderer) —
  that alone restores CPU/GPU overlap.

Suggested implementation notes:

- Factor the slot/async-state machinery shared with
  `MetalRayTracingRenderer.mm` into a reusable helper if it falls out
  naturally; do not force it.
- `MetalViewport`'s texture cache already handles multiple native textures
  per renderer (it was built for the RT slot ring).
- Verify with a large structure that FPS improves and no flicker/tearing or
  stale-frame artifacts appear when orbiting.

Relevant files:

- `src/render/metal/MetalRenderer.mm`
- `src/render/metal/MetalRenderer.h`
- `src/ui/components/MetalViewport.mm`

Current status:

- Fixed.

Verification notes:

- Slot/async-state machinery extracted into the shared header
  `src/render/metal/MetalAsyncOutput.h` (used by both Metal renderers; the RT
  renderer's local duplicate was removed).
- `MetalRenderer` now renders into a 3-slot texture ring with completion
  handlers; `waitUntilCompleted` removed from the render path. `colorTexture()`
  returns the latest completed slot; `hasPendingRender()` / `needsMoreFrames()`
  expose scheduling state to the viewport. `createRenderTargets()` bumps the
  output generation so frames in flight against old-size textures are dropped,
  never presented.
- Built (`cmake --build build`) and ran the app: clean startup, no Metal
  validation errors.

### PERF-002: Raster mode repaints continuously even when idle

Problem:

- Step 10 of `MetalViewport::updatePaintNode` calls `update()`
  unconditionally in raster mode (`src/ui/components/MetalViewport.mm`),
  so a fully static scene re-renders at vsync forever.

Why it matters:

- Wasted GPU/CPU time, fan noise, and battery drain while the user is not
  interacting.
- The RT path already renders on demand via `needsMoreFrames()`; raster
  should behave the same.

Proposed solution:

- Stop the unconditional `update()` in raster mode. All camera, settings,
  and structure changes already call `update()` through the property
  setters and mouse handlers, so on-demand repaint requires no new wiring.
- Re-check the FPS counter logic: with on-demand rendering an "FPS" that
  counts repaints will read low when idle. Either freeze the displayed FPS
  when no frames are requested or report frame time during interaction only.

Suggested implementation notes:

- Coordinate with PERF-001: while a raster frame is in flight and a newer
  repaint request arrives, one trailing `update()` must be scheduled so the
  last state is always presented (no stale final frame after the user stops
  orbiting).
- Verify: idle viewport shows ~0% GPU in Activity Monitor; interaction
  remains visually identical and responsive.

Relevant files:

- `src/ui/components/MetalViewport.mm`
- `src/ui/components/OpenGLViewport.cpp` (same pattern, if present)

Current status:

- Fixed (Metal viewport; the OpenGL viewport keeps its continuous raster loop
  — it renders synchronously into Qt's FBO and is the fallback path; gate it
  later if it matters).

Verification notes:

- Added `computeRasterFrameHash()` (`src/render/common/RenderStateHash.cpp`)
  covering camera, all image-affecting settings, overlays, tessellation, and
  viewport size.
- `MetalViewport::updatePaintNode` renders raster frames only when the hash
  changed, the scene/appearance changed, or a previous request was dropped
  while in flight; `update()` is scheduled only while
  `MetalRenderer::needsMoreFrames()` is true, so an idle viewport schedules
  no frames (with a safety net that forces a render if no output exists and
  nothing is in flight).
- Measured with `top` on an idle viewport: ~6-10% CPU before, ~0.1% after.
- FPS counter note: the displayed FPS freezes at its last value when idle
  (frames are only counted when presented); acceptable for now.

### PERF-003: RT accumulates only one sample per vsync tick

Problem:

- Each RT command buffer encodes exactly one accumulation pass
  (`renderRTPass` called once in `MetalRayTracingRenderer::render()`), and
  only one frame is ever in flight.
- Convergence speed is therefore capped by display refresh: 500 samples take
  at least 8.3 s at 60 Hz even if one sample costs 2 ms of GPU time.
- The OpenGL RT path has the same one-sample-per-`render()` structure
  (`RayTracingRenderer::render()`).

Why it matters:

- Time-to-converged-image is the primary UX metric of the RT mode; small and
  medium scenes converge an order of magnitude slower than the GPU allows.

Proposed solution:

- Encode N accumulation passes per command buffer (N render encoders into
  the accumulation texture, incrementing the per-pass `frameCount` uniform
  so RNG sequences stay distinct, exactly as if N frames had run).
- Choose N adaptively from the previous command buffer's measured GPU time
  (`GPUEndTime - GPUStartTime`), targeting a fixed budget (~30 ms) so
  interaction stays responsive; clamp N to `maxSamples - sampleCount`.
- OpenGL equivalent: loop the RT draw N times per `render()` with a frame
  timer (`GL_TIME_ELAPSED` query or CPU timing) driving N.

Suggested implementation notes:

- The accumulation-clear logic (`m_accumNeedsClear`) must apply only to the
  first pass in a batch.
- `m_sampleCount` and `trackSubmittedFrame`'s `submittedSampleCount` must
  reflect the batch total.
- Image quality is identical by construction: the same additive samples are
  accumulated, just submitted in batches. Verify converged output matches
  the pre-change output for a fixed sample count.

Relevant files:

- `src/render/metal/MetalRayTracingRenderer.mm`
- `src/render/metal/MetalRayTracingRenderer.h`
- `src/render/opengl/RayTracingRenderer.cpp`

Current status:

- Partial: Metal implemented; OpenGL deferred.

Verification notes:

- Metal: `render()` encodes an adaptive batch of RT passes per command buffer
  (30 ms GPU budget, capped at 256 samples/submit). Batch size is derived
  from per-sample GPU time measured via `GPUStartTime`/`GPUEndTime` in the
  completion handler (stored in `AsyncFrameState::gpuNanosPerSample`).
- The first sample after an accumulation reset is always submitted alone, so
  interaction (camera moves reset accumulation per frame) keeps 1 cheap
  sample per frame; batching ramps up once the camera is still (4 samples
  until the first timing arrives).
- The deferred accumulation clear applies only to the first pass of a batch;
  `frameCount` increments per pass so RNG sequences stay distinct — the
  accumulated image is identical to N single-sample frames by construction.
- OpenGL deferred: its RT pass renders synchronously on the Qt render thread
  and the QOpenGLFunctions wrapper lacks `GL_TIME_ELAPSED` queries; batching
  without GPU timing risks multi-frame UI stalls. Revisit if the OpenGL RT
  path becomes a priority (e.g. via QOpenGLTimerQuery).

### PERF-004: Color/selection changes rebuild the entire BVH

Problem:

- `uploadSceneData()` is monolithic in both RT renderers: any invalidation
  (selection highlight click, color-scheme switch, transparency change)
  repacks positions, repacks bonds, and rebuilds the full BVH, even when
  geometry did not change.
- The raster sub-renderers have the same coarse granularity
  (`setAtomData`/`setBondData` repack everything).

Why it matters:

- Selection and appearance interactions are frequent; on large structures
  each click currently costs an O(N log N) BVH rebuild instead of an O(N)
  color-buffer copy.
- Supersedes/extends old plan item RT-003.

Proposed solution:

- Split dirty state into:
  - geometry-dirty (atom positions/radii, bond endpoints/radii) → repack
    geometry + rebuild BVH + reset accumulation
  - appearance-dirty (atom colors, bond colors, selection highlight) →
    re-upload only the color buffers + reset accumulation
- Add a corresponding invalidation entry point (e.g.
  `invalidateAppearance()`) on the `Renderer` interface and route
  `onStructureStyleChanged` / selection updates through it from the
  viewports.

Suggested implementation notes:

- `packAtomRenderColors()` and the bond color packing already exist as
  separable steps; the work is in the dirty-flag plumbing.
- Be careful: bond radius changes affect BVH bounds (old plan RT-005), so
  bond-radius edits must stay on the geometry-dirty path.
- Verify: clicking atoms in a large structure no longer hitches; the
  highlight renders identically.

Relevant files:

- `src/render/common/Renderer.h`
- `src/render/metal/MetalRayTracingRenderer.mm` / `.h`
- `src/render/opengl/RayTracingRenderer.cpp` / `.h`
- `src/render/metal/MetalRenderer.mm` (raster dirty flags)
- `src/ui/components/MetalViewport.mm`
- `src/ui/components/OpenGLViewport.cpp`

Current status:

- Fixed.

Verification notes:

- `Renderer::invalidateAppearance()` added with a default falling back to
  full atom+bond invalidation (used as-is by the raster renderers, which
  have no BVH); both RT renderers override it with an appearance-only
  upload (`uploadAppearanceData()`) that re-uploads atom colors and bond
  endpoint colors, resets accumulation, and skips geometry packing and the
  BVH rebuild. A count-mismatch guard falls back to a full upload.
- New `packBondRenderColors()` helper in `BondRenderData` packs bond colors
  (selection highlight applied) without building full segments.
- `StructureModel` gained `structureGeometryChanged()`: emitted by
  `applyAtomScaleToSelection` and `applyBondRadiusToSelection` (radii feed
  BVH bounds) and by `resetSelectedObjects` (alongside
  `structureStyleChanged`, which remains the appearance-only signal and the
  QML NOTIFY).
- Both viewports route `structureStyleChanged` → appearance invalidation and
  `structureGeometryChanged` → full structure update; the global
  color-scheme switch (`setAtomColorScheme`) is now appearance-only as well.

Problem:

- `buildNode()` in `src/render/common/BVH.cpp` is a serial recursive median
  split; for multi-million-atom structures the build dominates load and
  update latency.

Why it matters:

- Structure loads, trajectory frames, and (until PERF-004 lands) even
  appearance changes pay this cost on one core while the rest idle.
- This is the parallelization stage of old plan item RT-010.

Proposed solution:

- Parallelize the top of the tree: the two child-subtree builds at each node
  are independent. Spawn tasks (e.g. `std::async` or a small task pool) down
  to a fixed depth (~3-4 levels, i.e. 8-16 subtree tasks), then build
  serially below.
- Each parallel subtree must append into its own node/primitive-index
  arrays, merged (with index fix-up) at the end — the current shared
  `ctx.result` vectors are not safe to share across threads.

Suggested implementation notes:

- Keep the output format (`BVHNodeGPU`, primitive index list) identical so
  shaders are untouched.
- Determinism: fix the subtree→offset assignment order so the resulting
  tree is reproducible run to run.
- Benchmark build time before/after on a large structure; verify the traced
  image is unchanged.

Relevant files:

- `src/render/common/BVH.cpp`
- `src/render/common/BVH.h`

Current status:

- Fixed.

Verification notes:

- Deterministic three-phase build: a serial skeleton splits ranges down to
  ≤ 4 levels (≤ 16 tasks, sized from hardware concurrency), subtrees build in
  parallel via `std::async` into local results sharing the index array on
  disjoint ranges, then merge in fixed task order with node/primitive index
  fix-up. `BVHBuildOptions::maxParallelTasks` (0 = auto, 1 = serial) added.
- Parallel path engages only at ≥ 32768 primitives; below that the serial
  path runs unchanged.
- Standalone benchmark (clang++ -O2, random scenes, best-of-3): 2.60× at
  100k prims (24.7 → 9.5 ms), 2.65× at 1M (355 → 134 ms), 2.30× at 4M
  (1872 → 813 ms). Canonical DFS comparison (AABBs + sorted leaf primitive
  sets) confirms serial and parallel trees are exactly equivalent.
- Remaining serial fraction is the skeleton's top-level `computeStats` +
  `nth_element` passes over the full range (Amdahl); parallelizing those is
  a possible follow-up if BVH build remains a bottleneck.

### PERF-006: RT traversal fetches primitive alpha even in fully opaque scenes

Problem:

- `traceClosest` and `traceOcclusionAlpha` call `primitiveAlpha()` for every
  primitive in every visited leaf (1-2 color buffer reads per primitive) just
  to skip fully transparent primitives — on the hottest GPU loop, in both the
  Metal (`MetalShaderLibrary.mm`) and OpenGL (`RayTracingRenderer.cpp`)
  shaders.
- In the common case nothing in the scene is transparent.

Why it matters:

- Pure memory-traffic overhead on every primary, shadow, and AO ray.

Proposed solution:

- Compute a `hasTransparentPrims` flag CPU-side during scene upload (any
  atom or bond alpha < 0.999) and pass it in the RT uniforms.
- In the shaders, skip all `primitiveAlpha` fetches when the flag is false.
- Additionally, when the flag is false, shadow/occlusion rays can return on
  the first confirmed hit (they already early-out at alpha >= 0.999, so this
  is mostly about skipping the alpha reads before the intersection tests).

Suggested implementation notes:

- The flag must be refreshed on appearance-dirty updates (ties into
  PERF-004).
- Image is identical by definition when no primitive is transparent;
  transparent scenes keep the existing path. Verify both cases visually.

Relevant files:

- `src/render/metal/MetalShaderLibrary.mm`
- `src/render/metal/MetalTypes.h` (RTUniforms)
- `src/render/metal/MetalRayTracingRenderer.mm`
- `src/render/opengl/RayTracingRenderer.cpp`

Current status:

- Fixed.

Verification notes:

- New `packedColorsHaveTransparency()` helper in `BondRenderData` scans the
  already-packed RGBA arrays; both RT renderers compute the flag during
  scene upload AND appearance-only upload (alpha edits go through the
  appearance path), so it always tracks the latest colors.
- Metal: `RTUniforms::hasTransparency` replaced a pad field (layout size
  unchanged on both CPU and MSL sides); `traceClosest` and
  `traceOcclusionAlpha` take an `anyTransparent` flag. OpenGL: new
  `uHasTransparency` uniform, same shader logic.
- Opaque path skips all `primitiveAlpha` fetches and, with alpha pinned at
  1.0, occlusion rays early-out on the first confirmed hit. Flag changes
  always coincide with an accumulation reset (they ride the existing
  invalidation paths), so the image is unchanged by construction.

### PERF-007: GPU scene buffers are reallocated on every upload

Problem:

- Metal: `uploadSceneData()` allocates ~12 fresh `MTLBuffer`s with
  `newBufferWithBytes` on every scene update, and BVH node data is first
  copied into three temporary `std::vector`s before upload.
- OpenGL: every upload re-specifies storage via
  `glBufferData(..., GL_STATIC_DRAW)`.

Why it matters:

- Allocation churn and an extra full copy on every update; matters most for
  trajectory playback and for PERF-004's appearance-only updates.

Proposed solution:

- Metal: keep buffers across uploads, reallocate only when required capacity
  grows (with headroom), and write directly into `buffer.contents` (shared
  storage) instead of staging through temporary vectors.
- OpenGL: allocate with `GL_DYNAMIC_DRAW` once at capacity, then update via
  `glBufferSubData` (or orphan with `glBufferData(nullptr)` + fill).

Suggested implementation notes:

- With PERF-001/PERF-003 style async submission, a buffer being written must
  not still be referenced by an in-flight command buffer. The current
  "skip render work while a frame is in flight" gate in the RT renderer
  already prevents this; keep that invariant explicit when reusing buffers.
- Convert the BVH node packing loops to write straight into the destination
  buffer memory.

Relevant files:

- `src/render/metal/MetalRayTracingRenderer.mm`
- `src/render/opengl/RayTracingRenderer.cpp`
- `src/render/metal/MetalSphereRenderer.mm` / `MetalBondRenderer.mm` (same
  pattern at smaller scale)

Current status:

- Fixed (OpenGL raster sub-renderers left as-is — fallback path, small
  buffers via QOpenGLBuffer).

Verification notes:

- New shared header `src/render/metal/MetalBufferUtil.h`:
  `ensureSharedBuffer()` / `fillSharedBuffer()` reuse a shared-storage
  MTLBuffer when capacity suffices. Safety contract documented: writes only
  happen while no command buffer is in flight, which both Metal renderers
  guarantee by gating all upload work behind their single-frame-in-flight
  check (slot acquisition precedes uploads).
- Metal RT: all ~12 scene buffers reuse storage; BVH node min/max/meta data
  is written directly into `buffer.contents`, eliminating the three staging
  vectors. Unit-cell instance buffers and the raster sphere/bond instance
  buffers reuse storage too.
- OpenGL RT: `uploadTBOData()` member re-specifies with `GL_DYNAMIC_DRAW`
  only on growth (capacity map keyed by buffer id, cleared in `cleanup()`),
  otherwise updates in place via `glBufferSubData`.

### PERF-008: RT display pass always renders into a 4x MSAA target

Problem:

- `renderDisplayPass` draws the accumulated image as a fullscreen quad into
  a 4x MSAA BGRA8 texture and resolves it every frame
  (`MetalRayTracingRenderer.mm`), even when no overlay (unit cell, axes,
  rotation gizmo) is drawn. A fullscreen quad gains nothing from MSAA.

Why it matters:

- ~4x color write bandwidth plus a resolve pass per displayed frame, paid
  continuously during accumulation.

Proposed solution:

- When no overlay will be encoded this frame, render the display quad
  directly into the single-sample output texture (no MSAA attachment, no
  resolve).
- Keep the existing MSAA path whenever any overlay is enabled, so overlay
  edge quality is unchanged.

Suggested implementation notes:

- The render pass descriptor is already built per frame; this is a branch on
  the same condition used for overlay encoding.
- Verify output is pixel-identical with overlays off, and overlay
  antialiasing is unchanged with overlays on.

Relevant files:

- `src/render/metal/MetalRayTracingRenderer.mm`
- `src/render/metal/MetalShaderLibrary.h/.mm` (single-sample display pipeline)

Current status:

- Fixed.

Verification notes:

- `renderDisplayPass` branches on the same overlay condition used for
  encoding (unit cell with data, rotation gizmo, viewport axes). With no
  overlay, the quad renders straight into the resolved output texture via a
  new single-sample display pipeline variant (`displayPipelineSingleSample`,
  aliasing the MSAA pipeline when the device runs single-sample anyway) —
  no MSAA target writes, no resolve, no depth attachment.
- Output is pixel-identical: a fullscreen quad covers every pixel exactly
  once, so MSAA resolve of N identical samples equals the single sample.
- Runtime launch confirmed all pipelines (including the new variant)
  compile and create successfully.

### PERF-009: Sphere impostor fragment shader disables early-Z

Problem:

- `sphere_fragment` declares its depth output `[[depth(any)]]`
  (`MetalShaderLibrary.mm`), which forces late-Z: in dense scenes every
  overlapping billboard fragment runs the full ray-sphere intersection and
  shading even when occluded.

Why it matters:

- Overdraw-heavy viewpoints (looking into a thick slab of atoms) pay the
  full fragment cost per layer.

Proposed solution:

- For the inner sphere surface, the written depth is always <= the billboard
  plane's interpolated depth, so `[[depth(less)]]` is legal and lets the GPU
  keep conservative early-Z.
- The outline shell writes depth *behind* the plane and would violate
  `[[depth(less)]]`, so: compile a second fragment variant / pipeline with
  `[[depth(less)]]` and select it when outlines are disabled (the common
  case); keep the `[[depth(any)]]` pipeline for outline-enabled rendering.

Suggested implementation notes:

- Caveat to verify during implementation: confirm the depth(less) contract
  holds in orthographic mode as well (back-surface fallback when the camera
  is inside a sphere writes deeper depth — if that path can trigger with
  depth(less), bind the variant only when it cannot, or drop this item).
- Verify identical images with outlines on and off, then profile a dense
  scene front-to-back vs. back-to-front.

Relevant files:

- `src/render/metal/MetalShaderLibrary.mm`
- `src/render/metal/MetalSphereRenderer.mm` (pipeline selection)

Current status:

- Not fixed.

### PERF-010: Bond vertex shader recomputes per-instance values per vertex

Problem:

- `bond_vertex` and `bond_outline_vertex` view-transform both bond
  endpoints, build the orthonormal basis, and compute `splitT` for every
  vertex of every cylinder instance (~80-160 vertices per bond), although
  these values are constant per bond per frame
  (`MetalShaderLibrary.mm`).

Why it matters:

- Vertex-stage cost scales as bonds x segments; only significant for very
  bond-heavy scenes or high `cylinderSegments`, hence lower priority.

Proposed solution:

- Compute the per-bond frame data (view-space endpoints, basis vectors,
  splitT, effective radius) once per frame into a small per-instance buffer
  via a lightweight compute pass (or on the CPU during encode if bond counts
  are modest), and have the vertex shader read it by `instance_id`.

Suggested implementation notes:

- Keep the existing shader path as a fallback; this only pays off above some
  bond count — measure before committing.
- Output must be bit-identical math; verify split-color boundaries and
  outlines look unchanged.

Relevant files:

- `src/render/metal/MetalShaderLibrary.mm`
- `src/render/metal/MetalBondRenderer.mm`

Current status:

- Not fixed.

### PERF-011: Viewport emits sampleCountChanged every RT frame

Problem:

- `updatePaintNode` assigns `m_sampleCount` and emits `sampleCountChanged()`
  unconditionally every frame in RT mode
  (`src/ui/components/MetalViewport.mm`), even when the value did not change
  (e.g. while a frame is in flight).

Why it matters:

- Needless QML binding re-evaluation during accumulation. Trivial fix.

Proposed solution:

- Emit only when the value actually changed, like every other property
  setter in the file.

Relevant files:

- `src/ui/components/MetalViewport.mm`
- `src/ui/components/OpenGLViewport.cpp` (if it has the same pattern)

Current status:

- Not fixed.

## Suggested Order Of Work

1. PERF-001: raster async submission (unblocks PERF-002)
2. PERF-002: raster render-on-demand
3. PERF-003: RT multi-sample batching
4. PERF-004: geometry/appearance dirty split
5. PERF-007: buffer reuse (pairs naturally with PERF-004)
6. PERF-005: parallel BVH build
7. PERF-006: opaque-scene alpha-fetch skip
8. PERF-008: conditional MSAA display target
9. PERF-011: signal guard (can be folded into any nearby change)
10. PERF-009: early-Z pipeline variant (needs careful validation)
11. PERF-010: per-instance bond precompute (measure first)

Each step should be verified independently: build (`cmake --build build`),
run the app, confirm identical visuals, and where relevant compare FPS /
convergence time / GPU utilization before and after.

## Update Instructions

When an item changes, update:

- the `Implementation Progress` table
- the `Current status` field inside the relevant work item
- any notes about accepted design decisions or rejected alternatives
