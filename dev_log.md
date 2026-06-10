# Development Log

This file records development sessions and decisions for future reference.

---

## 2026-06-10: Renderer Performance Pass 2 — Parallel BVH Build, Opaque-Scene Alpha Skip, GPU Buffer Reuse, Conditional MSAA Display

### Summary

Second implementation session of `renderer_improvement_plan_2.md`, fixing items PERF-005 through PERF-008. As with pass 1, all changes preserve visualization quality exactly.

The BVH builder now parallelizes across subtrees: a serial skeleton phase performs the same median splits as before down to at most four levels, up to 16 subtree builds then run concurrently via `std::async` on disjoint ranges of a shared index array, and the results merge in fixed task order with node/primitive index fix-up. A standalone benchmark measured 2.3–2.65× faster builds (1M primitives: 355 → 134 ms; 4M: 1.87 s → 0.81 s) with a canonical-DFS comparison proving the serial and parallel trees exactly equivalent.

Both RT shaders (Metal MSL and OpenGL GLSL) now skip all per-primitive alpha fetches when the scene contains no transparent primitives — a flag computed CPU-side during scene and appearance uploads. In the opaque case, shadow/AO occlusion rays also early-out on the first confirmed hit instead of continuing to accumulate alpha.

GPU scene buffers are now reused across uploads instead of reallocated: Metal gained shared `ensureSharedBuffer`/`fillSharedBuffer` helpers (BVH node data is written directly into mapped buffer memory, eliminating three staging vectors), and the OpenGL RT renderer re-specifies TBO storage only on growth, updating in place with `glBufferSubData` otherwise.

Finally, the Metal RT display pass renders directly into the resolved output texture when no overlay (unit cell, gizmo, axes) is drawn, via a new single-sample display pipeline — eliminating the 4× MSAA color writes, the resolve, and the depth attachment for overlay-free frames.

### Files Modified

| File | Purpose |
|------|---------|
| `src/render/common/BVH.h/.cpp` | PERF-005: three-phase parallel build (skeleton split, parallel subtrees, ordered merge); `BVHBuildOptions::maxParallelTasks` (0 = auto, 1 = serial); helpers factored so serial and skeleton paths share split logic |
| `src/render/common/BondRenderData.h/.cpp` | PERF-006: `packedColorsHaveTransparency()` helper scanning packed RGBA arrays |
| `src/render/metal/MetalTypes.h` | PERF-006: `RTUniforms::hasTransparency` replaced a pad field (struct size unchanged) |
| `src/render/metal/MetalShaderLibrary.h/.mm` | PERF-006: `anyTransparent` parameter through `traceClosest`/`traceOcclusionAlpha` with opaque fast path; PERF-008: single-sample display pipeline variant |
| `src/render/metal/MetalBufferUtil.h` | PERF-007: new shared `ensureSharedBuffer`/`fillSharedBuffer` helpers with documented in-flight safety contract |
| `src/render/metal/MetalRayTracingRenderer.h/.mm` | PERF-006: transparency flag computed in both upload paths and passed in RT uniforms; PERF-007: buffer reuse everywhere, BVH nodes written directly into buffer contents; PERF-008: overlay-free direct display path |
| `src/render/metal/MetalSphereRenderer.mm` / `MetalBondRenderer.mm` | PERF-007: instance buffer reuse |
| `src/render/opengl/RayTracingRenderer.h/.cpp` | PERF-006: `uHasTransparency` uniform + shader fast path; PERF-007: `uploadTBOData()` with capacity tracking (`GL_DYNAMIC_DRAW` + `glBufferSubData`) |
| `src/render/CMakeLists.txt` | Registered the new `MetalBufferUtil.h` header |

### Architecture Decisions

#### 1. Parallel BVH Build Is Deterministic and Tree-Identical by Construction
- The skeleton phase performs exactly the same `computeStats`/`nth_element` splits as the serial path; subtrees are appended in fixed task order, so output is reproducible run to run and independent of thread scheduling
- Subtree contexts share one index array but partition strictly disjoint ranges, so no synchronization is needed during the parallel phase
- Verified by a canonical DFS comparison (per-node AABBs plus sorted leaf primitive sets) between serial and parallel builds at 100k/1M/4M primitives — exactly equal
- Parallelism only engages at ≥ 32768 primitives; the speedup ceiling (~2.6×) comes from the serial skeleton's top-level O(N) passes, noted in the plan as a possible follow-up

#### 2. The Transparency Flag Lives With the Color Data, Not the Settings
- Whether alpha fetches are needed is a property of the uploaded colors, so the flag is computed from the already-packed RGBA arrays during scene upload and appearance upload (alpha edits arrive via the appearance path from pass 1)
- Flag changes therefore always coincide with an accumulation reset, making the optimization invisible in the image
- In the opaque case occlusion alpha is pinned to 1.0, which turns the existing `>= 0.999` accumulation checks into a first-hit early-out for shadow and AO rays with no extra branching

#### 3. Buffer Reuse Leans on the Single-Frame-In-Flight Invariant
- Both Metal renderers acquire an output slot (which fails while a frame is in flight) before any upload work, so rewriting shared-storage buffer contents can never race the GPU; `MetalBufferUtil.h` documents this contract explicitly
- Buffers grow but never shrink during a session (full release on structure clear/cleanup), trading a little memory for zero-allocation steady-state updates — the case that matters for trajectories and the pass-1 appearance updates
- OpenGL keeps a per-buffer capacity map because GL buffer objects don't expose their allocated size; `glBufferSubData` synchronization is the driver's responsibility

#### 4. Overlay-Free Display Needs Its Own Pipeline, Not Just Another Pass Descriptor
- Metal requires the pipeline's `rasterSampleCount` to match the render pass, so skipping MSAA required a second display pipeline compiled at sample count 1 (aliased to the MSAA one when the device already runs single-sample)
- A fullscreen quad covers every pixel exactly once, so resolving N identical samples equals the single sample — the no-MSAA output is pixel-identical, purely saving bandwidth (~4× color writes plus a resolve per displayed frame during accumulation)
- The branch reuses the exact overlay condition that gates overlay encoding, so the two can never disagree

### Build Commands

```bash
cmake --build build
./build/bin/atom-studio.app/Contents/MacOS/atom-studio
```

### Testing

- Built successfully; only the pre-existing macOS OpenGL deprecation warnings remain
- All CTest tests passed: `4/4`
- BVH benchmark (standalone clang++ -O2 harness over `BVH.cpp`, archived at `archived/bvh_bench.cpp`; random scenes, best-of-3): 2.60× at 100k prims, 2.65× at 1M, 2.30× at 4M, with serial/parallel tree equivalence verified by canonical DFS comparison at every size
- Runtime launch verified MSL compilation and all pipelines (including the new single-sample display pipeline) create successfully; app runs cleanly
- Recommended in-app visual checks (not yet performed): RT image identical with shadows/AO on for an opaque scene and for a scene with transparent atoms; RT display output identical with all overlays hidden; trajectory-style repeated structure updates for buffer-reuse stability

---

## 2026-06-10: Renderer Performance Pass 1 — Async Raster Submission, Render-on-Demand, RT Sample Batching, Appearance/Geometry Dirty Split

### Summary

First implementation session of `renderer_improvement_plan_2.md`, fixing items PERF-001 through PERF-004. All changes are scheduling, bandwidth, or redundant-work fixes with no visualization quality change.

The Metal raster renderer no longer blocks the CPU with `waitUntilCompleted` every frame: it renders into a 3-slot output texture ring with command-buffer completion handlers, reusing the asynchronous-submission design the Metal RT renderer already had (that machinery was extracted into a shared header used by both). On top of that, the Metal viewport now renders raster frames on demand — only when a state hash over camera, settings, overlays, and viewport size changes — instead of repainting at vsync forever; an idle viewport's CPU usage dropped from ~6-10% to ~0.1% (measured with `top`).

The Metal RT renderer now encodes an adaptive batch of accumulation samples per command buffer (30 ms GPU budget, sized from per-sample GPU time measured via `GPUStartTime`/`GPUEndTime`), so convergence is no longer capped at one sample per display refresh. The first sample after an accumulation reset is always submitted alone, keeping camera interaction at one cheap sample per frame.

Finally, scene invalidation was split into geometry-dirty vs. appearance-dirty. Selection highlights, per-selection colors/transparency, and global color-scheme switches now re-upload only color buffers in the RT renderers instead of repacking geometry and rebuilding the whole BVH; radius-affecting edits (selection atom scale, bond radius) still take the full geometry path because radii feed BVH bounds.

### Files Modified

| File | Purpose |
|------|---------|
| `renderer_improvement_plan_2.md` | New working plan for the performance effort (PERF-001..011); statuses and verification notes updated for this session |
| `src/render/metal/MetalAsyncOutput.h` | New shared header: output slot ring constants, lock-free `AsyncFrameState`, `acquireOutputSlot()`, `resetOutputSlots()`, and the per-sample GPU timing atomic |
| `src/render/CMakeLists.txt` | Registered the new shared header in the Metal source list |
| `src/render/metal/MetalRenderer.h/.mm` | PERF-001: 3-slot texture ring, completion-handler tracking, generation-based invalidation on resize, `colorTexture()` returns the latest completed slot, new `hasPendingRender()`/`needsMoreFrames()`; removed `waitUntilCompleted` |
| `src/render/metal/MetalRayTracingRenderer.h/.mm` | PERF-003: adaptive RT sample batching per command buffer with GPU-time feedback; PERF-004: `invalidateAppearance()` + `uploadAppearanceData()`; switched to the shared async-output header |
| `src/render/common/RenderStateHash.h/.cpp` | PERF-002: added `computeRasterFrameHash()` covering camera, all image-affecting settings, overlays, tessellation, and viewport size |
| `src/render/common/Renderer.h` | PERF-004: added virtual `invalidateAppearance()` with a full-invalidation default (used as-is by the BVH-less raster renderers) |
| `src/render/common/BondRenderData.h/.cpp` | PERF-004: added `packBondRenderColors()` to pack bond endpoint colors (with selection highlight) without building full segments |
| `src/render/opengl/RayTracingRenderer.h/.cpp` | PERF-004: appearance-only TBO re-upload path mirroring the Metal RT renderer |
| `src/ui/components/StructureModel.h/.cpp` | PERF-004: new `structureGeometryChanged()` signal for radius-affecting edits; `structureStyleChanged()` is now appearance-only (reset emits both) |
| `src/ui/components/MetalViewport.h/.mm` | PERF-002: hash-gated raster rendering and on-demand frame scheduling; PERF-004: appearance/geometry dirty routing; forced raster re-render on RT→raster mode switch |
| `src/ui/components/OpenGLViewport.h/.cpp` | PERF-004: appearance/geometry dirty routing (its continuous raster loop is intentionally unchanged) |

### Architecture Decisions

#### 1. The Raster Renderer Adopted the RT Renderer's Async Design, Extracted Into a Shared Header
- The RT renderer already solved safe async presentation (slot states, in-flight tracking, generation counters); duplicating it would have meant two diverging copies
- `MetalAsyncOutput.h` holds the pure-C++ parts (atomics, slot acquisition/reset policy); each renderer keeps its own completion handler since the per-slot texture sets differ
- `createRenderTargets()` bumps the generation counter so a frame in flight against old-size textures is marked Free (never Ready) by its completion handler — a never-rendered new texture can never be presented

#### 2. Render-on-Demand Is Gated by a Frame Hash, Not by Tracking Call Sites
- Every property setter and input handler already calls `update()`; the question is whether `updatePaintNode` should actually submit GPU work
- A raster-specific hash (`computeRasterFrameHash`) decides this; it is a superset of the RT accumulation hash, adding overlays, tessellation, and viewport size — fields that must NOT reset RT accumulation but do require a raster redraw
- The viewport keeps scheduling frames only while the renderer reports pending work (in-flight frame, dropped render request, or an unpresented completed frame), so the final frame after interaction always gets presented and the loop then stops
- Known cosmetic side effect: the FPS readout freezes at its last value when idle, since frames are only counted when presented

#### 3. RT Batching Preserves the Image by Construction and Stays Responsive by Policy
- A batch is N independent accumulation passes in one command buffer: per-pass `frameCount` increments keep RNG sequences distinct, and the deferred accumulation clear applies only to a batch's first pass — identical accumulated output to N single-sample frames
- N is derived from measured GPU nanoseconds per sample (stored atomically by the completion handler) against a 30 ms budget, capped at 256 samples per submit
- `m_sampleCount == 0` always submits exactly one sample, so interactive camera motion (which resets accumulation every frame) never pays batch latency
- OpenGL RT batching was deferred: its RT pass runs synchronously on the Qt render thread without GPU timer queries available through `QOpenGLFunctions`, so batching there risks multi-frame UI stalls; revisit via `QOpenGLTimerQuery` if the fallback path becomes a priority

#### 4. Appearance vs. Geometry Is Split at the Signal Level, Decided Where the Edit Happens
- `StructureModel` knows whether an edit touched radii (geometry) or only colors/alpha (appearance), so the split is expressed as two signals rather than inferred downstream
- `structureStyleChanged` remains the QML NOTIFY for selected-color/transparency properties and is now strictly appearance-only; `structureGeometryChanged` is emitted by selection atom-scale and bond-radius edits; `resetSelectedObjects` emits both
- RT renderers override `invalidateAppearance()` to re-upload only atom and bond color buffers (with a count-mismatch guard falling back to full upload); raster renderers use the base-class default since they have no BVH and full repack is already cheap
- A subtle interaction with render-on-demand: scene changes arriving while RT mode is active only set dirty flags on the raster renderer, so switching back to raster forces one re-render by resetting the frame hash

### Build Commands

```bash
cmake --build build
./build/bin/atom-studio.app/Contents/MacOS/atom-studio
```

### Testing

- Built successfully after each stage; only the pre-existing macOS OpenGL deprecation warnings remain
- Runtime launch verified clean Metal initialization (shader compilation, all pipelines) with no errors after the async raster conversion
- Idle CPU measured with `top` against a baseline build of the same tree (`git stash` round-trip): ~6-10% before, ~0.1% after — confirming both the removal of the per-frame GPU stall and the on-demand repaint gating
- Recommended in-app visual checks (not yet performed): orbit a loaded structure in raster mode (identical visuals, no stale frames after releasing the mouse), RT convergence speed after the camera stops (sample counter should climb much faster), selection clicks and color-scheme switches on a large structure (no hitch, identical highlight), and RT→raster mode switching after scene edits

---

## 2026-06-10: Stroke Outlines for Atoms and Bonds (Metal Raster + RT)

### Summary

Added solid, sharp stroke outlines along the visible silhouettes and occluding boundaries of atoms and bonds in both the Metal raster renderer and the Metal ray-tracing renderer, using the inverted-hull (shell) technique: each object is also rendered as a slightly inflated copy whose far/inner surface is shown, so ordinary depth testing produces outlines only where forms turn away or overlap. Where a bond meets the front-facing surface of an atom, the bond's shell stays inside the sphere and is depth-occluded, so the contact region remains continuous with no separating stroke — matching the target visual language without special-case logic.

Stroke width is constant in screen pixels in both perspective and orthographic projection, so a sphere's outline is a geometrically exact circle at any distance. This replaced earlier screen-space depth-edge-detection attempts, which produced orientation-dependent line thickness ("rounded square" silhouettes at small projected sizes).

Sidebar controls live under the `Structure` tab: a `Show Strokes` checkbox, a `Stroke Thickness` slider (0.5–6 px), and a stroke color picker (default black). The implementation plan for this session is archived at `archived/stroke_outline_implementation_plan.md`.

### Files Modified

| File | Purpose |
|------|---------|
| `src/render/common/RenderSettings.h` | Added `outlineEnabled`, `outlineWidth` (pixels), and `outlineColor` settings |
| `src/render/common/RenderStateHash.cpp` | Hashed the outline settings so RT accumulation resets when they change |
| `src/render/metal/MetalTypes.h` | Extended `SceneUniforms` (outline width/color/pixel scale) and `RTUniforms` (outline scale/color/world-max bound) |
| `src/render/metal/MetalShaderLibrary.h/.mm` | Sphere impostor shell logic; new `bond_outline_*` shaders and `bondOutlinePipeline`; RT shell intersection helpers and outline-aware `traceClosest`/`testNodeAABB`/`rt_fragment` |
| `src/render/metal/MetalBondRenderer.mm` | Second instanced draw of the bond cylinder mesh with front-face culling when outlines are active |
| `src/render/metal/MetalRenderer.mm` | Filled raster outline uniforms (pixel→world scale from `P[1][1]` and target height) |
| `src/render/metal/MetalRayTracingRenderer.h/.mm` | Filled RT outline uniforms; tracked scene bounds for conservative BVH AABB padding |
| `src/render/metal/MetalUnitCellRenderer.mm` | Zeroed outline width for the unit-cell object so its impostor corner joints do not grow shells |
| `src/ui/components/MetalViewport.h/.mm` | Added `outlineEnabled`/`outlineWidth`/`outlineColor` Q_PROPERTYs; width is dpr-scaled into `RenderSettings` |
| `src/ui/components/OpenGLViewport.h/.cpp` | Added the same Q_PROPERTYs for QML interface parity (OpenGL renderers ignore them for now) |
| `src/ui/qml/Sidebar.qml` | Added `Show Strokes`, `Stroke Thickness`, and stroke color controls under the `Structure` tab |

### Architecture Decisions

#### 1. Inverted Hull Instead of Screen-Space Edge Detection
- Screen-space depth-discontinuity detection was tried previously and rejected: edge thickness varies with silhouette orientation relative to the pixel grid, deforming circles into rounded squares when viewed from afar
- The inverted hull renders the outline as real geometry with an analytically exact silhouette, so stroke shape and width are correct by construction
- No explicit depth-layer manipulation is needed: showing the inflated copy's far/inner surface makes the real object win the depth test inside its own silhouette, while the stroke still occludes farther objects and is occluded by closer ones

#### 2. Bond–Atom Contact Continuity Falls Out of Depth Testing
- Where a bond emerges from an atom's front face, the bond's inflated shell is still inside the sphere, so the sphere surface depth-occludes it — no stroke at the contact
- The stroke begins only once the bond stands proud of the sphere by more than the shell width, giving the "bond emerges naturally from the atom" look without contact-detection logic
- The same rule suppresses strokes at interpenetrating atom–atom seams, matching the "no strokes at object–object contact boundaries" requirement

#### 3. Raster Atoms Need No Extra Draw Call
- The sphere impostor shader was extended instead: the billboard covers `R + w`, and fragments that miss the inner sphere but hit the inflated one output the outline color with depth from the inflated sphere's far intersection
- This is the analytic equivalent of an inverted hull at zero additional instance cost
- Raster bonds use the classic two-draw form: the same instanced cylinder mesh is re-drawn dilated (radius `+w`, ends extended by `w`) with front faces culled; scaling rather than normal-offsetting keeps the hull closed at cap rims

#### 4. RT Outlines Compete in the Same Closest-Hit Selection
- In `traceClosest`, when a primitive is missed, the inflated primitive's exit (far) intersection becomes an outline candidate; real hits and outline candidates compete for the global closest hit
- Outline hits shade as flat unlit color (no shadows or AO), and shadow/AO/any-hit rays ignore shells entirely
- BVH node AABB tests get a conservative per-frame `outlineWorldMax` expansion derived from the camera distance to the farthest scene-bounds corner, so shells near leaf boundaries are not culled

#### 5. Constant Pixel Width Via a Projection-Agnostic Scale Factor
- `pixelScale = 2 / (P[1][1] * viewportHeightPx)` is valid for both projections (`P[1][1] = 1/tan(fovY/2)` perspective, `2/orthoHeight` orthographic)
- World-space shell width is `widthPx × pixelScale × viewDepth` in perspective and `widthPx × pixelScale` in ortho, computed per instance (raster) or per primitive (RT)
- The QML-facing width is in logical pixels and multiplied by the device pixel ratio when building `RenderSettings`, matching the viewport-axes convention

#### 6. Overlay Objects Get No Strokes
- The unit cell, rotation gizmo, and viewport axes are informational overlays and must not be outlined
- The unit-cell renderer shares the sphere impostor pipeline for its corner joints, which initially inherited shells and showed bulging dark spheres at unit-cell vertices in raster mode (the joints are tiny, so a 2 px shell dominated them); fixed by zeroing `outlineWidthPx` in the unit-cell's local uniforms copy
- The gizmo and viewport-axes renderers were verified unaffected: they use flat-color pipelines with no outline logic

### Build Commands

```bash
cmake --build build
./build/bin/atom-studio.app/Contents/MacOS/atom-studio
```

### Testing

- Built successfully after each stage (settings, shaders, pipelines, viewport wiring, QML)
- Runtime launch verified MSL compilation succeeds and all pipelines (including the new bond outline pipeline) are created
- Runtime launch after the sidebar changes showed no QML errors or warnings
- User-reported regression (spheres appearing at unit-cell vertices in raster mode with strokes enabled) was diagnosed as the shared sphere impostor pipeline and fixed; clean startup re-verified
- Visual checks performed in-app: stroke circles at silhouettes, smooth bond–atom contacts, and the new Structure-tab controls; OpenGL backend rendering of outlines remains a follow-up

---

## 2026-06-09: Selected Atom Color, Transparency, and Reset Controls

### Summary

Added selected-only atom color and transparency customization under the `Atoms` sidebar tab, plus a `Reset selected objects` action under the `Selection` tab. Transparency uses the requested UI semantics where `0` is opaque and `100` is invisible.

Atom color/transparency changes now apply only to selected atoms. Bonds now store their own endpoint colors and alpha so unselected bonds do not automatically change when connected atoms are customized. A selected bond updates only when it is selected together with a connected selected atom; color updates affect the matching endpoint color, while transparency updates set the whole bond alpha from the latest selected connected atom change.

Ray-traced shadows and ambient occlusion now account for atom/bond alpha, so fully transparent objects do not contribute shadowing and partially transparent objects contribute proportionally weaker occlusion.

### Files Modified

| File | Purpose |
|------|---------|
| `src/ui/qml/Sidebar.qml` | Added selected atom color/transparency controls and the Selection-tab reset button |
| `src/ui/components/StructureModel.h/.cpp` | Added selected atom color/transparency APIs, reset logic, and selected-bond propagation rules |
| `src/data/BondList.h/.cpp` | Added persistent per-bond endpoint colors and whole-bond alpha storage |
| `src/data/Structure.h/.cpp` | Added bond color refresh helpers and preserved atom alpha during color-scheme changes |
| `src/render/common/BondRenderData.cpp` | Packed stored bond colors instead of deriving bond colors live from connected atoms |
| `src/ui/components/OpenGLViewport.cpp` | Kept global color-scheme changes synchronized with stored bond endpoint colors |
| `src/ui/components/MetalViewport.mm` | Kept global color-scheme changes synchronized with stored bond endpoint colors |
| `src/render/opengl/ShaderManager.cpp` | Discarded fully transparent raster atom/bond fragments |
| `src/render/opengl/OpenGLRenderer.cpp` | Enabled alpha blending for OpenGL raster rendering |
| `src/render/opengl/RayTracingRenderer.cpp` | Made OpenGL RT visibility, shadows, and AO alpha-aware |
| `src/render/metal/MetalShaderLibrary.mm` | Made Metal raster/RT visibility, shadows, and AO alpha-aware |
| `src/data/tests/StructureTest.cpp` | Added coverage for stored bond appearance and alpha preservation |

### Architecture Decisions

#### 1. Bond Appearance Is Stored, Not Derived Live
- Bonds previously derived endpoint colors from connected atom colors during render packing
- That would violate selected-only customization because unselected bonds would change when their connected atoms changed
- Bond endpoint colors and alpha now live in `BondList`, and renderers consume that stored state

#### 2. Atom Color and Transparency Are Independent
- Color customization updates RGB only and preserves atom alpha
- Transparency customization updates alpha only and preserves RGB
- Existing color-scheme paths were adjusted so color refreshes do not accidentally reset customized transparency

#### 3. Selected Bonds Follow Only Selected Connected Atom Changes
- Selected atom color changes copy the changed atom RGB into the corresponding endpoint of selected connected bonds
- Selected atom transparency changes write the selected connected atom alpha to the entire selected bond
- If the bond is not selected, it is left unchanged even when its connected atoms are selected and customized

#### 4. Reset Uses Current Scene Defaults
- The reset action restores selected atoms to default element radii, current color-scheme RGB, and full opacity
- Selected bonds reset to the current default bond radius, endpoint element colors for the current color scheme, and full opacity
- Selection is preserved so users can continue working with the same selected objects after reset

#### 5. Transparent Objects Must Not Cast Opaque Shadows
- RT shadow and AO tests now return alpha-weighted occlusion rather than a boolean hit
- Alpha `0` objects are skipped for RT primary visibility and shadow/AO contribution
- Raster shaders also discard alpha `0` fragments so fully invisible objects do not write depth

### Build Commands

```bash
cmake --build build
```

### Testing

```bash
ctest --test-dir build --output-on-failure
git diff --check
```

Validation results:

- Build completed successfully with the existing macOS OpenGL deprecation warnings.
- All CTest tests passed: `4/4`.
- `git diff --check` passed.
- A standalone Metal shader compile was attempted for the embedded runtime shader string, but the local machine is missing the Metal Toolchain (`xcodebuild -downloadComponent MetalToolchain`), so that extra shader syntax check could not be completed.

---

## 2026-05-04: Sidebar Selection Modes and Shared Selection Rendering

### Summary

Added a new `Selection` sidebar tab with three selection ranges: disabled selection, individual atom/bond selection, and connected molecule selection. Selection is click-toggle based in the active viewports, clears when the selection mode changes, and is reset by the existing `Reset to original` structure action.

Selected atoms and bonds receive a yellow highlight overlay, and supported style operations now apply only to the active selection when selection is enabled. When selection is disabled, the existing behavior is preserved by treating all atoms and bonds as selected for style operations.

Also fixed two regressions found during runtime validation:

- Added the missing `FileController::loadingStarted` signal used by `Main.qml`.
- Restored Metal ray-tracing unit-cell shader compatibility by adding a `traceAnyHit` overload for callers that still use a uniform bond radius.

### Files Modified

| File | Purpose |
|------|---------|
| `resources/icons/selection.svg` | Added the sidebar icon for the new Selection tab |
| `resources/resources.qrc` | Registered the Selection tab icon in Qt resources |
| `src/ui/qml/Sidebar.qml` | Added the Selection sidebar section and range dropdown |
| `src/ui/components/StructureModel.h/.cpp` | Added selection mode state, selected-only style operations, and selection reset handling |
| `src/ui/components/ViewportSelection.h/.cpp` | Added shared viewport click-selection behavior |
| `src/ui/components/OpenGLViewport.h/.cpp` | Integrated shared selection picking and selected-only style updates |
| `src/ui/components/MetalViewport.h/.mm` | Integrated shared selection picking and selected-only style updates |
| `src/data/Structure.h/.cpp` | Added atom selection state and structure-level selection helpers |
| `src/data/BondList.h/.cpp` | Added bond selection state and per-bond radius storage |
| `src/data/StructureOperations.h/.cpp` | Added unwrap-compatible connected selection graph helpers |
| `src/render/common/Picking.h/.cpp` | Added shared CPU ray picking for atoms and bonds |
| `src/render/common/BondRenderData.h/.cpp` | Added selected-object highlight packing and per-bond radius data |
| `src/render/opengl/*Renderer*` | Applied selection highlighting and per-bond radii for OpenGL raster/ray tracing |
| `src/render/metal/*Renderer*` | Applied selection highlighting and per-bond radii for Metal raster/ray tracing |
| `src/render/CMakeLists.txt` | Added shared picking source to the render target |
| `src/ui/CMakeLists.txt` | Added shared viewport selection source to the UI target |
| `src/ui/components/FileController.h/.cpp` | Added the `loadingStarted` signal consumed by QML |

### Architecture Decisions

#### 1. Shared Selection State Lives in Data Models

Selection state is stored directly on `Structure` and `BondList` instead of being renderer-specific. This keeps selection persistent across renderer changes and lets the UI model apply operations without duplicating selection bookkeeping in OpenGL, Metal, or ray tracing code.

#### 2. Molecule Selection Reuses the Unwrap Connection Rules

Connected molecule selection is computed through shared helpers in `StructureOperations`, using the same graph eligibility rules as molecule unwrapping. This keeps "select molecule" behavior aligned with the existing structural interpretation of connected components.

#### 3. Picking and Click Handling Are Shared

Atom and bond picking were moved into `src/render/common/Picking.*`, while selection click handling lives in `ViewportSelection.*`. OpenGL and Metal viewports now supply camera/settings data and delegate the selection decision to shared code.

#### 4. Highlighting Is Packed Once for Renderers

Selection highlighting is applied when atom and bond render data are packed. Raster and ray-tracing renderers consume the same highlighted colors, avoiding separate highlight rules per backend.

#### 5. Bond Radius Is Now Per Bond

