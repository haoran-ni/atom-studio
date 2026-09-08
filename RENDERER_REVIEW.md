# macOS renderer review

Reviewed 2026-09-07 at revision `80ebe31`; implementation completed 2026-09-08 in the working tree. The findings below describe the original revision. Baseline line labels are historical; links open the current files. Implementation results and measured limits follow at the end.

The largest opportunities are to stop unnecessary ray-tracing submissions after convergence, merge accumulation draws within each existing sample batch, reduce render-target memory, and remove repeated geometry preparation from interaction paths. These changes can retain current resolution, samples, shading, outlines, selection, and export behavior. Several correctness bugs were also reproduced on an Apple M2 Pro.

The detailed review covers the implemented Metal raster and ray-tracing renderers, embedded MSL shaders and pipeline creation, sphere/bond/unit-cell/gizmo/axes rendering, output-slot bookkeeping, Metal viewport scheduling, shared BVH construction, camera state, picking, and render-data packing. OpenGL was inspected for shared behavior and fallback differences. The current macOS entry point explicitly selects Metal; OpenGL-specific performance work is lower priority. Unimplemented Vulkan paths, future hardware ray tracing, and Windows behavior are outside this review.

**Evidence and limits**

- Rebuilt the four existing renderer test targets. Camera and render-state-hash tests passed.
- The restricted test run skipped both GPU tests because it could not access a graphics device. Running the Metal background test with native GPU access passed.
- Running that same Metal test with `MTL_DEBUG_LAYER=1` aborted on a depth-attachment/pipeline format mismatch. A separate bond-free scene aborted on missing buffer bindings.
- A temporary probe linked against the current renderer library reproduced unnecessary converged submissions, stale atom visibility, incorrect empty-scene alpha, incorrect bond occlusion of the unit cell, and clipped off-axis raster spheres.
- The CPU probe compiled Camera, Picking, BondRenderData, and BVH with `-O3 -DNDEBUG`, linking the existing data library. Its measurements are isolated baselines, not end-to-end Release application benchmarks or claimed optimization speedups.
- GPU checks used the existing Debug renderer library on an Apple M2 Pro. Concurrency findings below come from code interleavings, not a reproduced GPU corruption event. No full-window Instruments capture was performed.

| Probe | Observed result |
| --- | --- |
| Unchanged RT scene after reaching 4 samples | 12/12 further calls produced a different completed output texture; 12/12 immediately reported more scheduled work; sample count stayed at 4 |
| Toggle `showAtoms` after convergence | Center remained `#7b7b7b`; explicitly invalidating restored the expected background `#143264` |
| Empty RT scene, axes enabled, transparent background | Background alpha was 255 instead of 0 |
| Per-bond radius 0.5; change only global fallback radius 0.1 → 0.5 | 431 pixels changed in unit-cell occlusion although the rendered bond geometry was unchanged |
| Raster sphere: radius 2, center (8, 0, 0), camera distance 10, FOV 120°, 256² output | 62 of 824 analytic interior pixels were missing; the comparison excluded the silhouette fringe |
| 100,000 atoms / 99,999 bonds | Median pick 1.46 ms; temporary bond segment array 6.4 MB; sphere BVH build 6.93 ms |
| 500,000 atoms / 499,999 bonds | Median pick 6.28 ms; temporary bond segment array 32.0 MB; sphere BVH build 33.44 ms |
| 500,000-sphere BVH node vector | 6.29 MB used, 48.0 MB reserved; parallel subtree vectors add temporary capacity during construction |

Reproduction sources: [Metal probe](/tmp/atom_renderer_review_probe.mm), [CPU probe](/tmp/atom_renderer_cpu_probe.cpp). These temporary files are outside application source. Images: [clipped raster sphere](/tmp/atom_review_off_axis.png), [unit cell with mismatched fallback radius](/tmp/atom_review_cell_thin.png), [same geometry with matching fallback radius](/tmp/atom_review_cell_thick.png).

**Performance findings, in proposed order**

