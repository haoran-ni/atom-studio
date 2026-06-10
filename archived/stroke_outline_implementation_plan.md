# Stroke Outline Implementation Plan

## Goal

Add dark, solid, sharp stroke outlines to atoms and bonds in both the Metal raster
renderer and the Metal ray-tracing renderer.

Target effect:
- Outlines appear along visible outer silhouettes and at occluding boundaries
  (where one object passes in front of another).
- **No** stroke where a bond meets the front-facing visible surface of an atom —
  the contact must stay visually continuous, as if the bond emerges from the
  sphere surface.
- No strokes at object–object contact boundaries in general; only where forms
  turn away, overlap with a real depth gap, or are externally exposed.
- Constant screen-space stroke width (a sphere's outline is a geometrically
  perfect circle at any distance — no screen-space edge-detection artifacts).
- Solid color, hard edge (anti-aliased by MSAA / RT accumulation only, no fades).

## Method: Inverted Hull (Shell) Outlines

For every atom and bond, also render a slightly inflated copy of the same shape
and show only its **far/inner surface**. Ordinary depth testing then produces
exactly the target behavior with no special cases:

- The shell's back wall is everywhere behind the real object's front surface, so
  the object always wins inside its own silhouette.
- Just outside the silhouette, the shell's back wall is visible → an exact,
  constant-width ring.
- The ring sits at (approximately) the object's edge depth, so closer objects
  occlude it and it draws over farther objects — outlines appear at occlusion
  boundaries automatically.
- Where a bond emerges from a sphere's front face, the bond's inflated shell is
  still *inside* the sphere → depth-occluded → no stroke at the contact. The
  stroke starts only once the bond stands proud of the sphere by more than the
  shell thickness.

Inflation width is computed per object in **world units from the desired pixel
width**: `w_world = outlineWidthPx * pixelScale * viewDepth` (perspective) or
`w_world = outlineWidthPx * pixelScale` (orthographic), where
`pixelScale = 2 / (P[1][1] * viewportHeightPx)` works for both projection types
(`P[1][1] = 1/tan(fovY/2)` perspective, `2/orthoHeight` orthographic).

### Raster renderer mapping

- **Atoms (impostor spheres)** — no second draw call. Extend the existing
  sphere impostor shader:
  - Vertex: compute view-space shell width `w`; size the billboard to cover
    radius `R + w`; pass the outer radius to the fragment stage.
  - Fragment: intersect the **outer** sphere (`R + w`); miss → discard. If the
    **inner** sphere (`R`) is hit → existing shading path, unchanged. Otherwise
    output the outline color with depth taken from the **far** intersection of
    the outer sphere (this *is* the inverted hull, analytically).
- **Bonds (instanced mesh cylinders)** — classic two-draw inverted hull:
  - New `bond_outline_vertex` / `bond_outline_fragment` MSL functions and a
    `bondOutlinePipeline` (same color/depth formats as the bond pipeline).
  - Vertex inflation maps the unit cylinder (radius 1, z ∈ [0,1]) to a dilated
    solid: radial scale `bondRadius + w`, axial extension
    `z * (bondLength + 2w) - w`. This keeps the hull closed (no normal-offset
    gaps at cap rims since the mesh duplicates rim vertices with different
    normals).
  - Encode with `MTLCullModeFront` so only inner/back faces are visible;
    standard depth test + write; fragment outputs flat outline color with the
    bond's per-side alpha (so hidden/transparent bonds don't leave strokes).
  - Skipped entirely when outlines are disabled or width is 0.
- Unit cell, gizmos, and viewport axes get **no** outlines (informational
  overlays).

### Ray-tracing renderer mapping

Per-primitive shell logic inside `traceClosest` (used only by primary rays —
shadow/AO rays are intentionally unaffected):

- For each leaf primitive, first test the real primitive (near hit) as today.
- If the real primitive is **missed** and outlines are enabled, test the
  inflated primitive (sphere radius `r + w`; capped cylinder radius `+w` and
  endpoints extended by `w` along the axis) and use its **exit** (far)
  intersection `t` as an outline candidate.
- All real hits and outline candidates compete in the same closest-hit
  selection. An outline winner shades as flat outline color (no lighting,
  shadow, or AO), composited with the background using the primitive's alpha.
- `w` per primitive: `w = outlineScale * distance(camera, primitive center)` in
  perspective (`outlineScale = widthPx * pixelScale`), constant in ortho. The
  distance approximation via primitive center is exact enough since `w` is a
  few pixels.
- **BVH correctness**: node AABB tests get an extra conservative expansion
  `outlineWorldMax`, computed on the CPU per frame as
  `outlineScale * (distance from camera to farthest scene-bounds corner)`
  (perspective) or `outlineScale` (ortho). Scene bounds are recorded during
  `uploadSceneData`. Occlusion/any-hit traversals pass 0 (no outline there).
