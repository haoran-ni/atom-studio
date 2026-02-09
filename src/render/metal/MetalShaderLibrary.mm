#import <Metal/Metal.h>
#include "MetalShaderLibrary.h"
#include <QDebug>

namespace atom::render::metal {

// ---------------------------------------------------------------------------
// MSL Shader Sources (embedded as C-strings, compiled at runtime)
// ---------------------------------------------------------------------------

static const char* metalShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

// -------------------------------------------------------
// Shared struct definitions (must match MetalTypes.h)
// -------------------------------------------------------

struct SceneUniforms {
    float4x4 viewMatrix;
    float4x4 projectionMatrix;
    float4x4 viewProjectionMatrix;
    float3   lightDir;
    float    ambient;
    float    diffuse;
    float    specular;
    float    shininess;
    float    atomScale;
    float    bondRadius;
    float    _pad[2];
};

struct SphereInstance {
    float4 positionAndRadius;
    float4 color;
};

struct BondInstance {
    float3 start;
    float  _pad0;
    float3 end;
    float  _pad1;
    float4 color;
};

struct LineVertex {
    float3 position;
    float  _pad0;
    float4 color;
};

struct RTUniforms {
    float4x4 invView;
    float4x4 invProjection;
    float3   cameraPosition;
    float    atomScale;
    float3   lightDir;
    float    ambient;
    float3   backgroundColor;
    float    diffuse;
    float    specular;
    float    shininess;
    int      atomCount;
    int      width;
    int      height;
    uint     frameCount;
    int      enableShadows;
    int      enableAO;
    int      aoSamples;
    float    aoRadius;
    int      maxSamples;
    float    _pad[3];
};

struct DisplayUniforms {
    float sampleCount;
    float _pad[3];
};

struct RTUnitCellUniforms {
    float4x4 viewProjectionMatrix;
    float3   cameraPosition;
    float    atomScale;
    int      atomCount;
    float    occlusionBias;
    float    unitCellRadius;
    float    _pad0;
    float4   unitCellColor;
};

// -------------------------------------------------------
// Sphere Impostor Shader
// -------------------------------------------------------

struct SphereVertexOut {
    float4 position [[position]];
    float3 viewCenter;
    float  radius;
    float4 color;
    float3 viewPosOnQuad;
};

vertex SphereVertexOut sphere_vertex(
    uint vid [[vertex_id]],
    uint iid [[instance_id]],
    constant SceneUniforms& scene [[buffer(0)]],
    constant packed_float3* quadVertices [[buffer(1)]],
    constant SphereInstance* instances [[buffer(2)]])
{
    SphereVertexOut out;

    SphereInstance inst = instances[iid];
    out.color = inst.color;
    out.radius = inst.positionAndRadius.w * scene.atomScale;

    // Transform sphere center to view space
    float4 viewCenter = scene.viewMatrix * float4(inst.positionAndRadius.xyz, 1.0);
    out.viewCenter = viewCenter.xyz;

    // Perspective-correct billboard size
    float dist = -viewCenter.z;
    float R = out.radius;
    float billboardR;
    if (dist > R * 1.01) {
        billboardR = R * dist / sqrt(dist * dist - R * R);
    } else {
        billboardR = dist * 100.0;
    }
    billboardR *= 1.05;

    float3 qv = quadVertices[vid];
    float2 offset = qv.xy * billboardR;

    float4 viewPos = viewCenter;
    viewPos.xy += offset;

    out.viewPosOnQuad = viewPos.xyz;
    out.position = scene.projectionMatrix * viewPos;
    return out;
}

struct SphereFragmentOut {
    float4 color [[color(0)]];
    float  depth [[depth(any)]];
};

fragment SphereFragmentOut sphere_fragment(
    SphereVertexOut in [[stage_in]],
    constant SceneUniforms& scene [[buffer(0)]])
{
    SphereFragmentOut out;

    // Perspective ray-sphere intersection in view space
    float3 rayDir = normalize(in.viewPosOnQuad);
    float3 C = in.viewCenter;
    float R = in.radius;

    float b = dot(rayDir, C);
    float c = dot(C, C) - R * R;
    float disc = b * b - c;

    if (disc < 0.0) discard_fragment();

    float sqrtDisc = sqrt(disc);
    float t = b - sqrtDisc;
    if (t < 0.0) t = b + sqrtDisc;
    if (t < 0.0) discard_fragment();

    float3 hitPos = t * rayDir;
    float3 normal = normalize(hitPos - C);

    // Blinn-Phong lighting
    float3 lightDir = normalize(scene.lightDir);
    float3 viewDir = normalize(-hitPos);

    float3 ambient = scene.ambient * in.color.rgb;
    float diff = max(dot(normal, lightDir), 0.0);
    float3 diffuse = scene.diffuse * diff * in.color.rgb;
    float3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), scene.shininess);
    float3 specular = scene.specular * spec * float3(1.0);

    out.color = float4(ambient + diffuse + specular, in.color.a);

    // Custom depth: Metal NDC depth is [0,1]
    float4 clipPos = scene.projectionMatrix * float4(hitPos, 1.0);
    out.depth = clipPos.z / clipPos.w;
    return out;
}