Bond radius storage was added to `BondList` so selected-only bond radius changes can persist. Global bond radius updates still work by assigning the radius to every bond when selection is disabled.

### Build Commands

```bash
cmake --build build
```

### Testing

```bash
ctest --test-dir build --output-on-failure
```

Validation results:

- Build completed successfully with the existing macOS OpenGL deprecation warnings.
- All CTest tests passed: `4/4`.
- Runtime launch verified that Metal shader compilation succeeds, Metal renderer initialization succeeds, and the previous QML `onLoadingStarted` warning is gone.

---

## 2026-04-08: Raster Bond Mesh Replacement in OpenGL and Metal

### Summary
Replaced the raster bond path in both OpenGL and Metal so bonds are no longer rendered as analytically intersected capped cylinders in raster mode. Raster bonds now use instanced polygonal capped-cylinder meshes while preserving the existing bond data model, the current center-to-center bond geometry, the existing global bond radius control, and the hard two-color split derived from the connected atoms. The ray-tracing bond path remains analytic and unchanged. The default raster bond tessellation was set to `20` segments.

### Files Modified
| File | Purpose |
|------|---------|
| `src/render/common/RenderSettings.h` | Changed the default bond cylinder tessellation from `16` to `20` segments |
| `src/render/opengl/BondRenderer.h` | Switched the OpenGL bond renderer interface and owned resources from quad-impostor rendering to indexed cylinder-mesh rendering |
| `src/render/opengl/BondRenderer.cpp` | Replaced OpenGL raster bond billboard draws with instanced capped-cylinder mesh draws and mesh caching by segment count |
| `src/render/opengl/ShaderManager.cpp` | Replaced the OpenGL analytic raster bond shader with a lit mesh-bond shader that preserves the hard endpoint color split |
| `src/render/metal/MetalBondRenderer.h` | Switched the Metal bond renderer interface to indexed mesh rendering and segment-aware geometry management |
| `src/render/metal/MetalBondRenderer.mm` | Replaced Metal raster bond quad-impostor draws with instanced capped-cylinder mesh draws and geometry caching by segment count |
| `src/render/metal/MetalRenderer.mm` | Passed raster bond tessellation settings through to the Metal bond renderer |
| `src/render/metal/MetalShaderLibrary.mm` | Replaced the Metal analytic raster bond shader path with a lit mesh-bond shader while keeping the separate flat solid-cylinder path for unit cell and gizmo rendering |

### Architecture Decisions

#### 1. The Bond Data Model Stays Unchanged
- The existing `Bond`, `BondList`, and `BondRenderSegment` flow already provides everything raster bonds need: endpoints, endpoint colors, endpoint radii, and periodic-image-corrected segment positions
- The renderer change therefore happens entirely at the raster rendering layer instead of introducing a second bond object in `src/data`

#### 2. The Existing Capped Unit-Cylinder Mesh Was Reused
- The existing mesh generator already defines a suitable capped cylinder with separate side and cap normals
- Reusing that mesh keeps OpenGL and Metal aligned and avoids introducing duplicate bond geometry definitions
- The mesh is instanced from bond start to bond end, preserving the existing center-to-center bond geometry

#### 3. Raster Bonds Keep the Previous Visual Semantics
- Bonds still use the global `bondRadius` setting as their cylinder radius
- Bonds still use a hard two-color split between the connected atoms rather than a smooth color interpolation
- The split location is still derived from the endpoint radii and `atomScale`, matching the previous analytic raster behavior

#### 4. Raster and Ray-Tracing Bond Paths Now Diverge Intentionally
- Raster bonds are mesh-based so they can benefit from standard rasterization and MSAA-friendly polygon edges
- Ray-traced bonds remain analytic cylinders, since that path was not part of the anti-aliasing problem being addressed
- This keeps the raster fix isolated without disturbing RT traversal, shading, or BVH behavior

#### 5. Bond Tessellation Is a Renderer Quality Setting
- Raster bond geometry now rebuilds on demand from `RenderSettings.cylinderSegments`
- The default was raised to `20` segments as the initial quality/performance tradeoff for mesh bonds
- This keeps future tuning available without redesigning the bond data flow again

### Build Commands
```bash
cmake --build build -j4
```

### Testing
- Built successfully after replacing the OpenGL raster bond path with indexed capped-cylinder mesh rendering
- Built successfully after replacing the Metal raster bond path with indexed capped-cylinder mesh rendering
- Did not perform a final live runtime verification in the app; recommended checks are bond silhouette quality, faceting at the new `20`-segment default, hard two-color split correctness, cap shading, and parity between OpenGL and Metal raster modes

---

## 2026-04-05: Analytic Raster Bonds and Bond/Unit-Cell/Gizmo Renderer Separation

### Summary
Changed raster bond rendering from tessellated cylinder meshes to analytically intersected capped cylinders in shader code, while leaving the ray-tracing bond path analytic as before. After that exposed coupling between bond rendering and overlay/object rendering, refactored the renderer ownership so bonds, the unit-cell object, and the center gizmo no longer share the same rendering path. The unit cell remains a pure-color connected scene object with normal scene depth behavior, while the center gizmo now renders in dedicated overlay passes so it stays on top of the scene but still preserves correct self-occlusion.

### Files Modified
| File | Purpose |
|------|---------|
| `src/render/CMakeLists.txt` | Registered the new OpenGL gizmo renderer sources in the render target |
| `src/render/opengl/BondRenderer.h` | Switched the OpenGL bond renderer interface from mesh-cylinder assumptions to analytic billboard impostor rendering |
| `src/render/opengl/BondRenderer.cpp` | Replaced instanced bond-cylinder mesh draws with instanced quad impostors for analytic raster bonds |
| `src/render/opengl/ShaderManager.h` | Continued exposing shared shader programs used by the separated OpenGL renderers |
| `src/render/opengl/ShaderManager.cpp` | Rewrote the OpenGL bond shader as an analytic capped-cylinder shader and kept flat solid-cylinder overlay shading available for non-bond objects |
| `src/render/opengl/GizmoRenderer.h` | Added a dedicated OpenGL center-gizmo renderer class |
| `src/render/opengl/GizmoRenderer.cpp` | Implemented the OpenGL center-gizmo renderer as its own overlay cylinder renderer with self-occluding depth |
| `src/render/opengl/UnitCellRenderer.cpp` | Detached unit-cell edge rendering from the bond shader so the unit cell uses its own pure-color cylinder path |
| `src/render/opengl/OpenGLRenderer.h` | Added ownership of the dedicated OpenGL gizmo renderer |
| `src/render/opengl/OpenGLRenderer.cpp` | Moved the OpenGL center gizmo into an overlay pass and preserved separate scene vs overlay responsibilities |
| `src/render/opengl/RayTracingRenderer.h` | Added ownership of the dedicated OpenGL gizmo renderer for RT display overlays |
| `src/render/opengl/RayTracingRenderer.cpp` | Rendered the OpenGL center gizmo as a separate topmost overlay in RT display passes with self-occlusion preserved |
| `src/render/metal/MetalShaderLibrary.h` | Added a dedicated flat solid-cylinder pipeline accessor for unit-cell and gizmo rendering |
| `src/render/metal/MetalShaderLibrary.mm` | Added the Metal flat solid-cylinder pipeline and separated it from the bond analytic pipeline |
| `src/render/metal/MetalUnitCellRenderer.mm` | Switched Metal unit-cell edge rendering off the bond pipeline onto the dedicated solid-cylinder pipeline |
| `src/render/metal/MetalGizmoRenderer.mm` | Switched the Metal center gizmo off the bond pipeline onto the dedicated solid-cylinder pipeline |
| `src/render/metal/MetalRenderer.mm` | Moved the Metal center gizmo out of the main scene pass into the overlay pass so it always appears on top |
| `src/render/metal/MetalRayTracingRenderer.mm` | Kept the Metal RT center gizmo in a separate overlay path with self-occluding depth behavior |
| `src/ui/components/OpenGLViewport.h` | Added OpenGL viewport state for showing the transient rotation-center gizmo during orbit interaction |
| `src/ui/components/OpenGLViewport.cpp` | Propagated rotation-center overlay state into `RenderSettings` and matched Metal’s left-drag gizmo visibility behavior |

### Architecture Decisions

#### 1. Analytic Bonds Belong Only to the Bond Renderer
- Raster bonds were changed to analytic capped cylinders so they are mathematically smooth without adding more mesh segments
- That analytic logic is now treated as bond-specific behavior, not as a generic cylinder behavior that other objects should inherit
- This keeps future bond rendering changes isolated from unrelated scene objects

#### 2. The Unit Cell Is Scene Geometry, Not a Bond Variant
- The unit cell remains a connected pure-color object built from cylinders plus corner joints
- It should participate in the main scene pass and write correct scene depth
- Its edge cylinders therefore no longer borrow the bond shader or bond pipeline

#### 3. The Center Gizmo Is Overlay Geometry, Not Scene Geometry
- The center gizmo must always appear on top of the scene while still self-occluding correctly
- The final design renders it in a dedicated overlay pass after clearing scene depth, rather than drawing it inside the main scene pass
- This preserves internal depth relationships inside the gizmo without allowing scene depth to hide it

#### 4. Separation Needed to Happen at the Shader/Pipeline Layer, Not Just the C++ Class Layer
- Metal already had separate `MetalUnitCellRenderer` and `MetalGizmoRenderer` classes, but both still depended on `bondPipeline`
- OpenGL already had a separate `UnitCellRenderer`, but its edges still depended on `bondShader`, and it had no separate gizmo renderer at all
- The fix therefore introduced distinct ownership at the shader/pipeline level and, for OpenGL, added the missing dedicated gizmo renderer

#### 5. Existing Geometry/Data Flow Was Preserved Where Reasonable
- The unit cell still uses 12 cylinders and 8 corner joints
- The center gizmo still uses 6 axis cylinders
- The refactor avoided redesigning viewport state or scene-data packing; it mainly rerouted which renderer/shader path owns which object

### Build Commands
```bash
cmake --build build -j4
```

### Testing
- Built successfully after converting raster bonds to analytic capped-cylinder impostors
- Built successfully after separating unit-cell and gizmo rendering ownership from the bond-owned path in both OpenGL and Metal
- Did not perform a final live runtime verification in the app; recommended checks are raster vs RT bonds, unit-cell depth behavior, and center-gizmo overlay/self-occlusion in both backends

---

## 2026-04-03: Sidebar Refactor, Bond Radius Control, and Quick Guide Migration

### Summary
Refactored the sidebar so atom and bond controls are split into dedicated sections instead of sharing the old `Visualization` tab. The existing `Bond Scale` control was renamed to `Neighborlist Cutoff Scale` to reflect its real purpose in bond detection, and a new `Bond Radius` slider was added to expose the actual rendered bond thickness with a tunable range of `0.01` to `0.6` Angstroms. The sidebar was also reorganized and renamed in several places: `Structure Manipulation` became `Structure`, `Structure Info` became `Info`, and a new `Quick Guide` section was added at the end of the sidebar after `Background`. The viewport’s floating lower-right help overlay was removed, with its controls guidance rewritten as structured explanatory text inside the new sidebar guide section.

### Files Modified
| File | Purpose |
|------|---------|
| `src/ui/components/OpenGLViewport.h` | Added a user-facing `bondRadius` property for the OpenGL viewport |
| `src/ui/components/OpenGLViewport.cpp` | Propagated the new bond radius into `RenderSettings` and clamped the UI-editable range to `0.01`–`0.6` |
| `src/ui/components/MetalViewport.h` | Added the matching `bondRadius` property for the Metal viewport |
| `src/ui/components/MetalViewport.mm` | Propagated the new bond radius into `RenderSettings` and matched the UI clamp range |
| `src/ui/qml/Sidebar.qml` | Split `Visualization` into `Atoms` and `Bonds`; renamed `Structure Manipulation` to `Structure` and `Structure Info` to `Info`; added the `Quick Guide` section; reordered guide placement after `Background`; moved `Show Bonds` to the top of the Bonds section |
| `src/ui/qml/ViewportPanel.qml` | Removed the floating viewport interaction help overlay and cleaned up image-export state that previously hid/restored it |
| `resources/resources.qrc` | Added resource aliases for `atom.svg`, `bond.svg`, `structure.svg`, and `guide.svg` so the new sidebar sections can use their dedicated icons |

### Architecture Decisions

#### 1. Bond Detection Scale and Bond Render Radius Are Separate Controls
- The existing `bondScale` viewport property already controlled neighbor-list bond detection, not visual thickness
- Rather than overloading that setting, a new `bondRadius` viewport property was introduced and copied into `RenderSettings.bondRadius`
- This keeps chemistry/topology detection independent from how bonds are drawn

#### 2. Sidebar Labels Were Aligned With Actual Behavior
- `Bond Scale` was misleading because it changed which atom pairs became bonded
- Renaming it to `Neighborlist Cutoff Scale` makes the control match the underlying `NeighborList` rebuild path
- The new `Bond Radius` slider now owns visual bond thickness explicitly

#### 3. Interaction Help Lives in the Sidebar, Not on Top of the Viewport
- The old floating viewport hint box consumed screen space and duplicated information continuously
- The replacement `Quick Guide` section provides the same guidance in clearer language and with better structure
- Removing the in-viewport block also simplified export-state handling, because the export path no longer needs to hide and restore that overlay

#### 4. Sidebar Organization Was Split by Domain
- Atom-specific controls now live in `Atoms`
- Bond-specific controls now live in `Bonds`
- Reference/help content now lives in `Quick Guide`
- This reduces the overload of the previous `Visualization` section and makes the control grouping more predictable

### Build Commands
```bash
cmake --build build
```

### Testing
- Built successfully after adding the `bondRadius` viewport property and sidebar slider
- Built successfully after renaming and splitting sidebar sections into `Atoms` and `Bonds`
- Built successfully after adding the `Quick Guide` sidebar section and removing the floating viewport help overlay
- Did not perform a final manual UI verification in the app; recommended checks are the new section ordering, icon loading, and the `Bond Radius` / `Neighborlist Cutoff Scale` behavior in both viewport backends

---

## 2026-04-03: Bond Caps, Hidden-Atom Bond Mode, and Ray-Traced Unit-Cell Depth Fixes

### Summary
Extended bond rendering so rasterized bonds use capped cylinder meshes instead of open tubes, with cap colors inherited naturally from the existing per-end bond coloring. Added a viewport rule that hides atoms entirely when bond rendering is enabled and the atom scale slider reaches its minimum value (`0.1`), so the smallest-radius bond view becomes a bond-only representation. On the ray-tracing side, fixed two related bond issues: unit-cell overlay depth against bonds in Metal RT, and blurry bond intersections in orthographic RT after cap support was added. The final RT implementation keeps orthographic-stable bond intersections, supports bond end caps, and preserves correct unit-cell occlusion even when atoms are hidden.

### Files Modified
| File | Purpose |
|------|---------|
| `src/render/opengl/CylinderMesh.cpp` | Rebuilt the shared OpenGL cylinder mesh as a capped cylinder with explicit cap normals |
| `src/render/opengl/ShaderManager.cpp` | Updated bond raster shading to consume mesh-supplied local normals so bond caps shade correctly |
| `src/render/opengl/UnitCellRenderer.cpp` | Switched unit-cell edge geometry to the capped cylinder mesh layout and bound the added normal/radius attributes |
| `src/render/opengl/RayTracingRenderer.cpp` | Added capped-cylinder RT intersection with explicit hit classification; restored orthographic-stable bond side-wall intersection; added `uShowAtoms` handling |
| `src/render/metal/MetalTypes.h` | Extended RT uniform structs with bond/unit-cell overlay state needed for bond occlusion and explicit atom visibility flags |
| `src/render/metal/MetalUnitCellShared.h` | Expanded the RT unit-cell uniform builder interface to accept bond counts for overlay occlusion |
| `src/render/metal/MetalUnitCellShared.cpp` | Added capped cylinder mesh generation for Metal unit-cell edges and filled the expanded RT overlay uniforms |
| `src/render/metal/MetalShaderLibrary.mm` | Updated raster bond vertex shading for cap normals; added capped-cylinder RT intersection details; fixed unit-cell overlay occlusion to test bonds and honor `showAtoms` separately from BVH counts |
| `src/render/metal/MetalRayTracingRenderer.mm` | Bound bond buffers into the RT unit-cell overlay pass; passed explicit atom/bond visibility state without corrupting BVH primitive indexing |
| `src/ui/components/OpenGLViewport.cpp` | Hid atoms automatically when bonds are shown at minimum atom scale |
| `src/ui/components/MetalViewport.mm` | Same hidden-atom-at-minimum-scale behavior for the Metal viewport |

### Architecture Decisions

#### 1. Bond Caps Reuse Existing Per-End Color Logic
- The raster bond mesh now includes cap vertices whose axial coordinate remains at `0` or `1`
- Because the shader already chooses color from bond position along the segment, cap colors stay consistent with the corresponding atom-colored bond end without adding new cap-specific color paths

#### 2. Hidden Atoms Are a Visibility Rule, Not a Geometry Rewrite
- When the atom scale reaches the smallest value while bonds are shown, the viewport now disables atom rendering through `RenderSettings.showAtoms`
- This keeps atom buffers, bond buffers, and scene construction intact while changing only what is rendered

#### 3. RT Bond Caps Must Not Replace the Orthographic-Stable Side-Wall Solver
- A simpler capped-cylinder RT intersection reintroduced blurry bonds in orthographic mode
- The fix was to keep the older numerically stable side-wall intersection for the cylinder body and layer start/end cap plane tests on top
- RT shading now uses explicit hit classification (`side`, `start cap`, `end cap`) so cap normals are selected robustly

#### 4. Unit-Cell RT Occlusion Must Use Real Scene Counts Plus Explicit Visibility Flags
- The Metal RT unit-cell overlay originally rendered above bonds because it only tested atom occlusion
- A first fix added bond occlusion correctly, but a later hidden-atoms workaround broke overlay traversal by zeroing `atomCount`, which no longer matched BVH primitive indexing
- The final design keeps real atom and bond counts in RT overlay uniforms and uses separate `showAtoms` / `showBonds` flags to decide which primitive classes can occlude the unit cell

### Build Commands
```bash
cmake --build build
```

### Testing
- Built successfully after adding capped bond geometry and shader normal updates
- Built successfully after adding hidden-atom behavior at minimum atom scale
- Built successfully after the first Metal RT unit-cell overlay bond-occlusion fix
- Built successfully after restoring orthographic-stable capped-cylinder RT bond intersections
- Built successfully after fixing the hidden-atoms RT overlay regression by separating BVH counts from visibility flags
- Did not perform a final manual in-app visual verification in this session; the remaining recommended check is RT rendering with `atom scale = 0.1` and `Show Bonds` enabled in both perspective and orthographic views

---

## 2026-04-03: Bond Rendering Cleanup and Radius-Aware Bi-Color Split

### Summary
Reworked bond coloring so each bond is rendered as two atom-colored halves instead of a single averaged color, then refined the split point to account for the connected atom sphere radii. The split is no longer fixed at the geometric midpoint: it is now computed from the bond length and the two endpoint radii using the scaled radii currently active in the viewport, so changing the atom radius control also changes the bond color transition in both raster and ray-traced rendering. In the same session, duplicated bond CPU-preparation logic was extracted into a shared render helper so OpenGL and Metal no longer independently rebuild periodic bond endpoints and per-end colors.

### Files Modified
| File | Purpose |
|------|---------|
| `src/render/common/BondRenderData.h` | Added shared bond render-data types for resolved bond endpoints, per-end colors, per-end radii, and packed upload buffers |
| `src/render/common/BondRenderData.cpp` | Centralized bond endpoint expansion across periodic images and packing of start/end positions, radii, and colors for renderer upload |
| `src/render/CMakeLists.txt` | Added the shared bond render-data helper to the render library build |
| `src/render/opengl/BondRenderer.h` | Added an instanced radius buffer for raster bond rendering |
| `src/render/opengl/BondRenderer.cpp` | Switched bond upload to shared packed bond data; uploads per-end colors and per-end radii; passes `uAtomScale` to the bond shader |
| `src/render/opengl/ShaderManager.cpp` | Bond raster shader now uses per-end colors and computes a radius-aware split point instead of a hardcoded midpoint |
| `src/render/opengl/RayTracingRenderer.h` | Documented RT bond endpoint buffers as carrying radii in the `w` channel |
| `src/render/opengl/RayTracingRenderer.cpp` | RT bond upload now uses shared packed bond data; ray-traced bond shading computes the split from bond length plus scaled endpoint radii |
| `src/render/metal/MetalTypes.h` | Extended `BondInstance` with per-end radii for raster bond rendering |
| `src/render/metal/MetalBondRenderer.mm` | Switched Metal bond upload to the shared bond render-data helper and fills per-end radii/colors |
| `src/render/metal/MetalShaderLibrary.mm` | Metal bond raster and RT shaders now compute a radius-aware color split from bond length and scaled endpoint radii |
| `src/render/metal/MetalRayTracingRenderer.mm` | Metal RT bond upload now uses shared packed bond data; endpoint buffers carry radii in `w` |
| `src/render/metal/MetalGizmoRenderer.mm` | Updated reused `BondInstance` setup to remain valid after the bond-instance layout grew per-end radii |
| `src/render/metal/MetalViewportAxesRenderer.mm` | Same `BondInstance` compatibility update for viewport axes overlay segments |
| `src/render/metal/MetalUnitCellShared.cpp` | Same `BondInstance` compatibility update for unit-cell edge instances |
| `src/render/metal/MetalUnitCellRenderer.mm` | Same `BondInstance` compatibility update for dynamic unit-cell edge recoloring |

### Architecture Decisions

#### 1. Bond Color Split Is Computed in Shader Space, Not Baked on the CPU
- The final split depends on the live atom radius control (`atomScale`), so baking the split point into bond instance data would require a bond-data rebuild every time the atom size slider changes
- Instead, the CPU uploads stable per-end bond geometry data: start/end positions, start/end colors, and start/end unscaled atom radii
- Raster and RT shaders compute `splitDistance = (L + scaledA - scaledB) / 2` at draw time, which keeps the split responsive to runtime atom scale changes

#### 2. Shared Bond CPU Preparation Lives in `src/render/common/`
- OpenGL raster, OpenGL RT, Metal raster, and Metal RT were all independently walking the bond list, resolving periodic images, and extracting per-end color data
- That duplication is now centralized in `BondRenderData`, which produces a canonical resolved segment representation and packed arrays suited for backend upload
- Backend code still owns the API-specific upload step, but no longer owns the chemistry/structure-to-segment expansion logic

#### 3. RT Reuses Existing Position Buffer Layout
- The RT bond endpoint buffers were already `vec4` / `float4`; previously the `w` lane was unused
- Rather than add extra RT buffers just for radii, the per-end unscaled radius is now stored in `bondStartPositions.w` and `bondEndPositions.w`
- This keeps the RT interface compact and avoids unnecessary additional bindings

#### 4. `BondInstance` Is Functionally a Generic Segment Payload
- Metal’s `BondInstance` is reused by actual chemical bonds, the rotation gizmo, viewport axes, and unit-cell edge rendering
- The struct was extended with per-end radii because it is the generic “oriented segment” payload used by the bond pipeline
- Non-bond uses retain their current appearance because their start/end radii and colors are initialized compatibly

### Radius-Aware Bond Split Rule
- Let the resolved bond length be `L`
- Let the two atom radii after applying the atom scale control be `A` and `B`
- The split point is placed at distance `(L + A - B) / 2` from atom A and `(L + B - A) / 2` from atom B
- In shader form, this is converted to `splitT = clamp(((L + A - B) / 2) / L, 0, 1)` when `L > 0`, with a `0.5` fallback for degenerate bonds

### Build Commands
```bash
cmake --build build
./build/bin/atom-studio.app/Contents/MacOS/atom-studio
```

### Testing
- Built successfully after the initial bi-colored bond implementation across OpenGL and Metal raster/RT paths
- Built successfully again after extracting shared bond CPU preparation into `BondRenderData`
- Built successfully after adding the radius-aware split rule and per-end radius payloads
- Confirmed `RenderStateHash` already includes `atomScale`, so RT accumulation should invalidate correctly when atom radii change
- Did not perform a final manual viewport visual verification in this session; runtime inspection of the bond split behavior remains recommended

---

## 2026-04-01: Export Images Feature — Background Alpha, Sidebar Files Section, and Image Save Pipeline

### Summary
Added a complete image export pipeline: a new "Files" sidebar section with Import/Export controls, background transparency via an alpha slider in the Background color picker, and a `grabToImage`-based export handler that hides HUD overlays before capture. The render backends (Metal raster, Metal RT, OpenGL) now carry alpha through from the UI color picker to the clear color and shader miss return. Premultiplied-alpha clear values are used so the saved image preserves true transparency while the live viewport composites correctly over the Qt scene graph.

### Files Modified
| File | Purpose |
|------|---------|
| `src/ui/qml/Sidebar.qml` | Added "Files" `SidebarSection` with Import Structures button, Export Structures combo (disabled placeholder), and Export Images combo with dynamic format list and Include Axes checkbox; passed `showAlpha: true` to the Background `RGBColorPicker` |
| `src/ui/qml/RGBColorPicker.qml` | Added optional alpha slider (`showAlpha` property, `alphaControl` NumericSliderControl); all four `onValueApplied` handlers now include alpha in the emitted color |
| `src/ui/qml/ViewportPanel.qml` | Added `suppressBackground` property for transparent gradient during export; added IDs to `infoOverlay` and `cameraHintsOverlay`; added `Connections` block listening to `FileController.onSaveImagePathSelected` that hides HUD, grabs image, and restores; removed the floating tab bar (`floatingTabBar`, `fileMenu`, `editMenu`, `AppMenuActions`) |
| `src/ui/components/FileController.h` | Added `Q_INVOKABLE openSaveImageDialog(format, includeAxes)` and `saveImagePathSelected` signal |
| `src/ui/components/FileController.cpp` | Implemented `openSaveImageDialog` — opens a native save dialog with format-appropriate filter and emits the signal |
| `src/ui/components/MetalViewport.mm` | Removed forced `alpha=255` in `setBackgroundColor` so alpha propagates to render settings |
| `src/ui/components/OpenGLViewport.cpp` | Same alpha propagation fix as MetalViewport |
| `src/render/metal/MetalRenderer.mm` | Clear color now uses premultiplied alpha: `r*a, g*a, b*a, a` |
| `src/render/opengl/OpenGLRenderer.cpp` | Same premultiplied-alpha clear color |
| `src/render/metal/MetalTypes.h` | `RTUniforms::backgroundColor` changed from `simd_float3` to `simd_float4` to carry alpha |
| `src/render/metal/MetalShaderLibrary.mm` | MSL struct `float3` → `float4` for `backgroundColor`; miss return uses premultiplied alpha; display fragment pass-through preserves alpha channel |
| `src/render/metal/MetalRayTracingRenderer.mm` | Packs alpha into `backgroundColor` uniform; clear colors use premultiplied alpha |
| `resources/resources.qrc` | Added `export.svg`, `files.svg`, `import.svg` icon entries |

### Architecture Decisions

#### 1. Background Alpha Lives in the Viewport Color, Not a Per-Export Toggle
- Rather than a separate "transparent background" checkbox in the export UI, alpha is part of the background color itself via the existing `RGBColorPicker` extended with an optional alpha slider
- This gives live visual feedback in the viewport and avoids a disconnect between what the user sees and what gets exported
- The alpha slider is opt-in (`showAlpha: false` by default) so other color pickers are unaffected

#### 2. Premultiplied Alpha for Clear Colors
- All render backends clear with `(r*a, g*a, b*a, a)` — standard premultiplied alpha
- This ensures correct compositing when the texture is displayed in the Qt scene graph (which uses premultiplied blending)
- The saved image also comes out correct because `grabToImage` captures the composited result

#### 3. Dynamic Export Format List Based on Alpha
- When background alpha < 1, only `.png` appears in the Export Images dropdown since JPEG does not support transparency
- When fully opaque, `.png`, `.jpg`, and `.pdf` are all offered (PDF is a placeholder, not yet implemented)

#### 4. Files Section Consolidates Import and Export
- Import Structures (opens file dialog), Export Structures (placeholder combo), and Export Images are grouped under a single "Files" sidebar section
- This replaces the previous floating tab bar's File menu, which was removed in this session

#### 5. HUD Hiding During Export Capture
- The `grabToImage` handler temporarily hides `infoOverlay`, `cameraHintsOverlay`, and optionally `axisOverlay` before capture, then restores them in the completion callback
- `suppressBackground` makes the QML gradient transparent so only the rendered viewport content appears in the captured image

### Known Issue: Black Viewport Background at Low Alpha
- When background alpha approaches 0, the viewport displays a black background instead of white because premultiplied `(r*0, g*0, b*0, 0)` = `(0,0,0,0)` composites over whatever Qt draws behind the viewport item (which is dark/black by default)
- The saved image is correct (transparent background as intended)
- A fix would composite-over-white for viewport display (`r*a + (1-a)`) while keeping premultiplied values for export — deferred to a follow-up

