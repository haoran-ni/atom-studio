# Orthographic View Implementation Plan

## Status

Planning only. No implementation work has been done in this step beyond creating this document.

## User Requirements

1. Implement orthographic view for the main viewport.
2. Use the already-existing projection control in `src/ui/qml/Sidebar.qml` to switch between perspective and orthographic.
3. Do not add any projection or view-type control to the viewport floating tab bar.
4. Treat this as a high-risk camera and rendering change. Perspective rendering must keep its current behavior, especially in ray tracing.
5. Update all linked code paths that currently assume perspective projection instead of only changing the camera projection matrix.

## Current Code Observations

- `src/render/common/Camera.h` and `src/render/common/Camera.cpp` already contain dormant orthographic state:
  - `m_perspective`
  - `m_fov`
  - `m_orthoScale`
  - `setProjection(bool perspective)`
- `setProjection()` is never called anywhere in the current codebase.
- `src/ui/qml/Sidebar.qml` already exposes a `Projection` `ComboBox`, but it is hardcoded and not wired to the viewport.
- `src/ui/qml/HeaderBar.qml` also contains a dormant Projection menu. It is not the required UI surface for this task.
- Both OpenGL and Metal raster sphere impostors are perspective-specific.
- Both OpenGL and Metal RT primary-ray generation are perspective-specific.
- Metal RT unit-cell overlay occlusion is also perspective-specific.
- RT accumulation reset is already mostly protected because `src/render/common/RenderStateHash.cpp` hashes:
  - camera orientation
  - distance
  - field of view
  - aspect ratio
  - perspective flag
  - orthographic scale

## Implementation Goals

- Add orthographic mode without regressing perspective mode.
- Preserve framing when switching modes so the view does not jump unexpectedly.
- Keep camera interaction behavior correct in orthographic mode:
  - orbit
  - pan
  - zoom
  - reset
  - fit to view
- Apply the change consistently across:
  - OpenGL raster
  - Metal raster
  - OpenGL RT
  - Metal RT

## Proposed Design

### 1. Projection State Lives in the Viewport and Camera

- Add a `projectionMode` `Q_PROPERTY` to both viewport classes:
  - `src/ui/components/OpenGLViewport.h`
  - `src/ui/components/MetalViewport.h`
- Use enum-style integer values:
  - `0 = Perspective`
  - `1 = Orthographic`
- The viewport setter will call a camera helper that switches projection while preserving framing.
- The setter must emit:
  - `projectionModeChanged`
  - `cameraChanged`
- The setter must also call `update()` so the active renderer re-renders immediately.

### 2. Camera Owns Projection-Switching Math

- Extend `src/render/common/Camera.h` and `src/render/common/Camera.cpp` with a helper such as:
  - `setProjectionMode(int mode, bool preserveFraming = true)`
- Preserve framing using target-plane scale conversion:
  - perspective -> orthographic:
    - `orthoScale = distance * tan(fov / 2)`
  - orthographic -> perspective:
    - `distance = orthoScale / tan(fov / 2)`
- Orthographic zoom should primarily change `orthoScale`.
- Perspective zoom should primarily change `distance`.
- `fitToView()` should remain mode-specific.
- Add projection-aware screen-space helpers for viewport interaction, for example:
  - screen ray origin and direction
  - screen point mapped onto the camera target plane
- Those helpers should be used by pan and zoom-to-cursor logic.

### 3. Sidebar Is the Required UI Surface

- Wire the existing `Projection` `ComboBox` in `src/ui/qml/Sidebar.qml`.
- Replace its hardcoded `currentIndex: 0` with a binding to the active viewport property.
- Update the selection handler to write back to `viewport.projectionMode`.
- Do not modify `src/ui/qml/ViewportPanel.qml` for this task.
- Optional follow-up cleanup:
  - bind the existing `HeaderBar.qml` Projection menu to the same property
  - or explicitly leave it disabled until it is wired

### 4. Raster Rendering Must Become Projection-Aware

OpenGL:

- Update `src/render/opengl/ShaderManager.cpp`.
- Sphere impostor shader changes:
  - perspective path keeps the existing billboard radius and ray generation
  - orthographic path uses world-radius billboard size in view space
  - orthographic ray origin must vary per fragment in x/y and use a constant ray direction
  - orthographic shading view direction should be constant
- Bond shader changes:
  - geometry projection already works through the projection matrix
  - specular `viewDir` must stop assuming the eye is at the origin in orthographic mode
- Uniform plumbing changes will be needed in:
  - `src/render/opengl/SphereRenderer.cpp`
  - `src/render/opengl/BondRenderer.cpp`
  - `src/render/opengl/UnitCellRenderer.cpp`

Metal:

- Update the shared shader structs and MSL logic in:
  - `src/render/metal/MetalTypes.h`
  - `src/render/metal/MetalShaderLibrary.mm`
- Apply the same sphere and bond shader logic as in OpenGL.
- `src/render/metal/MetalUnitCellRenderer.mm` will automatically benefit for unit-cell corner joints because it uses the shared sphere pipeline.

