# Renderer Performance Follow-up Notes

This document summarizes potential performance problems identified in the current renderer implementation, with exact code locations and recommended replacement logic.

## ~~1) CPU/GPU hard synchronization via `waitUntilCompleted()`~~ (FIXED)

**Resolved**: consolidated all RT sub-passes into a single command buffer per frame. The RT renderer previously submitted 2-3 separate command buffers per frame (reset + RT pass + display pass), each with `waitUntilCompleted`. Now `render()` creates one command buffer, passes it to `renderRTPass()` / `renderDisplayPass()` / `renderUnitCellOverlay()`, and commits+waits once at the end. Raster renderer (`MetalRenderer`) already used a single command buffer, unchanged.

Note: the final `waitUntilCompleted` before returning from `render()` is necessary because `updatePaintNode()` runs on the Qt render thread with the GUI thread blocked, and Qt's scene graph reads the output texture immediately after. A ring-buffer scheme would remove this last sync but is not warranted given the Qt synchronization model.

## ~~2) Accumulation reset performs a separate clear pass (and sync) on every reset~~ (FIXED)

**Resolved**: `resetAccumulation()` now just sets `m_sampleCount = 0` and `m_accumNeedsClear = true`. The next `renderRTPass()` uses `loadAction = MTLLoadActionClear` when the flag is set, then clears the flag. No separate command buffer or GPU sync.

## ~~3) Per-frame `QSGTexture` wrapper recreation in `MetalViewport`~~ (FIXED)

**Resolved**: `MetalViewport::updatePaintNode()` now uses a bounded cache of `QSGTexture` wrappers keyed by native texture handle + size + window. `QNativeInterface::QSGMetalTexture::fromNative(...)` is called only on cache miss (e.g., resize or output texture replacement), not every frame.

Additional lifecycle fixes:
- `QSGSimpleTextureNode` is configured with `setOwnsTexture(false)` so wrapper lifetime is not tied to per-frame node churn.
- `releaseResources()` now schedules render-thread cleanup for cached wrappers, preventing leaks when the scene graph tears down.

## ~~4) Unit-cell overlay occlusion still uses brute-force atom loop~~ (FIXED)

**Resolved**: `rt_unit_cell_fragment` now uses BVH `traceAnyHit(...)` instead of a linear atom loop. Overlay passes bind BVH buffers (`nodeMin`, `nodeMax`, `nodeMeta`, `primIndices`) and pass `bvhNodeCount` through `RTUnitCellUniforms`, preserving the same discard rule (hide unit-cell fragment only if an atom is hit before `maxT`).

## ~~5) Duplicated `computeStateHash` across OpenGL and Metal RT renderers~~ (FIXED)

**Resolved**: extracted a shared `computeRenderStateHash(const Camera&, const RenderSettings&)` helper into `src/render/common/RenderStateHash.h/.cpp`, and switched both RT backends to use it. Backend-local `computeStateHash()` methods were removed from:
- `src/render/opengl/RayTracingRenderer.*`
- `src/render/metal/MetalRayTracingRenderer.*`

This removes cross-backend drift risk when adding settings that should trigger accumulation reset.

## ~~6) Duplicated unit-cell overlay rendering code in Metal RT renderer~~ (FIXED)

**Resolved**: extracted shared unit-cell geometry/data/uniform logic into `src/render/metal/MetalUnitCellShared.h/.cpp`, including:
- unit cylinder mesh builder
- unit sphere mesh builder
- lattice -> edge/joint instance packing
- shared unit-cell style packing
- shared RT unit-cell uniform packing

`MetalUnitCellRenderer` and `MetalRayTracingRenderer` now both consume these helpers. In addition, RT overlay draw setup was deduplicated by routing both overlay call sites through `MetalRayTracingRenderer::encodeUnitCellOverlayDraws(...)`.

## 7) Dead empty-buffer guard checks in BVH upload code

- Potential problem:
  - Minor code-quality issue. Upload paths check `if (bvhData.nodes.empty())` and bail, but `buildSphereBVH` always produces at least one root node when `atomCount > 0`, making this branch dead code.
- Problematic code location:
  - `src/render/opengl/RayTracingRenderer.cpp` — `uploadAtomData()`, empty-node guard
  - `src/render/metal/MetalRayTracingRenderer.mm` — `uploadAtomData()`, empty-node guard
- Corresponding fix:
  - Remove the dead branches, or replace with an assertion (`assert(!bvhData.nodes.empty())`) to catch unexpected conditions during development.

## 8) Unused `#include <cstring>` in BVH header

- Potential problem:
  - Trivial. `src/render/common/BVH.h` includes `<cstring>` but no `memcpy`/`memset`/`strcmp` etc. is used.
- Corresponding fix:
  - Remove the include.

---

## Recommended implementation order

1. ~~Non-blocking command submission + completed-texture handoff.~~ (DONE)
2. ~~Deferred accumulation clear (`clear-on-next-RT-pass`).~~ (DONE)
3. ~~`QSGTexture` wrapper cache/reuse in `MetalViewport`.~~ (DONE)
4. ~~BVH traversal for unit-cell overlay occlusion path.~~ (DONE)
5. ~~Extract shared `computeStateHash` to common code.~~ (DONE)
6. ~~Extract shared unit-cell overlay code.~~ (DONE)
7. Remove dead empty-buffer guards (trivial).
8. Remove unused `#include <cstring>` (trivial).
