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
    int      isPerspective;
    float    _pad;
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
    float4 startColor;
    float4 endColor;
    float  startRadius;
    float  endRadius;
    float  bondRadius;
    float  _pad2;
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
    float4   backgroundColor;
    float    diffuse;
    float    specular;
    float    shininess;
    int      atomCount;
    int      width;
    int      height;
    uint     frameCount;
    int      enableShadows;
    float    shadowOpacity;
    int      enableAO;
    int      aoSamples;
    float    aoRadius;
    int      maxSamples;
    int      bvhNodeCount;
    int      bondCount;
    float    bondRadius;
    int      showAtoms;
    int      showBonds;
    int      isPerspective;
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
    int      bvhNodeCount;
    float    occlusionBias;
    float    unitCellRadius;
    float4   unitCellColor;
    int      bondCount;
    float    bondRadius;
    int      showAtoms;
    int      showBonds;
    int      isPerspective;
    float    cameraForwardX;
    float    cameraForwardY;
    float    cameraForwardZ;
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

    // Billboard size
    float R = out.radius;
    float billboardR;
    if (scene.isPerspective) {
        // Perspective-correct: projected silhouette grows as sphere nears camera
        float dist = -viewCenter.z;
        if (dist > R * 1.01) {
            billboardR = R * dist / sqrt(dist * dist - R * R);
        } else {
            billboardR = dist * 100.0;
        }
    } else {
        // Orthographic: constant billboard size (no foreshortening)
        billboardR = R;
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
    if (in.color.a <= 0.001f) discard_fragment();

    float3 C = in.viewCenter;
    float R = in.radius;
    float3 hitPos;
    float3 normal;

    if (scene.isPerspective) {
        // Perspective ray-sphere intersection in view space
        float3 rayDir = normalize(in.viewPosOnQuad);

        float b = dot(rayDir, C);
        float c = dot(C, C) - R * R;
        float disc = b * b - c;

        if (disc < 0.0) discard_fragment();

        float sqrtDisc = sqrt(disc);
        float t = b - sqrtDisc;
        if (t < 0.0) t = b + sqrtDisc;
        if (t < 0.0) discard_fragment();

        hitPos = t * rayDir;
    } else {
        // Orthographic ray-sphere intersection in view space
        // Ray: origin = (quad.x, quad.y, 0), direction = (0, 0, -1)
        float dx = in.viewPosOnQuad.x - C.x;
        float dy = in.viewPosOnQuad.y - C.y;
        float disc = R * R - dx * dx - dy * dy;

        if (disc < 0.0) discard_fragment();

        float sqrtDisc = sqrt(disc);
        // Front hit z = C.z + sqrtDisc (closest to camera, i.e. largest z).
        // If that surface is behind the camera (hz > 0), fall back to the back
        // surface — this guards against the camera entering the sphere volume.
        float hz = C.z + sqrtDisc;
        if (hz > 0.0) hz = C.z - sqrtDisc;
        hitPos = float3(in.viewPosOnQuad.xy, hz);
    }

    normal = normalize(hitPos - C);

    // Blinn-Phong lighting — light direction is in view space (camera-relative)
    float3 lightDir = normalize(scene.lightDir);
    float3 viewDir = scene.isPerspective ? normalize(-hitPos) : float3(0.0, 0.0, 1.0);

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
// Bond Mesh Shader
// -------------------------------------------------------

struct BondMeshVertex {
    packed_float3 position;
    packed_float3 normal;
};

struct BondVertexOut {
    float4 position [[position]];
    float3 viewPos;
    float3 normalView;
    float4 startColor;
    float4 endColor;
    float  axial;
    float  splitT;
};

vertex BondVertexOut bond_vertex(
    uint vid [[vertex_id]],
    uint iid [[instance_id]],
    constant SceneUniforms& scene [[buffer(0)]],
    constant BondMeshVertex* cylinderVertices [[buffer(1)]],
    constant BondInstance* instances [[buffer(2)]])
{
    BondVertexOut out;

    BondInstance bond = instances[iid];
    out.startColor = bond.startColor;
    out.endColor = bond.endColor;

    float4 startView4 = scene.viewMatrix * float4(bond.start, 1.0f);
    float4 endView4 = scene.viewMatrix * float4(bond.end, 1.0f);
    float3 startView = startView4.xyz;
    float3 endView = endView4.xyz;

    float3 bondDir = endView - startView;
    float bondLength = length(bondDir);
    float3 axisDir = (bondLength > 1e-6f) ? (bondDir / bondLength) : float3(0.0f, 0.0f, 1.0f);
    float3 up = fabs(axisDir.y) < 0.99f ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 right = normalize(cross(up, axisDir));
    up = cross(axisDir, right);

    if (bondLength > 1e-6f) {
        float scaledA = bond.startRadius * scene.atomScale;
        float scaledB = bond.endRadius * scene.atomScale;
        float splitDistance = 0.5f * (bondLength + scaledA - scaledB);
        out.splitT = clamp(splitDistance / bondLength, 0.0f, 1.0f);
    } else {
        out.splitT = 0.5f;
    }

    BondMeshVertex vert = cylinderVertices[vid];
    float bondRadius = bond.bondRadius > 0.0f ? bond.bondRadius : scene.bondRadius;
    float3 localPos = right * vert.position.x * bondRadius +
                      up * vert.position.y * bondRadius +
                      axisDir * vert.position.z * bondLength;
    out.viewPos = startView + localPos;

    float3 localNormal = float3(vert.normal);
    out.normalView = normalize(right * localNormal.x +
                               up * localNormal.y +
                               axisDir * localNormal.z);
    out.axial = vert.position.z;
    out.position = scene.projectionMatrix * float4(out.viewPos, 1.0f);
    return out;
}

fragment float4 bond_fragment(
    BondVertexOut in [[stage_in]],
    constant SceneUniforms& scene [[buffer(0)]])
{
    float3 normal = normalize(in.normalView);
    float4 bondColor = (in.axial < in.splitT) ? in.startColor : in.endColor;
    if (bondColor.a <= 0.001f) discard_fragment();

    // Light direction is in view space (camera-relative)
    float3 lightDir = normalize(scene.lightDir);
    float3 viewDir = scene.isPerspective ? normalize(-in.viewPos) : float3(0.0f, 0.0f, 1.0f);

    float3 ambient = scene.ambient * bondColor.rgb;
    float diff = max(dot(normal, lightDir), 0.0);
    float3 diffuse = scene.diffuse * diff * bondColor.rgb;
    float3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), scene.shininess);
    float3 specular = scene.specular * spec * float3(1.0);

    return float4(ambient + diffuse + specular, bondColor.a);
}

