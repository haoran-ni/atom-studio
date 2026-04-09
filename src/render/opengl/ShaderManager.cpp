#include "ShaderManager.h"
#include <QDebug>

namespace atom::render {

// Sphere impostor shader - renders spheres as billboard quads with ray-sphere intersection
namespace shaders {

const char* sphereVertexShader = R"(
#version 410 core

layout(location = 0) in vec3 aPosition;  // Quad vertex position (-1,-1), (1,-1), (1,1), (-1,1)
layout(location = 1) in vec4 aInstancePos; // xyz = center, w = radius
layout(location = 2) in vec4 aInstanceColor;

uniform mat4 uViewMatrix;
uniform mat4 uProjectionMatrix;
uniform float uAtomScale;
uniform int uIsPerspective;

out vec3 vViewCenter;    // Sphere center in view space
out float vRadius;
out vec4 vColor;
out vec3 vViewPosOnQuad; // View-space position of this quad vertex

void main() {
    vColor = aInstanceColor;
    vRadius = aInstancePos.w * uAtomScale;

    // Transform sphere center to view space
    vec4 viewCenter = uViewMatrix * vec4(aInstancePos.xyz, 1.0);
    vViewCenter = viewCenter.xyz;

    // Billboard size
    float R = vRadius;
    float billboardR;
    if (uIsPerspective != 0) {
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
    billboardR *= 1.05; // small margin for numerical safety

    vec2 offset = aPosition.xy * billboardR;

    // Create view-space position (billboard facing camera)
    vec4 viewPos = viewCenter;
    viewPos.xy += offset;

    vViewPosOnQuad = viewPos.xyz;
    gl_Position = uProjectionMatrix * viewPos;
}
)";

const char* sphereFragmentShader = R"(
#version 410 core

in vec3 vViewCenter;
in float vRadius;
in vec4 vColor;
in vec3 vViewPosOnQuad; // Interpolated view-space position on billboard

uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;
uniform vec3 uLightDir;     // World space, transformed to view space below
uniform float uAmbient;
uniform float uDiffuse;
uniform float uSpecular;
uniform float uShininess;
uniform int uIsPerspective;

out vec4 fragColor;

void main() {
    vec3 C = vViewCenter;
    float R = vRadius;
    vec3 hitPos;
    vec3 normal;

    if (uIsPerspective != 0) {
        // Perspective ray-sphere intersection in view space
        vec3 rayDir = normalize(vViewPosOnQuad);

        float b = dot(rayDir, C);
        float c = dot(C, C) - R * R;
        float disc = b * b - c;

        if (disc < 0.0) discard;

        float sqrtDisc = sqrt(disc);
        float t = b - sqrtDisc;
        if (t < 0.0) t = b + sqrtDisc;
        if (t < 0.0) discard;

        hitPos = t * rayDir;
    } else {
        // Orthographic ray-sphere intersection in view space
        // Ray: origin = (quad.x, quad.y, 0), direction = (0, 0, -1)
        float dx = vViewPosOnQuad.x - C.x;
        float dy = vViewPosOnQuad.y - C.y;
        float disc = R * R - dx * dx - dy * dy;

        if (disc < 0.0) discard;

        float sqrtDisc = sqrt(disc);
        // Front hit z = C.z + sqrtDisc (closest to camera, i.e. largest z)
        hitPos = vec3(vViewPosOnQuad.xy, C.z + sqrtDisc);
    }

    normal = normalize(hitPos - C);

    // Lighting calculation — light direction is in view space (camera-relative)
    vec3 lightDir = normalize(uLightDir);
    vec3 viewDir = (uIsPerspective != 0) ? normalize(-hitPos) : vec3(0.0, 0.0, 1.0);

    // Ambient
    vec3 ambient = uAmbient * vColor.rgb;

    // Diffuse
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = uDiffuse * diff * vColor.rgb;

    // Specular (Blinn-Phong)
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), uShininess);
    vec3 specular = uSpecular * spec * vec3(1.0);

    vec3 result = ambient + diffuse + specular;
    fragColor = vec4(result, vColor.a);

