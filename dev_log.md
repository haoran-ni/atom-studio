# Development Log

This file records development sessions and decisions for future reference.

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