// -------------------------------------------------------
// Bond Cylinder Shader
// -------------------------------------------------------

struct BondVertexOut {
    float4 position [[position]];
    float3 normal;
    float3 viewPos;
    float4 color;
};

vertex BondVertexOut bond_vertex(
    uint vid [[vertex_id]],
    uint iid [[instance_id]],
    constant SceneUniforms& scene [[buffer(0)]],
    constant packed_float3* cylinderVertices [[buffer(1)]],
    constant BondInstance* instances [[buffer(2)]])
{
    BondVertexOut out;

    BondInstance bond = instances[iid];
    out.color = bond.color;

    float3 bondDir = bond.end - bond.start;
    float bondLength = length(bondDir);
    bondDir = normalize(bondDir);

    // Orthonormal basis
    float3 up = abs(bondDir.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 right = normalize(cross(up, bondDir));
    up = cross(bondDir, right);

    float3 vert = cylinderVertices[vid];
    float3 localPos = right * vert.x * scene.bondRadius +
                      up * vert.y * scene.bondRadius +
                      bondDir * vert.z * bondLength;

    float3 worldPos = bond.start + localPos;
    float4 viewPos = scene.viewMatrix * float4(worldPos, 1.0);
    out.viewPos = viewPos.xyz;

    // Normal
    float3 localNormal = normalize(float3(vert.x, vert.y, 0.0));
    float3 worldNormal = right * localNormal.x + up * localNormal.y;
    out.normal = (scene.viewMatrix * float4(worldNormal, 0.0)).xyz;

    out.position = scene.projectionMatrix * viewPos;
    return out;
}

fragment float4 bond_fragment(
    BondVertexOut in [[stage_in]],
    constant SceneUniforms& scene [[buffer(0)]])
{
    float3 normal = normalize(in.normal);
    float3 lightDir = normalize(scene.lightDir);
    float3 viewDir = normalize(-in.viewPos);

    float3 ambient = scene.ambient * in.color.rgb;
    float diff = max(dot(normal, lightDir), 0.0);
    float3 diffuse = scene.diffuse * diff * in.color.rgb;
    float3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), scene.shininess);
    float3 specular = scene.specular * spec * float3(1.0);

    return float4(ambient + diffuse + specular, in.color.a);
}

// -------------------------------------------------------
// Line Shader (unit cell wireframe)
// -------------------------------------------------------

struct LineVertexOut {
    float4 position [[position]];
    float4 color;
};

vertex LineVertexOut line_vertex(
    uint vid [[vertex_id]],
    constant SceneUniforms& scene [[buffer(0)]],
    constant LineVertex* vertices [[buffer(1)]])
{
    LineVertexOut out;
    LineVertex v = vertices[vid];
    out.color = v.color;
    out.position = scene.viewProjectionMatrix * float4(v.position, 1.0);
    return out;
}