### Build Commands
```bash
cmake --build build
./build/bin/atom-studio.app/Contents/MacOS/atom-studio
```

### Testing
- Built successfully after all changes
- Verified alpha slider appears in Background color picker and controls viewport transparency
- Verified Export Images dropdown shows only `.png` when alpha < 1, all formats when opaque
- Verified clicking an export format opens the native save dialog with correct filter
- Verified exported `.png` with transparent background has correct alpha channel
- Verified exported `.png` with opaque background has correct solid color
- Verified HUD overlays (FPS, camera hints) are hidden in exported images
- Verified Include Axes checkbox controls whether the axis glyph appears in exports

---

## 2026-04-01: Application Branding Setup — Qt Runtime Icon and macOS Bundle Icon

### Summary
Hooked up the application logo in both places required by the desktop app: the runtime Qt application/window icon now loads from the bundled Qt resource `temp_logo.png`, and the macOS app bundle now packages `temp_logo.icns` and exposes it through `CFBundleIconFile` so Finder and Dock can use the same branding asset family.

### Files Modified
| File | Purpose |
|------|---------|
| `src/main.cpp` | Added `QIcon` include and set the runtime application/window icon from the Qt resource system |
| `resources/resources.qrc` | Added a `/branding` resource entry exposing `temp_logo.png` as `app-logo.png` |
| `resources/Info.plist.in` | Changed `CFBundleIconFile` from empty to `${MACOSX_BUNDLE_ICON_FILE}` |
| `CMakeLists.txt` | Registered `resources/temp_logo.icns` as a macOS bundle resource and set `MACOSX_BUNDLE_ICON_FILE` to `temp_logo.icns` |
| `GUIDELINES.md` | Documented the established runtime-icon and macOS bundle-icon configuration paths |

### Architecture Decisions

#### 1. Keep the Runtime Icon in the Qt Resource System
- The application already bundles QML and sidebar SVG assets through `resources/resources.qrc`
- Loading the runtime icon through `QIcon(":/branding/app-logo.png")` keeps icon resolution independent from the working directory and consistent with the rest of the app asset pipeline
- This also avoids relying on loose files next to the executable during local runs

#### 2. Keep the macOS Bundle Icon Separate as `.icns`
- macOS Finder and Dock expect a bundle icon asset in `.icns` format rather than the PNG used by the Qt runtime icon path
- The icon file is now packaged into `Contents/Resources` via `MACOSX_PACKAGE_LOCATION "Resources"` and referenced using `MACOSX_BUNDLE_ICON_FILE`
- This matches the standard CMake/macOS bundle flow instead of using a custom post-build copy step just for branding

#### 3. Document Both Branding Paths Explicitly
- Desktop branding now has two intentionally different asset paths: Qt runtime icon and macOS bundle icon
- `GUIDELINES.md` was updated so future branding changes do not update only one of the two integration points

### Build Commands
```bash
cmake --build build --target atom-studio
```

### Testing
- Built successfully after the runtime icon and macOS bundle icon changes
- Verified the generated app bundle contains `build/bin/atom-studio.app/Contents/Resources/temp_logo.icns`
- Verified the generated `Info.plist` sets `CFBundleIconFile` to `temp_logo.icns`
- Verified the runtime executable still links and launches with the updated `QIcon` resource path

---

## 2026-04-01: Sidebar UI Polish — Icon Chevrons, Bold Titles, Color Hierarchy, White Background Default

### Summary
Continued sidebar UI refinement with four targeted changes: replaced Unicode glyph indicators on ComboBox controls with proper SVG chevron assets; applied `Font.DemiBold` weight to all option, slider, and checkbox title labels for clearer visual hierarchy; corrected the tab/button color hierarchy so expanded tab headers are visually darker than the option blocks beneath them; and changed the default viewport background color from light gray (230, 230, 230) to pure white (255, 255, 255).

### Files Modified
| File | Purpose |
|------|---------|
| `src/ui/qml/Sidebar.qml` | SVG chevron indicator on `SidebarComboBox`; bold titles on option labels and `SidebarCheckBox`; corrected `tabFill`/`buttonFill` color hierarchy; `buttonFill` darkened; ComboBox dropdown highlight aligned to `buttonFill`; QML background `defaultColor` set to white |
| `src/ui/qml/NumericSliderControl.qml` | Added `font.weight: Font.DemiBold` to slider title label |
| `src/render/common/RenderSettings.h` | Default `backgroundColor` changed from `QColor(230,230,230)` to `QColor(255,255,255)` |
| `src/ui/components/MetalViewport.h` | Default `m_backgroundColor` changed to `QColor(255,255,255)` |
| `src/ui/components/OpenGLViewport.h` | Default `m_backgroundColor` changed to `QColor(255,255,255)` |

### Architecture Decisions

#### 1. SVG Chevrons for ComboBox Indicator
- The previous `indicator` was a `Text` item using Unicode characters (`›` and `⌃`) with different `font.pixelSize` values for the two states, producing inconsistent sizing
- Replaced with an `Image` item sourcing `qrc:/icons/chevron-right.svg` (closed) and `qrc:/icons/chevron-down.svg` (open) at a fixed 16×16 size
- Consistent with the existing `SidebarSection` header which already used these same SVG assets

#### 2. `Font.DemiBold` Applied at the Component Level
- Option titles in Sidebar.qml (above ComboBoxes and for grouped control blocks) were plain `Label` items with no explicit weight
- `SidebarCheckBox` and `NumericSliderControl` each define their own text rendering internally
- Bold was applied at each definition site rather than via a shared property so the weight is stable regardless of how the components are used

#### 3. Color Hierarchy: Tab Header Darker Than Option Blocks
- Original palette had `tabFill: #e2e3e8` (lighter) and `buttonFill: #d8dae0` (darker), which meant option blocks appeared more prominent than the section header they belong to
- Fixed by swapping the semantic roles: `tabFill` → `#d8dae0` (darker, for expanded section headers), `buttonFill` → `#e2e3e8` then further refined to `#e2e3e8` with the reset button already at that value
- ComboBox dropdown item highlight was also moved from `tabFill` to `buttonFill` so hovered items match the surrounding control tone rather than the heavier tab bar tone
- Final values: `tabFill: #d8dae0`, `buttonFill: #e2e3e8`; reset button in `NumericSliderControl` was already `#e2e3e8` and now matches without a code change

#### 4. White Default Background Propagated to All Initialization Sites
- The default background was `QColor(230, 230, 230)` defined independently in three places: `RenderSettings`, `MetalViewport`, and `OpenGLViewport`
- The QML `defaultColor` on the `RGBColorPicker` in the Background section was a fourth independent copy
- All four were updated together so the "Reset to Default" button, the initial render on first launch, and the QML picker's reset reference all agree on pure white

### Build Commands
```bash
cmake --build build
./build/bin/atom-studio.app/Contents/MacOS/atom-studio
```

---

## 2026-03-31: Sidebar UI Restyle — Bright Neutral Theme, Icon Assets, and Tunable-Parameter Hierarchy

### Summary
Restyled only the right-hand sidebar to match a cleaner card/list visual language without changing the rest of the application shell. The sidebar now uses a bright neutral palette (white, gray, black), dedicated SVG tab icons from `resources/icons`, consistent header sizing between collapsed and expanded states, and hierarchy connector lines that only appear for tunable parameter sections. The connector backbone now aligns to the vertical center of each parameter block, spans continuously between neighboring rows, and terminates cleanly at the final branch without an extra tail.

### Files Modified
| File | Purpose |
|------|---------|
| `src/ui/qml/Sidebar.qml` | Reworked the sidebar-only visual style, section-header sizing, SVG icon usage, bright neutral palette, and tunable-parameter hierarchy-line behavior |
| `resources/resources.qrc` | Added sidebar SVG assets under the `/icons` resource prefix |
| `resources/icons/chevron-right.svg` | Sidebar expand/collapse arrow asset, switched to fixed dark stroke for light-theme rendering |
| `resources/icons/chevron-down.svg` | Sidebar expand/collapse arrow asset, switched to fixed dark stroke for light-theme rendering |
| `resources/icons/background.svg` | Background tab icon, switched to fixed dark stroke |
| `resources/icons/camera.svg` | Camera tab icon, switched to fixed dark stroke |
| `resources/icons/info.svg` | Structure Info tab icon, switched to fixed dark stroke |
| `resources/icons/manipulation.svg` | Structure Manipulation tab icon, switched to fixed dark stroke |
| `resources/icons/render.svg` | Render Settings tab icon, switched to fixed dark stroke |
| `resources/icons/unit-cell.svg` | Unit Cell tab icon, switched to fixed dark stroke |
| `resources/icons/visualization.svg` | Visualization tab icon, switched to fixed dark stroke |

### Architecture Decisions

#### 1. Scope the Restyle to `Sidebar.qml` Only
- The sidebar visual redesign was kept local to `src/ui/qml/Sidebar.qml`
- Shared QML controls and the rest of the app window were intentionally left unchanged
- This avoids accidental style drift across the viewport and top-level shell when only the sidebar is under active design iteration

#### 2. Use SVG Assets for Tab Arrows and Section Icons
- The previous right-side arrow inconsistency came from using different text glyphs for collapsed and expanded states
- The sidebar now uses resource-backed SVG assets for both chevrons and section icons
- Asset-based icons give stable visual sizing and eliminate font-glyph inconsistencies

#### 3. Hierarchy Lines Only for Tunable Controls
- `Structure Info` is informational, not interactive, so it no longer uses connector lines
- Connector branches are now reserved for sections containing tunable parameters or actions
- This makes the hierarchy line language semantically meaningful instead of decorative everywhere

#### 4. Connector Alignment Based on Actual Rendered Block Height
- Each branch joins the backbone at the vertical center of the rendered parameter block, not a fixed offset
- This keeps tall grouped controls such as the RGB picker aligned correctly, while ordinary slider blocks still connect at their visual midpoint
- The branch-row container also owns inter-row spacing so the backbone remains continuous between adjacent parameter groups

### Technical Issues Resolved

#### Issue 1: Expanded Sidebar Tabs Changed Icon/Text/Arrow Size
**Problem**: Expanded tabs visually magnified the label, arrow, and placeholder icon, creating inconsistent sizing between collapsed and expanded states.

**Solution**: Fixed the sidebar header height and the icon/text/arrow sizes so expansion only changes highlighting and content visibility, not the perceived scale of the header controls.

#### Issue 2: Hierarchy Connector Misalignment for Tall Controls
**Problem**: The connector branch used a hardcoded vertical join position, which misaligned branches for taller controls like the RGB color picker.

**Solution**: Recomputed the branch join from the actual rendered content height of each parameter block so the connector terminates at the block’s vertical midpoint.

#### Issue 3: Backbone Gaps and Tail at the Final Branch
**Problem**: Neighboring connector segments were separated by layout spacing, and the final backbone extended slightly beyond the last branch join.

**Solution**: Moved connector ownership into the branch-row container so the backbone spans the spacing between rows, and ended the final backbone exactly at the start of the last curve.

### Build Commands
```bash
cmake --build build
./build/bin/atom-studio.app/Contents/MacOS/atom-studio
```

### Testing
- Built successfully after all sidebar, resource, and SVG updates
- Verified the sidebar compiles with the new `/icons` Qt resource entries
- Verified tab arrows and tab icons now come from SVG assets rather than placeholder glyphs/canvas shapes
- Verified collapsed and expanded section headers keep consistent icon/text/arrow sizing and fixed header height
- Verified hierarchy lines:
  - are absent from `Structure Info`
  - connect only to tunable parameter blocks
  - join at each block’s visual vertical center
  - remain continuous between adjacent rows
  - terminate cleanly at the final branch without an extra tail

---

## 2026-03-31: QML UI Refactor — Extract Reusable Components

### Summary
Extracted duplicated QML code into standalone reusable component files. No visual or behavioral changes; purely structural. The Sidebar had three inline `component` definitions and two near-identical RGB color picker blocks. ViewportPanel had two structurally identical semi-transparent overlay boxes. Both ViewportPanel and HeaderBar independently defined the same File and Edit menu actions.

### Files Modified
| File | Purpose |
|------|---------|
| `src/ui/qml/Sidebar.qml` | Removed 3 inline component definitions; replaced 2 RGB picker blocks with `RGBColorPicker`; removed redundant `sliderInputErrorDialog` and `showSliderInputError` |
| `src/ui/qml/ViewportPanel.qml` | Replaced 2 overlay `Rectangle`s with `InfoOverlayBox`; replaced inline `Action` items with `MenuItem { action: appActions.xxx }` |
| `src/ui/qml/HeaderBar.qml` | Replaced inline `Action` items with `MenuItem { action: appActions.xxx }` |
| `resources/resources.qrc` | Added 6 new QML component entries |

### New Files
| File | Purpose |
|------|---------|
| `src/ui/qml/CollapsibleSection.qml` | Expandable section with click-to-toggle header |
| `src/ui/qml/PropertyRow.qml` | Label + value display row |
| `src/ui/qml/NumericSliderControl.qml` | Slider + text field + reset button; self-contained validation dialog using `Overlay.overlay` |
| `src/ui/qml/RGBColorPicker.qml` | RGB Color label + preview rectangle + R/G/B sliders; exposes `sourceColor`, `defaultColor`, `colorApplied` signal |
| `src/ui/qml/InfoOverlayBox.qml` | Semi-transparent dark rounded box with `default property alias` for child labels |
| `src/ui/qml/AppMenuActions.qml` | `QtObject` holding shared File and Edit `Action` instances for use in both ViewportPanel and HeaderBar |

### Architecture Decisions
- `NumericSliderControl` now owns its own error dialog (`Overlay.overlay` + `anchors.centerIn: parent`) instead of delegating to the parent `Sidebar`. This makes the component fully self-contained and centers the dialog in the window rather than the sidebar panel.
- `RGBColorPicker` uses `sourceColor`/`defaultColor` color properties and a `colorApplied(color)` signal, keeping it decoupled from which viewport property it targets.
- `AppMenuActions` is a `QtObject` (not a visual item), allowing it to be instantiated inside any QML file as a non-visual child and accessed by id.

### Build Commands
```bash
cmake --build build
./build/bin/atom-studio.app/Contents/MacOS/atom-studio
```

### Testing
- Built successfully with no errors or warnings

---

## 2026-03-31: Fix Orthographic View Rendering Bugs and RT Blurriness

### Summary
Fixed three bugs that all manifested in orthographic view: (1) atoms disappearing in raster mode under heavy zoom, (2) sphere interiors shown in RT mode under heavy zoom, and (3) RT mode blurriness after the camera fix was applied. The root cause of the first two was `Camera::zoom()` moving the camera physically closer to the scene in ortho mode, which has no visual effect in orthographic projection but brings the camera inside atom volumes. The fix locks the ortho camera at a safe scene-scale distance and only changes the visible half-extent (`m_orthoScale`). The RT blurriness was a consequential floating-point cancellation problem: with the camera now fixed at a large distance, the classic `disc = b²−c` sphere/cylinder discriminant suffered catastrophic cancellation. Both intersection functions were replaced with geometrically stable reformulations.

### Files Modified
| File | Purpose |
|------|---------|
| `src/render/common/Camera.cpp` | OVITO-style ortho camera: zoom only changes `m_orthoScale`; `m_distance`/near/far fixed at scene scale; `fitToView` stores `m_sceneExtent`; `setProjection` uses stored extent on mode switch; pan scales by `m_orthoScale` in ortho |
| `src/render/common/Camera.h` | Added `m_sceneExtent` member; added `viewScale()` helper (returns `m_distance` in perspective, `m_orthoScale` in ortho) |
| `src/render/metal/MetalRenderer.mm` | Gizmo size: `camera.distance()` → `camera.viewScale()` |
| `src/render/metal/MetalRayTracingRenderer.mm` | Gizmo size: `camera.distance()` → `camera.viewScale()` at two locations |
| `src/render/metal/MetalShaderLibrary.mm` | Four shader fixes (see below) |

### Architecture Decisions

#### 1. OVITO-Style Orthographic Camera
Orthographic projection has no foreshortening, so camera distance has no visual effect on the rendered image — only `m_orthoScale` (the visible half-extent) determines zoom level. The previous code shrank `m_distance` alongside `m_orthoScale` on every zoom step, eventually placing the camera inside atom volumes. The fix: `Camera::zoom()` never touches `m_distance`, `m_near`, or `m_far` in ortho mode. These are set once by `fitToView()` (`m_distance = extent×2`, `m_far = extent×5`) and restored on perspective→ortho switch using `m_sceneExtent`.

#### 2. Store `m_sceneExtent` in `fitToView()`
`setProjection()` previously inferred scene scale by back-computing `extent = m_orthoScale / 0.6`. This fails after perspective zoom (which does modify `m_orthoScale`), corrupting the camera distance on mode switch and clipping long unit cells. Storing the true extent from `fitToView()` gives a reliable source of truth.

#### 3. Stable Ray-Primitive Intersection
The classic formulation `disc = b² − (|oc|² − r²)` suffers catastrophic cancellation when `|oc| ≈ m_distance >> r`: both `b²` and `|oc|² − r²` are ~`m_distance²` (~40 000), their difference is ~`r²` (~a few Å²), and float32 ULP at 40 000 is ~0.004 — a tens-of-percent relative error in the discriminant for near-limb rays, visible as blurry atom/bond silhouettes. The fix reformulates the discriminant as `r² − |oc ⊥ rd|²`, where `oc ⊥ rd` is the purely lateral component of `oc` with magnitude ≤ r — no large intermediate values. The same two-stage projection is applied to `intersectCylinder`.

### Technical Issues Resolved

#### Issue 1: Atoms Disappear in Raster Mode (Ortho Heavy Zoom)
**Problem**: The ortho branch of the sphere fragment shader always selected the front surface (`hz = C.z + sqrtDisc`). When the camera was inside a sphere volume, `hz > 0` (front surface behind the camera), depth was written as 0, the billboard covered the screen at depth 0, and all subsequent atoms failed the depth test.

**Solution**: Added back-surface fallback: `if (hz > 0.0) hz = C.z - sqrtDisc`. Primary fix: camera never enters sphere volumes (OVITO camera). Shader change is defense-in-depth.

#### Issue 2: Sphere Interiors Shown in RT Mode (Ortho Heavy Zoom)
**Problem**: `intersectSphere` used `t > 0.001` epsilon. When `m_distance ≈ R`, `t_front ≈ 0` was rejected and the back surface was returned instead.

**Solution**: Changed epsilon to `t > 0.0`. Secondary-ray self-intersection is handled by the existing biased origin (`bias = max(R×0.01, 0.05)`). Primary fix: camera never approaches sphere surfaces (OVITO camera).

#### Issue 3: RT Mode Blurriness After Camera Fix
**Problem**: After locking `m_distance` at `extent×2` (~200 Å), the discriminant `b²−c` in `intersectSphere` and `intersectCylinder` suffered catastrophic float32 cancellation at large camera distance, producing incorrect normals near atom/bond silhouettes and visible blurring.

**Solution**: Replaced both intersection functions with stable formulations computing `disc = r² − |lateral|²` directly, where `lateral` is the component of `oc` perpendicular to both the ray direction and (for cylinders) the cylinder axis — always ≤ r in magnitude regardless of camera distance.

### Build Commands
```bash
cmake --build build
./build/bin/atom-studio.app/Contents/MacOS/atom-studio
```

### Testing
- Built successfully after all changes
- Verified in raster mode: atoms remain visible at all ortho zoom levels
- Verified in RT mode: no sphere interior rendering at any ortho zoom level
- Verified in RT mode: atom and bond silhouettes are sharp under orthographic view
- Verified perspective mode: behavior unchanged
- Verified pan speed proportional to zoom level in ortho mode
- Verified gizmo remains visually proportional when zoomed in ortho mode
- Verified perspective→ortho switch at various zoom levels: camera resets correctly for all cell shapes including long/skewed cells

---

## 2026-03-25: Align Element Colors With ASE and Add Visualization Color-Scheme Selector

### Summary
Aligned the software's hardcoded element colors with ASE and exposed the available ASE element color regimes in the Visualization sidebar. The data layer now stores ASE Jmol colors as the default per-element colors and a separate ASE CPK table for explicit selection. Both viewport backends can switch the active structure coloring between Jmol and CPK at runtime, and the Visualization tab now provides a user-facing selector for that choice.

### Files Modified
| File | Purpose |
|------|---------|
| `src/data/ElementData.cpp` | Replaced hardcoded element colors with ASE-derived Jmol values and added a separate ASE-derived CPK table |
| `src/data/ElementData.h` | Added `ElementColorScheme` and a scheme-aware `colorForElement()` API |
| `src/data/Structure.cpp` | Defaulted new atoms to ASE Jmol colors and made color refresh scheme-aware |
| `src/data/Structure.h` | Updated `updateColorsFromElements()` to accept a selected color scheme |
| `src/data/CMakeLists.txt` | Registered the new data-layer color regression test |
| `src/ui/components/OpenGLViewport.h` | Added `atomColorScheme` QML property |
| `src/ui/components/OpenGLViewport.cpp` | Applied selected color scheme on load and on user changes |
| `src/ui/components/MetalViewport.h` | Added `atomColorScheme` QML property |
| `src/ui/components/MetalViewport.mm` | Applied selected color scheme on load and on user changes |
| `src/ui/qml/Sidebar.qml` | Added Visualization sidebar combo box for `Jmol` vs `CPK` |

### Files Created
| File | Purpose |
|------|---------|
| `src/data/tests/ElementDataColorTest.cpp` | Regression coverage for ASE Jmol defaults, ASE CPK lookup, and high-Z fallback behavior |
| `archived/print_ase_element_colors.py` | Utility script to print ASE per-element RGB values for all discovered ASE element color schemes |

### Architecture Decisions

#### 1. Use ASE Jmol as the Default Runtime Color Scheme
- ASE exposes both `jmol_colors` and `cpk_colors`, but the previous in-app table was effectively a custom approximation
- The software now defaults to ASE Jmol colors for the standard visualization path
- This keeps the default rendering aligned with ASE while still allowing explicit CPK selection

#### 2. Keep Both Color Tables in the Data Layer
- The element database now distinguishes between the stored default Jmol color and the selectable CPK lookup table
- `ElementData::colorForElement()` accepts an explicit `ElementColorScheme`
- This keeps color policy centralized and avoids duplicating color tables in the UI or renderer layers

#### 3. Recolor Structures in the Viewport Layer
- `StructureModel` hands a cloned structure to the active viewport
- The viewport now reapplies the selected element color scheme to that owned structure on load and when the user changes the selector
- This avoids changing file-import behavior while keeping UI interaction immediate

### Technical Issues Resolved

#### Issue 1: In-App Element Colors Drifted From ASE
**Problem**: The software used a hardcoded element color table that did not exactly match ASE's published Jmol/CPK color arrays.

**Solution**: Replaced the hardcoded Jmol-like colors with exact ASE Jmol values and added exact ASE CPK values as a separate selectable regime.

#### Issue 2: No User Control Over Element Color Regime
**Problem**: Users could not switch between ASE-supported element coloring conventions in the Visualization UI.

**Solution**: Added a `Color Scheme` combo box in the Visualization sidebar and wired it to both OpenGL and Metal viewports.

#### Issue 3: Missing Regression Coverage for Color-Table Alignment
**Problem**: There was no automated check preventing future drift from ASE's color tables.

**Solution**: Added a focused data-layer regression test covering default Jmol colors, explicit CPK lookup, and fallback handling for elements beyond ASE's table coverage.

### Build Commands
```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure -R "atom-data-(element-color|structure)"
```

### Testing
- Built the full application and updated data-layer test targets
- Ran `ctest --test-dir build --output-on-failure -R "atom-data-(element-color|structure)"`
- Verified:
  - default element colors now resolve to ASE Jmol values
  - explicit CPK color lookup returns ASE CPK values
  - high-Z elements beyond ASE table coverage fall back consistently
  - the UI/backend changes compile cleanly for both OpenGL and Metal viewport paths
- Result: passed

---

## 2026-03-25: Initial Viewport Rotation Center from Unit Cell or Geometry Center

### Summary
Changed the initial viewport rotation center used by `fitToView()` and camera reset. Periodic structures now initialize the camera target at the unit-cell center, while non-periodic structures initialize it at the geometric center of the atom positions. The framing extent now uses a combined view bounding box so periodic scenes still fit both the unit cell and the atoms.

### What Changed

**1. Structure Geometry Helpers — `Structure.h`, `Structure.cpp`**
- Added `geometricCenter()` to compute the arithmetic mean of all atom positions.
- Added `unitCellCenter()` to compute the Cartesian position of fractional coordinate `(0.5, 0.5, 0.5)`.
- Added `computeUnitCellBoundingBox()` to enclose all eight unit-cell corners in Cartesian space.
- Added `computeViewBoundingBox()` to return the union of the atom bounding box and the unit-cell bounding box when a lattice exists.

**2. Viewport Fit Logic — `OpenGLViewport.cpp`, `MetalViewport.mm`**
- Updated `fitToView()` in both viewport backends to choose the initial camera target by structure type:
  - `hasLattice() == true` → use `unitCellCenter()`
  - `hasLattice() == false` → use `geometricCenter()`
- Replaced the old fit extent source (`computeBoundingBox()`) with `computeViewBoundingBox()` so centering on the unit cell does not under-estimate the required framing size.

**3. Tests — `src/data/tests/StructureTest.cpp`, `src/data/CMakeLists.txt`**
- Added a dedicated data-layer test target covering:
  - geometric center for non-periodic structures,
  - unit-cell center for periodic structures,
  - merged view bounds that cover both atoms and the unit cell.

### What Is NOT Changed
- **Orbit behavior after initialization**: rotation still happens around `camera.target()`, and pan / zoom-to-cursor can still move that target afterward.
- **Center of mass logic**: `centerOfMass()` remains unchanged and is still separate from the viewport initialization policy.
- **Renderer behavior**: no shader or renderer-side rotation logic changed; only the initial camera target and fit extent inputs changed.

### Files Modified
| File | Change |
|------|--------|
| `src/data/Structure.h` | Declared `geometricCenter()`, `unitCellCenter()`, `computeUnitCellBoundingBox()`, and `computeViewBoundingBox()` |
| `src/data/Structure.cpp` | Implemented the new center and bounding-box helpers |
| `src/ui/components/OpenGLViewport.cpp` | Updated `fitToView()` to use unit-cell center or geometric center and the merged view bounds |
| `src/ui/components/MetalViewport.mm` | Updated `fitToView()` to use unit-cell center or geometric center and the merged view bounds |
| `src/data/tests/StructureTest.cpp` | Added geometry helper coverage |
| `src/data/CMakeLists.txt` | Added `atom-data-structure-test` target |

### Key Design Decisions
- **Unit-cell center for periodic inputs**: uses crystallographic context as the initial rotation pivot instead of the atom-cloud enclosure center.
- **Geometric center for non-periodic inputs**: uses the mean atom position rather than the atom bounding-box center, matching the requested “geometry center” behavior.
- **Merged fit bounds for periodic inputs**: keeps framing robust when atoms extend outside the nominal cell or when the unit-cell overlay is larger than the atom cloud in some directions.

### Verification
- `cmake -S . -B build` — success.
- `cmake --build build --target atom-data-structure-test atom-ui` — success.
- `ctest --test-dir build --output-on-failure -R "atom-data-(structure|neighborlist|structureops)"` — 3/3 tests passed.

---

## 2026-03-24: Preset View Directions (+X, -X, +Y, -Y, +Z, -Z)

### Summary
Added six preset view direction buttons to the Camera tab in the sidebar, allowing users to quickly snap the camera to axis-aligned views. Works with both perspective and orthographic projection since only the camera orientation quaternion is changed — projection mode, distance, target, and ortho scale are all preserved.

### What Changed

**1. Camera Preset View Logic — `Camera.h`, `Camera.cpp`**
- Added `enum class ViewDirection { PlusX, MinusX, PlusY, MinusY, PlusZ, MinusZ }` in the `atom::render` namespace.
- Added `Camera::setPresetView(ViewDirection dir)` which uses `QQuaternion::fromDirection(forward, up)` to compute the orientation quaternion for each axis-aligned view.
- Z-up convention: X/Y views use `up = (0,0,1)`; Z views use `up = (0,1,0)`.
- "+X" means the positive X axis points toward the user (camera positioned on the +X side of the target).

**2. Viewport Slots — `MetalViewport.h/.mm`, `OpenGLViewport.h/.cpp`**
- Added `Q_INVOKABLE void setViewDirection(int direction)` to both viewport classes.
- Validates input range (0–5), casts to `ViewDirection`, delegates to `Camera::setPresetView()`, emits `cameraChanged()`, and triggers a repaint.

