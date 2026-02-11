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
    float         _pad[2];
};

// Per-instance sphere data — [[buffer(2)]] in sphere shader
struct SphereInstance {
    simd_float4 positionAndRadius;    // xyz = world center, w = covalent radius
    simd_float4 color;                // rgba
};

// Per-instance bond data — [[buffer(2)]] in bond shader
struct BondInstance {
    simd_float3 start;
    float       _pad0;
    simd_float3 end;
    float       _pad1;
    simd_float4 color;                // rgba
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
    simd_float3   backgroundColor;
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
    float         _pad[2];
};

// Unit-cell object overlay uniforms for RT output compositing
struct RTUnitCellUniforms {
    simd_float4x4 viewProjectionMatrix;
    simd_float3   cameraPosition;
    float         atomScale;
    int32_t       atomCount;
    float         occlusionBias;
    float         unitCellRadius;
    float         _pad0;
    simd_float4   unitCellColor;
};

// Display pass uniforms — [[buffer(0)]] for display shader
struct DisplayUniforms {
    float sampleCount;
    float _pad[3];
};

} // namespace atom::render::metal