1. **High priority — stop converged ray tracing from sustaining its own display loop. Reproduced.**

   [MetalRayTracingRenderer.mm:326](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalRayTracingRenderer.mm) skips accumulation after convergence but always encodes and commits a display pass. [MetalViewport.mm:875](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/ui/components/MetalViewport.mm) calls it before inspecting whether more work remains. The newly submitted display frame makes `needsMoreFrames()` true and schedules another update. If completion happens sufficiently quickly the loop can stop by timing accident; there is no explicit idle gate. Empty scenes take a similar submission path.

   Track accumulation changes separately from display changes. Submit only for unfinished accumulation, a changed background/overlay, a pending scene update, a resize, or an explicit frame request. Consume the last completed frame without automatically submitting another. Make the final unpresented completion a reason to wake up, not to render again. Preserve background edits and transparent export without resetting accumulated samples.

   Validate with a real viewport: submission count and wakeups should settle after convergence, while axes movement, background changes, resize, export tokens, and increased sample limits still refresh correctly.

2. **High priority — use one accumulation encoder per existing RT batch. Code-backed optimization; speedup unmeasured.**

   [MetalRayTracingRenderer.mm:349](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalRayTracingRenderer.mm) loops over samples, but each [renderRTPass call](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalRayTracingRenderer.mm) rebuilds uniforms, inverts matrices, binds the same resources, and opens/closes a render encoder. The RGBA32Float attachment is stored and loaded between samples. A batch can contain up to 256 such passes.

   Prepare invariant uniforms once and issue the existing per-sample draws in a single encoder, changing only sample-dependent values. Clear/load once and store once per batch. Keep the same sample indices, RNG sequence, additive blend order, precision, and first-sample interaction policy. This targets CPU encoding and attachment traffic without reducing rendering quality. Apple specifically recommends merging compatible passes on Apple silicon to avoid tile-memory round trips. [Apple render-pass guidance](https://developer.apple.com/videos/play/wwdc2023/10125/?time=482).

   Compare fixed-seed accumulated foreground RGB and coverage at identical sample counts, including AO/shadows, transparent backgrounds, and interrupted batches. Measure CPU encode time and GPU time separately before changing the adaptive batching budget.

3. **High priority — prepare geometry/BVH outside the blocked GUI/render synchronization phase, and reuse it across mode changes. Measured baseline.**

   [MetalViewport.mm:742](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/ui/components/MetalViewport.mm) runs with the GUI thread blocked. It calls `render()`, whose dirty path packs the full scene and [builds the BVH synchronously](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalRayTracingRenderer.mm). BVH subtree construction is already parallel; the caller still waits for all workers. Switching back into RT also [unconditionally calls setStructure](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/ui/components/MetalViewport.mm), repeating preparation even if geometry has not changed.

   Introduce geometry revisions and prepare immutable render data in a worker. Publish only the latest revision and keep a valid previous frame while preparation runs. Avoid passing a mutable Structure to background packing. Cache prepared geometry across renderer switches. Radius-only changes can refit bounds while retaining topology; topology changes still require rebuilding.

   The isolated 500,000-sphere BVH build took 33.44 ms before bond packing or GPU upload. Measure complete load/edit/mode-switch latency, and test rapid loading, deletion, replication, radius edits, and mode switching while an older preparation task is pending.

4. **High priority — reduce duplicated full-size render targets. Code-backed allocation opportunity.**

   [Raster target creation](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalRenderer.mm) allocates MSAA color and depth for all three output slots. [RT target creation](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalRayTracingRenderer.mm) likewise allocates per-slot MSAA color, MSAA depth, and single-sample depth, even when overlays are disabled. Both renderer instances remain alive after switching modes.

   At 4× MSAA the nominal texture storage is 108 bytes/pixel for raster and 136 bytes/pixel for RT. At 3840×2160, retaining both means approximately **1.88 GiB** of nominal target storage, excluding scene buffers and Qt resources. Actual allocated/resident memory depends on the driver; this is descriptor arithmetic, not a measured RSS figure.

   Keep separate resolved presentation textures, but share transient MSAA/depth attachments within each renderer because only one renderer submission is allowed in flight. Allocate overlay attachments only when needed. Consider memoryless depth and RT MSAA attachments on supported Apple GPUs. Raster MSAA color currently crosses a pass boundary through Store/Load, so it cannot simply become memoryless without restructuring that pass. Preserve an Intel-Mac-compatible storage path. [Apple memoryless storage documentation](https://developer.apple.com/documentation/metal/mtlstoragemode/memoryless).

   Verify resolution/DPR changes, overlays toggled after convergence, repeated mode switches, and transparent export; measure actual Metal allocations before and after.

5. **High priority — accelerate picking and stop rebuilding bond render data for every mouse event. Measured.**

   [Picking.cpp:130](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/common/Picking.cpp) tests every atom, then [materializes every bond segment](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/common/Picking.cpp), including colors and selection data that picking does not need. [MetalViewport mouse handling](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/ui/components/MetalViewport.mm) calls this synchronously during orbiting and panning as well as hovering.

   Cache geometry-only bond endpoints and use a CPU spatial index for candidate rejection. Reuse prepared scene bounds where possible, with revision tracking for positions, radius edits, periodic-image endpoints, and replication. Preserve the current bond hit tolerance and ranking when accelerating the query; replacing its capsule-like test with a different intersection rule would be a separate behavior change.

   At roughly 500,000 atoms and bonds, one pick took a median 6.28 ms and created a 32 MB bond array. Check identical results against the existing brute-force picker across camera modes and edited structures, including thin bonds and atoms overlapping bonds.

6. **Medium priority — make appearance and selection invalidation truly narrow. Code-backed optimization.**

   Metal raster inherits [Renderer::invalidateAppearance](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/common/Renderer.h), which dirties both complete atom and bond buffers. Thus selecting one item or recoloring atoms repacks all positions/radii, recomputes atom bounds, and uploads full instances through [setAtomData](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalSphereRenderer.mm) and `setBondData`. RT already avoids rebuilding the BVH for appearance changes, but [uploadAppearanceData](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalRayTracingRenderer.mm) still repacks all colors and all selection masks together.

   Separate geometry, colors, and selection revisions. Update only changed attributes/ranges, or use separate attribute buffers when that reduces total traffic. Keep RT accumulation invalidation for visible selection/color changes. Avoid repeated intermediate arrays when packing a full scene: bond packing currently creates segments, then several SoA vectors, including duplicated endpoint radii and selection representations.

   Validate atom-only and bond-only selection, molecule selection, custom endpoint colors, selection clearing, and global color-scheme changes.

7. **Medium priority — avoid selection-outline work when nothing is selected. Code-backed optimization.**

   [MetalRenderer.mm:207](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalRenderer.mm) always supplies a positive selection-outline width. Consequently [MetalBondRenderer.mm:203](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalBondRenderer.mm) issues the full outline draw even with normal outlines disabled and no selected bonds; the fragment shader discards the work later. The RT path similarly pads every traversed AABB for [selection outlines](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalRayTracingRenderer.mm), even when there are no selected primitives. The raster early-depth gate is also made more conservative than necessary.

   Cache whether selected atoms/bonds exist when selection data changes. Use zero selection width/padding when they do not, and skip the unnecessary bond draw. When only a few bonds are selected and regular outlines are disabled, consider a compact selection-only draw. Do not scan all selection masks on every frame to calculate the gate.

   Test none/some/all selected, outlines on/off, DPR changes, and outlines near the camera plane. Measure dense bonded scenes and zoomed-out RT scenes.

8. **Medium priority — improve BVH pruning and construction memory before replacing the builder. Code-backed optimization.**

   [traceClosest](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalShaderLibrary.mm) stores node indices without their entry distance. A far leaf queued before a closer hit is found is still tested when popped. A root leaf also tests every primitive for every pixel without an initial root-bound rejection. Any-hit traversals use a fixed child order rather than attempting nearer hits first. These are places to benchmark conservative rejection improvements while retaining existing primitive tests.

   Start with the closest-hit stack: preserve each queued node's entry distance and skip it once it exceeds the current nearest hit. Preserve equal-distance tie behavior. Benchmark root-bound testing separately because an extra root test may not help all scenes. For any-hit rays, test whether near-first traversal saves more intersections than its ordering costs.

   The builder also [reserves two nodes per primitive](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/common/BVH.cpp), including in each parallel subtree. The measured 500,000-sphere result used 6.29 MB of nodes but reserved 48 MB. Reserve based on the configured leaf size, allowing growth when needed, and avoid keeping oversized subtree buffers alive longer than necessary.

   Check accelerated results against brute-force sphere/cylinder intersections, including overlapping objects, periodic bonds, large atom scales, outlines, and degenerate centroids. Benchmark sparse molecules, dense crystals, and elongated bonded structures independently.

9. **Lower-cost follow-ups — remove a redundant raster resolve and unnecessary initialization work. Code-backed opportunities.**

   The raster scene pass uses [StoreAndMultisampleResolve](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalRenderer.mm) when overlays are enabled. The next pass loads the MSAA color, draws overlays, and resolves again; nothing samples the first resolved image. Store the MSAA attachment without the first resolve, then resolve once after overlays. Preserve the separate depth clear that keeps overlays above the scene.

   Both renderer instances own a full shader library, and [initialization](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalShaderLibrary.mm) compiles the entire MSL source and creates all raster/RT pipelines. Share immutable library/pipeline objects per device and configuration, create only needed variants, and consider building a metallib during the normal build. Measure before prioritizing: the first probe saw 288 ms for raster initialization, a later run 8 ms, and RT initialization after raster approximately 0 ms, showing that driver caching already helps repeated setup substantially.

**Correctness findings**

10. **Fix before GPU performance validation — overlay pipelines have an incompatible depth attachment format. Reproduced with Metal API validation.**

    The [display pipeline](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalShaderLibrary.mm) and RT unit-cell pipelines leave `depthAttachmentPixelFormat` invalid. The [overlay display pass](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalRayTracingRenderer.mm) attaches Depth32Float. Running the existing Metal background test with API validation aborts: “the render pipeline's pixelFormat (MTLPixelFormatInvalid) does not match the framebuffer's pixelFormat (MTLPixelFormatDepth32Float).”

    Create compatible depth-format pipeline variants for passes with overlays. Keep the direct, overlay-free display pipeline configured for no depth attachment. Check both 1× and 4× paths, axes, unit cell, and the rotation gizmo. Normal non-validation execution passing is insufficient validation of this API usage.

11. **Bond-free RT scenes leave required shader buffers unbound. Reproduced with Metal API validation.**

    [MetalRayTracingRenderer.mm:820](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalRayTracingRenderer.mm) binds bond resources only when bonds exist. `rt_fragment` still declares them. The one-atom probe aborted with missing bindings at indices 7, 8, 9, 10, 11, and 13. Unit-cell occlusion has similar optional-resource binding concerns when geometry is absent.

    Bind valid, correctly sized dummy resources for empty arrays, or use shader variants that actually remove unused resource arguments. Counts can remain zero. Validate isolated atoms, structures before asynchronous bond detection completes, all bonds deleted, and lattice-only scenes.

12. **Perspective raster billboards can clip off-axis spheres. Reproduced.**

    [MetalShaderLibrary.mm:168](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalShaderLibrary.mm) computes a symmetric billboard size using radius and view depth only. Off-axis perspective projection changes the silhouette extent and shifts its projected center; the fixed 5% expansion does not conservatively cover it. The early-depth version preserves the same insufficient rectangle. The OpenGL sphere shader uses the same formula.

    The 256² probe at FOV 120° missed 62 pixels well inside the analytic sphere silhouette, producing a visibly flat clipped side. Compute conservative projected sphere bounds using the off-axis center, with a safe fallback for spheres crossing the camera/near plane. Preserve the early-depth promise when placing that rectangle.

    Validate the analytic silhouette over the supported FOV range, aspect ratios, panning, sphere sizes, outlines, and near-plane cases. Do not solve this merely by increasing the global billboard margin, which increases fragment work without establishing a correct bound.

13. **RT unit-cell occlusion ignores per-bond radii. Reproduced.**

    Primary and shadow rays use the bond-radius array, but [traceAnyHit](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalShaderLibrary.mm) uses a single global `bondRadius` for unit-cell visibility. Selection-mode radius editing changes the stored per-bond radius without changing that global setting.

    A thick bond therefore lets unit-cell edges show through; a thin bond may hide edges it should not. The probe changed 431 pixels by changing only the global fallback radius while retaining identical per-bond geometry. Pass the actual radius buffer into unit-cell traversal and use the same effective radius rules and BVH bounds as primary rays. Test differently sized bonds against edges and corners in both projections.

14. **Empty RT scenes with overlays force an opaque background. Reproduced.**

    [MetalRayTracingRenderer.mm:980](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalRayTracingRenderer.mm) clears to `(r, g, b, 1)` when no atoms exist and an overlay is visible. Other paths correctly use premultiplied `(r*a, g*a, b*a, a)`. With axes enabled, the probe returned background alpha 255 for a requested alpha of 0.

    Use the same premultiplied clear helper in every path. Test alpha 0, partial alpha, and alpha 1 with no atoms, a remaining lattice, axes, and rotation gizmo. This also warrants a real-backend image-export case; the current UI export test uses a controlled fake viewport.

15. **Completion publication can race generation resets. Static concurrency finding; no observed corruption claimed.**

    Both [RT completion handling](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalRayTracingRenderer.mm) and [raster completion handling](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/metal/MetalRenderer.mm) clear `inFlightSlot` before validating and publishing the completed output. Generation checking and slot publication are separate atomic operations, while resize/reset can change the generation and clear/reallocate slots.

    A permitted interleaving is: the handler clears the in-flight marker and reads the old generation as valid; the render thread resizes, changes generation, and resets/replaces textures; the old handler then publishes its slot as ready. Readers can associate that old completion with a replacement texture that has not been rendered. The atomics individually avoid data races but do not make this compound transition atomic.

    Serialize completion publication with reset/resize, or give each output generation independent immutable bookkeeping and publish validated results on the render thread. Releasing the in-flight marker last helps but does not by itself serialize a resize already allowed to happen concurrently. Add deterministic interleaving tests and stress resize/scene replacement/export before increasing GPU concurrency.

16. **Imported Qt textures omit alpha capability. Documentation-backed integration finding.**

    [MetalViewport.mm:934](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/ui/components/MetalViewport.mm) calls `QSGMetalTexture::fromNative` without `TextureHasAlphaChannel`, although the renderer produces transparent and partially transparent backgrounds. Qt uses that option to describe the imported texture's alpha capability. This can select opaque scene-graph compositing for an alpha-bearing output. [Qt native texture documentation](https://doc.qt.io/qt-6/qnativeinterface-qsgmetaltexture.html).

    Mark alpha-bearing outputs appropriately. If opaque and alpha-capable wrappers are both retained for performance, include that distinction in the wrapper cache key and switch when background opacity changes. Verify real Qt composition over a colored parent and native transparent PNG export; direct Metal texture readback cannot test this integration. This finding was not reproduced with a full-window compositing probe.

17. **The RT accumulation hash omits atom visibility. Reproduced through the renderer interface.**

    [RenderStateHash.cpp:33](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/render/common/RenderStateHash.cpp) includes `showBonds` but omits `showAtoms`, although `rt_fragment` reads both. Hiding atoms after convergence preserved the visible atom until explicit invalidation. The current QML path usually derives visibility from atom scale/bond visibility, which are hashed, so this probe exposes an interface-level hole rather than an independently reproduced QML toggle bug.

    Include every value that changes accumulated foreground, including atom visibility and the selection-outline pixel ratio where applicable. Keep display-only background/overlay settings outside that hash. Audit camera projection dependencies at the same time and add targeted hash-change/image tests.

**Additional checks to carry into implementation**

- Before allowing multiple renderer submissions in flight, resolve Qt consumer ownership explicitly. `lastPresentedSlot` means handed to Qt, not proven complete on Qt's GPU command queue. Triple texture buffering alone is not a consumer-completion signal; the renderer owns a different command queue. Apple's synchronization guidance distinguishes conflicts within a queue from cross-queue coordination. No tearing was reproduced in this review. [Apple resource synchronization](https://developer.apple.com/documentation/metal/resource-synchronization).
- The RT initialization failure branch in [MetalViewport.mm:781](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/ui/components/MetalViewport.mm) sets the mode back to raster but leaves the RT pointer non-null, then selects it in the following `if`. Reset the failed object and retain the working renderer. This is an error-path finding, not a reproduced normal-startup failure.
- Shader sphere intersections used by shadows/unit-cell visibility still use the cancellation-prone `b*b-c` formula even though primary RT sphere intersections have a stable perpendicular-distance version. Large-coordinate and distant-occluder regression cases should compare those paths before consolidating them.
- The OpenGL fallback continuously requests raster frames in [OpenGLViewport.cpp:72](/Users/haoran/Desktop/haoran_git_repos/atom-studio/src/ui/components/OpenGLViewport.cpp). It also retains the shared picking/packing/BVH issues. It is not selected by the current macOS startup path, so it should not displace the Metal work above. The fallback's unimplemented outline behavior was not treated as a bug to fix in this review.

**Suggested implementation sequence and acceptance criteria**

First repair Metal API validation failures and add the reproduced image cases, so optimization comparisons use a valid baseline. Then fix RT idle scheduling and completion publication together, retaining frame-request/export guarantees. Next merge RT batch encoders, eliminate the extra raster resolve, and gate unused selection-outline work. Follow with target-memory changes, geometry revisions/background preparation, accelerated picking, and narrow attribute uploads. Shader/BVH tuning should follow measured bottlenecks.

For each change, retain current sample count, image resolution, MSAA quality, AO/shadow settings, outline semantics, individual atom/bond styling, periodic endpoints, projection modes, and background-independent RT accumulation. Use deterministic image comparisons for unchanged behavior and explicit expected-image assertions for corrected bugs. Exercise empty and bond-free scenes, dense crystals, sparse molecules, radius edits, selected objects, wide-FOV/near-plane views, resize/DPR changes, renderer switches, and transparent export. Track idle submissions, first-feedback latency, CPU preparation/picking/encoding time, GPU time per sample, convergence time, and actual Metal memory separately.

No speedup factor is promised by this review; only the current-code costs and failures above were measured.


**Implementation results — 2026-09-08**

Implemented the proposed sequence for the current macOS Metal path:

- Corrected pipeline depth formats, zero-count shader bindings, off-axis/camera-plane sphere coverage, per-bond unit-cell occlusion, empty-scene opacity, Qt texture alpha metadata, failed RT initialization, and missing accumulation hash inputs.
- Added an explicit RT idle/display gate and serialized reset/completion publication. Background/overlay changes and explicit frame tokens still produce an image while preserving foreground samples. An unchanged converged viewport stops scheduling frames.
- Merged accumulation draws within each adaptive sample batch into one render encoder. Removed the first raster resolve when an overlay pass follows. Gated selection outlines and their traversal padding when no object is selected. Resolution, MSAA, sample order, AO/shadow settings, and accumulation precision are unchanged.
- Shared transient attachments among output slots, made supported Apple GPU attachments memoryless, and allocated RT overlay attachments only when needed. Resolved output textures remain separate. Immutable shader libraries and pipelines are shared per device/sample count.
- Added an immutable geometry snapshot and a worker that coalesces queued edits and publishes only the newest revision. Metal raster, RT, and picking share prepared geometry. The previous image stays visible while preparation runs, and switching back to RT retains its prepared scene and converged samples.
- Accelerated picking using the shared BVH, preserving bond tolerance and atom-first/index tie ordering. The fallback query no longer allocates a complete bond render array. Sphere picking and secondary Metal sphere intersections use the stable perpendicular-distance discriminant.
- Appearance updates compare and write existing color/selection attributes directly. Geometry and bounds are retained; full raster uploads also fill shared instance buffers directly. BVH node reservation follows the balanced tree's leaf capacity instead of reserving two nodes per primitive.

A separate resource ownership bug emerged during implementation: Objective-C++ sources were built without ARC, although Metal resource owners relied on automatic releases. Enabling ARC fixes persistent leaks of textures, buffers, shader resources, and queues. Qt wrapper cache entries and deferred cleanup jobs now explicitly retain their native textures for the wrapper lifetime.

| Isolated check on Apple M2 Pro | Result |
| --- | --- |
| Resize resource leak, 12 sizes from 1024×768 to 1035×768, both renderers | Without ARC: 44.79 → 558.79 MB allocated and 558.79 MB after cleanup. With ARC: 44.79 → 47.12 MB, then 0.39 MB after cleanup. Both probes already used the new attachment layout; this isolates the ownership fix. |
| 100,000 atoms / 99,999 bonds; median of 101 pick queries | Linear 1.604 ms; indexed 0.00163 ms; matching results |
| 500,000 atoms / 499,999 bonds; median of 101 pick queries | Linear 8.062 ms; indexed 0.00654 ms; matching results |
| Geometry preparation for that 500,000-atom/bond scene | Snapshot copy 1.25 ms; geometry packing + unified BVH 99.42 ms in the worker; retained BVH node capacity 12.58 MB |
| Primary-ray BVH pruning experiment: 12,000 atoms, bonds, 768×512, 32 samples | Matching image hashes with and without AO/shadows. Timings overlapped; AO/shadow convergence was about 634 ms before and 629 ms after. The experimental stack/pruning change was removed because this did not establish a useful gain. |

CPU timings compile the common hot paths with `-O3 -DNDEBUG` and link the existing data library. GPU checks use the Debug renderer build. These are synthetic local measurements, not application-wide speedup guarantees. Source probes: [picking](/tmp/atom_renderer_picking_probe.cpp), [memory](/tmp/atom_renderer_memory_probe.mm), [traversal timing](/tmp/atom_renderer_timing_probe.mm).

**Validation**

The full `cmake --build build -j 4` succeeds, including bundled Python validation. With native GPU access and `MTL_DEBUG_LAYER=1`, CTest passed 16 tests; the OpenGL offscreen test skipped because its context was unavailable. Running that executable with `QT_QPA_PLATFORM=cocoa` also passed, covering all 17 tests across the two runs. The final added Metal/viewport cases were rerun after the full suite and passed.

- Metal regressions cover zero-bond and zero-atom scenes, empty overlays, alpha, visibility, unit-cell bond radii, identical batched/individual accumulation, cached/full geometry parity, appearance/full upload parity, resize memory reuse, off-axis coverage, and a camera inside a sphere.
- The real Qt Metal viewport test covers alpha compositing, transparent PNG capture, explicit frame tokens, rapid geometry replacement, mode switches without losing samples, convergence idleness, and resize.
- CPU tests cover 19,200 linear/indexed picking comparisons, periodic endpoints, tie ordering, geometry worker coalescing/lifetime/error recovery, BVH storage, and 2,000 reset/completion races. Existing camera, background-compositing, structure, replication, color-picker, and export tests pass.

**Remaining measured limits**

Appearance updates still scan attributes to find changes; the current model does not supply dirty index ranges. Radius/topology edits rebuild the prepared BVH rather than refitting it. Snapshot copying and GPU buffer population still run during synchronization, while geometry packing/BVH construction run in the worker. Shared prepared geometry occupies CPU memory to support fast picking and renderer switching. The experimental traversal change was not retained; offline shader compilation, hardware RT, and further GPU traversal changes remain profiling work. Intel Mac hardware and Windows were not tested.

**RT rotation regression follow-up**

The first scheduling change reset the accumulation generation before checking whether a GPU frame was still in flight. A new camera request therefore invalidated the pending frame. Under continuous movement, every GPU completion could be discarded, holding an old image on screen until input stopped. The earlier static-image and idle tests did not cover this case.

`MetalRayTracingRenderer::render` now defers camera-state invalidation until the current submission completes. That completed view remains publishable; the next submission uses the latest camera and fresh accumulation. Resize and explicit scene invalidation retain their generation protection.

A native GPU motion stress test (12,000 atoms, 768×512, 32 AO rays per pixel, camera requests approximately every millisecond) published **0 new images in one second before the fix, and 58 afterward**. These count renderer output updates in a synthetic stress test, not measured Qt display FPS. The new regression test also compares the final accumulated image against a fresh render of the final camera to detect mixing of samples from different views. The app was rebuilt and the renderer, native viewport, and image-export regressions passed with Metal API validation enabled; the unchanged OpenGL offscreen test skipped.

**Additional regression audit — 2026-09-08**

Two further issues were reproduced and corrected:

- **RT diagonal bond outlines could lose edge pixels.** Removing unused selection-outline padding exposed insufficient bounds for capped-cylinder outlines. Their endpoints and radius both expand, so a world-axis bound can grow by up to `sqrt(2) * outlineWidth`, rather than just `outlineWidth`. The renderer now applies that conservative factor to traversal padding when bonds are visible; the actual outline width is unchanged. An eight-bond probe compared normal BVH traversal with a reference whose bounds disable spatial rejection. At outline widths 1, 2, 4, and 8 pixels, the old result differed by 1, 7, 43, and 234 pixels; the fixed result matched exactly at all four widths. Larger widths also exposed the underlying pre-existing bound defect. The regression test covers both projections and selected/unselected bonds.
- **Raster spheres entirely behind the camera performed fullscreen shading.** The new camera-plane fallback also caught spheres wholly behind the eye. The vertex shader now rejects those spheres before fragment shading, while preserving the fallback for spheres crossing the camera plane. In an isolated 512×512 probe with 256 hidden spheres, rendering took approximately 7.9 ms before the fix and 2.0 ms afterward, close to the 1.95 ms atoms-disabled reference. These are wall-clock measurements including readback and polling, not GPU timestamps or application FPS. The rendered pixels remain identical to the empty reference.

The audit also exercised cleanup and reinitialization with GPU submissions in flight, including repeated RT recreation and comparison with a fresh accumulated image. Those checks passed. No additional lifetime or shader-cache failure was reproduced in the paths checked; this is not proof that all possible regressions have been excluded.

The full app build, including bundled Python validation, succeeded after these fixes. With native GPU access and `MTL_DEBUG_LAYER=1`, all eight runnable renderer, Metal viewport, and image-export tests passed. The unchanged OpenGL offscreen test skipped because its context was unavailable. `git diff --check` passed.

**Stationary sampling investigation — 2026-09-08, not yet reproduced**

The user reported slower stationary sampling with a large supercell of `ptcda_1103_geometry.in`, reduced atom radii, bonds, AO, and shadows. The input cell contains 182 atoms; the application's neighbor-list implementation produces 381 bonds at the default detection scale. Exact replication counts, radius setting, and view still need to be matched.

Controlled local comparisons used the earlier renderer/shader source from Git HEAD versus the current working tree, with the same current common/data libraries and ARC enabled in both. The Qt comparison also compiled the earlier viewport source. These isolate the changed rendering paths; they are not comparisons of complete historical app bundles. Benchmarks ran without Metal API validation, after scene preparation and a warmup sample, and waited for completed output rather than stopping at the submitted-sample counter. All timings below are medians of three trials, with atom radius scale 0.3, four AO rays per sample, shadows, bonds, and orthographic projection. Test views and window sizes were fixed within each comparison; the user's exact view is not known.

| PTCDA workload | Earlier path | Current path |
| --- | --- | --- |
| 5×5×1, 4,550 atoms / 9,525 bonds, native Qt viewport, 2048×1364 pixels, 32 samples | 1.85 s | 1.53 s |
| 10×10×1, 18,200 atoms / 38,100 bonds, direct Metal, 1536×1024 pixels, 16 samples | 0.628 s | 0.493 s |

An initial single-cell, separate-process comparison suggested that merging sample passes cost approximately 35%. This did **not** survive reverse-order and within-process alternating comparisons, so it is not a confirmed regression and does not justify reverting the pass change. On the 5×5×1 scene, alternating merged/separate passes produced approximately 1.01 s in either case for 32 AO/shadow samples, with identical image hashes. Heavy samples already consume the adaptive budget individually, so pass merging has no effect on those submissions.

No production rendering changes were made during this investigation. The reported slowdown remains unresolved pending a closer reproduction; these measurements do not rule out a configuration-specific regression. Reproduction sources are retained locally in [the direct Metal probe](/tmp/atom_sampling_probe.mm), [the Qt viewport probe](/tmp/atom_sampling_viewport.mm), and [the alternating-pass probe](/tmp/atom-sampling-variants/alternating-probe.mm). The input geometry was not copied into the repository.