**3. QML Sidebar UI — `Sidebar.qml`**
- Added a "View Direction" label and a 3×2 `GridLayout` of buttons (+X, −X, +Y, −Y, +Z, −Z) in the Camera section, positioned between the FOV slider and the Reset Camera button.
- Each button calls `sidebar.viewport.setViewDirection(index)` where the index maps directly to the `ViewDirection` enum.

### What Is NOT Changed
- **Camera class core**: No changes to orbit, pan, zoom, projection, or matrix computation.
- **Renderers/shaders**: No shader changes — preset views only set the orientation quaternion.
- **RenderStateHash**: Already includes orientation via the view matrix — RT accumulation resets correctly on view change.

### Files Modified
| File | Change |
|------|--------|
| `src/render/common/Camera.h` | Added `ViewDirection` enum and `setPresetView()` declaration |
| `src/render/common/Camera.cpp` | Implemented `setPresetView()` using `QQuaternion::fromDirection()` |
| `src/ui/components/MetalViewport.h` | Added `Q_INVOKABLE setViewDirection(int)` |
| `src/ui/components/MetalViewport.mm` | Implemented `setViewDirection()` slot |
| `src/ui/components/OpenGLViewport.h` | Added `Q_INVOKABLE setViewDirection(int)` |
| `src/ui/components/OpenGLViewport.cpp` | Implemented `setViewDirection()` slot |
| `src/ui/qml/Sidebar.qml` | Added view direction label and 3×2 button grid in Camera section |

---

## IMPORTANT — 2026-03-12: Orthographic View Implementation (All Renderers)

### Summary
Implemented orthographic projection rendering across all backends (Metal raster, Metal RT, OpenGL raster, OpenGL RT). The Camera class already supported orthographic projection matrices, zoom, and fit-to-view, but all shader code assumed perspective projection and the QML sidebar controls were disconnected. This change wires the existing QML controls to the camera, then fixes every perspective-dependent shader path so orthographic rendering is correct without breaking perspective mode.

### What Changed

**1. GPU Uniform Structs (`MetalTypes.h`)**
Added `int32_t isPerspective` to three structs:
- `SceneUniforms`: for raster sphere impostor shaders
- `RTUniforms`: for ray tracing fragment shader
- `RTUnitCellUniforms`: for RT unit cell occlusion shader (also added `float cameraForwardX/Y/Z` for orthographic occlusion ray direction)

**2. Metal Sphere Impostor (Raster) — `MetalShaderLibrary.mm`**
- **Vertex shader (`sphere_vertex`)**: Orthographic billboard sizing uses constant `R * 1.05` (no perspective foreshortening) vs the perspective formula `R * dist / sqrt(dist² - R²)`.
- **Fragment shader (`sphere_fragment`)**: Orthographic ray-sphere intersection uses parallel rays along -Z with origin at the billboard fragment position, instead of rays from the camera origin through the fragment. View direction for shading is constant `float3(0,0,1)` in ortho mode.

**3. Metal RT Ray Generation — `MetalShaderLibrary.mm`**
- Orthographic: ray origins vary per pixel (unprojected near-plane points in world space), ray direction is constant (camera forward). Perspective: single origin (camera position), varying directions.
- Changed `viewDir = normalize(cameraPos - hitPos)` to `viewDir = -rayDir` — correct for both projection modes.

**4. Metal RT Unit Cell Occlusion — `MetalShaderLibrary.mm`**
- Orthographic: traces backward parallel ray from fragment toward camera (using `cameraForward` direction), limited by distance from fragment to camera plane.
- Perspective: traces ray from camera position toward fragment (existing behavior).

**5. Metal Raster Uniform Upload**
- `MetalRenderer.mm`: uploads `isPerspective` to `SceneUniforms`.
- `MetalRayTracingRenderer.mm`: uploads `isPerspective` to `RTUniforms` and gizmo `SceneUniforms`.
- `MetalUnitCellShared.cpp`: uploads `isPerspective` and `cameraForward` vector to `RTUnitCellUniforms`.

**6. OpenGL Sphere Impostor (Raster) — `ShaderManager.cpp`**
- Same orthographic billboard sizing and ray-sphere intersection logic as Metal, using `uniform int uIsPerspective`.
- `SphereRenderer.cpp` and `UnitCellRenderer.cpp`: upload the new uniform.

**7. OpenGL RT Ray Generation — `RayTracingRenderer.cpp`**
- Same orthographic ray generation logic as Metal RT (varying origins, constant direction).
- Changed `viewDir = normalize(uCameraPos - hitPos)` to `viewDir = -rayDir`.
- Uploads `uIsPerspective` uniform.

**8. Viewport Q_PROPERTYs — `MetalViewport.h/.mm`, `OpenGLViewport.h/.cpp`**
- Added `isPerspective` (bool) and `fieldOfView` (float) Q_PROPERTYs with `projectionChanged` signal.
- Getters/setters delegate to `Camera` class.

**9. QML Sidebar Wiring — `Sidebar.qml`**
- Wired the existing but disconnected Perspective/Orthographic ComboBox to `sidebar.viewport.isPerspective`.
- Wired the existing FOV slider to `sidebar.viewport.fieldOfView`.
- FOV slider hides when orthographic is selected (not relevant in ortho mode).
- FOV range expanded to 10–120 to match Camera's valid range.

### What Is NOT Changed
- **Bond cylinder shaders**: use standard MVP transforms — correct with orthographic projection matrix as-is.
- **Line shaders / unit cell wireframe (raster)**: use `viewProjectionMatrix` directly — correct for both projections.
- **Viewport axes overlay**: uses its own ortho projection — unaffected.
- **RenderStateHash**: already included `isPerspective` and `orthoScale` — no changes needed.
- **Camera class**: already fully supported orthographic — no changes needed.
- **Rotation center gizmo**: uses cylinder shader with MVP — works correctly with orthographic.

### Files Modified
| File | Change |
|------|--------|
| `src/render/metal/MetalTypes.h` | Added `isPerspective` to `SceneUniforms`, `RTUniforms`, `RTUnitCellUniforms`; added `cameraForwardX/Y/Z` to `RTUnitCellUniforms` |
| `src/render/metal/MetalShaderLibrary.mm` | Updated MSL struct mirrors; ortho billboard sizing in sphere vertex; ortho ray-sphere in sphere fragment; ortho RT ray generation; ortho unit cell occlusion |
| `src/render/metal/MetalRenderer.mm` | Upload `isPerspective` to raster `SceneUniforms` |
| `src/render/metal/MetalRayTracingRenderer.mm` | Upload `isPerspective` to `RTUniforms` and gizmo `SceneUniforms` |
| `src/render/metal/MetalUnitCellShared.cpp` | Upload `isPerspective` and `cameraForward` to `RTUnitCellUniforms` |
| `src/render/opengl/ShaderManager.cpp` | Added `uIsPerspective` uniform; ortho billboard sizing and ray-sphere intersection in GLSL |
| `src/render/opengl/SphereRenderer.cpp` | Upload `uIsPerspective` uniform |
| `src/render/opengl/UnitCellRenderer.cpp` | Upload `uIsPerspective` uniform for unit cell corner spheres |
| `src/render/opengl/RayTracingRenderer.cpp` | Added `uIsPerspective` to RT shader; ortho ray generation; upload uniform |
| `src/ui/components/MetalViewport.h` | Added `isPerspective`/`fieldOfView` Q_PROPERTYs, getters, setters, signal |
| `src/ui/components/MetalViewport.mm` | Implemented `isPerspective`/`fieldOfView` getters and setters |
| `src/ui/components/OpenGLViewport.h` | Added `isPerspective`/`fieldOfView` Q_PROPERTYs, getters, setters, signal |
| `src/ui/components/OpenGLViewport.cpp` | Implemented `isPerspective`/`fieldOfView` getters and setters |
| `src/ui/qml/Sidebar.qml` | Wired projection ComboBox and FOV slider to viewport properties |

### Key Design Decisions
- **Billboard sizing**: Orthographic uses constant radius (no foreshortening) since all objects are at effectively infinite distance. The `1.05×` margin prevents edge clipping.
- **Ray-sphere intersection**: Orthographic parallel rays along -Z with per-fragment origins, vs perspective rays from camera origin. Both write depth via the same `projectionMatrix * hitPos` path — `clipPos.w = 1.0` under ortho, so depth works correctly without special handling.
- **RT ray generation**: Orthographic origins are computed by unprojecting each pixel's NDC through `invProjection` then transforming to world space with `invView`. Direction is constant camera forward vector.
- **viewDir = -rayDir**: Replaces the previous `normalize(cameraPos - hitPos)` which was only correct for perspective. Using `-rayDir` is correct for both modes.
- **Unit cell occlusion**: Orthographic occlusion uses backward parallel rays from the fragment, limited by the signed distance from the fragment to the camera plane. This correctly tests whether atoms occlude unit cell lines from the viewer's direction.
- **cameraForward as scalar floats**: Used `float cameraForwardX/Y/Z` instead of `simd_float3` in `RTUnitCellUniforms` to avoid SIMD alignment complexity in the struct layout.

### Verification
- Build: `cmake --build build` — success (no errors, only pre-existing OpenGL deprecation warnings on macOS).

---

## 2026-03-08: Fix RT-001 Resize Race and Wasted In-Flight Scene Upload

### Summary
Fixed two follow-up issues in the RT-001 async Metal RT submission introduced on 2026-03-07.

### Issue 1: Resize race on `inFlightSlot`
- `createRenderTargets()` forcibly reset `inFlightSlot` to -1 even when an old completion handler was still pending on the GPU.
- If the old handler fired after a new frame had been submitted, its CAS would incorrectly clear the new frame's `inFlightSlot`, breaking the one-in-flight invariant and potentially allowing concurrent writes to the single accumulation texture.
- **Fix:** removed the `inFlightSlot.store(-1)` from `createRenderTargets()`. The old completion handler now drains it naturally via CAS, and the generation check prevents it from marking stale slots as Ready.

### Issue 2: Wasted CPU work during in-flight frames
- The in-flight early-return check ran after `uploadSceneData()` and `uploadUnitCellData()`, so scene packing, BVH build, and buffer allocation were performed even when no frame could be submitted.
- **Fix:** moved the in-flight check to before the uploads. Dirty flags remain set and are processed once the in-flight frame drains.

### Files Modified
| File | Change |
|------|--------|
| `src/render/metal/MetalRayTracingRenderer.mm` | Removed `inFlightSlot` reset from `createRenderTargets()`; moved in-flight check before scene uploads in `render()` |

### Verification
- Build: `cmake --build build --target atom-render -j4` — success.

---

## 2026-03-07: RT-001 Metal Ray Tracing Without Per-Frame CPU Blocking

### Summary
Removed the Metal ray tracing renderer's per-frame `waitUntilCompleted()` stall from the normal render path. The renderer now submits work asynchronously, keeps multiple output textures in a small ring, and lets the viewport present the latest completed frame when it becomes available.

### Root Cause
- The Metal RT path previously blocked on the command buffer every frame.
- That forced CPU and GPU work into lockstep, preventing overlap between progressive sample submission and GPU execution.
- As scene cost increased, the render thread paid the full GPU latency on every sample.

### What Changed
- `MetalRayTracingRenderer` now tracks asynchronous output-slot state with a small buffered texture ring.
- Normal-frame submission no longer calls `waitUntilCompleted()` after RT and display passes.
- Command buffer completion handlers mark output slots ready once the GPU finishes rendering them.
- `outputTexture()` now returns the latest completed slot instead of assuming the just-submitted frame is immediately displayable.
- Added `needsMoreFrames()` so the viewport can keep scheduling progressive frames while GPU work is still in flight.
- `MetalViewport` now tolerates `outputTexture()` returning `nullptr` temporarily and keeps requesting frames until a completed RT texture is ready.

### Key Design Decisions
- Keep at most one RT frame in flight at a time, but avoid blocking the CPU while that frame executes.
- Use buffered output textures so the viewport never needs to read from the same texture currently being rendered.
- Preserve the existing progressive accumulation model and overlay composition flow.

### Files Modified
| File | Change |
|------|--------|
| `src/render/metal/MetalRayTracingRenderer.h` | Added async frame/output-slot helpers and `needsMoreFrames()` |
| `src/render/metal/MetalRayTracingRenderer.mm` | Replaced blocking waits with async submission, buffered output textures, and completion-handler state tracking |
| `src/ui/components/MetalViewport.mm` | Continue scheduling RT frames until a completed Metal output texture is ready |
| `renderer_improvement_plan.md` | Marked RT-001 fixed and recorded verification notes |

### Verification
- Build: `cmake --build build --target atom-render -j4` — success.
- Runtime validation: the Metal RT path now compiles and the viewport-side scheduling logic handles frames whose output texture is not ready yet without stalling the render thread.

---

## IMPORTANT — 2026-03-07: Automatic Bundled Python Synchronization in Normal Builds

### Summary
Fixed a build/runtime mismatch where the application binary was linked against one embedded Python version while the app bundle still contained an older bundled Python payload from a previous build. The concrete failure seen was `_struct` import failure because the executable was using Python 3.12 while the bundle still had `Resources/python/lib/python3.14`.

### Root Cause
- `cmake --build build` rebuilt the application binary, but bundled Python was only refreshed by the separate `deploy-python` / `deploy` targets.
- `PythonRuntime` previously selected the first `python3*` directory it found in the bundle, so stale bundled runtimes could be picked accidentally.
- This made the app vulnerable to ABI mismatches whenever the selected CMake Python changed or an old bundle was left in place.

### What Changed
- `bundle-python` is now part of the default build (`ALL`) so normal builds automatically synchronize the bundled Python runtime.
- `cmake/BundlePython.cmake` now:
  - records a manifest for the selected interpreter, version, stdlib source, ABI suffix, and platform,
  - rebuilds the bundled standard library when those inputs change,
  - synchronizes `site-packages` from the configured Python environment,
  - validates the result with `import struct, numpy, ase.io`.
- Added `scripts/sync_python_packages.py` plus `cmake/python-requirements.txt` so bundled Python dependencies are synchronized deterministically from the configured interpreter rather than installed ad hoc during deploy.
- `PythonRuntime.cpp` now only accepts a bundled `pythonX.Y` directory that exactly matches the embedded interpreter version; otherwise it skips the bundle instead of crashing on startup.

### IMPORTANT Rule Going Forward
- The user should never need to manage bundled Python manually for normal development builds.
- `cmake --build build` must always leave the app in a runnable state with a bundled Python runtime that matches the interpreter selected by CMake.
- Any future Python-related build changes must preserve automatic invalidation/rebuild of stale bundled runtimes.

### Verification
- `cmake --build build -j4` rebuilt the stale bundled runtime automatically.
- A second `cmake --build build -j4` run no-op'd cleanly with the bundled runtime reported as up to date.
- Launching `./build/bin/atom-studio.app/Contents/MacOS/atom-studio -platform offscreen` confirmed the app now uses bundled Python 3.12 and loads ASE successfully.

---

## 2026-03-07: Bond Ray Tracing (Unified BVH, Metal + OpenGL)

### Summary
Added bond (cylinder) rendering to the ray tracing renderer. Bonds now appear in RT mode with full Blinn-Phong shading, shadows, and ambient occlusion — matching the raster renderer's bond visualization. Uses a unified BVH containing both atom (sphere) and bond (cylinder) primitives for single-traversal efficiency.

### Architecture
- **Unified BVH**: Both atom and bond primitives share one acceleration structure. Primitive indices `[0, atomCount)` are spheres; `[atomCount, atomCount+bondCount)` are cylinders. One BVH traversal per ray covers both primitive types.
- **Generic BVH builder**: Refactored `BVH.h/cpp` to accept precomputed `PrimitiveBounds` (AABB + centroid + maxRadius). `buildSphereBVH` is now a thin wrapper. Bond AABBs bake in `bondRadius` with `maxRadius=0` (no runtime expansion needed).
- **Ray-cylinder intersection**: Standard Iq finite-cylinder formula with height bounds checking.
- **Bond data pipeline**: Bond world positions computed from atom positions + periodic image shifts (same logic as raster `BondRenderer`). Colors are averaged from the two bonded atoms.

### What Changed

| File | Change |
|------|--------|
| `src/render/common/BVH.h` | Added `PrimitiveBounds` struct and generic `buildBVH()` entry point |
| `src/render/common/BVH.cpp` | Refactored internals to AABB-based builder; `buildSphereBVH` wraps `buildBVH` |
| `src/render/common/RenderStateHash.cpp` | Added `bondRadius`, `showBonds` to hash |
| `src/render/metal/MetalTypes.h` | Added `bondCount`, `bondRadius`, `showBonds` to `RTUniforms` |
| `src/render/metal/MetalRayTracingRenderer.h` | Added bond buffer members, `m_bondCount`, `m_bondDataDirty`; renamed `uploadAtomData` → `uploadSceneData` |
| `src/render/metal/MetalRayTracingRenderer.mm` | Bond data upload (start/end/color buffers), unified BVH build, bond buffer binding at indices 7-9, `invalidateBondData` sets dirty flag |
| `src/render/metal/MetalShaderLibrary.mm` | Added `intersectCylinder` MSL function; updated `traceClosest`/`traceAnyHit` for unified traversal; `rt_fragment` branches shading on sphere vs cylinder hits |
| `src/render/opengl/RayTracingRenderer.h` | Added bond TBO members, `m_bondCount`, `m_bondDataDirty`; renamed `uploadAtomData` → `uploadSceneData` |
| `src/render/opengl/RayTracingRenderer.cpp` | Bond TBO upload, unified BVH build, GLSL `intersectCylinder`, unified leaf traversal + cylinder shading |

### Key Design Decisions
- **Unified BVH over dual BVH**: Single traversal per ray avoids redundant node tests. Bond primitives in shared leaf nodes are skipped when `showBonds=false`.
- **maxRadius=0 for bonds**: Bond cylinder AABBs already include `bondRadius`, so no runtime AABB expansion is needed. Only atom nodes expand when `atomScale > 1`.
- **Unit cell overlay compatibility**: The unit cell occlusion shader calls `traceAnyHit` with `bondCount=0, showBonds=false` — bond primitives in the unified BVH are safely skipped.

---

## 2026-03-05: World-Space Light Direction with Azimuth/Elevation Controls

### Summary
Changed the light direction from a view-space 3-component vector (X/Y/Z) to a world-space direction defined by two spherical angles (azimuth and elevation in degrees). This fixes two issues: (1) the old sliders coupled direction with brightness, and (2) the light was glued to the camera so orbiting never revealed a shadowed side.

### What Changed
1. **RenderSettings**: Replaced `lightDirX/Y/Z` with `lightAzimuth` (0°) and `lightElevation` (45°). Added `lightDirWorld()` helper that computes a unit vector using the convention: azimuth rotates in XY plane (0° = +X, 90° = +Y), elevation is angle from XY plane toward +Z (90° = +Z).
2. **Raster renderers** (SphereRenderer, BondRenderer, UnitCellRenderer): Transform world-space light direction to view space via `camera.viewMatrix()` before passing to shader. Shaders unchanged.
3. **RT renderers** (OpenGL + Metal): Pass `lightDirWorld()` directly — removed the old view→world transform that was keeping the light camera-relative.
4. **Viewport properties**: Replaced 3 Q_PROPERTYs (`lightDirX/Y/Z`) with 2 (`lightAzimuth`, `lightElevation`) in both OpenGLViewport and MetalViewport.
5. **Sidebar**: Replaced 3 direction sliders with 2 angle sliders (azimuth: -180° to 180°, elevation: -90° to 90°).

### Behavior Change
- Light is now fixed in world space — orbiting the camera reveals lit and shadowed sides of the structure.
- Angle-based parameterization always produces a unit vector, so direction changes cannot affect brightness.

### Files Modified
| File | Change |
|------|--------|
| `src/render/common/RenderSettings.h` | Replaced `lightDirX/Y/Z` with `lightAzimuth`/`lightElevation` + `lightDirWorld()` helper |
| `src/render/common/RenderStateHash.cpp` | Hash azimuth/elevation instead of X/Y/Z |
| `src/render/opengl/SphereRenderer.cpp` | World→view transform for `uLightDir` |
| `src/render/opengl/BondRenderer.cpp` | World→view transform for `uLightDir` |
| `src/render/opengl/UnitCellRenderer.cpp` | World→view transform for `uLightDir` |
| `src/render/opengl/RayTracingRenderer.cpp` | Pass `lightDirWorld()` directly, removed view→world transform |
| `src/render/metal/MetalRenderer.mm` | World→view transform for `uniforms.lightDir` |
| `src/render/metal/MetalRayTracingRenderer.mm` | Pass `lightDirWorld()` directly, removed view→world transform |
| `src/ui/components/OpenGLViewport.h` | Replaced 3 properties with 2 angle properties |
| `src/ui/components/OpenGLViewport.cpp` | Updated getters, setters, settings sync |
| `src/ui/components/MetalViewport.h` | Replaced 3 properties with 2 angle properties |
| `src/ui/components/MetalViewport.mm` | Updated getters, setters, settings sync |
| `src/ui/qml/Sidebar.qml` | Replaced 3 sliders with 2 angle sliders, updated reset defaults |

---

## 2026-02-26: Viewport XYZ Axes Solid Overlay + Correct Self-Depth (No Hollow Appearance)

### Summary
Fixed the viewport-corner XYZ axes overlay so it renders as a visually solid closed object with correct self-depth, eliminating the hollow-looking appearance seen from some camera angles.

### Root Cause
The axes overlay geometry already used capped cylinders/cones, but two overlay rendering choices caused the visual artifact:
1. A black center sphere was drawn as a masking hack over the axis junction, which could visually read like a hollow opening.
2. Back-face culling on the overlay meshes could produce cap/winding artifacts depending on orientation.

### What Changed
1. **Removed center masking sphere from viewport axes overlay**
   - Deleted the black center sphere draw from the viewport axes overlay render path in both OpenGL and Metal.
   - The axes now render using only the closed cylinder/cone meshes.
2. **Disabled face culling for viewport axes overlay meshes**
   - Kept depth testing enabled for correct self-occlusion.
   - Disabled culling for the overlay axes pass to avoid cap visibility/winding artifacts across orientations.
3. **Dedicated viewport-axes overlay shader/pipeline (flat color)**
   - Added a dedicated unlit/flat-color overlay shader program (OpenGL) and pipeline (Metal) for the viewport axes.
   - This decouples the axes overlay from the shared bond shader/pipeline and keeps the axes overlay logic isolated from scene bond rendering.

### Behavior Kept Unchanged
- **Overlay only**: the axes do not participate in scene raster geometry rendering or ray tracing geometry/BVH.
- **Size / scaling**: existing viewport position, scaling, and DPR behavior preserved.
- **Appearance**: axis colors and `X/Y/Z` labels remain unchanged.
- **Depth model**: scene depth is still cleared before axes overlay draw so it stays on top of the scene, while the axes object still self-occludes correctly.

### Files Modified
| File | Change |
|------|--------|
| `src/render/opengl/ViewportAxesRenderer.cpp` | Switched to dedicated axes shader; removed black center sphere draw; disabled culling for axes overlay pass |
| `src/render/opengl/ShaderManager.h` | Added `viewportAxesShader()` accessor + shader member declarations |
| `src/render/opengl/ShaderManager.cpp` | Added embedded viewport-axes vertex/fragment shaders and initialization/cleanup wiring |
| `src/render/metal/MetalViewportAxesRenderer.mm` | Switched to dedicated viewport axes pipeline; disabled culling; removed black center sphere draw |
| `src/render/metal/MetalShaderLibrary.h` | Added `viewportAxesPipeline()` accessor |
| `src/render/metal/MetalShaderLibrary.mm` | Added viewport axes overlay MSL shader functions and pipeline creation/cleanup/accessor |

### Verification
- Build: `cmake --build build --parallel 4` — success.
- Visual check: user confirmed the viewport axes visualization is now correct (solid appearance with proper depth cues).

---

## 2026-02-25: Viewport Overlay Layout Updates + Interactive XYZ Axes Widget

### Summary
Updated the viewport overlays in QML:
1. Moved the FPS/info overlay (including RT sample info) to the upper-right corner.
2. Reworked the XYZ axes indicator into an interactive UI-only overlay that can be hovered, dragged, and resized with the mouse wheel.

### What Changed
1. **FPS/info overlay repositioned**
   - Moved the viewport info block from bottom-left to top-right.
   - This includes the FPS label and RT-only `Mode: Ray Tracing` / `Samples` lines.
2. **XYZ axes overlay repositioned**
   - Moved the axes indicator to the lower-left area of the viewport.
3. **Removed axes background panel**
   - Replaced the old background `Rectangle` with a non-visual `Item`, so the axes render without a boxed background.
4. **Removed effective clipping window**
   - The axes now draw on a larger transparent `Canvas`, preventing arrowheads/labels from being cut off when they extend beyond the previous small canvas bounds.
5. **Interactive axes overlay (QML only)**
   - Hovering the axes area marks it as selected (no pre-click selection step).
   - Selected/hovered state is indicated by a slight axes enlargement.
   - Left click + hold directly drags the axes overlay.
   - Mouse wheel over the axes scales it (`up = enlarge`, `down = shrink`) with clamped limits.
   - When hovered, a hint bubble appears: `Left click to move. Scroll to change size.`
   - When not hovered, the axes are considered deselected.
6. **Viewport resize handling**
   - Added clamping logic so the draggable/resizable axes overlay stays within the viewport bounds when the viewport size changes.

### Files Modified
| File | Change |
|------|--------|
| `src/ui/qml/ViewportPanel.qml` | Repositioned FPS overlay; moved/de-backgrounded axes overlay; added hover/drag/wheel-resize interaction, hover hint, and bounds clamping for the XYZ axes widget |

### Notes
- The XYZ axes remain a **UI overlay only** and do not participate in scene rendering (raster or ray tracing).
- Axis orientation is still driven by `viewport.getAxisDirections()`; only the QML overlay behavior/presentation changed.

### Verification
- `qmllint src/ui/qml/ViewportPanel.qml` — no syntax errors in the updated QML; only expected module import warnings due to local lint environment not loading the `AtomStudio` QML module.
- No app run/build performed in this session.

---

## 2026-02-25: Ray Tracing Shadow Opacity Control + RT Sidebar Order Tweaks

### Summary
Added a `Shadow opacity` control for the ray tracing renderer to reduce cast-shadow darkness without changing other lighting terms, and exposed it in the sidebar (ray tracing mode). Also adjusted the order of several RT sidebar controls per request.

### What Changed
1. **New RT-only shadow strength parameter**: added `shadowOpacity` (default `1.0`) to `RenderSettings`.
   - `1.0` keeps the previous behavior (fully dark direct-light shadows on occlusion).
   - `0.0` removes direct-light shadow darkening.
2. **OpenGL RT shader path**: added `uShadowOpacity` and applied it only to the shadow factor when a shadow ray hits.
3. **Metal RT shader path**: matched the same behavior after discovering the initial implementation only affected OpenGL. The active macOS RT backend (`MetalRayTracingRenderer`) now uploads and uses `shadowOpacity`.
4. **State hashing**: included `shadowOpacity` in `computeRenderStateHash(...)` so progressive RT accumulation resets when the slider changes.
5. **Sidebar (RT mode)**:
   - Added `Shadow opacity` slider.
   - Reordered controls:
     - Moved `Ambient occlusion samples` and `Ambient occlusion radius` to after `Light direction (Z)`.
     - Moved `Shadow opacity` to after `Diffuse`.
   - Updated RT reset function to restore `shadowOpacity = 1.0`.

### Files Modified
| File | Change |
|------|--------|
| `src/render/common/RenderSettings.h` | Added `shadowOpacity` field (default `1.0f`) |
| `src/render/common/RenderStateHash.cpp` | Added `shadowOpacity` to render-state hash |
| `src/render/opengl/RayTracingRenderer.cpp` | Added `uShadowOpacity`, applied shadow opacity in RT shader, uploaded uniform |
| `src/render/metal/MetalTypes.h` | Added `shadowOpacity` to `RTUniforms` |
| `src/render/metal/MetalShaderLibrary.mm` | Applied `shadowOpacity` in Metal RT shadow shading |
| `src/render/metal/MetalRayTracingRenderer.mm` | Uploaded `shadowOpacity` into Metal RT uniforms |
| `src/ui/components/OpenGLViewport.h/.cpp` | Added Q_PROPERTY/getter/setter/signal/state sync for `shadowOpacity` |
| `src/ui/components/MetalViewport.h/.mm` | Added Q_PROPERTY/getter/setter/signal/state sync for `shadowOpacity` |
| `src/ui/qml/Sidebar.qml` | Added `Shadow opacity` slider, updated reset defaults, reordered RT controls |

### Notes
- The user reported the slider initially had no visible effect; root cause was backend coverage: only the OpenGL RT shader had been updated, while macOS uses the Metal RT path.
- The parameter intentionally affects **only cast-shadow darkness** (direct light shadow term), not global ambient/diffuse/specular strengths.