fragment float4 line_fragment(LineVertexOut in [[stage_in]])
{
    return in.color;
}

// -------------------------------------------------------
// RT unit-cell object overlay shaders (cylinders + joints)
// -------------------------------------------------------

struct RTUnitCellVertexOut {
    float4 position [[position]];
    float3 worldPos;
};

vertex RTUnitCellVertexOut rt_unit_cell_cylinder_vertex(
    uint vid [[vertex_id]],
    uint iid [[instance_id]],
    constant RTUnitCellUniforms& unitCell [[buffer(0)]],
    constant packed_float3* vertices [[buffer(1)]],
    constant BondInstance* edges [[buffer(2)]])
{
    RTUnitCellVertexOut out;
    BondInstance edge = edges[iid];

    float3 bondDir = edge.end - edge.start;
    float bondLength = length(bondDir);
    bondDir = (bondLength > 1e-6) ? (bondDir / bondLength) : float3(0.0, 0.0, 1.0);

    float3 up = abs(bondDir.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 right = normalize(cross(up, bondDir));
    up = cross(bondDir, right);

    float3 local = float3(vertices[vid]);
    float3 worldPos = edge.start +
                      right * local.x * unitCell.unitCellRadius +
                      up * local.y * unitCell.unitCellRadius +
                      bondDir * local.z * bondLength;

    out.worldPos = worldPos;
    out.position = unitCell.viewProjectionMatrix * float4(worldPos, 1.0);
    return out;
}

vertex RTUnitCellVertexOut rt_unit_cell_sphere_vertex(
    uint vid [[vertex_id]],
    uint iid [[instance_id]],
    constant RTUnitCellUniforms& unitCell [[buffer(0)]],
    constant packed_float3* vertices [[buffer(1)]],
    constant SphereInstance* joints [[buffer(2)]])
{
    RTUnitCellVertexOut out;
    SphereInstance joint = joints[iid];
    float3 center = joint.positionAndRadius.xyz;
    float3 local = float3(vertices[vid]);
    float3 worldPos = center + local * unitCell.unitCellRadius;

    out.worldPos = worldPos;
    out.position = unitCell.viewProjectionMatrix * float4(worldPos, 1.0);
    return out;
}

fragment float4 rt_unit_cell_fragment(
    RTUnitCellVertexOut in [[stage_in]],
    constant RTUnitCellUniforms& unitCell [[buffer(0)]],
    device const float4* atomPositions [[buffer(1)]])
{
    // Cast a ray from camera to the unit-cell fragment and hide it if an atom
    // is intersected first. This keeps atom-only occlusion while unit cell
    // remains outside RT accumulation.
    float3 ro = unitCell.cameraPosition;
    float3 toPoint = in.worldPos - ro;
    float pointDist = length(toPoint);

    if (unitCell.atomCount > 0 && pointDist > 1e-6) {
        float3 rd = toPoint / pointDist;
        float maxT = max(pointDist - unitCell.occlusionBias, 0.0);

        for (int i = 0; i < unitCell.atomCount; ++i) {
            float4 atom = atomPositions[i];
            float r = atom.w * unitCell.atomScale;

            float3 oc = ro - atom.xyz;
            float b = dot(oc, rd);
            float c = dot(oc, oc) - r * r;
            float disc = b * b - c;
            if (disc < 0.0) continue;

            float sqrtDisc = sqrt(disc);
            float t = -b - sqrtDisc;
            if (t <= 0.001) t = -b + sqrtDisc;
            if (t > 0.001 && t < maxT) discard_fragment();
        }
    }

    return unitCell.unitCellColor;
}

// -------------------------------------------------------
// Fullscreen quad vertex (shared by RT and Display)
// -------------------------------------------------------

struct FullscreenVertexOut {
    float4 position [[position]];
    float2 texCoord;
};

vertex FullscreenVertexOut fullscreen_vertex(
    uint vid [[vertex_id]],
    constant packed_float2* quadVertices [[buffer(0)]])
{
    FullscreenVertexOut out;
    float2 pos = quadVertices[vid];
    out.texCoord = pos * 0.5 + 0.5;
    out.position = float4(pos, 0.0, 1.0);
    return out;
}

// -------------------------------------------------------
// Ray Tracing Fragment Shader
// -------------------------------------------------------

// PCG random number generator
uint pcg(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float rand01(thread uint& rng_state) {
    rng_state = pcg(rng_state);
    return float(rng_state) / 4294967296.0;
}

float intersectSphere(float3 ro, float3 rd, float3 center, float radius) {
    float3 oc = ro - center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0) return -1.0;
    float sqrtDisc = sqrt(disc);
    float t = -b - sqrtDisc;
    if (t > 0.001) return t;
    t = -b + sqrtDisc;
    if (t > 0.001) return t;
    return -1.0;
}

void traceClosest(float3 ro, float3 rd,
                  device const float4* atomPositions,
                  int atomCount, float atomScale,
                  thread float& hitT, thread int& hitIndex) {
    hitT = 1e30;
    hitIndex = -1;
    for (int i = 0; i < atomCount; i++) {
        float4 atom = atomPositions[i];
        float r = atom.w * atomScale;
        float t = intersectSphere(ro, rd, atom.xyz, r);
        if (t > 0.0 && t < hitT) {
            hitT = t;
            hitIndex = i;
        }
    }
}

bool traceAnyHit(float3 ro, float3 rd, float maxDist,
                 device const float4* atomPositions,
                 int atomCount, float atomScale) {
    for (int i = 0; i < atomCount; i++) {
        float4 atom = atomPositions[i];
        float r = atom.w * atomScale;
        float3 oc = ro - atom.xyz;
        float b = dot(oc, rd);
        float c = dot(oc, oc) - r * r;
        float disc = b * b - c;
        if (disc >= 0.0) {
            float sqrtDisc = sqrt(disc);
            float t = -b - sqrtDisc;
            if (t > 0.001 && t < maxDist) return true;
            t = -b + sqrtDisc;
            if (t > 0.001 && t < maxDist) return true;
        }
    }
    return false;
}

float3 cosineWeightedHemisphere(float3 normal, thread uint& rng_state) {
    float r1 = rand01(rng_state);
    float r2 = rand01(rng_state);
    float cosTheta = sqrt(r1);
    float sinTheta = sqrt(1.0 - r1);
    float phi = 6.28318530718 * r2;

    float3 tangent;
    if (abs(normal.y) < 0.9)
        tangent = normalize(cross(normal, float3(0.0, 1.0, 0.0)));
    else
        tangent = normalize(cross(normal, float3(1.0, 0.0, 0.0)));
    float3 bitangent = cross(normal, tangent);

    return normalize(
        tangent   * (sinTheta * cos(phi)) +
        bitangent * (sinTheta * sin(phi)) +
        normal    * cosTheta
    );
}

fragment float4 rt_fragment(
    FullscreenVertexOut in [[stage_in]],
    constant RTUniforms& rt [[buffer(0)]],
    device const float4* atomPositions [[buffer(1)]],
    device const float4* atomColors [[buffer(2)]])
{
    // Initialize RNG
    uint rng_state = pcg(
        uint(in.position.x) +
        uint(in.position.y) * uint(rt.width) +
        rt.frameCount * uint(rt.width) * uint(rt.height)
    );

    // Sub-pixel jitter
    float2 jitter = float2(rand01(rng_state), rand01(rng_state)) - 0.5;
    float2 uv = (in.position.xy + jitter) / float2(float(rt.width), float(rt.height));

    // Generate camera ray
    float4 ndc = float4(uv * 2.0 - 1.0, -1.0, 1.0);
    float4 viewTarget = rt.invProjection * ndc;
    viewTarget.xyz /= viewTarget.w;
    float3 rayDir = normalize((rt.invView * float4(viewTarget.xyz, 0.0)).xyz);
    float3 rayOrigin = rt.cameraPosition;

    // Trace primary ray
    float hitT;
    int hitIndex;
    traceClosest(rayOrigin, rayDir, atomPositions,
                 rt.atomCount, rt.atomScale, hitT, hitIndex);

    if (hitIndex < 0) {
        return float4(rt.backgroundColor, 1.0);
    }

    // Shading
    float4 atomData = atomPositions[hitIndex];
    float4 atomColor = atomColors[hitIndex];
    float atomRadius = atomData.w * rt.atomScale;

    float3 hitPos = rayOrigin + rayDir * hitT;
    float3 normal = normalize(hitPos - atomData.xyz);

    float3 lightDir = normalize(rt.lightDir);
    float3 viewDir = normalize(rt.cameraPosition - hitPos);

    float NdotL = max(dot(normal, lightDir), 0.0);
    float3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), rt.shininess);

    float3 ambient  = rt.ambient  * atomColor.rgb;
    float3 diffuse  = rt.diffuse  * NdotL * atomColor.rgb;
    float3 specular = rt.specular * spec * float3(1.0);

    float bias = max(atomRadius * 0.01, 0.05);
    float3 biasedOrigin = hitPos + normal * bias;

    // Shadow
    float shadow = 1.0;
    if (rt.enableShadows) {
        if (traceAnyHit(biasedOrigin, lightDir, 10000.0,
                        atomPositions, rt.atomCount, rt.atomScale)) {
            shadow = 0.0;
        }
    }

    // Ambient occlusion
    float ao = 1.0;
    if (rt.enableAO && rt.aoSamples > 0) {
        float occluded = 0.0;
        for (int i = 0; i < rt.aoSamples; i++) {
            float3 aoDir = cosineWeightedHemisphere(normal, rng_state);
            if (traceAnyHit(biasedOrigin, aoDir, rt.aoRadius,
                            atomPositions, rt.atomCount, rt.atomScale)) {
                occluded += 1.0;
            }
        }
        ao = 1.0 - occluded / float(rt.aoSamples);
    }

    float3 result = ambient * ao + (diffuse + specular) * shadow;
    return float4(result, 1.0);
}

