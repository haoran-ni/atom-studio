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
uniform vec2 uViewportSize;
uniform float uAtomScale;

out vec3 vViewCenter;    // Sphere center in view space
out float vRadius;
out vec4 vColor;
out vec2 vQuadCoord;     // -1 to 1

void main() {
    vColor = aInstanceColor;
    vRadius = aInstancePos.w * uAtomScale;
    vQuadCoord = aPosition.xy;

    // Transform sphere center to view space
    vec4 viewCenter = uViewMatrix * vec4(aInstancePos.xyz, 1.0);
    vViewCenter = viewCenter.xyz;

    // Calculate billboard offset
    // Scale quad by radius, accounting for perspective
    float distance = length(viewCenter.xyz);
    float projScale = uProjectionMatrix[1][1]; // Get FOV scale

    // Expand quad slightly to avoid edge clipping
    vec2 offset = aPosition.xy * vRadius * 1.2;

    // Create view-space position (billboard facing camera)
    vec4 viewPos = viewCenter;
    viewPos.xy += offset;

    gl_Position = uProjectionMatrix * viewPos;
}
)";

const char* sphereFragmentShader = R"(
#version 410 core

in vec3 vViewCenter;
in float vRadius;
in vec4 vColor;
in vec2 vQuadCoord;

uniform mat4 uProjectionMatrix;
uniform vec3 uLightDir;     // Normalized, in view space
uniform float uAmbient;
uniform float uDiffuse;
uniform float uSpecular;
uniform float uShininess;

out vec4 fragColor;

void main() {
    // Ray-sphere intersection in view space
    // Ray origin is at (vViewCenter.xy + vQuadCoord * vRadius, 0) for orthographic
    // For perspective, we trace from the quad position towards the eye

    vec2 d = vQuadCoord * vRadius * 1.2;
    float r2 = vRadius * vRadius;
    float d2 = dot(d, d);

    // Check if inside sphere projection
    if (d2 > r2) {
        discard;
    }

    // Calculate z offset on sphere surface
    float z = sqrt(r2 - d2);

    // Normal in view space (sphere surface normal)
    vec3 normal = normalize(vec3(d.x, d.y, z));

    // View-space position on sphere surface
    vec3 fragViewPos = vViewCenter + vec3(d, -z);

    // Lighting calculation
    vec3 lightDir = normalize(uLightDir);
    vec3 viewDir = normalize(-fragViewPos);

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
    vec4 clipPos = uProjectionMatrix * vec4(fragViewPos, 1.0);
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

uniform vec3 uLightDir;
uniform float uAmbient;
uniform float uDiffuse;
uniform float uSpecular;
uniform float uShininess;

out vec4 fragColor;

void main() {
    vec3 normal = normalize(vNormal);
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