### Verification
- `git diff --check` — passed.
- No build/run performed in this session (no existing build directory in the workspace at the time of changes).

---

## 2026-02-25: Rotation Center Gizmo Overlay + Cylinder Geometry + Depth Ordering Fix

### Summary
Refined the Metal rotation-center axis gizmo used during orbit interaction:
1. **Raster mode overlay behavior**: the gizmo now always appears on top while visible (left mouse drag), matching the requested interaction behavior.
2. **Thicker gizmo geometry**: replaced 1-pixel line primitives with cylinder geometry so the axes are easier to see.
3. **Correct gizmo self-depth in overlay mode**: fixed the case where one axis could appear to stay on top due to fixed draw order when depth testing is disabled.
4. **Closed cylinder ends**: capped the cylinder mesh so axis tips no longer look hollow.
5. **Size tuning**: reduced gizmo half-length scale from `camera.distance() * 0.05f` to `camera.distance() * 0.03f`.

### Scope / Notes
- **Metal only**: the current branch does not have an equivalent OpenGL rotation-center gizmo render path wired in (`OpenGLRenderer` still renders unit cell, bonds, atoms only), so no OpenGL changes were made for this feature.
- **Trigger behavior unchanged**: the gizmo is still shown only while the left mouse button is held during orbit, and hidden on release.

### Architecture Decisions

#### 1. Overlay in Metal Raster Path
`MetalRenderer` now calls `MetalGizmoRenderer::render(..., depthTest=false)` (same as the RT display pass), so the gizmo is not occluded by atoms in raster mode.

#### 2. Reuse Bond Shader/Pipeline for Cylinders
Instead of creating a new shader, `MetalGizmoRenderer` now renders 6 instanced `BondInstance` cylinders (±X, ±Y, ±Z) using the existing bond pipeline. This preserves color control and keeps the implementation small.

#### 3. Flat Shading for Stable Axis Colors
The gizmo uses the bond shader path but overrides lighting terms (`ambient=1`, `diffuse=0`, `specular=0`) so the axis colors stay visually consistent regardless of scene lighting settings.

#### 4. Overlay-Mode Self-Depth via Painter Sorting
When `depthTest=false`, scene depth is intentionally disabled. To preserve the gizmo's own depth cues, the 6 half-axis cylinders are sorted by view-space midpoint `z` (back-to-front) and drawn one at a time.

#### 5. Capped Cylinder Mesh
The original cylinder mesh used for bonds/unit-cell edges is an open tube (no end caps). `MetalGizmoRenderer` now builds its own capped unit cylinder mesh so the axis tips render solid.

#### 6. RT Gizmo Uniforms Updated for Cylinder Path
After switching from the line shader to the bond shader, the RT display-pass gizmo path now fills `SceneUniforms.viewMatrix` and `SceneUniforms.projectionMatrix` (not only `viewProjectionMatrix`), since the bond vertex shader needs both.

### Files Modified
| File | Change |
|------|--------|
| `src/render/metal/MetalRenderer.mm` | Raster gizmo now renders with `depthTest=false`; gizmo half-length scale changed `0.05f` → `0.03f` |
| `src/render/metal/MetalRayTracingRenderer.mm` | RT gizmo `SceneUniforms` now fills `viewMatrix` + `projectionMatrix` for cylinder shader path; gizmo half-length scale changed `0.05f` → `0.03f` |
| `src/render/metal/MetalGizmoRenderer.h` | Updated docs: gizmo is now cylinder-based rather than line-based |
| `src/render/metal/MetalGizmoRenderer.mm` | Replaced line draw with cylinder mesh rendering via bond pipeline; added capped cylinder mesh generation; flat-color lighting override; overlay depth sorting of 6 axis segments |

### Verification
- Build: `cmake --build build --target atom-studio` — success.
- Build: `cmake --build build --target atom-render` — success (re-run after depth-order and capped-cylinder updates).

---

## 2026-02-24: Rotation Center Axis Gizmo + Bond Scale Default Fix

### Summary
Two related fixes:
1. Fixed `MetalViewport.h` bond scale default (`m_bondScale`) from 1.0 → 1.1, matching `OpenGLViewport.h` (the macOS Metal viewport was missed in the prior session).
2. Added a 3-axis (X/Y/Z) rotation center gizmo that appears at `Camera::m_target` while the left mouse button is held during rotation, then disappears on release.

### Rotation Center Gizmo — Design Decisions
- **Trigger**: shown on `mousePressEvent(LeftButton)`, hidden on `mouseReleaseEvent` when LeftButton is no longer pressed.
- **Appearance**: 6 line segments (±X red, ±Y green, ±Z blue) through the orbit center; half-length = `camera.distance() * 0.05f` for consistent apparent screen size.
- **Depth behavior**: depth-tested in raster mode (occluded naturally by atoms); depth-disabled in RT mode (displayed on top, since the RT display pass has no depth buffer).
- **Scope**: raster renderer draws it at the end of the main render pass. RT renderer draws it at the end of `renderDisplayPass()`, after the unit cell overlay.

### Architecture
New sub-renderer `MetalGizmoRenderer` follows the `MetalUnitCellRenderer` pattern. It reuses the existing `line_vertex`/`line_fragment` shaders and `linePipeline()` from `MetalShaderLibrary`. Vertex data (12 `LineVertex`) is built on the stack each call via `setVertexBytes:` — no persistent GPU buffer needed.

Gizmo state (`showRotationCenter`, `rotationCenterX/Y/Z`) is carried in `RenderSettings` (consistent with `showBonds`, `showUnitCell`). `MetalViewport::updatePaintNode()` writes `rotationCenterX/Y/Z` from `m_camera->target()` every frame, so it automatically tracks pan changes.

### Files Modified / Created
| File | Change |
|------|--------|
| `src/ui/components/MetalViewport.h` | `m_bondScale` 1.0f → 1.1f; added `m_showRotationCenter = false` |
| `src/render/common/RenderSettings.h` | Added `showRotationCenter`, `rotationCenterX/Y/Z` fields |
| `src/render/metal/MetalGizmoRenderer.h` | **New** — declares `MetalGizmoRenderer` with `render(..., bool depthTest = true)` |
| `src/render/metal/MetalGizmoRenderer.mm` | **New** — implements gizmo; selects `depthLessWriteState` or `depthDisabledState` based on `depthTest` flag |
| `src/render/metal/MetalRenderer.h` | Added `#include "MetalGizmoRenderer.h"` + `MetalGizmoRenderer m_gizmoRenderer` |
| `src/render/metal/MetalRenderer.mm` | Init/cleanup gizmo renderer; draw after spheres if `settings.showRotationCenter` |
| `src/render/metal/MetalRayTracingRenderer.h` | Added `#include "MetalGizmoRenderer.h"` + `MetalGizmoRenderer m_gizmoRenderer` |
| `src/render/metal/MetalRayTracingRenderer.mm` | Init/cleanup gizmo renderer; draw at end of `renderDisplayPass()` with `depthTest=false`; builds minimal `SceneUniforms` with remapped VP matrix |
| `src/render/CMakeLists.txt` | Added `MetalGizmoRenderer.h/.mm` to `RENDER_METAL_SOURCES` |
| `src/ui/components/MetalViewport.mm` | `mousePressEvent`: set `m_showRotationCenter=true` on LeftButton; `mouseReleaseEvent`: clear when LeftButton released; settings build: write `showRotationCenter` + center coords |

### Verification
- Build: `cmake --build build` — success (only expected macOS OpenGL deprecation warnings).

---

## 2026-02-24: Default Setting Changes + Raster Renderer Settings Exposure + Camera-Relative Light Direction

### Summary
Three related improvements to sidebar defaults, visibility, and light direction behavior:
1. Changed "Bond Scale" default from 1.0 → 1.1.
2. Changed "Specular" default from 0.5 → 0.0 for all renderers.
3. Exposed all general lighting/render settings (Ambient, Diffuse, Specular, Shininess, Light Direction X/Y/Z, Reset button) for the raster renderer — they were previously gated behind `rtSettingsVisible` (RT-only mode).
4. Fixed light direction slider behavior: changed light direction convention from world-space to camera-relative (view-space) across all four shader/renderer locations.

### Root Cause of Light Direction Issue
Raster shaders were transforming `lightDir` by the view matrix (`scene.viewMatrix * lightDir` in Metal; `mat3(uViewMatrix) * uLightDir` in OpenGL), expecting a world-space input. RT renderers used world-space directly. At the default camera orientation (yaw=45°, elevation=30°), world X and Z axes map diagonally into view space (partially onto the depth/forward axis), making those sliders produce minimal visual change and appear "limited in range." The fix: treat `lightDir` as view-space (camera-relative) in all shaders, and convert view→world for RT renderers using camera basis vectors.

### Changes

#### Default Values
| Location | Property | Old | New |
|----------|----------|-----|-----|
| `src/ui/qml/Sidebar.qml` | Bond Scale `defaultValue` | 1.0 | 1.1 |
| `src/ui/qml/Sidebar.qml` | Specular `defaultValue` | 0.5 | 0.0 |
| `src/ui/qml/Sidebar.qml` | Specular reset function | `= 0.5` | `= 0.0` |
| `src/ui/components/OpenGLViewport.h` | `m_bondScale` | 1.0f | 1.1f |
| `src/ui/components/OpenGLViewport.h` | `m_specularStrength` | 0.5f | 0.0f |
| `src/ui/components/MetalViewport.h` | `m_specularStrength` | 0.5f | 0.0f |
| `src/render/common/RenderSettings.h` | `specularStrength` | 0.5f | 0.0f |

#### Sidebar Visibility (Raster Mode Exposure)
Removed `visible: sidebar.rtSettingsVisible` from: Specular, Ambient, Diffuse, Shininess, Light Direction X, Light Direction Y, Light Direction Z, and the Reset button. Renamed Reset button text from `"Reset RT Settings"` → `"Reset Render Settings"`. RT-only items (Max RT Samples label+field, AO checkbox, Shadows checkbox, AO samples, AO radius) retain their `visible: sidebar.rtSettingsVisible` guard.

#### Light Direction — Camera-Relative Convention
| File | Change |
|------|--------|
| `src/render/metal/MetalShaderLibrary.mm` | `sphere_fragment` and `bond_fragment`: removed `(scene.viewMatrix * float4(scene.lightDir, 0.0)).xyz`, now uses `normalize(scene.lightDir)` directly — light dir is already in view/camera space |
| `src/render/metal/MetalRayTracingRenderer.mm` | `renderRTPass()`: converts view-space → world-space via `camera.rightVector()*x + camera.upVector()*y + (-camera.forwardVector())*z` before uploading to shader |
| `src/render/opengl/ShaderManager.cpp` | Sphere and bond fragment shaders: removed `mat3(uViewMatrix) *` from `lightDir` computation |
| `src/render/opengl/RayTracingRenderer.cpp` | `renderRTPass()`: same view→world transform as Metal RT |
| `src/render/common/RenderSettings.h` | Updated comment: `// Light direction (in view/camera space: X=right, Y=up, Z=toward viewer)` |

### Key Architectural Note
On macOS, `ViewportPanel.qml` instantiates `MetalViewport` (not `OpenGLViewport`) via `Qt.platform.os === "osx"`. Consequently, `MetalViewport.h` must be updated independently for any default value changes — it is not sufficient to only update `OpenGLViewport.h`.

---

## IMPORTANT — 2026-02-23: Unwrap Molecules Operation

### Summary
Added a "Unwrap Molecules" structure manipulation operation. For periodic structures, atoms are typically stored wrapped inside the unit cell, which can break the visual continuity of bonded molecules that span a periodic boundary. Unwrap resolves this by restoring each organic molecular fragment to a contiguous Cartesian arrangement, with the fragment's geometric centre placed inside the unit cell.

### Algorithm
1. **Precompute fractional coordinates** for all atoms using `Lattice::cartesianToFractional`.
2. **Classify atoms**: only non-metals and metalloids commonly found in organic chemistry are processed (H, B, C, N, O, F, Si, P, S, Cl, Ge, As, Se, Br, Sb, Te, I, At). All metallic and noble-gas atoms are skipped and their positions are never altered.
3. **Build an adjacency list** from the `BondList`, including only organic–organic bonds. The existing `Bond.imageX/Y/Z` fields encode which periodic image of the second atom is bonded to the first, so no minimum-image recomputation is needed.
4. **BFS over connected components**: starting from each unvisited organic atom, traverse the bond graph and accumulate an integer lattice-vector offset per atom so that bonded atoms are adjacent (offset propagation: `offset[v] = offset[u] + image_shift_from_bond`).
5. **Centre fragment in unit cell**: compute the geometric centre in adjusted fractional coordinates (`frac[i] + offset[i]`), then translate by `floor(centre)` to bring the centre into `[0, 1)³`.
6. **Write back Cartesian positions** via `Structure::setPosition()` using `Lattice::fractionalToCartesian`.

### Key Design Decisions
- **Bond image shifts used directly**: because `BondList` already stores the image vector `(imageX, imageY, imageZ)` for each cross-boundary bond (from bond detection), the unwrap step is exact and avoids floating-point minimum-image heuristics.
- **Operates on working structure** (`m_structure`), not the original. Modifications stack on top of whatever the current working copy is (e.g., you can replicate then unwrap). "Reset to Original" undoes everything.
- **Organic-only atoms moved**: skipping metals prevents accidentally dismantling coordination or ionic frameworks. Each organic fragment (including ligands of organometallic compounds) is unwrapped independently.
- **Button disabled until bonds exist**: the "Unwrap Molecules" button requires a non-empty `BondList`. It shows `"Bond detection required"` on hover if bonds haven't been detected yet, or `"Not applicable for non-periodic structures"` if the structure has no lattice.
- **Bond re-detection after unwrap**: emitting `structureUpdated(m_structure)` triggers the existing async bond-detection pipeline in the viewport, which re-detects bonds on the new positions (bonds across periodic images that are now resolved will have zero image shifts after re-detection).

### Files Modified
| File | Change |
|------|--------|
| `src/data/StructureOperations.h` | Added `void unwrapMolecules(Structure& s)` declaration + doc comment |
| `src/data/StructureOperations.cpp` | Implemented `unwrapMolecules` + internal `isOrganicElement` helper |
| `src/ui/components/StructureModel.h` | Added `Q_PROPERTY(bool hasBonds ...)`, `hasBonds()`, `Q_INVOKABLE void unwrapMolecules()` |
| `src/ui/components/StructureModel.cpp` | Implemented `hasBonds()` and `unwrapMolecules()` |
| `src/ui/qml/Sidebar.qml` | Added "Unwrap Molecules" button with disabled-overlay tooltip in Structure Manipulation section |

### Verification
- Build: `cmake --build build` — success (only expected macOS OpenGL deprecation warnings).

---

## IMPORTANT — 2026-02-23: Non-Destructive Structure Manipulation (Original + Working Copy)

### Summary
Introduced a non-destructive structure editing model. The original loaded structure is now preserved immutably; all user modifications (replication, future edits) operate on a separate working copy. The viewport always displays the working copy. A "Reset to Original" operation restores the working copy from the original at any time.

### Architecture — Original vs. Working Structure
`StructureModel` now owns two `shared_ptr<Structure>`:
- **`m_originalStructure`** — set once when a file is loaded, never modified thereafter.
- **`m_structure`** (working copy) — a deep clone of the original; receives all user edits and is what the viewport renders.

When `setStructure()` is called (file load), the incoming structure is stored as original and immediately cloned into the working copy. The viewport receives the clone via `structureUpdated`. This means the raw loaded structure is never directly rendered — the viewport always gets a clone.

### Structure::clone()
Added `std::unique_ptr<Structure> Structure::clone() const` — a full deep copy of all SoA arrays (positions, atomic numbers, symbols, radii, colors), optional arrays (velocities, forces, charges, masses), lattice, bond list (independent `BondList` copy-constructed from source), and all metadata.

### StructureOperations — replicateCell()
New pure function `atom::data::replicateCell(src, nx, ny, nz)` in `src/data/StructureOperations.h/.cpp`:
- Iterates all (i, j, k) image cells, copies each atom with position offset `i·a + j·b + k·c`
- Scales lattice vectors by nx/ny/nz; preserves PBC flags
- Replicates all optional per-atom arrays
- Leaves bond list empty — bond re-detection is triggered automatically by the viewport on `setStructure`
- Always operates on `m_originalStructure`, so clicking "Apply" with different values always produces the correct supercell size relative to the original (not cumulative)

### Sidebar — Structure Manipulation Section
New collapsible section in the sidebar with:
- **Replicate Unit Cell**: X/Y/Z integer text fields (1–99) + "Apply Replication" button. Fades to 40% opacity and shows tooltip "Not applicable for non-periodic structures" on hover when structure has no lattice.
- **Reset to Original**: button (enabled only when a structure is loaded) that clones `m_originalStructure` back into the working copy and pushes it to the viewport.

### Files Modified / Created
| File | Change |
|------|--------|
| `src/data/Structure.h` | Added `clone()` declaration |
| `src/data/Structure.cpp` | Implemented `clone()` |
| `src/data/StructureOperations.h` | **New** — declares `replicateCell()` |
| `src/data/StructureOperations.cpp` | **New** — implements `replicateCell()` |
| `src/data/CMakeLists.txt` | Added `StructureOperations.cpp/.h` to `atom-data` target |
| `src/ui/components/StructureModel.h` | Added `m_originalStructure`, `originalStructure()`, `resetToOriginal()`, `replicateCell()` |
| `src/ui/components/StructureModel.cpp` | Implemented `setStructure` (clone on load), `resetToOriginal`, `replicateCell`, updated `clear()` |
| `src/ui/qml/Sidebar.qml` | Added "Structure Manipulation" collapsible section |

### Verification
- Build: `cmake --build build` — success (only expected macOS OpenGL deprecation warnings).

---

## 2026-02-23: Mouse Interaction Fixes — Trackball Rotation + Metal Y-Flip

### Summary
Fixed two mouse interaction bugs in the Metal viewport: horizontal drag was rotating around the world Y axis (turntable style) instead of the camera's current up axis (true trackball), and vertical drag direction was inverted along with a mirrored coordinate frame in the axis indicator.

### Interaction Fixes
1. **Horizontal drag — trackball yaw** — `Camera::orbit()` was computing yaw around world +Y, causing rotation to always twist in the x-z plane regardless of camera tilt. Changed to use the camera's current up vector (`m_orientation.rotatedVector(QVector3D(0,1,0))`).
2. **Vertical drag direction + axis indicator** — `QSGSimpleTextureNode::MirrorVertically` was applied to the Metal texture in `MetalViewport.mm`. Metal textures are already Y-correct (the projection matrix maps world +Y → NDC Y=+1 → texture top), so the extra flip was double-inverting Y in the display. This caused drag-up to show the structure top instead of bottom, and made the axis indicator appear mirrored. Replaced with `NoTransform`. The RT renderer is unaffected — its NDC Y-inversion and the fullscreen quad texCoord mapping cancel independently.

### Files Modified
| File | Change |
|------|--------|
| `src/render/common/Camera.cpp` | `orbit()`: yaw axis changed from world +Y to camera up (`m_orientation.rotatedVector(0,1,0)`) |
| `src/ui/components/MetalViewport.mm` | `updatePaintNode()`: `MirrorVertically` → `NoTransform` |

### Verification
- Build: `cmake --build build` — success.
- Drag up → structure rotates to show its bottom face ✓
- Axis indicator X/Y/Z arrows align with the structure's coordinate frame ✓
- Ray Tracing mode renders correctly (not upside-down) ✓

---

## 2026-02-20: Quaternion Trackball Camera + Viewport Interaction Improvements

### Summary
Refactored the camera from Euler spherical coordinates (azimuth + elevation) to a quaternion-based orientation, and fixed four viewport interaction issues.

### Interaction Fixes
1. **Zoom-to-cursor** — scroll wheel now zooms toward the world point under the cursor rather than always toward the fixed orbit center, using `Camera::screenToWorld()` to shift `m_target` proportionally.
2. **MMB drag zoom direction** — drag up now zooms in (was inverted).
3. **Dynamic near/far planes** — `Camera::zoom()` now updates `m_near`/`m_far` after every zoom, matching `fitToView()` convention; prevents atom clipping when zooming in manually.
4. **macOS pinch-to-zoom** — added `bool event(QEvent*)` override in `OpenGLViewport` to handle `Qt::ZoomNativeGesture`, with the same zoom-to-pinch-center logic as the wheel handler.

### Quaternion Camera Refactor
Replaced `m_azimuth` + `m_elevation` + `clampElevation()` with a single `QQuaternion m_orientation`. Orbit style is turntable-with-quaternions: horizontal drag rotates around world Y (stable ground-plane feel), vertical drag rotates around the camera's current right axis. No elevation clamping → free rotation past the poles.

### Files Modified
| File | Change |
|------|--------|
| `src/render/common/Camera.h` | Replaced `m_azimuth`/`m_elevation` + setters/getters with `QQuaternion m_orientation`; removed `clampElevation()` |
| `src/render/common/Camera.cpp` | Rewrote `reset`, `orbit`, `position`, `rightVector`, `upVector`, `forwardVector`, `updateMatrices`; added `setOrientation()`; removed `setAzimuth/setElevation/clampElevation` |
| `src/render/common/RenderStateHash.cpp` | Hash 4 quaternion components instead of azimuth + elevation |
| `src/ui/components/OpenGLViewport.h` | Added `bool event(QEvent*)` override declaration |
| `src/ui/components/OpenGLViewport.cpp` | Added `#include <QNativeGestureEvent>`; rewrote `wheelEvent`; fixed `mouseMoveEvent` MMB sign; added `event()` for pinch |

### Verification
- Build: `cmake --build build` — success (only expected macOS OpenGL deprecation warnings).

---

## 2026-02-18: Sidebar Slider Controls — Numeric Input + Per-Field Reset

### Summary
Updated all sidebar slider controls to a unified UI pattern that adds:
1. A numeric text input synchronized with each slider.
2. A per-control `Reset` button.
3. Validation with error prompting and invalid-input revert behavior.

The new control layout is:
- top row: property label (left), numeric input (right), reset button (rightmost)
- second row: slider

This now applies to all current adjustable sliders and establishes the pattern for future slider controls.

### Files Modified
| File | Change |
|------|--------|
| `src/ui/qml/Sidebar.qml` | Added reusable `NumericSliderControl` component; migrated all 19 sliders; added slider input error dialog; wired per-slider reset and validation |
| `dev_log.md` | Added this session entry |

### Behavior Details

#### 1. Two-way Slider/Text Sync
- Moving a slider updates the numeric text field immediately.
- Entering a value in the text field and pressing `Enter` updates the slider and bound viewport property.

#### 2. Enter-Only Text Commit
- Numeric text fields commit values only on `Enter` (`onAccepted`).
- No auto-commit on focus loss.

#### 3. Type-Aware Validation
- Integer sliders accept integers only.
- Float sliders accept finite numeric values.
- Range checks enforce each slider's `from`/`to` bounds.

#### 4. Invalid Input Handling
- On invalid input, a modal error dialog is shown with a type/range-specific message.
- The text field reverts to the previous valid value (current slider value).

#### 5. Per-Control Reset
- Every slider row now includes a `Reset` button.
- Clicking `Reset` restores that control to its configured default and applies it through the same update path.

### Scope of Migrated Sliders (19 total)
- Visualization: `Atom Scale`, `Bond Scale`
- Unit Cell: `Thickness`, `R`, `G`, `B`
- Camera: `Field of View`
- Render Settings: `Ambient occlusion samples`, `Ambient occlusion radius`, `Ambient`, `Diffuse`, `Specular`, `Shininess`, `Light direction (X/Y/Z)`
- Background: `R`, `G`, `B`

### Verification
- `qmllint src/ui/qml/Sidebar.qml` completed successfully (no parse errors).
- Existing lint warnings unrelated to this feature (module import path/unqualified access/layout warnings) remain.

---

## 2026-02-18: Neighbor List / Bond Detection — Code Review Fixes (All 9 Steps)

### Summary
Applied all correctness and robustness fixes identified in `neighborlist_dev_code_review.md`. The review covered 10 findings; these fixes address 9 of them (Finding 7 is subsumed by the combined Finding 5+7 fix). All findings are now resolved and the progress table is complete.

### Files Modified (9 files)
| File | Findings addressed |
|------|-------------------|
| `src/data/NeighborList.h` | 1 — `applyMIC` per-axis PBC signature |
| `src/data/NeighborList.cpp` | 1 — `applyMIC` body + call sites; 3 — `perAtom` dedup; 4 — remove redundant MIC; 9 — `setBondScale`/`build()` guard; 10 — remove dead `covRadii[j]` guard |
| `src/data/ElementData.cpp` | 2 — noble gas covalent radii → −1 |
| `src/data/BondList.h` | 8 — `findBond`/`areBonded` image-shift params |
| `src/data/BondList.cpp` | 8 — `findBond`/`areBonded` image-shift comparison |
| `src/data/Structure.cpp` | 6 — `addAtom`, `updateRadiiFromElements` use `radiusForElement` |
| `src/ui/components/OpenGLViewport.h` | 5+7 — `BondResult` type + coalescing members |
| `src/ui/components/OpenGLViewport.cpp` | 5+7 — `launchBondTask`, `onBondsReady` structure-identity check; 9 — `setBondScale` clamping |
| `src/ui/components/MetalViewport.h` | 5+7 — `BondResult` type + coalescing members |
| `src/ui/components/MetalViewport.mm` | 5+7 — `launchBondTask`, `onBondsReady` structure-identity check; 9 — `setBondScale` clamping |

### Fix Details

#### Finding 1 — `applyMIC` wraps all axes regardless of `pbc[]` flags
`applyMIC` previously rounded all three fractional components unconditionally. Added `const std::array<bool, 3>& pbc` parameter; each axis is now gated by `pbc[k]`. Updated both call sites in `buildCellList`.

```cpp
double ridx = pbc[0] ? std::round(frac[0]) : 0.0;
double ridy = pbc[1] ? std::round(frac[1]) : 0.0;
double ridz = pbc[2] ? std::round(frac[2]) : 0.0;
```

**Impact**: Partial-PBC systems (slabs, wires) no longer receive spurious lattice translations along their non-periodic axes.

#### Finding 2 — Noble gases have positive covalent radii
Six noble gases (He, Ne, Ar, Kr, Xe, Rn) had positive covalent radii in `ElementData.cpp`, causing them to enter `activeAtoms` and form spurious bonds. Set covalent radius to `−1.0f` for all six; `vdwRadius` unchanged (used for display).

#### Finding 3 — Duplicate neighbor entries when cell dimension = 1
When `nx`, `ny`, or `nz` equals 1, all three `{−1, 0, +1}` offsets wrap to the same physical cell, producing identical `NeighborEntry` tuples three times. Added sort+unique deduplication of `perAtom[i]` before CSR packing:

```cpp
for (auto& neighbors : perAtom) {
    std::sort(neighbors.begin(), neighbors.end(), ...);
    neighbors.erase(std::unique(neighbors.begin(), neighbors.end(), ...), neighbors.end());
}
```

**Impact**: Eliminates triple bonds and inflated bond counts for small periodic cells.

#### Finding 4 — `buildBondList` discards MIC correction into dummy variables
`buildBondList` applied a second `applyMIC` call using throwaway accumulators — dead work that also carried the P1 bug. Removed the entire `hasPBC`/`applyMIC` block; the stored image shifts from `buildCellList` are already MIC-correct.

#### Findings 5 + 7 — Stale bond list + unbounded concurrent tasks
Replaced the monolithic `startBondDetection` with:
- **At-most-one coalescing**: if a task is running, the new request is recorded as pending and launched when the running task completes.
- **Structure-pointer identity check**: `BondResult` bundles `bonds` + the originating `structure` shared_ptr. `onBondsReady` checks `result.structure == m_structure` (pointer equality) before applying the result, discarding stale results from superseded structures.

Applied identically to both `OpenGLViewport` and `MetalViewport`.

#### Finding 6 — Sentinel −1 radius leaks into rendered atom radii
Two sites in `Structure.cpp` wrote `elem.covalentRadius` directly into `m_radii` without guarding against the `−1.0f` sentinel (which is set for elements Z ≥ 97 and for noble gases). Replaced both with `ElementData::radiusForElement()`, which already falls back to `vdwRadius` when `covalentRadius < 0`:

```cpp
// addAtom
m_radii.push_back(ElementData::radiusForElement(atomicNumber, false));

// updateRadiiFromElements
m_radii[i] = ElementData::radiusForElement(m_atomicNumbers[i], useVdW) * scale;
```

**Impact**: Eliminates negative atom display radii for noble gases and transuranium elements.

#### Finding 8 — `findBond`/`areBonded` ignore periodic image shifts
`findBond` compared only `atomIndex1` and `atomIndex2`, so for small PBC cells where the same atom pair can be bonded in two distinct images, the first match was always returned. Added `imageX/Y/Z = 0` default parameters and full image-shift comparison (with swap-and-negate normalization for `a1 > a2`). All existing callers using zero defaults remain unaffected.