// -------------------------------------------------------
// Display pass (divide accumulation by sample count)
// -------------------------------------------------------

fragment float4 display_fragment(
    FullscreenVertexOut in [[stage_in]],
    texture2d<float> accumTexture [[texture(0)]],
    constant DisplayUniforms& disp [[buffer(0)]])
{
    constexpr sampler nearestSampler(mag_filter::nearest, min_filter::nearest);
    float4 accum = accumTexture.sample(nearestSampler, in.texCoord);
    float3 color = accum.rgb / max(disp.sampleCount, 1.0);
    return float4(color, 1.0);
}
)";

// ---------------------------------------------------------------------------
// PIMPL implementation
// ---------------------------------------------------------------------------

struct MetalShaderLibrary::Impl {
    id<MTLDevice> device = nil;
    id<MTLLibrary> library = nil;
    id<MTLRenderPipelineState> spherePipeline = nil;
    id<MTLRenderPipelineState> bondPipeline = nil;
    id<MTLRenderPipelineState> linePipeline = nil;
    id<MTLRenderPipelineState> rtPipeline = nil;
    id<MTLRenderPipelineState> displayPipeline = nil;
    id<MTLRenderPipelineState> rtUnitCellCylinderPipeline = nil;
    id<MTLRenderPipelineState> rtUnitCellSpherePipeline = nil;
    id<MTLDepthStencilState> depthLessWriteState = nil;
    id<MTLDepthStencilState> depthDisabledState = nil;
};

