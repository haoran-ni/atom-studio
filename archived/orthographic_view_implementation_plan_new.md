# Orthographic View Implementation Plan

## Context

The Camera class already supports orthographic projection (matrix construction, zoom, fit-to-view), but the QML controls are disconnected from C++, and all raster/RT shaders assume perspective projection. This plan wires the existing QML controls to the camera, then fixes every perspective-dependent shader path so orthographic rendering is correct without breaking anything under perspective.

## Overview of Changes

|Area|Files|What changes|
|---|---|---|
|QML ↔ C++ wiring|MetalViewport.h/.mm, OpenGLViewport.h/.cpp, Sidebar.qml|Add `isPerspective` + `fieldOfView` Q_PROPERTYs; wire sidebar controls|
|Metal raster sphere|MetalShaderLibrary.mm, MetalTypes.h|Ortho billboard sizing + ortho ray-sphere intersection|
|Metal RT|MetalShaderLibrary.mm, MetalTypes.h, MetalRayTracingRenderer.mm|Ortho ray generation (parallel rays, varying origins)|
|Metal RT unit cell|MetalShaderLibrary.mm, MetalTypes.h, MetalUnitCellShared.cpp/.h|Ortho occlusion rays (parallel direction)|
|Metal raster uniforms|MetalRenderer.mm|Upload `isPerspective` to SceneUniforms|
|OpenGL raster sphere|ShaderManager.cpp|Same ortho billboard + intersection fix|
|OpenGL RT|RayTracingRenderer.cpp|Same ortho ray generation fix|
|OpenGL raster uniforms|SphereRenderer.cpp|Pass `isPerspective` uniform|
|State hash|(none)|Already includes `isPerspective` + `orthoScale` — no changes|

## Step-by-step Implementation

### Step 1: Add `isPerspective` to GPU Uniform Structs

**File: `src/render/metal/MetalTypes.h`**

Add `int32_t isPerspective` to three structs:

- **SceneUniforms**: replace one `_pad` → `int32_t isPerspective; float _pad;`
- **RTUniforms**: add `int32_t isPerspective` after `showBonds`
- **RTUnitCellUniforms**: add `int32_t isPerspective`, `float cameraForwardX/Y/Z` after `unitCellColor`

**File: `src/render/metal/MetalShaderLibrary.mm`** — update the mirrored MSL struct definitions at the top of the shader source to match.

### Step 2: Fix Metal Sphere Impostor (Raster)

**File: `src/render/metal/MetalShaderLibrary.mm`**

**Vertex shader (`sphere_vertex`)** — add orthographic billboard branch:

```msl
if (scene.isPerspective) {
    // existing perspective billboard code
    float dist = -viewCenter.z;
    float billboardR;
    if (dist > R * 1.01) {
        billboardR = R * dist / sqrt(dist * dist - R * R);
    } else {
        billboardR = dist * 100.0;
    }
    billboardR *= 1.05;
} else {
    // orthographic: billboard = sphere radius (no foreshortening)
    billboardR = R * 1.05;
}
```

**Fragment shader (`sphere_fragment`)** — add orthographic ray-sphere intersection:

```msl
if (scene.isPerspective) {
    // existing: ray from origin through viewPosOnQuad
    float3 rayDir = normalize(in.viewPosOnQuad);
    // ... existing quadratic solve ...
} else {
    // orthographic: parallel ray along -Z
    float3 rayOrigin = float3(in.viewPosOnQuad.xy, 0.0);
    float3 C = in.viewCenter;
    float R = in.radius;
    float dx = rayOrigin.x - C.x;
    float dy = rayOrigin.y - C.y;
    float disc = R * R - dx * dx - dy * dy;
    if (disc < 0.0) discard_fragment();
    float sqrtDisc = sqrt(disc);
    float t = -C.z - sqrtDisc;  // front hit (closest z)
    hitPos = float3(rayOrigin.xy, -t);  // z = origin.z - t * dir.z = -t since dir=(0,0,-1)
    normal = normalize(hitPos - C);
}
```

Depth output (`scene.projectionMatrix * float4(hitPos, 1.0)`) works unchanged for both modes since `clipPos.w = 1.0` under orthographic.

### Step 3: Fix Metal RT Ray Generation

**File: `src/render/metal/MetalShaderLibrary.mm`** — in `rt_fragment`:

```msl
float4 ndc = float4(uv * 2.0 - 1.0, -1.0, 1.0);
float4 viewTarget = rt.invProjection * ndc;
viewTarget.xyz /= viewTarget.w;

float3 rayDir;
float3 rayOrigin;
if (rt.isPerspective) {
    rayDir = normalize((rt.invView * float4(viewTarget.xyz, 0.0)).xyz);
    rayOrigin = rt.cameraPosition;
} else {
    // Orthographic: origin varies per pixel, direction is constant
    rayOrigin = (rt.invView * float4(viewTarget.xyz, 1.0)).xyz;
    rayDir = normalize((rt.invView * float4(0.0, 0.0, -1.0, 0.0)).xyz);
}
```

**File: `src/render/metal/MetalRayTracingRenderer.mm`** — in `renderRTPass`, upload the new field:

```cpp
rt.isPerspective = camera.isPerspective() ? 1 : 0;
```

### Step 4: Fix Metal RT Unit Cell Occlusion

**File: `src/render/metal/MetalShaderLibrary.mm`** — in `rt_unit_cell_fragment`:

```msl
float3 ro, rd;
float maxT;
if (unitCell.isPerspective) {
    // existing perspective logic
    ro = unitCell.cameraPosition;
    float3 toPoint = in.worldPos - ro;
    float pointDist = length(toPoint);
    rd = toPoint / pointDist;
    maxT = max(pointDist - unitCell.occlusionBias, 0.0);
} else {
    // orthographic: trace backward from fragment toward camera (parallel rays)
    float3 cameraFwd = float3(unitCell.cameraForwardX, unitCell.cameraForwardY, unitCell.cameraForwardZ);
    ro = in.worldPos - cameraFwd * unitCell.occlusionBias;
    rd = -cameraFwd;
    // Limit to distance from fragment to camera plane
    maxT = max(dot(in.worldPos - unitCell.cameraPosition, cameraFwd) - unitCell.occlusionBias, 0.0);
}
if (maxT > 0.0 && traceAnyHit(ro, rd, maxT, ...)) {
    discard_fragment();
}
```

**File: `src/render/metal/MetalUnitCellShared.cpp`** — in `makeRTUnitCellUniforms`:

```cpp
unitCell.isPerspective = camera.isPerspective() ? 1 : 0;
QVector3D fwd = camera.forwardVector();
unitCell.cameraForwardX = fwd.x();
unitCell.cameraForwardY = fwd.y();
unitCell.cameraForwardZ = fwd.z();
```

### Step 5: Upload `isPerspective` to Metal Raster Uniforms

**File: `src/render/metal/MetalRenderer.mm`** — in `render()`, after building SceneUniforms:

```cpp
uniforms.isPerspective = camera.isPerspective() ? 1 : 0;
```

Also set `isPerspective` in the gizmo uniforms in `MetalRayTracingRenderer.mm` (the `SceneUniforms gizmoUniforms` used for rotation center rendering):

```cpp
gizmoUniforms.isPerspective = camera.isPerspective() ? 1 : 0;
```

### Step 6: Fix OpenGL Sphere Impostor (Raster)

**File: `src/render/opengl/ShaderManager.cpp`**

Apply the same logic as Metal Step 2 to both the vertex and fragment shaders, using a `uniform int uIsPerspective;` instead of a struct field.

**Vertex shader**: branch on `uIsPerspective` for billboard sizing. **Fragment shader**: branch on `uIsPerspective` for ray-sphere intersection.

**File: `src/render/opengl/SphereRenderer.cpp`** — pass the new uniform:

```cpp
shader->setUniformValue("uIsPerspective", camera.isPerspective() ? 1 : 0);
```

### Step 7: Fix OpenGL RT Ray Generation

**File: `src/render/opengl/RayTracingRenderer.cpp`**

Add `uniform int uIsPerspective;` to the RT fragment shader, then apply the same branching as Metal Step 3:

```glsl
if (uIsPerspective != 0) {
    rayDir = normalize((uInvView * vec4(viewTarget.xyz, 0.0)).xyz);
    rayOrigin = uCameraPos;
} else {
    rayOrigin = (uInvView * vec4(viewTarget.xyz, 1.0)).xyz;
    rayDir = normalize((uInvView * vec4(0.0, 0.0, -1.0, 0.0)).xyz);
}
```

Upload in C++:

```cpp
m_rtShader->setUniformValue("uIsPerspective", camera.isPerspective() ? 1 : 0);
```