#### Finding 9 — `setBondScale` unclamped; `build()` lacks input guard
- `OpenGLViewport::setBondScale` and `MetalViewport::setBondScale`: added `std::isfinite` rejection and `std::clamp(scale, 0.1f, 5.0f)` before the fuzzy-compare early-out.
- `NeighborList::build()`: added mandatory guard at the top — returns immediately (with empty list) if scale is non-finite or ≤ 0, preventing division-by-zero in grid dimension calculation regardless of call site.

#### Finding 10 — Dead `covRadii[j] >= 0.0f` guard in `buildCellList`
`cellAtoms` is populated exclusively from `activeAtoms`, which already enforces `covRadii[i] >= 0`. The inner guard was always true. Removed the `if` and replaced with a clarifying comment.

---

## 2026-02-18: *IMPORTANT* Neighbor List and PBC-Aware Bond Detection

### Summary
Implemented a complete neighbor list and bond detection system to replace the previous O(n²) brute-force `detectBonds()` approach. Bond detection now runs asynchronously on a background thread using `QtConcurrent`, supports periodic boundary conditions (PBC) with minimum image convention (MIC), stores image shift vectors on each bond for correct rendering of cross-boundary bonds, and exposes a user-tunable `bondScale` slider in the sidebar.

This change is **IMPORTANT** because it:
- Replaces the only remaining O(n²) algorithm in the data layer with an O(N log N) cell-list approach that scales to large periodic structures.
- Adds full PBC awareness to bond detection, which was previously broken or absent.
- Records image shift vectors `(imageX, imageY, imageZ)` on every bond, enabling both bond renderers (OpenGL and Metal) to draw cross-boundary bonds correctly by displacing atom j to its periodic image.
- Moves bond computation off the main/render thread, keeping the UI responsive while bonds are computed in the background.

### Files Created (2 files)
| File | Purpose |
|------|---------|
| `src/data/NeighborList.h` | `NeighborEntry` struct + `NeighborList` class declaration |
| `src/data/NeighborList.cpp` | Full cell-list implementation with PBC, MIC, CSR storage, and `buildBondList()` |

### Files Modified (14 files)
| File | Change |
|------|--------|
| `src/data/ElementData.h` | Added `static std::optional<float> covalentRadius(int atomicNumber)` accessor |
| `src/data/ElementData.cpp` | Updated full covalent radii table from `resources/covalent_radii.md`; sentinel `-1.0f` for N/A elements (Z ≥ 97); updated `radiusForElement()` to fall back to VdW radius |
| `src/data/BondList.h` | Added `imageX/Y/Z` fields to `Bond` struct; updated `addBond()` signature to include image shifts; removed old `detectBonds()` method |
| `src/data/BondList.cpp` | Rewrote `addBond()` with normalization (`atomIndex1 < atomIndex2`) and sign flip on image shifts; removed O(n²) bond detection code |
| `src/data/Structure.h` | Changed `m_bonds` from `unique_ptr` to `shared_ptr<BondList>`; added `setBondList(shared_ptr<BondList>)` |
| `src/data/Structure.cpp` | Constructor uses `make_shared<BondList>()`; updated `setBondList()` |
| `src/ui/components/OpenGLViewport.h` | Added `bondScale` Q_PROPERTY; added `m_bondWatcher`, `startBondDetection()`, `onBondsReady()` |
| `src/ui/components/OpenGLViewport.cpp` | Added `bondScale` getter/setter; implemented `startBondDetection()` and `onBondsReady()` using `QtConcurrent::run`; modified `setStructure()` to call `startBondDetection()` |
| `src/ui/components/MetalViewport.h` | Same `bondScale` Q_PROPERTY, watcher, and async detection declarations as OpenGLViewport |
| `src/ui/components/MetalViewport.mm` | Same `bondScale` getter/setter, `startBondDetection()`, `onBondsReady()` implementations; modified `setStructure()` |
| `src/ui/components/StructureModel.h` | Added `notifyBondsUpdated()` public slot |
| `src/ui/components/StructureModel.cpp` | Implemented `notifyBondsUpdated()` — emits `structureChanged()` to refresh QML `bondCount` binding |
| `src/render/opengl/BondRenderer.cpp` | Applied periodic image shift to cylinder endpoint: `endPos += imageX*a + imageY*b + imageZ*c` |
| `src/render/metal/MetalBondRenderer.mm` | Same image shift applied to `instances[i].end` via lattice matrix |
| `src/data/CMakeLists.txt` | Added `NeighborList.cpp` / `NeighborList.h` to `atom-data` |
| `src/ui/CMakeLists.txt` | Added `Qt6::Concurrent` to `atom-ui` link libraries |
| `src/ui/qml/Sidebar.qml` | Added "Bond Scale" label and slider (range 0.5–2.0, default 1.0) in Visualization section |

### Architecture and Algorithm Details

#### 1. NeighborEntry and CSR Storage
Each entry in the neighbor list is:
```cpp
struct NeighborEntry {
    uint32_t index;
    int8_t imageX, imageY, imageZ;  // periodic image of atom j
};
```
Neighbors are stored in Compressed Sparse Row (CSR) format:
- `m_neighbors[]` — flat array of all neighbor entries
- `m_offsets[]` — size `n+1`; neighbors of atom `i` are at `[m_offsets[i], m_offsets[i+1])`
- Symmetric: each ordered pair `(i, j)` appears twice
- Image shifts describe which periodic image of atom `j` is nearest to atom `i`: `real_pos_j = pos[j] + imageX*a + imageY*b + imageZ*c`

#### 2. Cell-List Algorithm (O(N log N))
1. **Active atoms**: only atoms with a defined covalent radius (elements Z = 1–96) are included. Elements Bk (Z=97) and above have `-1.0f` as sentinel and are excluded entirely.
2. **Global cutoff**: `2 * max_covalent_radius_present * scale`. This defines both the grid cell size and the neighbor search radius.
3. **Grid dimensions**: `nx = max(1, floor(boxX / globalCutoff))` (etc.), giving cells of size ≥ cutoff.
4. **Bounding box**:
   - Full PBC: from lattice matrix column sums.
   - Non-PBC: from active atom bounding box padded by `globalCutoff`.
5. **27-cell search**: for each atom `i`, search all 26 neighboring cells (3×3×3 minus self). PBC directions wrap with image shift tracking; non-PBC directions skip out-of-range cells.
6. **MIC** (Minimum Image Convention): after computing the raw displacement and image shifts, `applyMIC()` projects to fractional coordinates, rounds to nearest integer image, subtracts from displacement. This ensures the nearest periodic image is used in all cases.
7. **Pack into CSR**: results from per-atom temporary vectors are packed into the flat CSR arrays.

#### 3. Bond Detection Pass
`buildBondList()` iterates the neighbor list and for each pair `(i, j)` with `j > i`:
- Recomputes actual distance using the stored image shift.
- Applies MIC to ensure nearest image.
- Bond criterion: `dist > 0.4 Å && dist < (r_cov_i + r_cov_j) * scale`.
- Adds to `BondList` via `addBond(i, j, imageX, imageY, imageZ)`.
- `addBond` normalizes so `atomIndex1 < atomIndex2`; if indices are swapped, image shifts are negated.

#### 4. Async Bond Detection
Both viewport backends use an identical async pattern:
```
setStructure() or setBondScale()
    └── startBondDetection()
           └── QtConcurrent::run(lambda)
                   captures: shared_ptr<Structure> (by value), float scale
                   runs on: worker thread
                   returns: shared_ptr<BondList>
    └── QFutureWatcher::finished → onBondsReady()
           sets m_structure->setBondList(newBonds)
           sets m_needsStructureUpdate = true
           emits bondCountChanged()
           calls StructureModel::instance()->notifyBondsUpdated()
           calls update()
```
Calling `setFuture()` on an already-running watcher is safe — the new future replaces the old one and only the new future's `finished()` will fire (last-wins semantics for rapid scale changes).

#### 5. Bond Rendering with Image Shifts
Previously, `BondRenderer::setBondData()` and `MetalBondRenderer::setBondData()` used `pos[a2]` directly as the cylinder endpoint. Now:
```cpp
float ex = px[a2];
float ey = py[a2];
float ez = pz[a2];
if (bond.imageX || bond.imageY || bond.imageZ) {
    ex += bond.imageX * m[0][0] + bond.imageY * m[1][0] + bond.imageZ * m[2][0];
    ey += bond.imageX * m[0][1] + bond.imageY * m[1][1] + bond.imageZ * m[2][1];
    ez += bond.imageX * m[0][2] + bond.imageY * m[1][2] + bond.imageZ * m[2][2];
}
```
This makes cross-boundary bonds draw correctly even when the two atoms are on opposite sides of the unit cell.

#### 6. Type Decision: `shared_ptr<BondList>` Instead of `unique_ptr`
`QFuture<T>` in Qt 6 requires `T` to be copyable (the `result()` accessor returns by value). `unique_ptr` is not copyable. To avoid this constraint, `BondList` ownership was changed from `unique_ptr` to `shared_ptr` throughout:
- `Structure::m_bonds` is now `shared_ptr<BondList>`.
- `NeighborList::buildBondList()` returns `shared_ptr<BondList>`.
- `Structure::setBondList()` accepts `shared_ptr<BondList>`.
- The async lambda result type and `QFutureWatcher` template argument match: `shared_ptr<BondList>`.