// -------------------------------------------------------
// Solid Cylinder Mesh Shader (flat color; unit cell / gizmo)
// -------------------------------------------------------

struct SolidCylinderVertexOut {
    float4 position [[position]];
    float4 color;
};

struct SolidCylinderMeshVertex {
    packed_float3 position;
    packed_float3 normal;
};

vertex SolidCylinderVertexOut solid_cylinder_vertex(
    uint vid [[vertex_id]],
    uint iid [[instance_id]],
    constant SceneUniforms& scene [[buffer(0)]],
    constant SolidCylinderMeshVertex* cylinderVertices [[buffer(1)]],
    constant BondInstance* instances [[buffer(2)]])
{
    SolidCylinderVertexOut out;

    BondInstance inst = instances[iid];
    out.color = inst.startColor;

    float3 axisVec = inst.end - inst.start;
    float axisLength = length(axisVec);
    float3 axisDir = (axisLength > 1e-8f) ? (axisVec / axisLength) : float3(0.0f, 0.0f, 1.0f);

    float3 up = abs(axisDir.y) < 0.99f ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 right = normalize(cross(up, axisDir));
    up = cross(axisDir, right);

    SolidCylinderMeshVertex vert = cylinderVertices[vid];
    float3 localPos = right * vert.position.x * scene.bondRadius +
                      up * vert.position.y * scene.bondRadius +
                      axisDir * vert.position.z * axisLength;

    float3 worldPos = inst.start + localPos;
    out.position = scene.projectionMatrix * (scene.viewMatrix * float4(worldPos, 1.0f));
    return out;
}

fragment float4 solid_cylinder_fragment(
    SolidCylinderVertexOut in [[stage_in]])
{
    return in.color;
}

// -------------------------------------------------------
// Viewport Axes Overlay Mesh Shader (unlit flat color)
// -------------------------------------------------------

struct AxesOverlayVertexOut {
    float4 position [[position]];
    float4 color;
};

