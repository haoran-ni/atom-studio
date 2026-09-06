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
    fragColor = vec4(result, 1.0);

    // Update depth buffer for correct intersections
    vec4 clipPos = uProjectionMatrix * vec4(hitPos, 1.0);
    float ndcDepth = clipPos.z / clipPos.w;
    gl_FragDepth = (ndcDepth + 1.0) * 0.5;
}
)";

const char* bondVertexShader = R"(
#version 410 core

layout(location = 0) in vec3 aPosition;    // Mesh vertex position (unit radius; z in [0,1])
layout(location = 1) in vec3 aStart;       // Bond start position (instanced)
layout(location = 2) in vec3 aEnd;         // Bond end position (instanced)
layout(location = 3) in vec4 aStartColor;  // Bond start color (instanced)
layout(location = 4) in vec4 aEndColor;    // Bond end color (instanced)
layout(location = 5) in vec3 aRadii;       // Bond start/end atom radii + bond radius (instanced)
layout(location = 6) in vec3 aLocalNormal; // Unit-cylinder local normal

uniform mat4 uViewMatrix;
uniform mat4 uProjectionMatrix;
uniform float uBondRadius;
uniform float uAtomScale;
uniform int uIsPerspective;

flat out vec4 vStartColor;
flat out vec4 vEndColor;
flat out float vSplitT;
out vec3 vViewPos;
out vec3 vNormalView;
out float vAxial;

void main() {
    vStartColor = aStartColor;
    vEndColor = aEndColor;
    vec3 startView = (uViewMatrix * vec4(aStart, 1.0)).xyz;
    vec3 endView = (uViewMatrix * vec4(aEnd, 1.0)).xyz;

    vec3 bondDir = endView - startView;
    float bondLength = length(bondDir);
    vec3 axisDir = (bondLength > 1e-6) ? (bondDir / bondLength) : vec3(0.0, 0.0, 1.0);
    vec3 up = (abs(axisDir.y) < 0.99) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, axisDir));
    up = cross(axisDir, right);

    if (bondLength > 1e-6) {
        float scaledA = aRadii.x * uAtomScale;
        float scaledB = aRadii.y * uAtomScale;
        float splitDistance = 0.5 * (bondLength + scaledA - scaledB);
        vSplitT = clamp(splitDistance / bondLength, 0.0, 1.0);
    } else {
        vSplitT = 0.5;
    }

    float bondRadius = aRadii.z;
    vec3 localPos = right * aPosition.x * bondRadius +
                    up * aPosition.y * bondRadius +
                    axisDir * aPosition.z * bondLength;
    vec3 viewPos = startView + localPos;

    vViewPos = viewPos;
    vNormalView = normalize(right * aLocalNormal.x +
                            up * aLocalNormal.y +
                            axisDir * aLocalNormal.z);
    vAxial = aPosition.z;

    gl_Position = uProjectionMatrix * vec4(viewPos, 1.0);
}
)";

const char* bondFragmentShader = R"(
#version 410 core

flat in vec4 vStartColor;
flat in vec4 vEndColor;
flat in float vSplitT;
in vec3 vViewPos;
in vec3 vNormalView;
in float vAxial;

uniform vec3 uLightDir;
uniform float uAmbient;
uniform float uDiffuse;
uniform float uSpecular;
uniform float uShininess;
uniform int uIsPerspective;

out vec4 fragColor;

void main() {
    vec3 normal = normalize(vNormalView);
    vec4 bondColor = (vAxial < vSplitT) ? vStartColor : vEndColor;

    // Light direction is in view space (camera-relative)
    vec3 lightDir = normalize(uLightDir);
    vec3 viewDir = (uIsPerspective != 0) ? normalize(-vViewPos) : vec3(0.0, 0.0, 1.0);

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
    fragColor = vec4(result, 1.0);
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