    // Update depth buffer for correct intersections
    vec4 clipPos = uProjectionMatrix * vec4(hitPos, 1.0);
    float ndcDepth = clipPos.z / clipPos.w;
    gl_FragDepth = (ndcDepth + 1.0) * 0.5;
}
)";

const char* bondVertexShader = R"(
#version 410 core

layout(location = 0) in vec3 aPosition;    // Quad vertex position
layout(location = 1) in vec3 aStart;       // Bond start position (instanced)
layout(location = 2) in vec3 aEnd;         // Bond end position (instanced)
layout(location = 3) in vec4 aStartColor;  // Bond start color (instanced)
layout(location = 4) in vec4 aEndColor;    // Bond end color (instanced)
layout(location = 5) in vec2 aRadii;       // Bond start/end radii (instanced)

uniform mat4 uViewMatrix;
uniform mat4 uProjectionMatrix;
uniform float uBondRadius;
uniform float uAtomScale;
uniform int uIsPerspective;

flat out vec3 vStartView;
flat out vec3 vEndView;
flat out vec4 vStartColor;
flat out vec4 vEndColor;
flat out float vSplitT;
out vec3 vViewPosOnQuad;

void main() {
    vStartColor = aStartColor;
    vEndColor = aEndColor;
    vStartView = (uViewMatrix * vec4(aStart, 1.0)).xyz;
    vEndView = (uViewMatrix * vec4(aEnd, 1.0)).xyz;

    vec3 bondDir = vEndView - vStartView;
    float bondLength = length(bondDir);
    if (bondLength > 1e-6) {
        float scaledA = aRadii.x * uAtomScale;
        float scaledB = aRadii.y * uAtomScale;
        float splitDistance = 0.5 * (bondLength + scaledA - scaledB);
        vSplitT = clamp(splitDistance / bondLength, 0.0, 1.0);
    } else {
        vSplitT = 0.5;
    }

    vec3 center = 0.5 * (vStartView + vEndView);
    float halfLength = 0.5 * bondLength;
    float boundRadius = sqrt(halfLength * halfLength + uBondRadius * uBondRadius);

    float billboardR;
    if (uIsPerspective != 0) {
        float dist = -center.z;
        if (dist > boundRadius * 1.01) {
            billboardR = boundRadius * dist / sqrt(dist * dist - boundRadius * boundRadius);
        } else {
            billboardR = max(abs(dist), boundRadius) * 100.0;
        }
    } else {
        billboardR = boundRadius;
    }
    billboardR *= 1.05;

    vec4 viewPos = vec4(center, 1.0);
    viewPos.xy += aPosition.xy * billboardR;
    vViewPosOnQuad = viewPos.xyz;

    gl_Position = uProjectionMatrix * viewPos;
}
)";

const char* bondFragmentShader = R"(
#version 410 core

flat in vec3 vStartView;
flat in vec3 vEndView;
flat in vec4 vStartColor;
flat in vec4 vEndColor;
flat in float vSplitT;
in vec3 vViewPosOnQuad;

uniform mat4 uProjectionMatrix;
uniform vec3 uLightDir;
uniform float uAmbient;
uniform float uDiffuse;
uniform float uSpecular;
uniform float uShininess;
uniform float uBondRadius;
uniform int uIsPerspective;

out vec4 fragColor;

const int CYL_HIT_NONE = 0;
const int CYL_HIT_SIDE = 1;
const int CYL_HIT_START_CAP = 2;
const int CYL_HIT_END_CAP = 3;

struct CylinderHit {
    float t;
    float axial;
    int kind;
};

