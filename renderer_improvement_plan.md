# Renderer Improvement Plan

Last updated: 2026-03-07

## Purpose

This file tracks the ray tracing renderer problems and improvement opportunities
identified during code review. It is intended to be the working checklist for
future implementation work across all renderer backends.

Current backend status:

- OpenGL ray tracing exists and is the fallback path on all platforms.
- Metal ray tracing exists and is the current macOS path.
- Vulkan ray tracing is not implemented yet; the current Vulkan viewport is a placeholder only.

## Implementation Progress

| ID | Status | Summary |
| --- | --- | --- |
| RT-001 | Fixed | Remove Metal per-frame GPU blocking. |
| RT-002 | Not fixed | Move scene packing and BVH building off the render thread. |
| RT-003 | Not fixed | Split dirty tracking so bond-only updates do not repack atom data. |
| RT-004 | Not fixed | Avoid traversing hidden bonds when `showBonds` is disabled. |
| RT-005 | Not fixed | Keep bond BVH bounds correct when bond radius changes. |
| RT-006 | Not fixed | Add OpenGL large-scene guards and fallback for TBO limits. |
| RT-007 | Not fixed | Replace hardcoded shadow ray distance with a scene-aware limit. |
| RT-008 | Not fixed | Implement a real Vulkan ray tracing backend or explicitly disable the placeholder path. |
| RT-009 | Not fixed | Reduce bond data duplication in GPU scene buffers. |
| RT-010 | Not fixed | Improve BVH build quality and scalability for large scenes. |

Status meanings:

- `Not fixed`: no implementation work has been completed yet.
- `Partial`: some infrastructure exists, but the item is not complete.
- `Fixed`: implemented and verified.

## Work Items

### RT-001: Metal ray tracing blocks every frame

Problem:

- The Metal ray tracing renderer calls `waitUntilCompleted()` on the command
  buffer every frame.
- This forces the CPU to wait for the GPU, removes overlap between scene
  submission and execution, and will reduce frame rate as scene cost grows.

Why it matters:

- This is the highest-impact performance problem in the current Metal RT path.
- It makes progressive accumulation less efficient because every sample is paid
  with a full CPU stall.

Proposed solution:

- Remove per-frame blocking waits from the normal render path.
- Use double-buffered or ring-buffered output textures so Qt can display the
  previous completed frame while the GPU renders the next one.
- Synchronize texture ownership with completion handlers, fences, or shared
  events instead of blocking the render thread.

Suggested implementation notes:

- Keep `waitUntilCompleted()` only for debugging or teardown if strictly needed.
- Ensure texture cache logic in `MetalViewport` can safely handle frame-latency.
- Add profiling before and after the change to confirm CPU/GPU overlap.

Relevant files:

- `src/render/metal/MetalRayTracingRenderer.mm`
- `src/ui/components/MetalViewport.mm`

Current status:

- Fixed.

Verification notes:

- Implemented asynchronous Metal RT submission with buffered output textures.
- Removed normal-path per-frame `waitUntilCompleted()` calls from the Metal RT renderer.
- Updated the Metal viewport to keep scheduling frames until a completed RT output is ready.
- Direct `clang++ -fsyntax-only` checks passed for:
  - `src/render/metal/MetalRayTracingRenderer.mm`
  - `src/ui/components/MetalViewport.mm`
- Full renderer build verification now also passed:
  - `cmake --build build --target atom-render -j4`

### RT-002: Scene packing and BVH building run on the render thread

Problem:

- Atom packing, bond endpoint packing, and BVH construction are performed
  synchronously during `render()` after dirty flags are detected.
- Large structures or bond recomputes will therefore stall rendering.

Why it matters:

- This creates visible hitches when loading files, recomputing bonds, or
  updating structures.
- Both OpenGL and Metal ray tracing backends currently pay this cost.

Proposed solution:

- Introduce an immutable "RT scene snapshot" that contains packed atom data,
  packed bond data, and BVH data.
- Build that snapshot off the render thread.
- Upload or swap the finished snapshot on the render thread once ready.

Suggested implementation notes:

- Build CPU-side arrays in a worker thread.
- Keep render-thread work limited to resource creation and pointer swaps.
- Make scene snapshots versioned so stale worker results can be dropped safely.

Relevant files:

- `src/render/opengl/RayTracingRenderer.cpp`
- `src/render/metal/MetalRayTracingRenderer.mm`
- `src/render/common/BVH.cpp`