- Outline edges are anti-aliased naturally by progressive accumulation jitter,
  same as object edges — stroke interiors stay solid.

## Settings & Wiring

New fields in `RenderSettings` (shared, `src/render/common/RenderSettings.h`):

```cpp
bool   outlineEnabled = true;
float  outlineWidth   = 2.0f;              // stroke width in rendered pixels
QColor outlineColor   = QColor(0, 0, 0);   // dark solid stroke
```

- `RenderStateHash.cpp`: hash all three (toggling/resizing resets RT
  accumulation).
- `MetalTypes.h` + MSL struct mirrors:
  - `SceneUniforms` += `outlineColor` (float4), `outlineWidthPx`,
    `outlinePixelScale` (+ explicit padding; CPU and MSL layouts must match).
  - `RTUniforms` += `outlineScale`, `outlineColor` (float4),
    `outlineWorldMax` (+ explicit padding).
- `MetalRenderer.mm` / `MetalRayTracingRenderer.mm`: fill the new uniform
  fields from settings + camera (`pixelScale` from `projectionMatrix()(1,1)`
  and the render-target height). Width of 0 / disabled ⇒ all outline code paths
  short-circuit.
- `MetalViewport` (`src/ui/components/MetalViewport.{h,mm}`): new Q_PROPERTYs
  `outlineEnabled`, `outlineWidth`, `outlineColor` following the existing
  setter pattern; `outlineWidth` is specified in logical pixels in QML and
  multiplied by the device pixel ratio when building `RenderSettings` (same
  convention as the viewport axes overlay).
- `OpenGLViewport`: same three Q_PROPERTYs plumbed into its `RenderSettings`
  for QML interface parity (the OpenGL renderers ignore them for now — OpenGL
  backend rendering is an explicit follow-up, out of scope here).
- `Sidebar.qml`: an "Outline" parameter group using existing reusable controls
  (`SidebarCheckBox` for enable, `NumericSliderControl` for width 0.5–6 px,
  `RGBColorPicker` for color), wired via `sidebar.viewport.propertyName`,
  following the sidebar style rules (branch rows / connectors for tunable
  parameter blocks).

## File-by-File Change List

| File | Change |
|---|---|
| `src/render/common/RenderSettings.h` | Add `outlineEnabled`, `outlineWidth`, `outlineColor` |
| `src/render/common/RenderStateHash.cpp` | Hash the new fields |
| `src/render/metal/MetalTypes.h` | Extend `SceneUniforms`, `RTUniforms` |
| `src/render/metal/MetalShaderLibrary.mm` | MSL struct mirrors; sphere impostor shell logic; `bond_outline_*` shaders; RT shell intersection helpers + `traceClosest`/`testNodeAABB`/`rt_fragment` changes; new `bondOutlinePipeline` |
| `src/render/metal/MetalShaderLibrary.h` | `bondOutlinePipeline()` accessor |
| `src/render/metal/MetalBondRenderer.mm` | Second instanced draw with front culling when outlines active |
| `src/render/metal/MetalRenderer.mm` | Fill outline uniform fields |
| `src/render/metal/MetalRayTracingRenderer.{h,mm}` | Fill RT outline uniforms; track scene bounds for `outlineWorldMax` |
| `src/ui/components/MetalViewport.{h,mm}` | Q_PROPERTY wiring + settings plumbing (dpr-scaled width) |
| `src/ui/components/OpenGLViewport.{h,cpp}` | Q_PROPERTY parity (no-op rendering) |
| `src/ui/qml/Sidebar.qml` | Outline controls group |

## Edge Cases & Behavior Notes

- **Interpenetrating atoms** (e.g. overlapping spheres at a bond): near the
  intersection seam the shell's far surface lies behind the neighbor's front
  surface → no stroke at the contact seam; strokes appear only where the depth
  gap exceeds the shell thickness. This matches the "no strokes at contact
  boundaries" requirement by construction.
- **Hidden/transparent objects**: outline fragments inherit the primitive's
  alpha and discard when alpha ≈ 0, so per-atom/bond visibility is respected.
- **`showAtoms`/`showBonds` toggles**: outlines are part of each object's
  renderer, so they follow visibility automatically.
- **Bond end caps** sit inside atom spheres; their inflated caps stay inside
  (shell width ≪ atom radius), so no stray strokes at bond ends.
- **RT unit-cell overlay occlusion** ignores shells (≤ width-px discrepancy
  where a stroke overlaps a unit-cell line — acceptable).

## Verification

1. `cmake --build build` — clean build.
2. Run the app, load a structure, check in **raster** mode:
   silhouette rings are perfect circles at all zoom levels; bonds outlined on
   sides; no stroke where bonds meet front faces of atoms; constant pixel width
   near/far; toggles and sliders respond.
3. Switch to **RT** mode: same appearance; accumulation resets on outline
   setting changes; shadows/AO unaffected by shells.
4. Toggle perspective/orthographic — outline width stays constant in pixels.