MetalShaderLibrary::MetalShaderLibrary()
    : m_impl(std::make_unique<Impl>())
{
}

MetalShaderLibrary::~MetalShaderLibrary() {
    cleanup();
}

bool MetalShaderLibrary::initialize(void* device) {
    if (m_initialized) return true;

    m_impl->device = (__bridge id<MTLDevice>)device;
    if (!m_impl->device) {
        qCritical() << "MetalShaderLibrary: null device";
        return false;
    }

    // Compile all shaders from source
    NSError* error = nil;
    NSString* source = [NSString stringWithUTF8String:metalShaderSource];
    MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
    options.languageVersion = MTLLanguageVersion2_4;

    m_impl->library = [m_impl->device newLibraryWithSource:source
                                                   options:options
                                                     error:&error];
    if (!m_impl->library) {
        qCritical() << "MetalShaderLibrary: MSL compilation failed:"
                     << error.localizedDescription.UTF8String;
        return false;
    }

    qInfo() << "MetalShaderLibrary: MSL compilation succeeded";

    // --- Create depth stencil states ---

    MTLDepthStencilDescriptor* depthDesc = [[MTLDepthStencilDescriptor alloc] init];
    depthDesc.depthCompareFunction = MTLCompareFunctionLess;
    depthDesc.depthWriteEnabled = YES;
    m_impl->depthLessWriteState = [m_impl->device newDepthStencilStateWithDescriptor:depthDesc];

    depthDesc.depthCompareFunction = MTLCompareFunctionAlways;
    depthDesc.depthWriteEnabled = NO;
    m_impl->depthDisabledState = [m_impl->device newDepthStencilStateWithDescriptor:depthDesc];

    // --- Sphere pipeline ---
    {
        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = [m_impl->library newFunctionWithName:@"sphere_vertex"];
        desc.fragmentFunction = [m_impl->library newFunctionWithName:@"sphere_fragment"];
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        if (!desc.vertexFunction || !desc.fragmentFunction) {
            qCritical() << "MetalShaderLibrary: sphere shader functions not found";
            return false;
        }

        m_impl->spherePipeline = [m_impl->device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!m_impl->spherePipeline) {
            qCritical() << "MetalShaderLibrary: sphere pipeline failed:"
                         << error.localizedDescription.UTF8String;
            return false;
        }
    }

    // --- Bond pipeline ---
    {
        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = [m_impl->library newFunctionWithName:@"bond_vertex"];
        desc.fragmentFunction = [m_impl->library newFunctionWithName:@"bond_fragment"];
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        if (!desc.vertexFunction || !desc.fragmentFunction) {
            qCritical() << "MetalShaderLibrary: bond shader functions not found";
            return false;
        }

        m_impl->bondPipeline = [m_impl->device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!m_impl->bondPipeline) {
            qCritical() << "MetalShaderLibrary: bond pipeline failed:"
                         << error.localizedDescription.UTF8String;
            return false;
        }
    }

    // --- Line pipeline ---
    {
        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = [m_impl->library newFunctionWithName:@"line_vertex"];
        desc.fragmentFunction = [m_impl->library newFunctionWithName:@"line_fragment"];
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        if (!desc.vertexFunction || !desc.fragmentFunction) {
            qCritical() << "MetalShaderLibrary: line shader functions not found";
            return false;
        }

        m_impl->linePipeline = [m_impl->device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!m_impl->linePipeline) {
            qCritical() << "MetalShaderLibrary: line pipeline failed:"
                         << error.localizedDescription.UTF8String;
            return false;
        }
    }

    // --- RT pipeline (additive blending, RGBA32Float, no depth) ---
    {
        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = [m_impl->library newFunctionWithName:@"fullscreen_vertex"];
        desc.fragmentFunction = [m_impl->library newFunctionWithName:@"rt_fragment"];
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Float;
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;

        if (!desc.vertexFunction || !desc.fragmentFunction) {
            qCritical() << "MetalShaderLibrary: RT shader functions not found";
            return false;
        }

        m_impl->rtPipeline = [m_impl->device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!m_impl->rtPipeline) {
            qCritical() << "MetalShaderLibrary: RT pipeline failed:"
                         << error.localizedDescription.UTF8String;
            return false;
        }
    }

    // --- Display pipeline (no blending, BGRA8, no depth) ---
    {
        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = [m_impl->library newFunctionWithName:@"fullscreen_vertex"];
        desc.fragmentFunction = [m_impl->library newFunctionWithName:@"display_fragment"];
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.colorAttachments[0].blendingEnabled = NO;

        if (!desc.vertexFunction || !desc.fragmentFunction) {
            qCritical() << "MetalShaderLibrary: display shader functions not found";
            return false;
        }

        m_impl->displayPipeline = [m_impl->device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!m_impl->displayPipeline) {
            qCritical() << "MetalShaderLibrary: display pipeline failed:"
                         << error.localizedDescription.UTF8String;
            return false;
        }
    }

    // --- RT unit-cell cylinder overlay pipeline (alpha blend, BGRA8, no depth) ---
    {
        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = [m_impl->library newFunctionWithName:@"rt_unit_cell_cylinder_vertex"];
        desc.fragmentFunction = [m_impl->library newFunctionWithName:@"rt_unit_cell_fragment"];
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

        if (!desc.vertexFunction || !desc.fragmentFunction) {
            qCritical() << "MetalShaderLibrary: RT unit-cell cylinder shader functions not found";
            return false;
        }

        m_impl->rtUnitCellCylinderPipeline = [m_impl->device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!m_impl->rtUnitCellCylinderPipeline) {
            qCritical() << "MetalShaderLibrary: RT unit-cell cylinder pipeline failed:"
                         << error.localizedDescription.UTF8String;
            return false;
        }
    }

    // --- RT unit-cell sphere overlay pipeline (alpha blend, BGRA8, no depth) ---
    {
        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = [m_impl->library newFunctionWithName:@"rt_unit_cell_sphere_vertex"];
        desc.fragmentFunction = [m_impl->library newFunctionWithName:@"rt_unit_cell_fragment"];
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

        if (!desc.vertexFunction || !desc.fragmentFunction) {
            qCritical() << "MetalShaderLibrary: RT unit-cell sphere shader functions not found";
            return false;
        }

        m_impl->rtUnitCellSpherePipeline = [m_impl->device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!m_impl->rtUnitCellSpherePipeline) {
            qCritical() << "MetalShaderLibrary: RT unit-cell sphere pipeline failed:"
                         << error.localizedDescription.UTF8String;
            return false;
        }
    }

    m_initialized = true;
    qInfo() << "MetalShaderLibrary: All pipelines created successfully";
    return true;
}