CylinderHit intersectCappedCylinderDetailed(vec3 ro, vec3 rd, vec3 pa, vec3 pb, float radius) {
    CylinderHit result;
    result.t = -1.0;
    result.axial = 0.0;
    result.kind = CYL_HIT_NONE;

    vec3 ba = pb - pa;
    float baba = dot(ba, ba);
    if (baba < 1e-8) return result;

    vec3 oc = ro - pa;
    float bard = dot(ba, rd);
    float baoc = dot(ba, oc);

    vec3 rdPerp = rd - (bard / baba) * ba;
    vec3 ocPerp = oc - (baoc / baba) * ba;
    float a = dot(rdPerp, rdPerp);
    if (a > 1e-8) {
        float hb = dot(ocPerp, rdPerp);
        vec3 q = ocPerp - (hb / a) * rdPerp;
        float disc = a * (radius * radius - dot(q, q));
        if (disc >= 0.0) {
            float sqrtDisc = sqrt(disc);

            float t0 = (-hb - sqrtDisc) / a;
            float y0 = baoc + t0 * bard;
            if (t0 > 0.001 && y0 > 0.0 && y0 < baba) {
                result.t = t0;
                result.axial = clamp(y0 / baba, 0.0, 1.0);
                result.kind = CYL_HIT_SIDE;
            }

            float t1 = (-hb + sqrtDisc) / a;
            float y1 = baoc + t1 * bard;
            if (t1 > 0.001 && y1 > 0.0 && y1 < baba &&
                (result.t < 0.0 || t1 < result.t)) {
                result.t = t1;
                result.axial = clamp(y1 / baba, 0.0, 1.0);
                result.kind = CYL_HIT_SIDE;
            }
        }
    }

    float axisLen = sqrt(baba);
    vec3 axis = ba / axisLen;
    float axisDenom = dot(rd, axis);
    if (abs(axisDenom) > 1e-8) {
        float tCap0 = dot(pa - ro, axis) / axisDenom;
        if (tCap0 > 0.001) {
            vec3 hit = ro + rd * tCap0 - pa;
            vec3 radial = hit - axis * dot(hit, axis);
            if (dot(radial, radial) <= radius * radius &&
                (result.t < 0.0 || tCap0 < result.t)) {
                result.t = tCap0;
                result.axial = 0.0;
                result.kind = CYL_HIT_START_CAP;
            }
        }

        float tCap1 = dot(pb - ro, axis) / axisDenom;
        if (tCap1 > 0.001) {
            vec3 hit = ro + rd * tCap1 - pb;
            vec3 radial = hit - axis * dot(hit, axis);
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

void main() {
    vec3 ro;
    vec3 rd;
    if (uIsPerspective != 0) {
        ro = vec3(0.0);
        rd = normalize(vViewPosOnQuad);
    } else {
        ro = vec3(vViewPosOnQuad.xy, 0.0);
        rd = vec3(0.0, 0.0, -1.0);
    }

    CylinderHit hit = intersectCappedCylinderDetailed(ro, rd, vStartView, vEndView, uBondRadius);
    if (hit.t < 0.0) discard;

    vec3 hitPos = ro + rd * hit.t;
    vec3 ba = vEndView - vStartView;
    float baLen2 = dot(ba, ba);
    if (baLen2 < 1e-8) discard;

    float bondLength = sqrt(baLen2);
    vec3 bondDir = ba / bondLength;
    vec3 normal;
    float h = hit.axial;
    if (hit.kind == CYL_HIT_START_CAP) {
        normal = -bondDir;
    } else if (hit.kind == CYL_HIT_END_CAP) {
        normal = bondDir;
    } else {
        float axial = dot(hitPos - vStartView, bondDir);
        h = clamp(axial / bondLength, 0.0, 1.0);
        normal = normalize(hitPos - (vStartView + bondDir * axial));
    }

    vec4 bondColor = (h < vSplitT) ? vStartColor : vEndColor;
    // Light direction is in view space (camera-relative)
    vec3 lightDir = normalize(uLightDir);
    vec3 viewDir = (uIsPerspective != 0) ? normalize(-hitPos) : vec3(0.0, 0.0, 1.0);

    // Ambient
    vec3 ambient = uAmbient * bondColor.rgb;

    // Diffuse
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = uDiffuse * diff * bondColor.rgb;

    // Specular
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), uShininess);
    vec3 specular = uSpecular * spec * vec3(1.0);

    vec3 result = ambient + diffuse + specular;
    fragColor = vec4(result, bondColor.a);

    vec4 clipPos = uProjectionMatrix * vec4(hitPos, 1.0);
    float ndcDepth = clipPos.z / clipPos.w;
    gl_FragDepth = (ndcDepth + 1.0) * 0.5;
}
)";