### 5. Ray Tracing Must Become Projection-Aware

OpenGL RT:

- Update `src/render/opengl/RayTracingRenderer.cpp`.
- Add uniforms needed for projection-aware ray generation and shading.
- Perspective mode must preserve the current logic.
- Orthographic mode must:
  - generate parallel primary rays
  - use per-pixel orthographic ray origins
  - use a constant view direction for specular calculations
- Do not rely on only flipping the projection matrix. Primary-ray construction must branch or use a generalized projection-aware formulation.

Metal RT:

- Update:
  - `src/render/metal/MetalTypes.h`
  - `src/render/metal/MetalShaderLibrary.mm`
  - `src/render/metal/MetalRayTracingRenderer.mm`
- Mirror the OpenGL RT changes for primary rays and shading.
- Perspective behavior must remain unchanged when `projectionMode` is perspective.

Metal RT Unit-Cell Overlay:

- Update:
  - `src/render/metal/MetalUnitCellShared.h`
  - `src/render/metal/MetalUnitCellShared.cpp`
  - RT unit-cell fragment logic in `src/render/metal/MetalShaderLibrary.mm`
- Current occlusion testing casts from `cameraPosition` to the fragment world point. That is correct for perspective but wrong for orthographic.
- Replace it with projection-aware occlusion logic:
  - perspective:
    - keep the current behavior
  - orthographic:
    - cast along the constant view direction
    - stop at the fragment depth

### 6. Viewport Interaction Code Needs Follow-Up Changes

OpenGL viewport:

- Update wheel and native-pinch paths in `src/ui/components/OpenGLViewport.cpp` to use projection-aware screen-to-plane helpers.
- Update pan logic so orthographic mode does not continue using perspective distance heuristics.

Metal viewport:

- Update wheel and mouse pan handling in `src/ui/components/MetalViewport.mm` to use the same projection-aware camera helpers.
- Consider adding native gesture parity on macOS if needed.

## Files Expected to Change During Implementation

- `src/ui/qml/Sidebar.qml`
- `src/ui/components/OpenGLViewport.h`
- `src/ui/components/OpenGLViewport.cpp`
- `src/ui/components/MetalViewport.h`
- `src/ui/components/MetalViewport.mm`
- `src/render/common/Camera.h`
- `src/render/common/Camera.cpp`
- `src/render/opengl/ShaderManager.cpp`
- `src/render/opengl/SphereRenderer.cpp`
- `src/render/opengl/BondRenderer.cpp`
- `src/render/opengl/UnitCellRenderer.cpp`
- `src/render/opengl/RayTracingRenderer.cpp`
- `src/render/metal/MetalTypes.h`
- `src/render/metal/MetalShaderLibrary.mm`
- `src/render/metal/MetalUnitCellRenderer.mm`
- `src/render/metal/MetalRayTracingRenderer.mm`
- `src/render/metal/MetalUnitCellShared.h`
- `src/render/metal/MetalUnitCellShared.cpp`

## Implementation Order

1. Add `projectionMode` to both viewport classes and wire the sidebar `ComboBox`.
2. Extend `Camera` with framing-preserving projection switching and projection-aware interaction helpers.
3. Update OpenGL raster shaders and uniform plumbing.
4. Update Metal raster shaders and uniform plumbing.
5. Update OpenGL RT.
6. Update Metal RT.
7. Update Metal RT unit-cell overlay occlusion.
8. Re-test pan, zoom, reset, and fit-to-view behavior in both projections.

## Risks and Regression Targets

- Raster sphere impostors may clip or render incorrect silhouettes if orthographic ray origin is wrong.
- Bond and sphere specular shading may look subtly wrong if `viewDir` remains perspective-based in orthographic mode.
- RT may appear to work but still be incorrect if rays stay camera-position-based.
- Metal RT unit-cell overlay can silently fail only in orthographic mode if its occlusion path is not updated.
- Pan and zoom feel can degrade if orthographic interaction keeps using perspective distance heuristics.
- Switching projection during RT accumulation must reset cleanly and not ghost old samples.

## Manual Verification Matrix

Use at least:

- one atom-only structure
- one structure with bonds
- one structure with a visible unit cell

Projection modes:

- Perspective
- Orthographic

Backends and renderers:

- OpenGL raster
- OpenGL RT
- Metal raster
- Metal RT

Actions:

- switch projection with a loaded structure
- orbit
- pan
- wheel zoom
- native pinch zoom if supported
- reset camera
- fit to view
- switch projection while RT accumulation is in progress
- toggle bonds
- toggle unit cell

Visual checks:

- no large framing jump when switching modes
- orthographic rays are parallel
- perspective view remains visually unchanged from the current implementation
- RT accumulation resets correctly after a mode switch
- Metal RT unit cell is occluded correctly by atoms in orthographic mode
- no unexpected clipping or depth artifacts

## Out of Scope for This Task

- adding any projection control to the viewport floating tab bar
- broader camera settings polish such as fully wiring the FOV slider
- renderer architecture changes beyond what is needed for a safe orthographic implementation
