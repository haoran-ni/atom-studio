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

    // Perspective-correct billboard size.
    // The projected silhouette of a sphere at distance d is:
    //   R_proj = R * d / sqrt(d^2 - R^2)
    // which is always >= R and grows as the sphere gets closer.
    float dist = -viewCenter.z;  // positive distance along view axis
    float R = vRadius;
    float billboardR;
    if (dist > R * 1.01) {
        billboardR = R * dist / sqrt(dist * dist - R * R);
    } else {
        // Sphere very close to or engulfing camera
        billboardR = dist * 100.0;
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

out vec4 fragColor;

void main() {
    // Perspective ray-sphere intersection in view space.
    // Ray from eye (0,0,0) through this fragment's view-space position on the billboard.
    vec3 rayDir = normalize(vViewPosOnQuad);

    // Sphere: |P - C|^2 = R^2,  Ray: P(t) = t * rayDir
    // Expanding: t^2 - 2t(rayDir . C) + |C|^2 - R^2 = 0
    vec3 C = vViewCenter;
    float R = vRadius;

    float b = dot(rayDir, C);
    float c = dot(C, C) - R * R;
    float disc = b * b - c;

    if (disc < 0.0) {
        discard;
    }

    float sqrtDisc = sqrt(disc);
    float t = b - sqrtDisc; // front intersection

    // If t < 0, camera is inside the sphere — use back intersection
    if (t < 0.0) t = b + sqrtDisc;
    if (t < 0.0) discard;

    // Hit position and normal in view space
    vec3 hitPos = t * rayDir;
    vec3 normal = normalize(hitPos - C);

    // Lighting calculation — light direction is in view space (camera-relative)
    vec3 lightDir = normalize(uLightDir);
    vec3 viewDir = normalize(-hitPos);

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

layout(location = 0) in vec3 aPosition;    // Cylinder vertex
layout(location = 1) in vec3 aStart;       // Bond start position (instanced)
layout(location = 2) in vec3 aEnd;         // Bond end position (instanced)
layout(location = 3) in vec4 aColor;       // Bond color (instanced)

uniform mat4 uViewMatrix;
uniform mat4 uProjectionMatrix;
uniform float uBondRadius;

out vec3 vNormal;
out vec3 vViewPos;
out vec4 vColor;

void main() {
    vColor = aColor;

    // Calculate bond direction and length
    vec3 bondDir = aEnd - aStart;
    float bondLength = length(bondDir);
    bondDir = normalize(bondDir);

    // Create orthonormal basis for cylinder
    vec3 up = abs(bondDir.y) < 0.99 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 right = normalize(cross(up, bondDir));
    up = cross(bondDir, right);

    // Transform cylinder vertex
    // aPosition: x,y are in [-1,1] for circle, z is in [0,1] for length
    vec3 localPos = right * aPosition.x * uBondRadius +
                    up * aPosition.y * uBondRadius +
                    bondDir * aPosition.z * bondLength;

    vec3 worldPos = aStart + localPos;
    vec4 viewPos = uViewMatrix * vec4(worldPos, 1.0);
    vViewPos = viewPos.xyz;

    // Transform normal
    vec3 localNormal = normalize(vec3(aPosition.x, aPosition.y, 0.0));
    vec3 worldNormal = right * localNormal.x + up * localNormal.y;
    vNormal = mat3(uViewMatrix) * worldNormal;

    gl_Position = uProjectionMatrix * viewPos;
}
)";

const char* bondFragmentShader = R"(
#version 410 core

in vec3 vNormal;
in vec3 vViewPos;
in vec4 vColor;

uniform mat4 uViewMatrix;
uniform vec3 uLightDir;
uniform float uAmbient;
uniform float uDiffuse;
uniform float uSpecular;
uniform float uShininess;

out vec4 fragColor;

void main() {
    vec3 normal = normalize(vNormal);
    // Light direction is in view space (camera-relative)
    vec3 lightDir = normalize(uLightDir);
    vec3 viewDir = normalize(-vViewPos);

    // Ambient
    vec3 ambient = uAmbient * vColor.rgb;

    // Diffuse
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = uDiffuse * diff * vColor.rgb;

    // Specular
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), uShininess);
    vec3 specular = uSpecular * spec * vec3(1.0);

    vec3 result = ambient + diffuse + specular;
    fragColor = vec4(result, vColor.a);
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