vertex AxesOverlayVertexOut axes_overlay_vertex(
    uint vid [[vertex_id]],
    uint iid [[instance_id]],
    constant SceneUniforms& scene [[buffer(0)]],
    constant packed_float3* meshVertices [[buffer(1)]],
    constant BondInstance* instances [[buffer(2)]])
{
    AxesOverlayVertexOut out;

    BondInstance inst = instances[iid];
    out.color = inst.startColor;

    float3 axisVec = inst.end - inst.start;
    float axisLength = length(axisVec);
    float3 axisDir = (axisLength > 1e-8f) ? (axisVec / axisLength) : float3(0.0f, 0.0f, 1.0f);

    float3 up = abs(axisDir.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 right = normalize(cross(up, axisDir));
    up = cross(axisDir, right);

    float3 vert = meshVertices[vid];
    float3 localPos = right * vert.x * scene.bondRadius +
                      up * vert.y * scene.bondRadius +
                      axisDir * vert.z * axisLength;

    float3 worldPos = inst.start + localPos;
    out.position = scene.projectionMatrix * (scene.viewMatrix * float4(worldPos, 1.0));
    return out;
}

fragment float4 axes_overlay_fragment(
    AxesOverlayVertexOut in [[stage_in]])
{
    return in.color;
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

bool traceAnyHit(float3 ro, float3 rd, float maxDist,
                 device const float4* atomPositions,
                 int atomCount, float atomScale, bool showAtoms,
                 device const float4* bondStartPositions,
                 device const float4* bondEndPositions,
                 int bondCount, float bondRadius, bool showBonds,
                 device const float4* bvhNodeMinData,
                 device const float4* bvhNodeMaxData,
                 device const uint4* bvhNodeMeta,
                 device const uint* bvhPrimIndices,
                 int bvhNodeCount);

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
    device const float4* atomPositions [[buffer(1)]],
    device const float4* bvhNodeMinData [[buffer(3)]],
    device const float4* bvhNodeMaxData [[buffer(4)]],
    device const uint4* bvhNodeMeta [[buffer(5)]],
    device const uint* bvhPrimIndices [[buffer(6)]],
    device const float4* bondStartPositions [[buffer(7)]],
    device const float4* bondEndPositions [[buffer(8)]])
{
    // Cast a ray from camera toward the unit-cell fragment and hide it if any
    // RT scene primitive is intersected first. The unit cell remains outside
    // RT accumulation, but its visibility still follows the scene depth.
    if ((((unitCell.showAtoms != 0) && unitCell.atomCount > 0) ||
         ((unitCell.showBonds != 0) && unitCell.bondCount > 0)) &&
        unitCell.bvhNodeCount > 0) {
        float3 ro, rd;
        float maxT;

        if (unitCell.isPerspective) {
            // Perspective: ray from camera position to fragment
            ro = unitCell.cameraPosition;
            float3 toPoint = in.worldPos - ro;
            float pointDist = length(toPoint);
            if (pointDist < 1e-6) return unitCell.unitCellColor;
            rd = toPoint / pointDist;
            maxT = max(pointDist - unitCell.occlusionBias, 0.0);
        } else {
            // Orthographic: parallel ray from fragment toward camera
            float3 cameraFwd = float3(unitCell.cameraForwardX,
                                      unitCell.cameraForwardY,
                                      unitCell.cameraForwardZ);
            ro = in.worldPos - cameraFwd * unitCell.occlusionBias;
            rd = -cameraFwd;
            maxT = max(dot(in.worldPos - unitCell.cameraPosition, cameraFwd)
                       - unitCell.occlusionBias, 0.0);
        }

        if (maxT > 0.0 &&
            traceAnyHit(ro, rd, maxT,
                        atomPositions, unitCell.atomCount, unitCell.atomScale, unitCell.showAtoms != 0,
                        bondStartPositions, bondEndPositions,
                        unitCell.bondCount, unitCell.bondRadius, unitCell.showBonds != 0,
                        bvhNodeMinData, bvhNodeMaxData, bvhNodeMeta, bvhPrimIndices,
                        unitCell.bvhNodeCount)) {
            discard_fragment();
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

constant int CYL_HIT_NONE = 0;
constant int CYL_HIT_SIDE = 1;
constant int CYL_HIT_START_CAP = 2;
constant int CYL_HIT_END_CAP = 3;

struct CylinderHit {
    float t;
    float axial;
    int kind;
};

CylinderHit intersectCappedCylinderDetailed(float3 ro, float3 rd, float3 pa, float3 pb, float radius) {
    CylinderHit result;
    result.t = -1.0;
    result.axial = 0.0;
    result.kind = CYL_HIT_NONE;

    float3 ba = pb - pa;
    float baba = dot(ba, ba);
    if (baba < 1e-8) return result;

    float3 oc = ro - pa;
    float bard = dot(ba, rd);
    float baoc = dot(ba, oc);

    // Stable side-wall intersection: avoid catastrophic cancellation in
    // orthographic view where |oc| can be much larger than the bond radius.
    float3 rd_perp = rd - (bard / baba) * ba;
    float3 oc_perp = oc - (baoc / baba) * ba;

    float a = dot(rd_perp, rd_perp);
    if (a > 1e-8) {
        float hb = dot(oc_perp, rd_perp);
        float3 q = oc_perp - (hb / a) * rd_perp;
        float disc = a * (radius * radius - dot(q, q));
        if (disc >= 0.0) {
            float sqrtDisc = sqrt(disc);

            float t0 = (-hb - sqrtDisc) / a;
            float y0 = baoc + t0 * bard;
            if (t0 > 0.0 && y0 > 0.0 && y0 < baba) {
                result.t = t0;
                result.axial = clamp(y0 / baba, 0.0f, 1.0f);
                result.kind = CYL_HIT_SIDE;
            }

            float t1 = (-hb + sqrtDisc) / a;
            float y1 = baoc + t1 * bard;
            if (t1 > 0.0 && y1 > 0.0 && y1 < baba &&
                (result.t < 0.0 || t1 < result.t)) {
                result.t = t1;
                result.axial = clamp(y1 / baba, 0.0f, 1.0f);
                result.kind = CYL_HIT_SIDE;
            }
        }
    }

    float axisLen = sqrt(baba);
    float3 axis = ba / axisLen;
    float axisDenom = dot(rd, axis);
    if (abs(axisDenom) > 1e-8) {
        float tCap0 = dot(pa - ro, axis) / axisDenom;
        if (tCap0 > 0.0) {
            float3 hit = ro + rd * tCap0 - pa;
            float3 radial = hit - axis * dot(hit, axis);
            if (dot(radial, radial) <= radius * radius &&
                (result.t < 0.0 || tCap0 < result.t)) {
                result.t = tCap0;
                result.axial = 0.0;
                result.kind = CYL_HIT_START_CAP;
            }
        }

        float tCap1 = dot(pb - ro, axis) / axisDenom;
        if (tCap1 > 0.0) {
            float3 hit = ro + rd * tCap1 - pb;
            float3 radial = hit - axis * dot(hit, axis);
            if (dot(radial, radial) <= radius * radius &&
                (result.t < 0.0 || tCap1 < result.t)) {
                result.t = tCap1;
                result.axial = 1.0;
                result.kind = CYL_HIT_END_CAP;
            }
        }
    }

    return result;
}

float intersectCylinder(float3 ro, float3 rd, float3 pa, float3 pb, float radius) {
    CylinderHit hit = intersectCappedCylinderDetailed(ro, rd, pa, pb, radius);
    return hit.t;
}

float intersectSphere(float3 ro, float3 rd, float3 center, float radius) {
    float3 oc = ro - center;
    float b = dot(oc, rd);
    // Stable: disc = r² − |oc ⊥ rd|²  avoids cancellation when |oc| >> radius
    // (ortho camera sits at m_distance >> r; b² ≈ c ≈ m_distance², diff ≈ r²)
    float3 q = oc - b * rd;
    float disc = radius * radius - dot(q, q);
    if (disc < 0.0) return -1.0;
    float sqrtDisc = sqrt(disc);
    float t = -b - sqrtDisc;
    if (t > 0.0) return t;
    t = -b + sqrtDisc;
    if (t > 0.0) return t;
    return -1.0;
}

constant uint BVH_INVALID_INDEX = 0xffffffffu;
constant int BVH_STACK_SIZE = 64;

bool intersectAABB(float3 ro, float3 invRd, float3 bmin, float3 bmax, float tMax,
                   thread float& tNearOut) {
    float3 t0 = (bmin - ro) * invRd;
    float3 t1 = (bmax - ro) * invRd;
    float3 tMin = min(t0, t1);
    float3 tMax3 = max(t0, t1);

    float tNear = max(max(tMin.x, tMin.y), max(tMin.z, 0.0));
    float tFar = min(min(tMax3.x, tMax3.y), tMax3.z);
    tNearOut = tNear;
    return (tFar >= tNear) && (tNear <= tMax);
}

// Test a BVH node's AABB only (fetches min/max, not meta).
bool testNodeAABB(int nodeIndex,
                  float3 ro,
                  float3 invRd,
                  float tMax,
                  float atomScale,
                  device const float4* bvhNodeMinData,
                  device const float4* bvhNodeMaxData,
                  thread float& tNearOut) {
    float4 minData = bvhNodeMinData[nodeIndex];
    float4 maxData = bvhNodeMaxData[nodeIndex];

    // BVH is built with base radii. Expand node AABBs conservatively when
    // runtime atom scale is larger than 1 to avoid misses.
    float expansion = max(atomScale - 1.0, 0.0) * minData.w;
    float3 bmin = minData.xyz - float3(expansion);
    float3 bmax = maxData.xyz + float3(expansion);
    return intersectAABB(ro, invRd, bmin, bmax, tMax, tNearOut);
}

float primitiveAlpha(uint primIndex,
                     device const float4* atomColors,
                     int atomCount,
                     device const float4* bondStartColors,
                     device const float4* bondEndColors,
                     int bondCount) {
    if (primIndex < uint(atomCount)) {
        return atomColors[primIndex].a;
    }

    int bondIdx = int(primIndex) - atomCount;
    if (bondIdx < 0 || bondIdx >= bondCount) return 0.0f;
    return max(bondStartColors[bondIdx].a, bondEndColors[bondIdx].a);
}

void traceClosest(float3 ro, float3 rd,
                  device const float4* atomPositions,
                  device const float4* atomColors,
                  int atomCount, float atomScale, bool showAtoms,
                  device const float4* bondStartPositions,
                  device const float4* bondEndPositions,
                  device const float4* bondStartColors,
                  device const float4* bondEndColors,
                  device const float* bondRadii,
                  int bondCount, bool showBonds,
                  device const float4* bvhNodeMinData,
                  device const float4* bvhNodeMaxData,
                  device const uint4* bvhNodeMeta,
                  device const uint* bvhPrimIndices,
                  int bvhNodeCount,
                  int skipIndex,
                  thread float& hitT, thread int& hitIndex) {
    hitT = 1e30;
    hitIndex = -1;
    if (bvhNodeCount <= 0) return;

    int totalPrims = atomCount + bondCount;
    float3 invRd = 1.0 / rd;
    int stack[BVH_STACK_SIZE];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        int nodeIndex = stack[--sp];
        uint4 meta = bvhNodeMeta[nodeIndex];

        uint primCount = meta.w;
        if (primCount > 0) {
            uint first = meta.z;
            for (uint i = 0; i < primCount; ++i) {
                uint primIndex = bvhPrimIndices[first + i];
                if (primIndex >= uint(totalPrims)) continue;
                if (int(primIndex) == skipIndex) continue;
                if (primitiveAlpha(primIndex, atomColors, atomCount,
                                   bondStartColors, bondEndColors, bondCount) <= 0.001f) continue;

                if (primIndex < uint(atomCount)) {
                    if (!showAtoms) continue;
                    float4 atom = atomPositions[primIndex];
                    float r = atom.w * atomScale;
                    float t = intersectSphere(ro, rd, atom.xyz, r);
                    if (t > 0.0 && t < hitT) {
                        hitT = t;
                        hitIndex = int(primIndex);
                    }
                } else if (showBonds) {
                    int bondIdx = int(primIndex) - atomCount;
                    float3 pa = bondStartPositions[bondIdx].xyz;
                    float3 pb = bondEndPositions[bondIdx].xyz;
                    float bondRadius = bondRadii[bondIdx];
                    float t = intersectCylinder(ro, rd, pa, pb, bondRadius);
                    if (t > 0.0 && t < hitT) {
                        hitT = t;
                        hitIndex = int(primIndex);
                    }
                }
            }
            continue;
        }

        uint left = meta.x;
        uint right = meta.y;
        bool hitLeft = false;
        bool hitRight = false;
        float leftNear = 0.0;
        float rightNear = 0.0;

        if (left != BVH_INVALID_INDEX) {
            hitLeft = testNodeAABB(int(left), ro, invRd, hitT, atomScale,
                                   bvhNodeMinData, bvhNodeMaxData, leftNear);
        }
        if (right != BVH_INVALID_INDEX) {
            hitRight = testNodeAABB(int(right), ro, invRd, hitT, atomScale,
                                    bvhNodeMinData, bvhNodeMaxData, rightNear);
        }

        if (hitLeft && hitRight) {
            uint nearChild = (leftNear < rightNear) ? left : right;
            uint farChild = (leftNear < rightNear) ? right : left;
            if (sp < BVH_STACK_SIZE) stack[sp++] = int(farChild);
            if (sp < BVH_STACK_SIZE) stack[sp++] = int(nearChild);
        } else if (hitLeft) {
            if (sp < BVH_STACK_SIZE) stack[sp++] = int(left);
        } else if (hitRight) {
            if (sp < BVH_STACK_SIZE) stack[sp++] = int(right);
        }
    }
}

float traceOcclusionAlpha(float3 ro, float3 rd, float maxDist,
                          device const float4* atomPositions,
                          device const float4* atomColors,
                          int atomCount, float atomScale, bool showAtoms,
                          device const float4* bondStartPositions,
                          device const float4* bondEndPositions,
                          device const float4* bondStartColors,
                          device const float4* bondEndColors,
                          device const float* bondRadii,
                          int bondCount, bool showBonds,
                          device const float4* bvhNodeMinData,
                          device const float4* bvhNodeMaxData,
                          device const uint4* bvhNodeMeta,
                          device const uint* bvhPrimIndices,
                          int bvhNodeCount) {
    if (bvhNodeCount <= 0) return 0.0f;

    int totalPrims = atomCount + bondCount;
    float3 invRd = 1.0 / rd;
    int stack[BVH_STACK_SIZE];
    int sp = 0;
    stack[sp++] = 0;
    float occlusionAlpha = 0.0f;

    while (sp > 0) {
        int nodeIndex = stack[--sp];
        uint4 meta = bvhNodeMeta[nodeIndex];

        uint primCount = meta.w;
        if (primCount > 0) {
            uint first = meta.z;
            for (uint i = 0; i < primCount; ++i) {
                uint primIndex = bvhPrimIndices[first + i];
                if (primIndex >= uint(totalPrims)) continue;
                float alpha = primitiveAlpha(primIndex, atomColors, atomCount,
                                             bondStartColors, bondEndColors, bondCount);
                if (alpha <= 0.001f) continue;

                if (primIndex < uint(atomCount)) {
                    if (!showAtoms) continue;
                    float4 atom = atomPositions[primIndex];
                    float r = atom.w * atomScale;
                    float3 oc = ro - atom.xyz;
                    float b = dot(oc, rd);
                    float c = dot(oc, oc) - r * r;
                    float disc = b * b - c;
                    if (disc >= 0.0) {
                        float sqrtDisc = sqrt(disc);
                        float t = -b - sqrtDisc;
                        if (t > 0.001 && t < maxDist) {
                            occlusionAlpha = max(occlusionAlpha, alpha);
                            if (occlusionAlpha >= 0.999f) return 1.0f;
                        }
                        t = -b + sqrtDisc;
                        if (t > 0.001 && t < maxDist) {
                            occlusionAlpha = max(occlusionAlpha, alpha);
                            if (occlusionAlpha >= 0.999f) return 1.0f;
                        }
                    }
                } else if (showBonds) {
                    int bondIdx = int(primIndex) - atomCount;
                    float3 pa = bondStartPositions[bondIdx].xyz;
                    float3 pb = bondEndPositions[bondIdx].xyz;
                    float bondRadius = bondRadii[bondIdx];
                    float t = intersectCylinder(ro, rd, pa, pb, bondRadius);
                    if (t > 0.001 && t < maxDist) {
                        occlusionAlpha = max(occlusionAlpha, alpha);
                        if (occlusionAlpha >= 0.999f) return 1.0f;
                    }
                }
            }
            continue;
        }

        uint left = meta.x;
        uint right = meta.y;
        float tNear;
        if (left != BVH_INVALID_INDEX && sp < BVH_STACK_SIZE) {
            if (testNodeAABB(int(left), ro, invRd, maxDist, atomScale,
                             bvhNodeMinData, bvhNodeMaxData, tNear))
                stack[sp++] = int(left);
        }
        if (right != BVH_INVALID_INDEX && sp < BVH_STACK_SIZE) {
            if (testNodeAABB(int(right), ro, invRd, maxDist, atomScale,
                             bvhNodeMinData, bvhNodeMaxData, tNear))
                stack[sp++] = int(right);
        }
    }

    return occlusionAlpha;
}

bool traceAnyHit(float3 ro, float3 rd, float maxDist,
                 device const float4* atomPositions,
                 int atomCount, float atomScale, bool showAtoms,
                 device const float4* bondStartPositions,
                 device const float4* bondEndPositions,
                 int bondCount, float bondRadius, bool showBonds,
                 device const float4* bvhNodeMinData,
                 device const float4* bvhNodeMaxData,
                 device const uint4* bvhNodeMeta,
                 device const uint* bvhPrimIndices,
                 int bvhNodeCount) {
    if (bvhNodeCount <= 0) return false;

    int totalPrims = atomCount + bondCount;
    float3 invRd = 1.0 / rd;
    int stack[BVH_STACK_SIZE];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        int nodeIndex = stack[--sp];
        uint4 meta = bvhNodeMeta[nodeIndex];

        uint primCount = meta.w;
        if (primCount > 0) {
            uint first = meta.z;
            for (uint i = 0; i < primCount; ++i) {
                uint primIndex = bvhPrimIndices[first + i];
                if (primIndex >= uint(totalPrims)) continue;

                if (primIndex < uint(atomCount)) {
                    if (!showAtoms) continue;
                    float4 atom = atomPositions[primIndex];
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
                } else if (showBonds) {
                    int bondIdx = int(primIndex) - atomCount;
                    float3 pa = bondStartPositions[bondIdx].xyz;
                    float3 pb = bondEndPositions[bondIdx].xyz;
                    float t = intersectCylinder(ro, rd, pa, pb, bondRadius);
                    if (t > 0.001 && t < maxDist) return true;
                }
            }
            continue;
        }

        uint left = meta.x;
        uint right = meta.y;
        float tNear;
        if (left != BVH_INVALID_INDEX && sp < BVH_STACK_SIZE) {
            if (testNodeAABB(int(left), ro, invRd, maxDist, atomScale,
                             bvhNodeMinData, bvhNodeMaxData, tNear))
                stack[sp++] = int(left);
        }
        if (right != BVH_INVALID_INDEX && sp < BVH_STACK_SIZE) {
            if (testNodeAABB(int(right), ro, invRd, maxDist, atomScale,
                             bvhNodeMinData, bvhNodeMaxData, tNear))
                stack[sp++] = int(right);
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
    device const float4* atomColors [[buffer(2)]],
    device const float4* bvhNodeMinData [[buffer(3)]],
    device const float4* bvhNodeMaxData [[buffer(4)]],
    device const uint4* bvhNodeMeta [[buffer(5)]],
    device const uint* bvhPrimIndices [[buffer(6)]],
    device const float4* bondStartPositions [[buffer(7)]],
    device const float4* bondEndPositions [[buffer(8)]],
    device const float4* bondStartColors [[buffer(9)]],
    device const float4* bondEndColors [[buffer(10)]],
    device const float* bondRadii [[buffer(11)]])
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

    bool showBonds = rt.showBonds != 0;
    bool showAtoms = rt.showAtoms != 0;

    // Trace primary ray
    float hitT;
    int hitIndex;
    traceClosest(rayOrigin, rayDir, atomPositions, atomColors,
                 rt.atomCount, rt.atomScale, showAtoms,
                 bondStartPositions, bondEndPositions,
                 bondStartColors, bondEndColors,
                 bondRadii, rt.bondCount, showBonds,
                 bvhNodeMinData, bvhNodeMaxData, bvhNodeMeta, bvhPrimIndices,
                 rt.bvhNodeCount, -1, hitT, hitIndex);

    if (hitIndex < 0) {
        return float4(rt.backgroundColor.rgb * rt.backgroundColor.a, rt.backgroundColor.a);
    }

    // Shading
    float3 hitPos = rayOrigin + rayDir * hitT;
    float3 normal;
    float3 surfaceColor;
    float surfaceAlpha;
    float biasRadius;

    if (hitIndex < rt.atomCount) {
        // Sphere hit
        float4 atomData = atomPositions[hitIndex];
        float4 atomColor = atomColors[hitIndex];
        normal = normalize(hitPos - atomData.xyz);
        surfaceColor = atomColor.rgb;
        surfaceAlpha = atomColor.a;
        biasRadius = atomData.w * rt.atomScale;
    } else {
        // Cylinder hit
        int bondIdx = hitIndex - rt.atomCount;
        float4 startColor = bondStartColors[bondIdx];
        float4 endColor = bondEndColors[bondIdx];
        float4 startPos = bondStartPositions[bondIdx];
        float4 endPos = bondEndPositions[bondIdx];
        float bondRadius = bondRadii[bondIdx];
        float3 pa = startPos.xyz;
        float3 pb = endPos.xyz;
        float3 ba = pb - pa;
        float baLen2 = dot(ba, ba);
        float bondLength = (baLen2 > 1e-8f) ? sqrt(baLen2) : 0.0f;
        float3 bondDir = (bondLength > 1e-8f) ? (ba / bondLength) : float3(0.0f, 0.0f, 1.0f);
        CylinderHit bondHit = intersectCappedCylinderDetailed(rayOrigin, rayDir, pa, pb, bondRadius);
        float h = bondHit.axial;
        if (bondHit.kind == CYL_HIT_START_CAP) {
            normal = -bondDir;
        } else if (bondHit.kind == CYL_HIT_END_CAP) {
            normal = bondDir;
        } else {
            float axial = dot(hitPos - pa, bondDir);
            h = (bondLength > 1e-8f) ? clamp(axial / bondLength, 0.0f, 1.0f) : h;
            normal = normalize(hitPos - (pa + bondDir * axial));
        }
        float splitT = 0.5f;
        if (bondLength > 1e-8f) {
            float scaledA = startPos.w * rt.atomScale;
            float scaledB = endPos.w * rt.atomScale;
            float splitDistance = 0.5f * (bondLength + scaledA - scaledB);
            splitT = clamp(splitDistance / bondLength, 0.0f, 1.0f);
        }
        float4 bondColor = h < splitT ? startColor : endColor;
        surfaceColor = bondColor.rgb;
        surfaceAlpha = bondColor.a;
        biasRadius = bondRadius;
    }

    float3 lightDir = normalize(rt.lightDir);
    float3 viewDir = -rayDir;

    float NdotL = max(dot(normal, lightDir), 0.0);
    float3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), rt.shininess);

    float3 ambient  = rt.ambient  * surfaceColor;
    float3 diffuse  = rt.diffuse  * NdotL * surfaceColor;
    float3 specular = rt.specular * spec * float3(1.0);

    float bias = max(biasRadius * 0.01, 0.05);
    float3 biasedOrigin = hitPos + normal * bias;

    // Shadow
    float shadow = 1.0;
    if (rt.enableShadows) {
        float shadowAlpha = traceOcclusionAlpha(
            biasedOrigin, lightDir, 10000.0,
            atomPositions, atomColors, rt.atomCount, rt.atomScale, showAtoms,
            bondStartPositions, bondEndPositions, bondStartColors, bondEndColors,
            bondRadii, rt.bondCount, showBonds,
            bvhNodeMinData, bvhNodeMaxData, bvhNodeMeta, bvhPrimIndices,
            rt.bvhNodeCount);
        shadow = 1.0 - clamp(rt.shadowOpacity, 0.0f, 1.0f) * shadowAlpha;
    }

    // Ambient occlusion
    float ao = 1.0;
    if (rt.enableAO && rt.aoSamples > 0) {
        float occluded = 0.0;
        for (int i = 0; i < rt.aoSamples; i++) {
            float3 aoDir = cosineWeightedHemisphere(normal, rng_state);
            occluded += traceOcclusionAlpha(
                biasedOrigin, aoDir, rt.aoRadius,
                atomPositions, atomColors, rt.atomCount, rt.atomScale, showAtoms,
                bondStartPositions, bondEndPositions, bondStartColors, bondEndColors,
                bondRadii, rt.bondCount, showBonds,
                bvhNodeMinData, bvhNodeMaxData, bvhNodeMeta, bvhPrimIndices,
                rt.bvhNodeCount);
        }
        ao = 1.0 - occluded / float(rt.aoSamples);
    }

    float3 result = ambient * ao + (diffuse + specular) * shadow;
    float alpha = clamp(surfaceAlpha, 0.0f, 1.0f);
    float3 background = rt.backgroundColor.rgb * rt.backgroundColor.a;
    return float4(background * (1.0f - alpha) + result * alpha, 1.0);
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
    float4 out_color = accum / max(disp.sampleCount, 1.0);
    return out_color;
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
    id<MTLRenderPipelineState> solidCylinderPipeline = nil;
    id<MTLRenderPipelineState> viewportAxesPipeline = nil;
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

bool MetalShaderLibrary::initialize(void* device, int rasterSampleCount) {
    if (m_initialized) return true;

    m_impl->device = (__bridge id<MTLDevice>)device;
    if (!m_impl->device) {
        qCritical() << "MetalShaderLibrary: null device";
        return false;
    }

    const NSUInteger rasterSamples =
        (rasterSampleCount > 1) ? static_cast<NSUInteger>(rasterSampleCount) : 1u;

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
        desc.rasterSampleCount = rasterSamples;
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
        desc.rasterSampleCount = rasterSamples;
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

    // --- Solid cylinder pipeline (flat color mesh; unit cell / gizmo) ---
    {
        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = [m_impl->library newFunctionWithName:@"solid_cylinder_vertex"];
        desc.fragmentFunction = [m_impl->library newFunctionWithName:@"solid_cylinder_fragment"];
        desc.rasterSampleCount = rasterSamples;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        if (!desc.vertexFunction || !desc.fragmentFunction) {
            qCritical() << "MetalShaderLibrary: solid cylinder shader functions not found";
            return false;
        }

        m_impl->solidCylinderPipeline =
            [m_impl->device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!m_impl->solidCylinderPipeline) {
            qCritical() << "MetalShaderLibrary: solid cylinder pipeline failed:"
                         << error.localizedDescription.UTF8String;
            return false;
        }
    }

    // --- Viewport axes overlay mesh pipeline (flat color, depth-enabled overlay) ---
    {
        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = [m_impl->library newFunctionWithName:@"axes_overlay_vertex"];
        desc.fragmentFunction = [m_impl->library newFunctionWithName:@"axes_overlay_fragment"];
        desc.rasterSampleCount = rasterSamples;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        if (!desc.vertexFunction || !desc.fragmentFunction) {
            qCritical() << "MetalShaderLibrary: viewport axes shader functions not found";
            return false;
        }

        m_impl->viewportAxesPipeline =
            [m_impl->device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!m_impl->viewportAxesPipeline) {
            qCritical() << "MetalShaderLibrary: viewport axes pipeline failed:"
                         << error.localizedDescription.UTF8String;
            return false;
        }
    }

    // --- Line pipeline ---
    {
        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = [m_impl->library newFunctionWithName:@"line_vertex"];
        desc.fragmentFunction = [m_impl->library newFunctionWithName:@"line_fragment"];
        desc.rasterSampleCount = rasterSamples;
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
        desc.rasterSampleCount = rasterSamples;
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
        desc.rasterSampleCount = rasterSamples;
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
        desc.rasterSampleCount = rasterSamples;
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
        m_impl->solidCylinderPipeline = nil;
        m_impl->viewportAxesPipeline = nil;
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

void* MetalShaderLibrary::solidCylinderPipeline() const {
    return (__bridge void*)m_impl->solidCylinderPipeline;
}

void* MetalShaderLibrary::viewportAxesPipeline() const {
    return (__bridge void*)m_impl->viewportAxesPipeline;
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