void MetalShaderLibrary::cleanup() {
    if (m_impl) {
        m_impl->spherePipeline = nil;
        m_impl->bondPipeline = nil;
        m_impl->linePipeline = nil;
        m_impl->rtPipeline = nil;
        m_impl->displayPipeline = nil;
        m_impl->rtUnitCellCylinderPipeline = nil;
        m_impl->rtUnitCellSpherePipeline = nil;
        m_impl->depthLessWriteState = nil;
        m_impl->depthDisabledState = nil;
        m_impl->library = nil;
        m_impl->device = nil;
    }
    m_initialized = false;
}

void* MetalShaderLibrary::spherePipeline() const {
    return (__bridge void*)m_impl->spherePipeline;
}

void* MetalShaderLibrary::bondPipeline() const {
    return (__bridge void*)m_impl->bondPipeline;
}

void* MetalShaderLibrary::linePipeline() const {
    return (__bridge void*)m_impl->linePipeline;
}

void* MetalShaderLibrary::rtPipeline() const {
    return (__bridge void*)m_impl->rtPipeline;
}

void* MetalShaderLibrary::displayPipeline() const {
    return (__bridge void*)m_impl->displayPipeline;
}

void* MetalShaderLibrary::rtUnitCellCylinderPipeline() const {
    return (__bridge void*)m_impl->rtUnitCellCylinderPipeline;
}

void* MetalShaderLibrary::rtUnitCellSpherePipeline() const {
    return (__bridge void*)m_impl->rtUnitCellSpherePipeline;
}

void* MetalShaderLibrary::depthLessWriteState() const {
    return (__bridge void*)m_impl->depthLessWriteState;
}

void* MetalShaderLibrary::depthDisabledState() const {
    return (__bridge void*)m_impl->depthDisabledState;
}

} // namespace atom::render::metal