Current status:

- Not fixed.

### RT-003: Dirty tracking is too coarse

Problem:

- `setStructure()` marks both atom and bond data dirty even when only bonds
  changed.
- After asynchronous bond detection completes, the viewport replaces the bond
  list and pushes a full structure update, which causes unnecessary atom
  repacking and BVH rebuild work.

Why it matters:

- Bond-only updates are common after loading a structure.
- Repacking static atom data increases CPU time and delays first usable frames.

Proposed solution:

- Split invalidation into at least:
  - atom geometry/colors changed
  - bond topology/endpoints changed
  - BVH rebuild required
  - accumulation reset required
- Add explicit bond-only update paths from the viewport to the RT renderers.

Suggested implementation notes:

- Consider adding a renderer method for bond list updates or a finer-grained
  scene update API.
- Atom pack buffers should be reused when only bonds change.

Relevant files:

- `src/render/opengl/RayTracingRenderer.h`
- `src/render/opengl/RayTracingRenderer.cpp`
- `src/render/metal/MetalRayTracingRenderer.h`
- `src/render/metal/MetalRayTracingRenderer.mm`
- `src/ui/components/OpenGLViewport.cpp`
- `src/ui/components/MetalViewport.mm`

Current status:

- Not fixed.

### RT-004: Hidden bonds still cost BVH traversal time

Problem:

- The unified BVH contains both atoms and bonds.
- When `showBonds` is disabled, the shaders skip bond intersection tests, but
  they still traverse bond-heavy BVH nodes and iterate leaf primitive ranges.

Why it matters:

- Bond-rich structures pay unnecessary traversal cost even when bonds are not
  visible.
- This affects primary rays, shadow rays, and AO rays.

Proposed solution:

- Split acceleration structures into:
  - atom-only BVH
  - bond BVH, built only when needed
- When bonds are hidden, trace only against the atom BVH.

Alternative solution:

- Keep a unified BVH only if a shader-side or CPU-side filtering scheme can
  cheaply skip bond-only regions. This is less attractive than separate BVHs.

Relevant files:

- `src/render/opengl/RayTracingRenderer.cpp`
- `src/render/metal/MetalShaderLibrary.mm`
- `src/render/common/BVH.cpp`

Current status:

- Not fixed.

### RT-005: Bond radius changes are not tied cleanly to BVH invalidation

Problem:

- Bond AABBs are built using the current bond radius during scene upload.
- Bond radius changes currently reset accumulation through render-state hashing,
  but they do not explicitly trigger a BVH rebuild.
- If bond thickness becomes user-adjustable in practice, BVH bounds can become
  stale or more conservative than necessary.

Why it matters:

- This is a correctness risk if bond radius is changed without a scene upload.
- It also prevents a clean separation between visual state changes and geometry
  acceleration state.

Proposed solution:

- Either:
  - include bond radius in BVH invalidation and rebuild bond bounds when it changes, or
  - store bond radius expansion data similarly to atom radius expansion and
    apply it during traversal.

Recommended direction:

- If separate bond BVHs are introduced, rebuild or refit only the bond BVH when
  bond radius changes.

Relevant files:

- `src/render/common/RenderStateHash.cpp`
- `src/render/opengl/RayTracingRenderer.cpp`
- `src/render/metal/MetalRayTracingRenderer.mm`

Current status:

- Not fixed.

### RT-006: OpenGL ray tracing does not guard against TBO size limits

Problem:

- The OpenGL RT path queries `GL_MAX_TEXTURE_BUFFER_SIZE` and logs it, but it
  does not validate uploads against that limit.
- Large scenes can exceed hardware limits for atom data, bond data, or BVH data.

Why it matters:

- Very large structures may fail unpredictably on different GPUs.
- This is a scalability problem for the current Windows/Linux fallback path.

Proposed solution:

- Add explicit size checks before allocating and binding TBO-backed buffers.
- If a scene exceeds limits, switch to a supported fallback path instead of
  proceeding blindly.

Possible fallback strategies:

- Chunked 2D textures instead of TBOs.
- Multi-buffer paging.
- A temporary "RT unavailable for this scene size" guard with a clear warning,
  if no proper fallback is ready yet.

Relevant files:

- `src/render/opengl/RayTracingRenderer.cpp`

Current status:

- Not fixed.

### RT-007: Shadow rays use a hardcoded max distance

Problem:

- Shadow rays use a fixed `10000.0` distance limit.
- Large simulation cells or wide camera ranges can exceed this distance.

Why it matters:

- Occluders outside the hardcoded range are ignored, which can cause incorrect
  lighting on large structures.

Proposed solution:

- Replace the fixed value with a scene-aware distance derived from the current
  structure bounds, unit cell extents, or camera/light configuration.
- Keep a small safety margin to avoid clipping legitimate blockers.

Suggested implementation notes:

- Compute a conservative scene radius once per scene snapshot.
- Pass the derived shadow distance as part of RT uniforms.

Relevant files:

- `src/render/opengl/RayTracingRenderer.cpp`
- `src/render/metal/MetalShaderLibrary.mm`
- `src/render/common/Camera.cpp`
- `src/data/Structure.cpp`

Current status:

- Not fixed.

### RT-008: Vulkan ray tracing backend is still missing

Problem:

- There is no Vulkan renderer implementation yet.
- The Vulkan viewport is only a placeholder and does not provide real rendering.

Why it matters:

- Windows and Linux currently depend on the OpenGL fallback path.
- Planned platform strategy says Vulkan should become the primary backend there,
  but the current codebase has no implementation.

Proposed solution:

- Implement a real Vulkan renderer path with the same scene abstraction used by
  OpenGL and Metal.
- Reuse shared CPU scene packing and BVH infrastructure where possible.

Minimum acceptable interim step:

- If Vulkan is not going to be implemented soon, explicitly disable the
  placeholder UI path so users are not presented with a non-functional backend.

Relevant files:

- `src/render/CMakeLists.txt`
- `src/ui/components/VulkanViewport.h`
- `src/ui/components/VulkanViewport.cpp`

Current status:

- Not fixed.

### RT-009: Bond data is duplicated more than necessary

Problem:

- Current RT scene upload duplicates bond start positions, end positions, and
  averaged colors into dedicated GPU buffers.
- This increases CPU packing time, GPU memory use, and upload bandwidth.

Why it matters:

- Bond-heavy structures will scale poorly compared with atom-only scenes.
- The cost is paid in both OpenGL and Metal implementations.

Proposed solution:

- Store compact bond topology data instead:
  - atom index 1
  - atom index 2
  - periodic image offset
  - optional bond style data
- Reconstruct bond endpoints and derived bond colors in the shader from atom
  position and color buffers.

Tradeoff:

- This increases shader work slightly, but should reduce total CPU and memory
  cost enough to be worthwhile for large scenes.

Relevant files:

- `src/render/opengl/RayTracingRenderer.cpp`
- `src/render/metal/MetalRayTracingRenderer.mm`
- `src/data/BondList.h`

Current status:

- Not fixed.

### RT-010: BVH builder quality and scalability can be improved

Problem:

- The current BVH builder uses a simple median split on the largest centroid
  extent with a fixed leaf size.
- This is straightforward and acceptable for now, but it is not ideal for very
  large or unevenly distributed structures.

Why it matters:

- Better BVHs reduce the cost of primary rays, shadow rays, and AO rays.
- Build cost also becomes important as structure sizes grow.

Proposed solution:

- Improve the BVH builder in stages:
  1. make leaf size configurable per backend and scene size
  2. parallelize upper-level build steps
  3. evaluate a better split heuristic such as SAH or binned SAH
  4. support refit for geometry changes when topology is unchanged

Recommended direction:

- Do not overcomplicate the first step. Start with configurable leaf size and
  simple benchmarking before adding a more expensive heuristic.

Relevant files:

- `src/render/common/BVH.h`
- `src/render/common/BVH.cpp`

Current status:

- Not fixed.

## Suggested Order Of Work

Recommended implementation order:

1. RT-001: remove Metal blocking waits
2. RT-002: move scene packing and BVH building off the render thread
3. RT-003: split dirty tracking
4. RT-004: split atom and bond acceleration structures
5. RT-006: add OpenGL large-scene guards
6. RT-005: clean bond-radius invalidation or runtime expansion
7. RT-007: replace hardcoded shadow distance
8. RT-009: reduce bond data duplication
9. RT-010: improve BVH build quality and scalability
10. RT-008: implement or explicitly disable Vulkan placeholder path

## Update Instructions

When an item changes, update:

- the `Implementation Progress` table
- the `Current status` field inside the relevant work item
- any notes about accepted design decisions or rejected alternatives