### Step 8: Add Viewport Q_PROPERTYs

**Files: `src/ui/components/MetalViewport.h` and `OpenGLViewport.h`**

Add two Q_PROPERTYs (same in both):

```cpp
Q_PROPERTY(bool isPerspective READ isPerspective WRITE setIsPerspective NOTIFY projectionChanged)
Q_PROPERTY(float fieldOfView READ fieldOfView WRITE setFieldOfView NOTIFY projectionChanged)
```

Add declarations:

```cpp
bool isPerspective() const;
float fieldOfView() const;
// slots:
void setIsPerspective(bool perspective);
void setFieldOfView(float fov);
// signals:
void projectionChanged();
```

**Files: `MetalViewport.mm` and `OpenGLViewport.cpp`**

Implement getters/setters that delegate to `m_camera`:

```cpp
bool MetalViewport::isPerspective() const { return m_camera->isPerspective(); }
float MetalViewport::fieldOfView() const { return m_camera->fieldOfView(); }

void MetalViewport::setIsPerspective(bool perspective) {
    if (m_camera->isPerspective() != perspective) {
        m_camera->setProjection(perspective);
        emit projectionChanged();
        update();
    }
}

void MetalViewport::setFieldOfView(float fov) {
    if (!qFuzzyCompare(m_camera->fieldOfView(), fov)) {
        m_camera->setFieldOfView(fov);
        emit projectionChanged();
        update();
    }
}
```

### Step 9: Wire Sidebar QML Controls

**File: `src/ui/qml/Sidebar.qml`** — Camera section:

```qml
ComboBox {
    Layout.fillWidth: true
    model: ["Perspective", "Orthographic"]
    currentIndex: sidebar.viewport ? (sidebar.viewport.isPerspective ? 0 : 1) : 0
    onActivated: function(index) {
        if (sidebar.viewport) {
            sidebar.viewport.isPerspective = (index === 0)
        }
    }
}

NumericSliderControl {
    title: qsTr("Field of View")
    visible: sidebar.viewport ? sidebar.viewport.isPerspective : true
    integer: true
    from: 10
    to: 120
    stepSize: 1
    defaultValue: 45
    sourceValue: sidebar.viewport ? sidebar.viewport.fieldOfView : 45
    onValueApplied: function(newValue) {
        if (sidebar.viewport) {
            sidebar.viewport.fieldOfView = newValue
        }
    }
}
```

FOV slider is hidden when orthographic is selected (not relevant in ortho mode). The FOV range is widened to match Camera's valid range (10-170, but 10-120 in UI is practical).

## What Is NOT Changed

- **Bond cylinder shaders** (Metal + OpenGL): use standard MVP transforms — work correctly with orthographic projection matrix as-is.
- **Line shaders / unit cell wireframe** (raster mode): use `viewProjectionMatrix` directly — correct for both projections.
- **Viewport axes overlay**: uses its own ortho projection — unaffected.
- **RenderStateHash**: already includes `isPerspective` and `orthoScale`.
- **Camera class**: already fully supports orthographic — no changes needed.
- **Rotation center gizmo visualization**: uses bond/cylinder shader with MVP — works correctly with orthographic. Not modified per requirement #4.
- **View direction in shading** (`normalize(-hitPos)` / `normalize(cameraPos - hitPos)`): the error under orthographic is negligible and not worth the code churn to fix.

## Verification

1. **Build**: `cmake --build build`
2. **Run**: `./build/bin/atom-studio.app/Contents/MacOS/atom-studio`
3. **Test perspective (regression)**:
    - Load a structure file, verify raster rendering looks identical
    - Switch to RT mode, verify rendering and convergence work
    - Toggle unit cell, verify occlusion is correct
4. **Test orthographic**:
    - Switch to Orthographic in sidebar → viewport should update immediately
    - Verify FOV slider hides when orthographic is selected
    - Verify atoms appear with correct, uniform size (no perspective foreshortening)
    - Zoom in/out → structure scales uniformly
    - Pan → structure moves correctly
    - Orbit → structure rotates correctly
    - Fit to View → structure fills viewport
    - Switch to RT mode → verify accumulation converges, atoms look correct
    - Toggle unit cell in RT mode → verify unit cell lines appear with correct occlusion (behind atoms = hidden, in front = visible)
    - Switch back to Perspective → verify everything still works
5. **Test Reset Camera**: should reset to perspective mode