const char* lineVertexShader = R"(
#version 410 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;

uniform mat4 uViewProjectionMatrix;

out vec4 vColor;

void main() {
    vColor = aColor;
    gl_Position = uViewProjectionMatrix * vec4(aPosition, 1.0);
}
)";

const char* lineFragmentShader = R"(
#version 410 core

in vec4 vColor;
out vec4 fragColor;

void main() {
    fragColor = vColor;
}
)";

const char* viewportAxesVertexShader = R"(
#version 410 core

layout(location = 0) in vec3 aPosition;    // Mesh vertex (unit radius; z in [0,1])
layout(location = 1) in vec3 aStart;       // Instance start point
layout(location = 2) in vec3 aEnd;         // Instance end point
layout(location = 3) in vec4 aColor;       // Instance color

uniform mat4 uViewMatrix;
uniform mat4 uProjectionMatrix;
uniform float uBondRadius;

out vec4 vColor;

void main() {
    vColor = aColor;

    vec3 axisVec = aEnd - aStart;
    float axisLength = length(axisVec);
    vec3 axisDir = (axisLength > 1e-8) ? (axisVec / axisLength) : vec3(0.0, 0.0, 1.0);

    vec3 up = abs(axisDir.y) < 0.99 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 right = normalize(cross(up, axisDir));
    up = cross(axisDir, right);

    vec3 localPos = right * aPosition.x * uBondRadius +
                    up * aPosition.y * uBondRadius +
                    axisDir * aPosition.z * axisLength;

    vec3 worldPos = aStart + localPos;
    gl_Position = uProjectionMatrix * (uViewMatrix * vec4(worldPos, 1.0));
}
)";

const char* viewportAxesFragmentShader = R"(
#version 410 core

in vec4 vColor;
out vec4 fragColor;

void main() {
    fragColor = vColor;
}
)";

} // namespace shaders

ShaderManager::ShaderManager() = default;

ShaderManager::~ShaderManager() {
    cleanup();
}

bool ShaderManager::initialize() {
    if (m_initialized) return true;

    m_sphereShader = std::make_unique<QOpenGLShaderProgram>();
    m_bondShader = std::make_unique<QOpenGLShaderProgram>();
    m_lineShader = std::make_unique<QOpenGLShaderProgram>();
    m_viewportAxesShader = std::make_unique<QOpenGLShaderProgram>();

    bool success = true;

    success &= compileShader(m_sphereShader.get(),
                             shaders::sphereVertexShader,
                             shaders::sphereFragmentShader);

    success &= compileShader(m_bondShader.get(),
                             shaders::bondVertexShader,
                             shaders::bondFragmentShader);

    success &= compileShader(m_lineShader.get(),
                             shaders::lineVertexShader,
                             shaders::lineFragmentShader);

    success &= compileShader(m_viewportAxesShader.get(),
                             shaders::viewportAxesVertexShader,
                             shaders::viewportAxesFragmentShader);

    if (!success) {
        cleanup();
        return false;
    }

    m_initialized = true;
    return true;
}

void ShaderManager::cleanup() {
    m_sphereShader.reset();
    m_bondShader.reset();
    m_lineShader.reset();
    m_viewportAxesShader.reset();
    m_initialized = false;
}

bool ShaderManager::compileShader(QOpenGLShaderProgram* program,
                                   const QString& vertexSource,
                                   const QString& fragmentSource) {
    if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexSource)) {
        qCritical() << "Vertex shader compilation failed:" << program->log();
        return false;
    }

    if (!program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentSource)) {
        qCritical() << "Fragment shader compilation failed:" << program->log();
        return false;
    }

    if (!program->link()) {
        qCritical() << "Shader program linking failed:" << program->log();
        return false;
    }

    return true;
}

} // namespace atom::render
