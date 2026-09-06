#pragma once

#include <simd/simd.h>

namespace atom::render::metal {

// ---------------------------------------------------------------------------
// Shared CPU/GPU struct definitions
// Layouts must exactly match MSL shader [[buffer(N)]] declarations.
// ---------------------------------------------------------------------------

// Scene uniforms — [[buffer(0)]] for raster shaders (sphere, bond, line)
struct SceneUniforms {
    simd_float4x4 viewMatrix;
    simd_float4x4 projectionMatrix;
    simd_float4x4 viewProjectionMatrix;
    simd_float3   lightDir;           // normalized, in view space
    float         ambient;
    float         diffuse;
    float         specular;
    float         shininess;
    float         atomScale;
    float         bondRadius;
    int32_t       isPerspective;
    float         outlineWidthPx;    // stroke width in rendered pixels (0 = off)
    simd_float4   outlineColor;      // rgb stroke color
    float         outlinePixelScale; // world units per pixel: × view depth (persp) or absolute (ortho)
    int32_t       sphereEarlyZ;      // 1 = near-tangent billboard placement (early-Z pipeline)
    float         selectionOutlineWidthPx; // selected-object stroke width in rendered pixels
    float         _pad2;
};

// Per-instance sphere data — [[buffer(2)]] in sphere shader
struct SphereInstance {
    simd_float4 positionAndRadius;    // xyz = world center, w = covalent radius
    simd_float4 color;                // rgb, opaque padding
    float       selected;             // 1 when selected, 0 otherwise
    float       _pad0;
    float       _pad1;
    float       _pad2;
};

// Per-instance bond data — [[buffer(2)]] in bond shader
struct BondInstance {
    simd_float3 start;
    float       _pad0;
    simd_float3 end;
    float       _pad1;
    simd_float4 startColor;           // rgb, opaque padding
    simd_float4 endColor;             // rgb, opaque padding
    float       startRadius;
    float       endRadius;
    float       bondRadius;
    float       selected;             // 1 when selected, 0 otherwise
};

// Line vertex (unit cell) — [[buffer(1)]] in line shader
struct LineVertex {
    simd_float3 position;
    float       _pad0;
    simd_float4 color;                // rgba
};

// Ray tracing uniforms — [[buffer(0)]] for RT shader
struct RTUniforms {
    simd_float4x4 invView;
    simd_float4x4 invProjection;
    simd_float3   cameraPosition;
    float         atomScale;
    simd_float3   lightDir;
    float         ambient;
    simd_float4   backgroundColor;
    float         diffuse;
    float         specular;
    float         shininess;
    int32_t       atomCount;
    int32_t       width;
    int32_t       height;
    uint32_t      frameCount;
    int32_t       enableShadows;
    int32_t       enableAO;
    int32_t       aoSamples;
    float         aoRadius;
    int32_t       maxSamples;
    int32_t       bvhNodeCount;
    int32_t       bondCount;
    float         bondRadius;
    int32_t       showAtoms;
    int32_t       showBonds;
    int32_t       isPerspective;
    float         outlineScale;      // widthPx × pixelScale; × hit distance (persp) or absolute (ortho); 0 = off
    simd_float4   outlineColor;      // rgb stroke color
    float         outlineWorldMax;   // conservative world-space width bound for BVH AABB padding
    float         selectionOutlineScale;    // selected widthPx × pixelScale
    float         selectionOutlineWorldMax; // conservative selected outline AABB padding
};

// Unit-cell object overlay uniforms for RT output compositing
struct RTUnitCellUniforms {
    simd_float4x4 viewProjectionMatrix;
    simd_float3   cameraPosition;
    float         atomScale;
    int32_t       atomCount;
    int32_t       bvhNodeCount;
    float         occlusionBias;
    float         unitCellRadius;
    simd_float4   unitCellColor;
    int32_t       bondCount;
    float         bondRadius;
    int32_t       showAtoms;
    int32_t       showBonds;
    int32_t       isPerspective;
    float         cameraForwardX;
    float         cameraForwardY;
    float         cameraForwardZ;
};

// Display pass uniforms — [[buffer(0)]] for display shader
struct DisplayUniforms {
    float sampleCount;
    float _pad[3];
};

} // namespace atom::render::metal