#### 7. Covalent Radii Table
Values sourced from `resources/covalent_radii.md` (from https://periodictable.com). Table covers H (0.31 Å) through Cm (1.69 Å). Elements Bk–Og use sentinel `-1.0f` (N/A) and are excluded from the neighbor list. The static method `ElementData::covalentRadius(int)` returns `std::optional<float>` (nullopt for N/A).

#### 8. Bond Scale Slider
`bondScale` (default 1.0, range 0.5–2.0) is a Q_PROPERTY on both viewport backends. Changing it calls `setBondScale()`, which triggers `startBondDetection()` asynchronously — no re-upload needed until the background task completes.

### Verification
- Build: `cmake --build build` — success (only expected macOS OpenGL deprecation warnings).

---

## 2026-02-17: Simplify Input Pipeline to ASE-Only

### Summary
Removed the legacy `FileReader`/`FileReaderRegistry` abstraction layer and routed structure loading directly through `ASEReader`.

### Files Modified
- `src/io/AsyncFileLoader.cpp` - Calls `ASEReader::read()` directly with `PythonRuntime::GILGuard`
- `src/io/AsyncFileLoader.h` - Removed unused cancel state field
- `src/io/CMakeLists.txt` - Keeps only `AsyncFileLoader` sources
- `src/ui/components/FileController.cpp` - Uses `ASEReader::fileDialogFilter()` directly

### Files Deleted
- `src/io/FileReader.h`
- `src/io/FileReader.cpp`
- `src/io/FileReaderRegistry.h`
- `src/io/FileReaderRegistry.cpp`

### Note
Input parsing now has a single path: `FileController` -> `AsyncFileLoader` -> `ASEReader`.

---

## 2026-02-11: Exposed Ray-Tracing Parameter Controls in Sidebar (OpenGL + Metal)

### Summary
Implemented user-facing ray-tracing parameter controls and plumbing across both viewport backends, plus UX follow-ups:
1. Exposed AO/lighting/shininess/light-direction RT parameters to QML for both OpenGL and Metal viewports.
2. Added RT-only controls under `Render Settings` in the sidebar (hidden in raster mode).
3. Added `Reset RT Settings` button to restore RT defaults.
4. Added hover help prompts on parameter names and applied requested parameter label renames.
5. Fixed QML startup regression by replacing invalid `Label.hoverEnabled` usage with `MouseArea`-based hover detection.

### Files Modified (6 files)
| File | Change |
|------|--------|
| `src/ui/components/OpenGLViewport.h` | Added new Q_PROPERTY declarations/signals/members for RT parameters |
| `src/ui/components/OpenGLViewport.cpp` | Added getters/setters with clamping and wired new fields into `m_renderSettings` sync |
| `src/ui/components/MetalViewport.h` | Added matching Q_PROPERTY declarations/signals/members for RT parameters |
| `src/ui/components/MetalViewport.mm` | Added getters/setters with clamping and wired new fields into `m_renderSettings` sync |
| `src/ui/qml/Sidebar.qml` | Added RT-only controls, reset button, tooltips-on-name labels, and requested label renames |
| `dev_log.md` | Added this session entry |

### Exposed RT Parameters
- `aoSamples` (default `4`, range `1..16`)
- `aoRadius` (default `3.0`, range `1..10`)
- `ambientStrength` (default `0.3`, range `0..1`)
- `diffuseStrength` (default `0.7`, range `0..1`)
- `specularStrength` (default `0.5`, range `0..1`)
- `shininess` (default `32`, range `1..128`)
- `lightDirX/Y/Z` (defaults `0.3/0.8/0.5`, range `-1..1`)

### UI/UX Notes
- All ray-tracing parameter controls are gated by `rendererMode === 1` and hidden in `Raster (Fast)` mode.
- Added reset action restoring:
  - `maxRTSamples=1000`
  - `enableAO=false`
  - `enableShadows=false`
  - all newly exposed RT parameter defaults above
- Parameter name tooltips now appear only when hovering names (not sliders), using per-label `MouseArea.containsMouse`.
- Applied renames:
  - `AO Samples` -> `Ambient occlusion samples`
  - `AO Radius` -> `Ambient occlusion radius`
  - `Light Dir X/Y/Z` -> `Light direction (X/Y/Z)`

### Verification
- Build: `cmake --build build -j4` — success.

---

## 2026-02-11: BVH Upload Guard Cleanup + Follow-up Notes Retirement

### Summary
Completed follow-up items #7 and #8:
1. Removed dead BVH upload empty-buffer guard patterns in both OpenGL and Metal RT upload paths.
2. Added debug assertions to enforce BVH invariants for non-empty structures.
3. Retired `renderer_performance_followup.md` after all tracked items were completed.

### Files Modified (4 files)
| File | Change |
|------|--------|
| `src/render/opengl/RayTracingRenderer.cpp` | Added BVH non-empty assertions after `buildSphereBVH(...)`; removed dead `empty() ? nullptr : data()` upload branches; replaced unused `<cstring>` include with `<cassert>` |
| `src/render/metal/MetalRayTracingRenderer.mm` | Added BVH non-empty assertions after `buildSphereBVH(...)`; removed dead `empty() ? nullptr : data()` upload branches; added `<cassert>` include |
| `renderer_performance_followup.md` | Marked #7/#8 as fixed before retirement |
| `dev_log.md` | Added this session log entry |

### Implementation Details

#### 1. Enforced expected BVH invariants in upload paths
- In both RT backends, immediately after BVH construction:
  - `assert(!bvh.nodes.empty())`
  - `assert(!bvh.primitiveIndices.empty())`
- This documents and enforces the assumption that `atomCount > 0` yields at least one BVH root node and at least one primitive index.

#### 2. Removed dead empty-pointer upload branches
- OpenGL path now uploads BVH arrays using direct `.data()` pointers.
- Metal path now creates BVH buffers using direct `.data()` pointers.
- The old `empty() ? nullptr : data()` pattern was unreachable in normal non-empty-structure flow and is now removed for clarity.

#### 3. Follow-up notes retirement
- `renderer_performance_followup.md` was used to track renderer performance cleanup tasks.
- With all listed items completed, the file is removed and the completion record is kept in `dev_log.md`.

### Verification
- Build: `cmake --build build -j4` — success (only existing macOS OpenGL deprecation warnings).

---

## 2026-02-11: RT State Hash Unification + Metal Unit-Cell Shared Helper Extraction

### Summary
Implemented follow-up fixes #5 and #6 from `renderer_performance_followup.md`:
1. Removed duplicated RT state-hash logic across OpenGL and Metal by moving it to shared common code.
2. Extracted shared Metal unit-cell geometry/instance/uniform packing helpers and refactored both raster and RT paths to use them.

### Files Modified (11 files)
| File | Change |
|------|--------|
| `src/render/common/RenderStateHash.h` | Added shared RT state-hash function declaration |
| `src/render/common/RenderStateHash.cpp` | Added shared RT state-hash implementation |
| `src/render/opengl/RayTracingRenderer.h` | Removed backend-local `computeStateHash()` declaration |
| `src/render/opengl/RayTracingRenderer.cpp` | Replaced local hash call with `computeRenderStateHash(...)`; removed duplicated implementation |
| `src/render/metal/MetalUnitCellShared.h` | Added shared unit-cell helper API (mesh builders, instance packing, style/uniform packing) |
| `src/render/metal/MetalUnitCellShared.cpp` | Added shared unit-cell helper implementations |
| `src/render/metal/MetalUnitCellRenderer.mm` | Switched cylinder mesh build, lattice instance build, and style packing to shared helper |
| `src/render/metal/MetalRayTracingRenderer.h` | Removed backend-local `computeStateHash`; added overlay helper method declarations |
| `src/render/metal/MetalRayTracingRenderer.mm` | Switched to shared RT state hash + shared unit-cell builders; deduplicated overlay draw setup via `encodeUnitCellOverlayDraws(...)` |
| `src/render/CMakeLists.txt` | Added new common hash and metal unit-cell helper sources to build |
| `renderer_performance_followup.md` | Marked items #5 and #6 as fixed and updated implementation order |

### Implementation Details

#### 1. Shared RT state hash in common code
- Added `computeRenderStateHash(const Camera&, const RenderSettings&)` in `src/render/common/`.
- OpenGL RT and Metal RT now call this shared function for accumulation reset state checks.
- Removed duplicated per-backend hash implementations and now-unused `<functional>` includes.

#### 2. Shared unit-cell helper module for Metal
- Added `MetalUnitCellShared` helper module with:
  - `buildUnitCylinderMesh(...)`
  - `buildUnitSphereMesh(...)`
  - `buildUnitCellInstances(...)`
  - `makeUnitCellStyle(...)`
  - `makeRTUnitCellUniforms(...)`
- `MetalUnitCellRenderer` now reuses shared geometry/data/style logic.
- `MetalRayTracingRenderer` now reuses shared geometry/data/uniform logic.

#### 3. RT overlay draw-path deduplication
- Added:
  - `hasUnitCellOverlayData() const`
  - `encodeUnitCellOverlayDraws(void* encoder, const RTUnitCellUniforms&)`
- Both RT overlay call sites (`renderDisplayPass()` and `renderUnitCellOverlay()`) now call the same draw encoder helper, eliminating duplicated pipeline/buffer binding and draw-call code.

### Verification
- Build: `cmake --build build --target atom-render` — success.

---

## 2026-02-11: Metal RT Unit-Cell Overlay Occlusion Migrated to BVH

### Summary
Replaced brute-force per-fragment atom scanning in the Metal RT unit-cell overlay shader with BVH any-hit traversal. This aligns overlay occlusion with the main RT path and removes linear `O(atomCount)` cost from each overlay fragment.

### Files Modified (3 files)
| File | Change |
|------|--------|
| `src/render/metal/MetalTypes.h` | Extended `RTUnitCellUniforms` with `bvhNodeCount` |
| `src/render/metal/MetalShaderLibrary.mm` | Updated `RTUnitCellUniforms` mirror; changed `rt_unit_cell_fragment` to use `traceAnyHit(...)` with BVH buffers; removed linear atom loop |
| `src/render/metal/MetalRayTracingRenderer.mm` | Populated `unitCell.bvhNodeCount`; bound BVH fragment buffers (slots `3..6`) for both overlay render paths |

### Implementation Details

#### 1. Shader-side occlusion now uses BVH any-hit
- `rt_unit_cell_fragment` now receives BVH node and primitive-index buffers.
- Occlusion test computes the same camera-to-fragment ray and `maxT` distance bound as before, then calls `traceAnyHit(...)`.
- If any atom is hit before `maxT`, the fragment is discarded (same visual rule as previous implementation).

#### 2. Overlay uniform includes BVH node count
- Added `bvhNodeCount` to `RTUnitCellUniforms` in both CPU and MSL definitions to gate traversal validity in the overlay shader.

#### 3. Both overlay execution paths now provide BVH data
- Updated `renderDisplayPass()` overlay draw path and `renderUnitCellOverlay()` draw path to bind:
  - atom positions (`buffer(1)`)
  - BVH node min/max/meta + primitive indices (`buffers 3..6`)

### Behavioral Impact
- Unit-cell overlay atom-occlusion behavior is preserved.
- Overlay occlusion traversal now scales with BVH-candidate intersections instead of scanning all atoms per fragment.

### Verification
- Build: `cmake --build build -j4` — success.

---

## 2026-02-11: MetalViewport QSGTexture Cache (Eliminate Per-Frame Wrapper Churn)

### Summary
Fixed a render-thread hot-path inefficiency in `MetalViewport`: the scene graph wrapper (`QSGTexture`) for the Metal output texture was being recreated every frame via `fromNative(...)`, and the previous wrapper was deleted immediately. Replaced this with keyed wrapper caching + explicit scene-graph resource cleanup.

### Files Modified (2 files)
| File | Change |
|------|--------|
| `src/ui/components/MetalViewport.h` | Added `releaseResources()` override for scene-graph teardown cleanup |
| `src/ui/components/MetalViewport.mm` | Added bounded texture-wrapper cache (native texture handle + size + window key), LRU pruning, render-thread cleanup job, and switched node ownership to `setOwnsTexture(false)` |

### Implementation Details

#### 1. Keyed wrapper reuse in `updatePaintNode()`
- Replaced unconditional per-frame `QNativeInterface::QSGMetalTexture::fromNative(...)` calls with cache lookup first.
- Cache key includes:
  - native `MTLTexture` pointer (as `void*`)
  - texture size
  - `QQuickWindow*` (wrapper is window-bound)
- On cache miss, create wrapper once and store it; on hit, reuse existing wrapper.

#### 2. Explicit ownership model for texture wrappers
- `QSGSimpleTextureNode` now uses `setOwnsTexture(false)`.
- Wrapper lifetime is managed by the viewport cache rather than per-frame node replacement/deletion behavior.

#### 3. Bounded cache with pruning
- Added a small bounded cache (`kMaxCachedTextures = 8`) with LRU-style pruning to prevent unbounded growth if output textures rotate across resizes/mode switches.

#### 4. Safe scene-graph teardown cleanup
- Implemented `releaseResources()` to flush cached wrappers.
- Cleanup is scheduled as a render job (`scheduleRenderJob`) so wrapper deletion happens on the render thread during scene-graph lifecycle transitions.

### Behavioral Impact
- Steady-state rendering no longer allocates/frees a `QSGTexture` wrapper every frame.
- Wrapper recreation now occurs only when the underlying native output texture identity or size changes.

### Verification
- Build: `cmake --build build -j4` — success.

---

## 2026-02-11: Metal RT Single Command Buffer + Deferred Accumulation Clear

### Summary
Consolidated the Metal ray-tracing renderer from 2-3 `waitUntilCompleted` syncs per frame down to 1, and replaced the immediate accumulation-clear pass with a deferred flag.

### Files Modified (2 files)
| File | Change |
|------|--------|
| `src/render/metal/MetalRayTracingRenderer.h` | Added `m_accumNeedsClear` flag; changed `renderRTPass` / `renderDisplayPass` / `renderUnitCellOverlay` signatures to accept `void* cmdBuffer` |
| `src/render/metal/MetalRayTracingRenderer.mm` | `render()` creates one `MTLCommandBuffer` and passes it to all sub-passes; `resetAccumulation()` just sets flag instead of submitting a GPU clear; `renderRTPass()` uses `loadAction = Clear` on first sample after reset |

### Implementation Details

#### 1. Single command buffer per frame
- Previously each sub-pass (`renderRTPass`, `renderDisplayPass`, `renderUnitCellOverlay`, `resetAccumulation`) created its own `MTLCommandBuffer`, committed, and called `waitUntilCompleted`.
- Now `render()` creates one command buffer, passes it (as `void*` through the C++ header boundary) to each sub-pass which adds render command encoders to it, and commits+waits once at the end.
- Metal handles resource dependencies between render passes within a single command buffer automatically.

#### 2. Deferred accumulation clear
- `resetAccumulation()` no longer submits a GPU command. It sets `m_sampleCount = 0` and `m_accumNeedsClear = true`.
- `renderRTPass()` checks the flag: if set, uses `loadAction = MTLLoadActionClear` (clearing to black) and resets the flag; otherwise uses `loadAction = MTLLoadActionLoad` to preserve existing accumulation.
- This eliminates a separate clear-only command buffer + sync that fired on every camera/settings change.

### Verification
- Build: `cmake --build build -j4` — success.

---

## 2026-02-11: BVH Traversal Optimization + Builder Safety Fix

### Summary
Two fixes identified during code review of the BVH implementation:

1. **Eliminated double-fetch in GPU BVH traversal** (both OpenGL GLSL and Metal MSL): every interior node's children were fetched and AABB-tested twice — once to determine near/far traversal order, and again when popped from the stack. Restructured `traceClosest` and `traceAnyHit` so popped nodes only fetch their metadata (1 buffer read), while children are AABB-tested (2 reads each) before pushing. Cuts per-node buffer reads roughly in half.

2. **Fixed fragile dangling reference in CPU BVH builder**: `buildNode()` took a `std::vector` reference via `back()` then called itself recursively, which pushes more nodes into the same vector. The reference survived only because of a pre-allocated `reserve()`. Replaced with index-based access that is unconditionally safe regardless of reallocation.

### Files Modified (3 files)
| File | Change |
|------|--------|
| `src/render/common/BVH.cpp` | Replaced `BVHNodeGPU& node = ctx.result.nodes.back()` with `ctx.result.nodes[nodeIndex]` at all usage sites |
| `src/render/opengl/RayTracingRenderer.cpp` | Replaced `fetchNodeIntersection` with `testNodeAABB` (AABB-only, no meta fetch); restructured `traceClosest` and `traceAnyHit` to fetch meta at pop time and test children before pushing |
| `src/render/metal/MetalShaderLibrary.mm` | Same traversal restructuring in MSL: replaced `fetchNodeIntersection` with `testNodeAABB`; restructured `traceClosest` and `traceAnyHit` |

### Implementation Details

#### 1. Traversal Restructuring (GLSL + MSL)
- Removed `fetchNodeIntersection` (fetched min + max + meta = 3 reads + AABB test per call).
- Added `testNodeAABB` (fetches min + max = 2 reads + AABB test, no meta).
- **Old flow**: pop node → 3 reads + AABB test → if interior, pre-fetch each child (3 reads + AABB test each) → push → children re-tested when popped.
- **New flow**: pop node → 1 read (meta only) → if leaf, test primitives → if interior, `testNodeAABB` each child (2 reads + AABB test) → push hits only.
- Each node is now fetched 3 times total (2 for AABB when tested as a child + 1 for meta when popped), down from 6 (3 when pre-tested + 3 when popped).
- `traceAnyHit` additionally benefits from pre-testing children before pushing, so non-intersecting nodes are never pushed to the stack.

#### 2. BVH Builder Safety (CPU)
- `buildNode()` used `BVHNodeGPU& node = ctx.result.nodes.back()` then recursed into `buildNode()` which pushes more nodes.
- The reference was only safe due to `reserve(atomCount * 2)` preventing reallocation.
- Replaced all 4 usages (`minAndMaxRadius`, `maxAndPad`, leaf `meta`, interior `meta`) with `ctx.result.nodes[nodeIndex].field` which is safe regardless of vector capacity.

### Verification
- Build: `cmake --build build -j4` — success (only existing macOS OpenGL deprecation warnings).

---

## 2026-02-11: *IMPORTANT* BVH Ray-Tracing Migration (Replace Brute-Force Atom Traversal)

### Summary
Implemented a full BVH-based ray traversal pipeline for both OpenGL and Metal ray-tracing renderers, replacing the previous brute-force per-ray atom scan.

This change is *IMPORTANT* because it fundamentally changes RT intersection complexity from testing every atom for every ray to testing only BVH-candidate atoms, which is the core scalability upgrade for large structures.

### Scope
This entry documents only the BVH method implementation.

### Files Modified (9 files)
| File | Change |
|------|--------|
| `src/render/common/BVH.h` | Added shared GPU-friendly BVH node layout and builder API |
| `src/render/common/BVH.cpp` | Implemented CPU BVH construction for atom spheres |
| `src/render/CMakeLists.txt` | Added BVH sources to `atom-render` common sources |
| `src/render/opengl/RayTracingRenderer.h` | Added BVH TBO resource handles and node count state |
| `src/render/opengl/RayTracingRenderer.cpp` | Built/uploaded BVH buffers; replaced GLSL brute-force traversal with stack-based BVH traversal |
| `src/render/metal/MetalTypes.h` | Extended RT uniform struct with BVH node count |
| `src/render/metal/MetalRayTracingRenderer.h` | Added BVH node count state |
| `src/render/metal/MetalRayTracingRenderer.mm` | Built/uploaded BVH `MTLBuffer`s; bound BVH buffers to RT fragment stage |
| `src/render/metal/MetalShaderLibrary.mm` | Replaced MSL brute-force traversal with stack-based BVH traversal in RT fragment shader |

### Implementation Details

#### 1. Shared BVH Builder (CPU-side)
- Added a backend-agnostic BVH builder in `src/render/common/`.
- Implemented a flat node array suitable for GPU traversal:
  - `minAndMaxRadius = vec4(min.xyz, maxBaseRadiusInNode)`
  - `maxAndPad = vec4(max.xyz, pad)`
  - `meta = uvec4(leftChild, rightChild, firstPrim, primCount)`
- Build strategy:
  - top-down recursive build
  - split axis chosen by largest centroid extent
  - median partition via `std::nth_element`
  - leaf threshold set by `leafSize` (default `8`)
- Output:
  - `nodes[]` (flat BVH node list)
  - `primitiveIndices[]` (leaf primitive index table)

#### 2. OpenGL RT Integration
- Added BVH texture-buffer resources:
  - node min data (`RGBA32F`)
  - node max data (`RGBA32F`)
  - node metadata (`RGBA32UI`)
  - primitive indices (`R32UI`)
- In `uploadAtomData()`:
  - build BVH from structure SoA (`positionsX/Y/Z`, `radii`)
  - flatten/upload all BVH arrays to TBOs
- In RT shader:
  - removed linear `for (i=0; i<uAtomCount; ++i)` traversal
  - added iterative stack traversal (`BVH_STACK_SIZE = 64`)
  - implemented BVH-based:
    - `traceClosest(...)`
    - `traceAnyHit(...)`
  - preserved existing shading/AO/shadow behavior
  - added primitive index bounds guard (`primIndex < uAtomCount`)

#### 3. Metal RT Integration
- Added BVH buffers in renderer implementation:
  - `bvhNodeMinBuffer`, `bvhNodeMaxBuffer`, `bvhNodeMetaBuffer`, `bvhPrimIndexBuffer`
- Extended RT uniform payload with `bvhNodeCount`.
- In `uploadAtomData()`:
  - build BVH using shared builder
  - pack node/min/max/meta arrays to `MTLBuffer`s
  - upload primitive index array
- In RT pass:
  - bound BVH buffers to fragment slots `[3..6]`
- In embedded MSL RT shader:
  - replaced brute-force traversal with iterative BVH traversal
  - implemented BVH-based closest-hit and any-hit functions
  - preserved AO/shadow/lighting logic
  - added primitive index bounds guard (`primIndex < atomCount`)

#### 4. Correctness Safeguard for Runtime Atom Scaling
- BVH is built from base radii in structure data.
- During traversal, node AABBs are conservatively expanded when `atomScale > 1` using node `maxBaseRadius`, preventing missed intersections without forcing BVH rebuilds on atom-scale UI changes.

### Behavioral Impact
- Primary RT intersection path now uses BVH traversal in both backends.
- Visual shading model is unchanged (same Blinn-Phong + optional AO + optional shadows).
- Expected effect: significant RT speedup for large atom counts due to reduced intersection candidates per ray.

### Verification
- Build command: `cmake --build build -j4`
- Result: success (no new build errors introduced by BVH migration).

---

## 2026-02-11: Background UX Refactor + Immediate RT Background Updates

### Summary
Refactored viewport background controls and behavior:
- simplified empty-viewport placeholder visuals (`ATOM STUDIO` only, removed grid and extra helper text)
- changed default background color to neutral light gray RGB `(230, 230, 230)`
- removed background control from `Render Settings` and introduced a dedicated `Background` sidebar section
- added live RGB background controls and a `Reset to Default` button in the new section
- exposed background color as a backend-agnostic viewport property for both OpenGL and Metal viewports
- fixed RT accumulation behavior so background color changes apply immediately during progressive sampling
- cleaned bottom-left viewport info overlay by removing `Atoms` and `Bonds` lines

### Files Modified (10 files)
| File | Change |
|------|--------|
| `src/render/common/RenderSettings.h` | Updated default background color to RGB `(230, 230, 230)` |
| `src/ui/components/OpenGLViewport.h` | Added `backgroundColor` Q_PROPERTY/getter/setter/signal and default member |
| `src/ui/components/OpenGLViewport.cpp` | Synced viewport `backgroundColor` into `RenderSettings`; implemented getter/setter |
| `src/ui/components/MetalViewport.h` | Added `backgroundColor` Q_PROPERTY/getter/setter/signal and default member |
| `src/ui/components/MetalViewport.mm` | Synced viewport `backgroundColor` into `RenderSettings`; implemented getter/setter |
| `src/ui/qml/Sidebar.qml` | Removed old background row under `Render Settings`; added new `Background` section with RGB sliders, preview swatch, and `Reset to Default` button |
| `src/ui/qml/ViewportPanel.qml` | Updated placeholder text/content; removed startup grid; set panel placeholder background defaults; removed `Atoms`/`Bonds` rows from viewport info overlay |
| `src/render/opengl/RayTracingRenderer.cpp` | Added background RGB channels to RT state hash so background edits trigger immediate accumulation reset |
| `src/render/metal/MetalRayTracingRenderer.mm` | Added background RGB channels to RT state hash so background edits trigger immediate accumulation reset |
| `dev_log.md` | Added this entry |

### Architecture Decisions

#### 1. Dedicated Background Controls
Background configuration now lives in its own sidebar section to separate scene/background concerns from renderer mode and quality controls.

#### 2. Single Source of Truth Per Viewport Backend
Both viewport implementations now expose a `backgroundColor` property that is copied into `RenderSettings` every frame. This keeps QML control wiring backend-independent and avoids hidden renderer-local divergence.

#### 3. RT Accumulation Resets on Background Changes
Progressive RT output is an average over accumulated samples. To avoid slow color blending artifacts when background changes mid-sampling, background RGB is part of RT state hashing so changes force `resetAccumulation()` immediately.

#### 4. Cleaner Startup and HUD
Removed non-essential startup visual noise (grid + helper text) and reduced HUD clutter by removing always-on `Atoms`/`Bonds` rows from the bottom-left overlay.

### Verification
- Build: `cmake --build build -j4` — success.
- Notes: only existing macOS OpenGL deprecation warnings were emitted (no new build errors).

---

## 2026-02-11: Replace Header Strip with Floating Viewport Tab Bar

### Summary
Reworked the UI shell for viewport controls:
- collapsed all `Properties` sidebar sections by default on startup
- removed the in-window header strip from `Main.qml`
- introduced a floating rounded tab bar overlay inside the viewport
- migrated previous menu content into the floating bar, then reduced it to `File` and `Edit` tabs per updated requirement
- added long-press drag repositioning for the floating bar
- added robust default placement logic: centered horizontally and `3%` from the top of the viewport
- added drag-handle UX polish: slightly smaller vertical handle block, hover tooltip, and double-click reset to default position

On macOS, this avoids relying on native menu-bar placement and keeps the control surface directly in the viewport region.

### Files Modified (4 files)
| File | Change |
|------|--------|
| `src/ui/qml/Main.qml` | Removed in-window `HeaderBar` usage and separator so `SplitView` starts at top |
| `src/ui/qml/Sidebar.qml` | Changed collapsible sections default from expanded to collapsed |
| `src/ui/qml/ViewportPanel.qml` | Added floating tab bar UI, `File`/`Edit` menus, long-press drag, bounds clamping, default placement (`center + top 3%`), hover tooltip, and double-click reset |
| `dev_log.md` | Added this entry |

### Architecture Decisions

#### 1. Keep Menu Surface in Viewport Space
Instead of the old header strip, controls now live in `ViewportPanel.qml` so positioning and interaction are consistent across platforms, including macOS where `MenuBar` is otherwise promoted to the system menu bar.

#### 2. Drag Requires Explicit Long Press
Floating-bar movement is gated behind `pressAndHold` to avoid accidental drags during normal clicking. Once armed, drag is bounded to viewport extents.

#### 3. Preserve Deterministic Default Placement
A dedicated `placeDefaultPosition()` function computes startup position from current viewport size (`x` centered, `y = 0.03 * height`). Repositioning is reapplied during early resizes until the user manually drags the bar.

#### 4. Add Fast Recovery Gesture
Double-click on the drag handle clears manual position state and restores default placement immediately.

### Verification
- Build: `cmake --build build` — success.
- Runtime smoke check: `QT_QPA_PLATFORM=offscreen ./build/bin/atom-studio.app/Contents/MacOS/atom-studio` — startup success, no QML load errors in this session.

---

## 2026-02-10: Unify Camera and Light Source Across Renderers

### Summary
Fixed lighting inconsistency when switching between Raster and Ray Tracing renderers. The root cause was a coordinate-space mismatch: raster shaders interpreted the light direction as **view space** (headlamp fixed to camera), while RT shaders interpreted it as **world space** (sun fixed in scene). The same default value `(0.3, 0.8, 0.5)` produced different visual results depending on the active renderer.

Also refactored render settings ownership: `RenderSettings` was previously owned by each renderer (via the `Renderer` base class), and the viewport only synced a subset of fields to the active renderer — lighting params were never synced and stayed at hard-coded defaults. Now the viewport owns `RenderSettings` and passes it to `render()`, so all renderers share identical settings.

### Files Modified (17 files)
| File | Change |
|------|--------|
| `src/render/common/Renderer.h` | Changed `render()` to accept `const RenderSettings&`; removed `m_settings` member and `settings()` accessors |
| `src/render/common/RenderSettings.h` | Updated lightDir comment from "view space" to "world space" |
| `src/render/opengl/OpenGLRenderer.h` | Updated `render()` and `renderBackground()` signatures |
| `src/render/opengl/OpenGLRenderer.cpp` | Use `settings` parameter instead of `m_settings`; pass to sub-renderers |
| `src/render/opengl/RayTracingRenderer.h` | Updated `render()` signature; added private `m_settings` for `isConverged()`/`computeStateHash()` |
| `src/render/opengl/RayTracingRenderer.cpp` | Accept and store settings at top of `render()` |
| `src/render/opengl/ShaderManager.cpp` | Sphere and bond fragment shaders: added `uniform mat4 uViewMatrix`, transform lightDir from world to view space |
| `src/render/opengl/SphereRenderer.cpp` | Updated comment: light direction is world space, shader transforms to view space |
| `src/render/metal/MetalRenderer.h` | Updated `render()` signature |
| `src/render/metal/MetalRenderer.mm` | Use `settings` parameter instead of `m_settings` throughout |
| `src/render/metal/MetalRayTracingRenderer.h` | Updated `render()` signature; added private `m_settings` |
| `src/render/metal/MetalRayTracingRenderer.mm` | Accept and store settings at top of `render()` |
| `src/render/metal/MetalShaderLibrary.mm` | Sphere and bond fragment shaders: transform lightDir from world to view space via `viewMatrix` |
| `src/ui/components/OpenGLViewport.h` | Added `RenderSettings m_renderSettings` member |
| `src/ui/components/OpenGLViewport.cpp` | Build settings from viewport properties; pass to `render()`; removed per-field sync via `settings()` |
| `src/ui/components/MetalViewport.h` | Added `RenderSettings m_renderSettings` member |
| `src/ui/components/MetalViewport.mm` | Build settings from viewport properties; pass to `render()`; removed per-field sync via `settings()` |

### Architecture Decisions

#### 1. Canonicalize Light Direction as World Space
All renderers now interpret `lightDir` as world space. Raster shaders (GLSL and MSL) transform to view space in the fragment shader: `lightDir_view = mat3(viewMatrix) * lightDir_world`. RT shaders already used world space — no change needed there.

#### 2. Viewport Owns RenderSettings
`RenderSettings` moved from `Renderer` base class to the viewport. The viewport builds settings from its member variables and passes them into `render(const Camera&, const RenderSettings&)`. This ensures both renderers always see identical settings, including lighting parameters that were previously never synced.

#### 3. RT Renderers Keep a Local Settings Copy
`isConverged()` and `computeStateHash()` are called outside `render()`, so RT renderers store a private `m_settings` copy updated at the top of each `render()` call.

### Verification
- Build: `cmake --build build` — success (only expected OpenGL deprecation warnings on macOS).
- Runtime validation: not executed in this session (build-only verification).

---

## 2026-02-09: Add MSAA for Metal Unit-Cell Rendering + Increase Cylinder Resolution

### Summary
Improved unit-cell visual quality and renderer consistency by:
- enabling multisample anti-aliasing (MSAA) for Metal raster output
- enabling MSAA for Metal RT display/output compositing paths used by the unit-cell overlay
- increasing unit-cell cylinder tessellation from 20 to 48 segments

This addresses jagged/dashed unit-cell lines when zoomed out and keeps unit-cell appearance aligned across OpenGL, Metal raster, and Metal RT views.

### Files Modified (8 files)
| File | Change |
|------|--------|
| `src/render/metal/MetalShaderLibrary.h` | Extended initialization API to accept raster sample count for rasterized pipelines |
| `src/render/metal/MetalShaderLibrary.mm` | Applied sample count to sphere/bond/line/display/RT-unit-cell-overlay pipelines |
| `src/render/metal/MetalRenderer.mm` | Added 4x MSAA render target path (with fallback), multisample color resolve to output texture |
| `src/render/metal/MetalRayTracingRenderer.h` | Updated display pass signature to include camera for integrated overlay compositing |
| `src/render/metal/MetalRayTracingRenderer.mm` | Added 4x MSAA display/overlay targets (with fallback), integrated display+overlay single-pass resolve path, updated no-atom overlay path, increased RT unit-cell cylinder segments to 48 |
| `src/render/metal/MetalUnitCellRenderer.mm` | Increased raster unit-cell cylinder segments from 20 to 48 |
| `src/render/opengl/UnitCellRenderer.cpp` | Increased OpenGL unit-cell cylinder segments from 20 to 48 for cross-renderer parity |
| `dev_log.md` | Added this entry |

### Architecture Decisions

#### 1. Resolve MSAA into Existing Output Textures
Both Metal raster and Metal RT keep their existing single-sample output textures for presentation, while rendering into multisample color targets and resolving per frame.

#### 2. Keep Pipeline Sample Count and Target Sample Count Matched
Rasterized Metal pipelines now use the configured sample count to avoid mismatches between pipeline state and multisample render targets.

#### 3. Make RT Display + Unit-Cell Overlay Share the Same MSAA Pass
In Metal RT mode, display compositing and unit-cell overlay are executed in one render pass when possible, so both benefit from the same MSAA resolve step.

#### 4. Increase Geometry Smoothness at Source
Unit-cell cylinders now use 48 segments across renderers to reduce faceting and improve silhouette quality.

### Verification
- Build: `cmake --build build` — success.
- Runtime validation: not executed in this session (build-only verification).

---

## 2026-02-09: Fix Metal Raster Unit-Cell Cylinder Interior Artifact

### Summary
Fixed a Metal raster rendering artifact where unit-cell cylinders could appear to show their interior shell when intersecting atom spheres.

Root cause: cylinder draws used back-face culling but did not explicitly set front-face winding on the Metal render encoder, so culling behavior could depend on implicit defaults. The cylinder index topology is authored as counter-clockwise (CCW), so we now set front-face winding explicitly before culling.

Applied the same fix to the shared bond-cylinder renderer path to keep cylinder face handling consistent across Metal raster geometry.

### Files Modified (3 files)
| File | Change |
|------|--------|
| `src/render/metal/MetalUnitCellRenderer.mm` | Set `MTLWindingCounterClockwise` before `MTLCullModeBack` for unit-cell edge-cylinder draw |
| `src/render/metal/MetalBondRenderer.mm` | Set `MTLWindingCounterClockwise` before `MTLCullModeBack` for bond-cylinder draw |
| `dev_log.md` | Added this entry |

### Architecture Decisions

#### 1. Make Face Orientation Explicit in Metal Raster Cylinder Passes
For cylinder meshes with authored CCW indices, explicitly setting front-face winding prevents dependency on encoder defaults and keeps back-face culling deterministic.

#### 2. Keep Cylinder Behavior Consistent Between Unit Cell and Bonds
Both passes use the same geometric convention; applying the same winding policy avoids class-specific rendering discrepancies.

### Verification
- Build: `cmake --build build` — success.
- Runtime validation: not executed in this session (build-only verification).

---

## 2026-02-09: Replace Unit Cell Lines with Dedicated Unit-Cell Object

### Summary
Replaced the previous line-primitive unit-cell visualization with a dedicated **unit-cell object** rendered as a thick wireframe cuboid:
- 12 cylindrical edges
- 8 corner joints for smooth edge intersections

The new unit-cell object is rendered outside ray-tracing accumulation and outside shadow logic. Atom objects remain the only geometry participating in ray tracing, AO, and shadows.

Added a dedicated **Unit Cell** section in the sidebar with runtime controls for:
- Show/hide unit cell
- Unit-cell thickness (cylinder/joint radius)
- Unit-cell color (RGB, opaque)

### Files Modified (18 files)
| File | Change |
|------|--------|
| `src/render/common/RenderSettings.h` | Replaced `unitCellLineWidth` with `unitCellThickness`; default unit-cell color is opaque RGB |
| `src/render/opengl/UnitCellRenderer.h` | Refactored renderer interface/state from line buffers to cylinder+joint object buffers |
| `src/render/opengl/UnitCellRenderer.cpp` | Implemented unit-cell object generation/rendering (instanced cylinders + corner sphere joints) |
| `src/render/opengl/OpenGLRenderer.cpp` | Updated unit-cell render stage comment/usage for object rendering |
| `src/render/metal/MetalUnitCellRenderer.h` | Updated API to render with full settings (thickness/color) |
| `src/render/metal/MetalUnitCellRenderer.mm` | Replaced line rendering with cylinder edges + sphere-joint object rendering |
| `src/render/metal/MetalRenderer.mm` | Updated unit-cell render call to pass settings and object path |
| `src/render/metal/MetalTypes.h` | Replaced RT line overlay uniforms with `RTUnitCellUniforms` |
| `src/render/metal/MetalShaderLibrary.h` | Replaced old RT line pipeline accessor with cylinder/sphere unit-cell overlay accessors |
| `src/render/metal/MetalShaderLibrary.mm` | Replaced RT line overlay shader/pipeline with RT unit-cell object overlay shaders/pipelines |
| `src/render/metal/MetalRayTracingRenderer.h` | Added unit-cell overlay object state counters (edges/joints/index counts) |
| `src/render/metal/MetalRayTracingRenderer.mm` | Replaced RT line overlay with object overlay (cylinders + joints), atom-occlusion-only fragment logic preserved |
| `src/ui/components/OpenGLViewport.h` | Added Q_PROPERTY controls for `showUnitCell`, `unitCellThickness`, `unitCellColor` |
| `src/ui/components/OpenGLViewport.cpp` | Wired unit-cell settings into renderer sync; added setters/getters/signals |
| `src/ui/components/MetalViewport.h` | Added Q_PROPERTY controls for `showUnitCell`, `unitCellThickness`, `unitCellColor` |
| `src/ui/components/MetalViewport.mm` | Wired unit-cell settings into renderer sync; added setters/getters/signals |
| `src/ui/qml/Sidebar.qml` | Added dedicated Unit Cell customization section (toggle, thickness slider, RGB sliders + swatch) |
| `dev_log.md` | Added this entry |

### Architecture Decisions

#### 1. Unit Cell as Its Own Rendered Object
The unit cell is now represented by explicit raster geometry (edge cylinders + corner joints), not API line primitives. This makes thickness and color behavior stable and visually consistent.

#### 2. Smooth Edge Connectivity
Each corner uses a spherical joint so adjacent cylindrical edges blend without visible gaps or hard line joins.

#### 3. Keep Unit Cell Outside Ray-Tracing/Shadows
- RT accumulation remains atom-only.
- Unit cell in Metal RT mode is composited in a dedicated post-display overlay pass.
- Overlay visibility uses atom-only occlusion tests; unit-cell geometry itself is not ray traced and does not cast/receive shadows.

#### 4. Centralized Runtime Controls
Unit-cell visibility, thickness, and RGB color are exposed in both viewport backends and bound directly to sidebar controls for immediate live updates.

### Verification
- Build: `cmake --build build` — success.
- Runtime validation: not executed in this session (build-only verification).

---

## 2026-02-09: Metal RT Unit Cell Overlay (Non-Ray-Traced)

### Summary
Added unit cell visualization to **Ray Tracing mode** on the **Metal backend** using a separate line-overlay pass (not ray-traced geometry). The unit cell is built from the lattice with the same 8-corner/12-edge topology as raster mode, then composited over the RT output.

To enforce correct spatial relationships with atoms, the new line fragment shader performs an atom-only occlusion test: for each line fragment, it casts a ray from the camera to the fragment and discards the fragment if any atom sphere is hit first. This means:
- Unit cell lines behind atoms are hidden.
- Unit cell lines in front of atoms remain visible.
- Atom shadowing/AO does not block unit cell lines (only atom geometry does).

Also updated the RT frame flow so the display + overlay passes still run after convergence, keeping the unit cell overlay responsive when the sample counter has reached `maxRTSamples`.

### Files Modified (6 files)
| File | Change |
|------|--------|
| `src/render/metal/MetalRayTracingRenderer.h` | Added unit-cell upload/render helpers and state flags (`m_unitCellDataDirty`, `m_unitCellEdgeCount`) |
| `src/render/metal/MetalRayTracingRenderer.mm` | Added unit cell buffers/upload path, RT overlay pass, and render sequencing updates (RT pass conditional, display+overlay always) |
| `src/render/metal/MetalShaderLibrary.h` | Added `rtLinePipeline()` accessor |
| `src/render/metal/MetalShaderLibrary.mm` | Added `RTLineUniforms`, `rt_line_vertex`, `rt_line_fragment`, and a dedicated RT line overlay pipeline |
| `src/render/metal/MetalTypes.h` | Added shared `RTLineUniforms` struct for CPU/GPU layout parity |
| `dev_log.md` | Added this entry |

### Architecture Decisions

#### 1. Keep Unit Cell Out of RT Accumulation
The unit cell is drawn in a dedicated post-display line pass to avoid introducing non-ray-traced primitives into the accumulation buffer. RT accumulation remains atom-only.

#### 2. Atom-Only Occlusion in Overlay Shader
Instead of depth-buffer compositing with RT output, the overlay fragment shader analytically tests visibility against atom spheres and discards occluded line fragments. This guarantees occlusion depends only on atoms, not on lighting/shadow terms.

#### 3. Reuse Raster Unit Cell Topology
Unit cell geometry uses the same lattice-derived corner construction and fixed 12-edge index topology as raster rendering for consistency across render modes.

#### 4. Overlay Still Draws After Convergence
When RT reaches `maxRTSamples`, sampling stops but display and overlay passes continue. This keeps unit cell visibility consistent during UI/camera redraws without adding new RT samples.

### Verification
- Build: `cmake --build build` — success.
- Runtime validation: not executed in this session (build-only verification).

---

## 2026-02-09: Ray Tracing Sample Cap + QML Stability Fixes

### Summary
Added a user-configurable maximum progressive sample cap for **Ray Tracing (Quality)** in the Render Settings panel. The cap now defaults to `1000`, accepts only integers in `[1, 10000]`, and stops accumulation when the current sample count reaches the cap. Accumulation resumes after camera/state changes because the existing reset path sets sample count back to zero.

Also fixed follow-up QML runtime issues from the initial UI integration:
- `ReferenceError: maxRTSamplesField is not defined`
- `QML Dialog: Binding loop detected for property "implicitWidth"`
- Font alias warning caused by explicit `"monospace"` family requests

### Files Modified (8 files)
| File | Change |
|------|--------|
| `src/render/common/RenderSettings.h` | Changed default `maxRTSamples` from `4096` to `1000` |
| `src/ui/components/OpenGLViewport.h` | Added `maxRTSamples` Q_PROPERTY, getter/setter, change signal, and default member value |
| `src/ui/components/OpenGLViewport.cpp` | Synced `maxRTSamples` into renderer settings and added range validation in setter (`1..10000`) |
| `src/ui/components/MetalViewport.h` | Added `maxRTSamples` Q_PROPERTY, getter/setter, change signal, and default member value |
| `src/ui/components/MetalViewport.mm` | Synced `maxRTSamples` into renderer settings and added range validation in setter (`1..10000`) |
| `src/ui/qml/Sidebar.qml` | Added RT sample input field (shown only in RT mode), hover tooltip text, validation/error dialog, and fixed field scope/binding issues |
| `src/ui/qml/ViewportPanel.qml` | Removed explicit monospace family declarations to avoid font alias fallback warning |
| `dev_log.md` | Added this entry |

### Behavior Notes
- Tooltip on the RT sample input is: `"Enter any integer between 1 and 10000"`.
- Invalid input (non-integer, `< 1`, `> 10000`) is rejected and reverted to the last valid value.
- The cap applies to both implemented RT backends (OpenGL and Metal), because both use `m_settings.maxRTSamples` for convergence checks.

### Verification
- Build: `cmake --build build` — success.
- Manual run verification: RT sample cap works as expected; entering invalid values shows error and preserves prior valid value.

---

## 2026-02-08: Metal Rendering Backend

### Summary
Implemented a complete Metal rendering backend for macOS with full parity to the existing OpenGL backend. Includes instanced raster rendering (impostor spheres, instanced cylinder bonds, wireframe unit cell) and a fragment-shader ray tracing renderer with progressive accumulation. The app now uses Metal as the primary graphics API on macOS, with OpenGL as the fallback on other platforms. Platform selection is automatic at build time (CMake) and runtime (QML Loader).

### Files Created (15 files)

**Infrastructure:**
| File | Purpose |
|------|---------|
| `src/render/metal/MetalTypes.h` | Shared CPU/GPU struct definitions (`SceneUniforms`, `SphereInstance`, `BondInstance`, `LineVertex`, `RTUniforms`, `DisplayUniforms`) using `<simd/simd.h>` |
| `src/render/metal/MetalShaderLibrary.h` | PIMPL-based shader library interface — compiles MSL, creates 5 pipeline states + 2 depth stencil states |
| `src/render/metal/MetalShaderLibrary.mm` | All MSL shader source (embedded C-string, runtime compiled) + pipeline creation with per-pass blend/depth config |

**Raster sub-renderers:**
| File | Purpose |
|------|---------|
| `src/render/metal/MetalSphereRenderer.h` | Impostor sphere renderer interface |
| `src/render/metal/MetalSphereRenderer.mm` | Billboard quad + instanced `SphereInstance` buffer, perspective-correct ray-sphere intersection in fragment shader |
| `src/render/metal/MetalBondRenderer.h` | Bond cylinder renderer interface |
| `src/render/metal/MetalBondRenderer.mm` | Procedural cylinder geometry (12 segments) + instanced `BondInstance` buffer with orthonormal basis construction |
| `src/render/metal/MetalUnitCellRenderer.h` | Unit cell wireframe renderer interface |
| `src/render/metal/MetalUnitCellRenderer.mm` | 8 vertices + 24 indices (12 edges) drawn as `MTLPrimitiveTypeLine` |

**Coordinators:**
| File | Purpose |
|------|---------|
| `src/render/metal/MetalRenderer.h` | Raster renderer implementing `Renderer` interface, owns shader library + 3 sub-renderers |
| `src/render/metal/MetalRenderer.mm` | Offscreen BGRA8 + Depth32Float textures, own command queue, depth remap from OpenGL [-1,1] to Metal [0,1] |
| `src/render/metal/MetalRayTracingRenderer.h` | RT renderer implementing `Renderer` interface with progressive accumulation |
| `src/render/metal/MetalRayTracingRenderer.mm` | Two-pass: additive RT into RGBA32Float, display pass divides by sample count. State hash for convergence detection |

**Qt integration:**
| File | Purpose |
|------|---------|
| `src/ui/components/MetalViewport.h` | `QQuickItem`-based viewport with identical Q_PROPERTY interface as `OpenGLViewport` |
| `src/ui/components/MetalViewport.mm` | `updatePaintNode()` gets MTLDevice from Qt, renders offscreen, wraps MTLTexture via `QNativeInterface::QSGMetalTexture::fromNative()`, displays via `QSGSimpleTextureNode` |

### Files Modified (6 files)
| File | Change |
|------|--------|
| `CMakeLists.txt` | Added `enable_language(OBJCXX)` in `if(APPLE)` for Objective-C++ compilation |
| `src/render/CMakeLists.txt` | Populated `RENDER_METAL_SOURCES` (13 files), enabled `-framework Metal -framework QuartzCore` linking |
| `src/ui/CMakeLists.txt` | Added `UI_METAL_SOURCES`, `ATOM_HAS_METAL` compile definition, Metal framework linking |
| `src/main.cpp` | Conditional `setGraphicsApi(Metal)` on macOS, OpenGL + QSurfaceFormat on other platforms |
| `src/core/Application.cpp` | Registered `MetalViewport` QML type under `#ifdef ATOM_HAS_METAL` |
| `src/ui/qml/ViewportPanel.qml` | Replaced direct `OpenGLViewport` with conditional `Loader` (`Qt.platform.os === "osx"` → Metal, else → OpenGL) |

### Architecture Decisions

#### 1. QQuickItem + Offscreen MTLTexture (not QQuickFramebufferObject)
`QQuickFramebufferObject` is OpenGL-specific. Metal viewport uses `QQuickItem` with offscreen rendering to an `MTLTexture`, wrapped as a `QSGTexture` via `QNativeInterface::QSGMetalTexture::fromNative()` and displayed through `QSGSimpleTextureNode`. Qt's scene graph is set to Metal via `QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal)`.

#### 2. PIMPL for Objective-C++ Isolation
All Metal classes use `struct Impl` (PIMPL) to keep Objective-C types (`id<MTLDevice>`, etc.) out of C++ headers. Public interfaces pass `void*` pointers with `(__bridge id<MTL*>)` casts in `.mm` files. Headers are pure C++ and importable from any translation unit.

#### 3. Metal NDC Depth [0,1] Remapping
OpenGL uses [-1,1] NDC depth; Metal uses [0,1]. A bias matrix (`z_metal = z_gl * 0.5 + 0.5`) is applied to the projection matrix in `MetalRenderer::remapDepthToMetal()`. The sphere impostor fragment shader writes `clipPos.z / clipPos.w` directly for custom depth via `[[depth(any)]]`.

#### 4. Atom Data as MTLBuffer (Simpler than OpenGL TBOs)
The RT renderer accesses atom positions and colors via `device const float4*` buffer pointers in the fragment shader — direct array indexing with no texture indirection. This replaces OpenGL's `samplerBuffer` + `texelFetch` pattern.

#### 5. MSL Shaders Embedded as C-Strings
All MSL shader source is a single C-string in `MetalShaderLibrary.mm`, compiled at runtime via `[device newLibraryWithSource:options:error:]`. This matches the OpenGL pattern in `ShaderManager.cpp` where GLSL is also embedded as inline strings.

#### 6. Duplicate Shader Library Instances
Both `MetalRenderer` and `MetalRayTracingRenderer` own their own `MetalShaderLibrary` instance. This avoids lifetime/ownership complexity when switching between renderers, at the cost of compiling the same MSL twice (one-time ~50ms each).

#### 7. Platform-Conditional QML Viewport
`ViewportPanel.qml` uses a `Loader` with `Qt.platform.os === "osx"` to select `MetalViewport` or `OpenGLViewport`. The `property var viewport: viewportLoader.item` alias ensures all external QML references (`sidebar.viewport.*`, `mainWindow.viewportPanel.viewport.*`) work transparently regardless of backend.

### MSL Shader Summary

| Shader | Purpose |
|--------|---------|
| `sphere_vertex` / `sphere_fragment` | Impostor billboard with perspective-correct sizing, ray-sphere intersection, Blinn-Phong, custom depth `[[depth(any)]]` |
| `bond_vertex` / `bond_fragment` | Instanced cylinders with orthonormal basis construction, Blinn-Phong |
| `line_vertex` / `line_fragment` | Unit cell wireframe (MVP transform + flat color) |
| `fullscreen_vertex` | Shared full-screen quad for RT and display passes |
| `rt_fragment` | Brute-force ray-sphere traversal, PCG RNG, sub-pixel jitter, shadows, ambient occlusion |
| `display_fragment` | Divides accumulation texture by sample count |

### Pipeline Configuration

| Pipeline | Pixel Format | Blend | Depth |
|----------|-------------|-------|-------|
| Sphere | BGRA8Unorm | Alpha | Less+Write |
| Bond | BGRA8Unorm | Alpha | Less+Write |
| Line | BGRA8Unorm | Alpha | Less+Write |
| RT | RGBA32Float | Additive (ONE,ONE) | Disabled |
| Display | BGRA8Unorm | None | Disabled |

### Verification
- Build: `cmake --build build` — 100% success, zero errors
- Launch: App starts with "Using Metal graphics API", MSL compilation succeeds, all pipelines created, Metal raster renderer initializes

---

## 2026-02-08: Refactor to Backend-Based Platform Architecture

### Summary
Reorganized the rendering system from a flat structure into a backend-based architecture. Shared abstractions (Renderer base, Camera, RenderSettings) moved to `src/render/common/`, with each graphics backend in its own subfolder (`opengl/`, `vulkan/`, `metal/`). CMake now uses platform-conditional backend selection — currently OpenGL on all platforms, with scaffolding for future Vulkan (Windows/Linux) and Metal (macOS) backends. Created `src/platform/{macos,windows,linux}/` directories for future OS-specific glue code.

### Files Moved
| From | To |
|------|----|
| `src/render/Renderer.h` | `src/render/common/Renderer.h` |
| `src/render/RenderSettings.h` | `src/render/common/RenderSettings.h` |
| `src/render/Camera.h` | `src/render/common/Camera.h` |
| `src/render/Camera.cpp` | `src/render/common/Camera.cpp` |

### Files Modified
| File | Change |
|------|--------|
| `src/render/CMakeLists.txt` | Rewritten with `RENDER_COMMON_SOURCES`, `RENDER_OPENGL_SOURCES`, platform-conditional backend selection |
| `src/ui/CMakeLists.txt` | Restructured with `UI_SHARED_SOURCES` and `UI_BACKEND_SOURCES` groups |
| `src/render/opengl/OpenGLRenderer.h` | Include path: `../Renderer.h` → `../common/Renderer.h` |
| `src/render/opengl/RayTracingRenderer.h` | Include path: `../Renderer.h` → `../common/Renderer.h` |
| `src/render/opengl/OpenGLRenderer.cpp` | Include path: `../Camera.h` → `../common/Camera.h` |
| `src/render/opengl/RayTracingRenderer.cpp` | Include path: `../Camera.h` → `../common/Camera.h` |
| `src/render/opengl/SphereRenderer.cpp` | Include paths: `../Camera.h`, `../RenderSettings.h` → `../common/` |
| `src/render/opengl/BondRenderer.cpp` | Include paths: `../Camera.h`, `../RenderSettings.h` → `../common/` |
| `src/render/opengl/UnitCellRenderer.cpp` | Include paths: `../Camera.h`, `../RenderSettings.h` → `../common/` |
| `src/ui/components/OpenGLViewport.cpp` | Include path: `../../render/Camera.h` → `../../render/common/Camera.h` |
| `CLAUDE.md` | Updated architecture sections and added source tree diagram |

### Directories Created
| Directory | Purpose |
|-----------|---------|
| `src/render/common/` | Shared renderer abstractions |
| `src/render/vulkan/` | Future Vulkan backend |
| `src/render/metal/` | Future Metal backend |
| `src/platform/macos/` | Future macOS-specific code |
| `src/platform/windows/` | Future Windows-specific code |
| `src/platform/linux/` | Future Linux-specific code |

### Architecture Decisions

#### 1. Backend-Based, Not OS-Based
Organized by graphics backend (opengl/, vulkan/, metal/) rather than OS (macos/, windows/, linux/) because the mapping isn't 1:1 — OpenGL works on all three platforms, and a single OS may support multiple backends. This avoids code duplication.

#### 2. CMake Platform-Conditional Compilation
`src/render/CMakeLists.txt` defines source sets per backend (`RENDER_OPENGL_SOURCES`, `RENDER_VULKAN_SOURCES`, `RENDER_METAL_SOURCES`) and appends the appropriate set based on platform. Currently OpenGL is enabled everywhere; Vulkan/Metal sections are commented-out scaffolding.

#### 3. Dual Include Directories
`target_include_directories` exposes both `src/render/` and `src/render/common/`, so downstream code can use either `#include "Renderer.h"` (via common/) or `#include "opengl/OpenGLRenderer.h"` (via render/).

---

## 2026-02-08: RT Renderer Bug Fixes

### Summary
Fixed Retina display viewport sizing (RT image was quarter-size) and noisy AO (black pixels from AO applied to direct lighting). Increased shadow/AO ray origin bias to scale with atom radius. Shadow noise from binary hard shadows with a single light remains an open issue.

### Files Modified
| File | Change |
|------|--------|
| `src/ui/components/OpenGLViewport.cpp` | Use physical pixels (`logical * devicePixelRatio`) for renderer `resize()` |
| `src/render/opengl/RayTracingRenderer.cpp` | AO only on ambient term; radius-scaled ray origin bias |

### Open Bug
Noisy black points when shadows enabled — binary shadow (0/1) + single light + sub-pixel jitter creates high-contrast noise at shadow boundaries during early accumulation.

---

## 2026-02-07: Fix XYZ Axes Indicator to Rotate with Camera

### Summary
The XYZ axes indicator in the top-left of the viewport was static — it drew hard-coded axis directions that never updated when the camera rotated. Replaced it with a camera-aware gizmo that projects the world X, Y, Z axes through the camera's view matrix, with correct depth-sorted draw ordering and arrowhead tips.

### Files Modified
| File | Change |
|------|--------|
| `src/ui/components/OpenGLViewport.h` | Added `Q_INVOKABLE getAxisDirections()` method and `cameraChanged()` signal |
| `src/ui/components/OpenGLViewport.cpp` | Implemented `getAxisDirections()` (extracts view matrix rotation → 2D screen projections); emits `cameraChanged()` from orbit, pan, zoom, reset, and fitToView |
| `src/ui/qml/ViewportPanel.qml` | Rewrote axes Canvas to use camera-projected directions with depth sorting and arrowheads |

### Architecture Decisions

#### 1. View Matrix Projection for Axis Directions
Each world axis direction (1,0,0), (0,1,0), (0,0,1) is multiplied by the upper-left 3×3 of the view matrix. The x-component gives the screen-right direction, the negated y-component gives the screen-down direction (canvas Y is inverted vs view-space Y), and the z-component provides depth for draw ordering.

#### 2. Depth-Sorted Draw Ordering (Painter's Algorithm)
Axes are sorted ascending by their view-space z-component. Axes pointing into the screen (negative z) are drawn first; axes pointing toward the camera (positive z) are drawn last, appearing on top.

#### 3. Q_INVOKABLE + Signal Pattern
`getAxisDirections()` returns a flat `QVariantList` of 9 floats (3 axes × 3 components each) callable from QML. The `cameraChanged()` signal triggers `Canvas.requestPaint()` via a QML `Connections` element, keeping the gizmo in sync with every camera update.

#### 4. Arrowheads for Direction Clarity
Each axis line now has a small V-shaped arrowhead at its tip, computed from the normalized axis direction and its perpendicular, making the positive direction unambiguous.

---

## 2026-02-06: Fix Impostor Sphere Rendering for Large/Overlapping Atoms

### Summary
Fixed two rendering bugs in the impostor sphere shaders that caused visual artifacts when atom sizes were large or spheres overlapped. The billboard quads were too small under perspective projection, and the ray-sphere intersection used an orthographic approximation instead of proper perspective rays.

### Files Modified
| File | Change |
|------|--------|
| `src/render/opengl/ShaderManager.cpp` | Rewrote sphere vertex shader (perspective-correct billboard sizing) and fragment shader (perspective ray-sphere intersection) |
| `src/render/opengl/SphereRenderer.h` | Removed unused `viewportWidth`/`viewportHeight` params from `render()` |
| `src/render/opengl/SphereRenderer.cpp` | Updated `render()` signature; removed `uViewportSize` uniform set |
| `src/render/opengl/OpenGLRenderer.cpp` | Updated `render()` call site to match new signature |

### Architecture Decisions

#### 1. Perspective-Correct Billboard Sizing
Previously, the billboard quad was expanded by a fixed `1.2 * R` factor in view space. Under perspective, the projected silhouette of a sphere at distance `d` is `R * d / sqrt(d^2 - R^2)`, which exceeds `1.2R` when `R > 0.55d`. The vertex shader now computes this exact formula, with a 1.05x safety margin and a fallback for degenerate cases (camera inside sphere).

#### 2. Perspective Ray-Sphere Intersection
Previously, the fragment shader used an orthographic approximation: `d = quadCoord * R; z = sqrt(R^2 - d^2)`. This assumes all view rays are parallel to the z-axis, which is wrong under perspective. Now, each fragment traces a ray from the eye `(0,0,0)` through the interpolated view-space billboard position, and solves the standard quadratic `t^2 - 2t(D.C) + |C|^2 - R^2 = 0`. This gives correct normals, shapes, and depth at all overlap regions.

#### 3. Camera-Inside-Sphere Handling
When the front intersection `t` is negative (camera inside sphere), the shader falls back to the back intersection `t = b + sqrt(disc)`, rendering the interior correctly.

#### 4. Removed Unused `uViewportSize`
The viewport size uniform and corresponding render parameters were vestigial — neither the old nor new shader logic required them.

---

## 2026-02-06: Fix Atom Scale Slider and Use Original Covalent Radii

### Summary
Connected the Atom Scale sidebar slider to the rendering pipeline and removed all hard-coded 0.5x radius scaling so atoms render at their true covalent radii by default. Fixed broken QML cross-file id resolution that prevented all sidebar controls from reaching the viewport.

### Files Modified
| File | Change |
|------|--------|
| `src/data/Structure.cpp` | `addAtom()` uses `elem.covalentRadius` directly (was `* 0.5f`) |
| `src/python/ASEReader.cpp` | `updateRadiiFromElements(1.0f, ...)` in both `readFile()` and `readFromString()` (was `0.5f`) |
| `src/render/RenderSettings.h` | `atomScale` default changed to `1.0f` (was `0.5f`) |
| `src/render/opengl/ShaderManager.cpp` | Added `uniform float uAtomScale` to sphere vertex shader; radius = `aInstancePos.w * uAtomScale` |
| `src/render/opengl/SphereRenderer.cpp` | Sets `uAtomScale` uniform from `settings.atomScale` each frame |
| `src/ui/components/OpenGLViewport.h` | `m_atomScale` default changed to `1.0f` (was `0.5f`) |
| `src/ui/qml/Sidebar.qml` | Added `property var viewport: null`; all handlers use `sidebar.viewport` instead of broken `mainWindow.viewportPanel.viewport` chain; slider default `1.0` |
| `src/ui/qml/Main.qml` | Passes `viewport: viewportPanel.viewport` to Sidebar |

### Architecture Decisions

#### 1. No Hidden Scaling
Previously, atom radii were scaled to 50% in three places: `addAtom()`, `updateRadiiFromElements()`, and `RenderSettings::atomScale`. Now all defaults are 1.0 — the raw covalent radii from `ElementData` are used, and the user controls visual scale via the sidebar slider.

#### 2. GPU-side Atom Scale
The `uAtomScale` uniform is applied in the sphere vertex shader (`vRadius = aInstancePos.w * uAtomScale`), so changing the slider does not require re-uploading buffer data — only a uniform update per frame.

#### 3. Explicit Viewport Property for QML Wiring
The original QML code accessed the viewport via `mainWindow.viewportPanel.viewport` — a cross-file id chain that doesn't resolve in Qt 6's component scoping. Fixed by passing the viewport as an explicit property (`property var viewport`) from `Main.qml` to `Sidebar.qml`. This also fixed the Show Bonds checkbox and Reset Camera / Fit to View buttons.

---

## 2026-02-06: Add Unit Cell Wireframe Visualization

### Summary
Added unit cell visualization to the interactive OpenGL viewport. The unit cell is rendered as 12 wireframe edges of the parallelepiped defined by the lattice vectors, using simple `GL_LINES` — no lighting or ray tracing.

### Files Created
| File | Purpose |
|------|---------|
| `src/render/opengl/UnitCellRenderer.h` | Unit cell wireframe renderer interface |
| `src/render/opengl/UnitCellRenderer.cpp` | Computes 8 corners & 12 edges from lattice vectors, draws with GL_LINES |

### Files Modified
| File | Change |
|------|--------|
| `src/render/opengl/OpenGLRenderer.h` | Added `UnitCellRenderer` member, accessor, and dirty flag |
| `src/render/opengl/OpenGLRenderer.cpp` | Initialize, cleanup, data upload, and render call for unit cell |
| `src/render/CMakeLists.txt` | Added UnitCellRenderer source files |

### Architecture Decisions

#### 1. Simple Line Rendering
Unit cell edges are drawn with `GL_LINES` using the existing line shader (`uViewProjectionMatrix` only). No lighting, no instancing — just 8 vertices and 12 indexed line segments.

#### 2. Parallelepiped from Lattice Vectors
The 8 corners are computed from the 3 lattice vectors (a, b, c) stored in `Structure::Lattice::matrix[3][3]`:
- Origin, A, B, C, A+B, A+C, B+C, A+B+C

#### 3. Render Order
Unit cell lines are drawn first (before bonds and atoms) so they appear behind the structure with correct depth testing.

#### 4. Conditional Rendering
- Only rendered when `Structure::hasLattice()` is true (i.e. cell volume > 0)
- Toggled via `RenderSettings::showUnitCell` (default: true)
- Color and line width from `RenderSettings::unitCellColor` and `unitCellLineWidth`

---

## 2026-02-05: Replace Native File Readers with Python ASE Integration

### Summary
Replaced all C++ native file readers (XYZ, LAMMPS, CIF) with Python ASE integration using pybind11 embedded interpreter. ASE supports 70+ atomic structure file formats.

### Commits
- `181a115` - feat: replace native file readers with Python ASE integration

### Files Created
| File | Purpose |
|------|---------|
| `src/python/PythonRuntime.h` | Python interpreter lifecycle management (singleton) |
| `src/python/PythonRuntime.cpp` | Init/finalize, GIL guards, bundled Python detection |
| `src/python/ASEReader.h` | ASE-based file reader interface |
| `src/python/ASEReader.cpp` | Reads files via `ase.io.read()`, converts to Structure |
| `src/python/CMakeLists.txt` | Build config for python module |
| `src/data/Structure.h` | New Structure class with SoA layout |
| `src/data/Structure.cpp` | Structure implementation |
| `cmake/BundlePython.cmake` | Script to bundle Python + ASE into app |

### Files Deleted
- `src/io/readers/XYZReader.{h,cpp}`
- `src/io/readers/LAMMPSDumpReader.{h,cpp}`
- `src/io/readers/CIFReader.{h,cpp}`
- `src/data/AtomicStructure.{h,cpp}`
- `src/data/UnitCell.{h,cpp}`

### Architecture Decisions

#### 1. Python GIL Management
- Python initialized once at startup in `main.cpp`
- Main thread releases GIL after init (`PyEval_SaveThread`)
- Worker threads acquire GIL via RAII `GILGuard` class
- All pybind11 objects must be destroyed before releasing GIL

```cpp
// Pattern for worker threads
{
    PythonRuntime::GILGuard gil;
    py::module_ ase_io = py::module_::import("ase.io");
    // ... use Python objects ...
}  // GIL released here
```

#### 2. ASE Pre-loading
ASE is imported at startup (not on first file open) to avoid delay:
```cpp
// In PythonRuntime::initialize()
{
    GILGuard gil;
    py::module_ ase_io = py::module_::import("ase.io");  // Pre-load
}
```

#### 3. No Extension Filtering
ASE supports 70+ formats, so we don't filter by extension:
- `supportedExtensions()` returns `{"*"}` (wildcard)
- `canRead()` only checks if file exists
- Let ASE try to read any file; it reports errors for unsupported formats

#### 4. Bundled Python
For deployment, Python is bundled into the app:
- macOS: `Contents/Resources/python/lib/python3.X/`
- Windows: `python/Lib/`
- Linux: `python/lib/python3.X/`

Build with: `cmake --build build --target deploy`

### Technical Issues Resolved

#### Issue 1: PyGILState_STATE Type Conflict
**Problem**: Header defined `using PyGILState_STATE = int;` which conflicted with Python.h's enum.
**Solution**: Changed to `int m_state;` in GILGuard, cast at usage points.

#### Issue 2: pybind11 Implicit Conversion
**Problem**: `atoms.attr("positions")` returns accessor, not array.
**Solution**: Add explicit `.cast<py::array_t<double>>()`.

#### Issue 3: GIL Error on Exit
**Problem**: pybind11 objects destroyed without GIL held.
**Solution**: Add nested scope to ensure objects destroyed before `PyEval_SaveThread()`.

#### Issue 4: "No reader available for file" Error
**Problem**: Extension map only had "*" but lookup used actual extension.
**Solution**: Check `canRead()` first in `readerForPath()`, then fallback to extension lookup with wildcard support.

### Dependencies
- pybind11 (via vcpkg)
- Python 3.8+ with NumPy
- ASE (`pip install ase`)

### Build Commands
```bash
# Regular build (uses system Python)
cmake --preset default
cmake --build build

# Deploy with bundled Python (for distribution)
cmake --build build --target deploy
```

### Testing
Run the app and open various file formats:
```bash
./build/bin/atom-studio.app/Contents/MacOS/atom-studio
```

Supported formats include: XYZ, CIF, LAMMPS dump/data, VASP POSCAR/CONTCAR, PDB, Gaussian, Quantum ESPRESSO, ASE traj/json/db, and 60+ more.

---

## 2026-03-25: Fix Neighbor List Candidate Generation for Skewed Periodic Cells

### Summary
Fixed neighbor-list candidate generation for periodic skewed/triclinic unit cells. The previous PBC path used an approximate Cartesian bounding box of the cell and cell-index wrapping, which could miss valid near-neighbor candidates near periodic boundaries in skewed cells. The new implementation wraps atoms into a principal cell, replicates immediate lattice images in periodic directions, and performs Cartesian cell-list search over those real-space image positions. Bond detection semantics and `imageX/Y/Z` storage remain unchanged.

### Files Modified
| File | Purpose |
|------|---------|
| `src/data/NeighborList.cpp` | Reworked PBC candidate generation to use wrapped principal-cell atoms plus neighboring lattice-image replicas |
| `src/data/NeighborList.h` | Updated helper documentation to describe the new skew-cell-safe PBC search |
| `src/data/CMakeLists.txt` | Added data-layer neighbor-list regression test target |
| `CMakeLists.txt` | Enabled CTest integration |

### Files Created
| File | Purpose |
|------|---------|
| `src/data/tests/NeighborListTest.cpp` | Brute-force regression test for skewed full-PBC and partial-PBC neighbor/bond detection |

### Architecture Decisions

#### 1. Keep MIC/Bond Semantics, Replace Only Candidate Generation
- Left `NeighborList::applyMIC()` unchanged
- Left `BondList` image-shift meaning unchanged
- Changed only the PBC candidate search stage, since the bug was in candidate pruning rather than final distance evaluation

#### 2. Principal-Cell Wrapping Before Replication
- For each active atom, convert Cartesian position to fractional coordinates
- Wrap periodic fractional components into `[0, 1)`
- Convert that wrapped position back to Cartesian as the principal-cell representative
- Track the integer wrap offsets so recovered `imageX/Y/Z` values still refer to the original stored atom positions

#### 3. Search Over Real-Space Neighboring Lattice Images
- For periodic axes, generate image replicas with shifts in `{-1, 0, +1}`
- Bin those replica positions into a Cartesian cell list
- Query neighbors around each atom's principal-cell position
- Reconstruct the final image shift relative to the original stored atom position, then apply MIC once more for canonical nearest-image output

This avoids relying on the old assumption that a skewed periodic cell can be searched correctly by wrapping only Cartesian cell indices.

### Technical Issues Resolved

#### Issue 1: Missed Neighbors in Skewed PBC Cells
**Problem**: The old full-PBC path estimated a Cartesian bounding box from absolute lattice-matrix components and searched wrapped neighboring Cartesian cells. In skewed cells, atoms that are close through the lattice topology can land outside that local Cartesian-cell neighborhood and never become candidates.

**Solution**: Explicitly materialize nearby lattice images in real space, then run the cell-list search over those image positions. Candidate generation now follows the actual skewed cell geometry.

#### Issue 2: No Regression Coverage for NeighborList
**Problem**: There was no focused automated test for data-layer neighbor/bond detection, especially not for skewed periodic cells.

**Solution**: Added `atom-data-neighborlist-test`, which compares `NeighborList`/`buildBondList()` output against a brute-force MIC reference on skewed full-PBC and partial-PBC structures.

### Remaining Limitation
- `NeighborEntry` and `Bond` still store `imageX/Y/Z` as `int8_t`
- Extremely unwrapped input structures spanning more than 127 unit-cell crossings along one periodic axis can overflow that representation
- Normal wrapped inputs and typical mildly unwrapped inputs are unaffected

### Build Commands
```bash
cmake -S . -B build
cmake --build build --target atom-data-neighborlist-test
ctest --test-dir build --output-on-failure -R atom-data-neighborlist
```

### Testing
- Built `atom-data-neighborlist-test`
- Ran `ctest --test-dir build --output-on-failure -R atom-data-neighborlist`
- Result: passed

---

## 2026-03-25: Review and Correct Unwrap Molecules Logic

### Summary
Reviewed `unwrapMolecules()` in `src/data` and corrected several logic issues. The previous implementation centered each connected component by its geometric center, which did not satisfy the requirement to keep the largest wrapped fragment of a molecule inside the unit cell and reconnect the smaller fragments to it. The unwrap operation now:
- normalizes stored atom positions to wrapped fractional coordinates before graph traversal
- reconstructs bond-image offsets relative to those wrapped coordinates
- keeps the largest wrapped fragment in-cell
- reconnects the remaining wrapped fragments to that anchor
- skips only invalid/unknown elements rather than using the old organic-only whitelist
- detects inconsistent cyclic bond-image assignments and leaves those components unchanged

### Files Modified
| File | Purpose |
|------|---------|
| `src/data/StructureOperations.cpp` | Reworked unwrap logic: wrapped-coordinate normalization, consistent offset propagation, largest-fragment anchoring, widened element eligibility |
| `src/data/StructureOperations.h` | Updated unwrap documentation to match the new behavior |
| `src/data/CMakeLists.txt` | Added data-layer structure-operations regression test target |

### Files Created
| File | Purpose |
|------|---------|
| `src/data/tests/StructureOperationsTest.cpp` | Regression tests for unwrap anchoring, inconsistent bond-image cycles, and non-organic bonded fragments |

### Architecture Decisions

#### 1. Anchor by Largest Wrapped Fragment, Not Geometric Centre
- The old code translated each connected component so its geometric center landed in `[0, 1)^3`
- That can move the largest already-visible wrapped fragment out of the unit cell
- The new code identifies wrapped subfragments by removing cross-boundary edges (`dx/dy/dz != 0`) and keeps the largest zero-shift-connected subfragment in-cell

#### 2. Normalize Positions Before Using Bond Image Shifts
- Stored atom positions may already lie outside the principal cell
- Before offset propagation, each atom is converted to fractional coordinates and wrapped back into `[0, 1)` on periodic axes
- The integer wrapping removed from each atom is tracked and folded into the bond-image offset reconstruction
- This makes unwrap robust for mildly unwrapped inputs instead of assuming all atoms were already stored in a canonical wrapped image

#### 3. Unwrap All Valid Elements With Recorded VdW Radius
- Replaced the hardcoded "organic element" whitelist
- Any valid element with a positive recorded van der Waals radius is now eligible for unwrap traversal
- This broadens unwrap to bonded inorganic and metallic fragments without inventing a second element-classification system

#### 4. Detect Inconsistent Cyclic Bond-Image Data
- BFS still guarantees no dead or infinite loops via the `visited` set
- In addition, when a node is reached through multiple paths, the propagated offset is now checked for consistency
- If a connected component has contradictory bond-image assignments, that component is left unchanged rather than being unwrapped with a silently corrupted offset field

### Technical Issues Resolved

#### Issue 1: Wrong Anchor for Broken Wrapped Molecules
**Problem**: A molecule broken into multiple wrapped fragments could be re-centered around its overall centroid, which violates the desired behavior of keeping the largest wrapped fragment inside the unit cell.

**Solution**: Identify wrapped subfragments using only zero-shift adjacency edges and choose the largest one as the anchor offset for the final translation.

#### Issue 2: Organic-Only Restriction
**Problem**: Unwrap only processed a fixed organic-element whitelist, leaving bonded non-organic fragments untouched even when element data existed and bonds were available.

**Solution**: Replace the whitelist with an `ElementData`-based eligibility check using the recorded van der Waals radius.

#### Issue 3: Silent Offset Corruption in Inconsistent Cycles
**Problem**: Cyclic bond graphs with contradictory image shifts could assign incompatible offsets depending on traversal order.

**Solution**: Check every revisited node against the expected propagated offset; if any mismatch is found, mark the component inconsistent and skip modifying it.

### Build Commands
```bash
cmake -S . -B build
cmake --build build --target atom-data-structureops-test
ctest --test-dir build --output-on-failure -R atom-data-structureops
```

### Testing
- Built `atom-data-structureops-test`
- Ran `ctest --test-dir build --output-on-failure -R atom-data-structureops`
- Added coverage for:
  - keeping the largest wrapped fragment in-cell
  - leaving inconsistent cyclic bond-image components unchanged
  - unwrapping a bonded non-organic fragment whose elements have recorded VdW radii
- Result: passed

---
