#include "RayTracingRenderer.h"
#include "../common/Camera.h"
#include "../common/BVH.h"
#include "../common/RenderStateHash.h"
#include "../../data/Structure.h"
#include "../../data/BondList.h"
#include <QDebug>
#include <cassert>

namespace atom::render {

// ---------------------------------------------------------------------------
// Shader sources
// ---------------------------------------------------------------------------

namespace rt_shaders {

const char* quadVertexShader = R"(
#version 410 core

layout(location = 0) in vec2 aPosition;

out vec2 vTexCoord;

void main() {
    vTexCoord = aPosition * 0.5 + 0.5;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

const char* rayTraceFragmentShader = R"(
#version 410 core

in vec2 vTexCoord;
out vec4 fragColor;

// Camera
uniform mat4 uInvView;
uniform mat4 uInvProjection;
uniform vec3 uCameraPos;

// Viewport
uniform int uWidth;
uniform int uHeight;

// Atom data (texture buffer objects)
uniform samplerBuffer uAtomPositions;   // vec4(x, y, z, radius)
uniform samplerBuffer uAtomColors;      // vec4(r, g, b, a)
uniform samplerBuffer uBVHNodeMinData;  // vec4(min.xyz, maxRadius)
uniform samplerBuffer uBVHNodeMaxData;  // vec4(max.xyz, pad)
uniform usamplerBuffer uBVHNodeMeta;    // uvec4(left, right, first, count)
uniform usamplerBuffer uBVHPrimIndices; // uint primitive index (x component)
uniform int uAtomCount;
uniform int uBVHNodeCount;
uniform float uAtomScale;

// Lighting
uniform vec3 uLightDir;
uniform float uAmbient;
uniform float uDiffuse;
uniform float uSpecular;
uniform float uShininess;
uniform vec3 uBackgroundColor;

// Progressive rendering
uniform uint uFrameCount;

// Bond data (texture buffer objects)
uniform samplerBuffer uBondStartPositions; // vec4(x, y, z, 0)
uniform samplerBuffer uBondEndPositions;   // vec4(x, y, z, 0)
uniform samplerBuffer uBondColors;         // vec4(r, g, b, a)
uniform int uBondCount;
uniform float uBondRadius;
uniform bool uShowBonds;

// Feature toggles
uniform bool uEnableShadows;
uniform bool uEnableAO;
uniform float uShadowOpacity;
uniform int uAOSamples;
uniform float uAORadius;
uniform int uIsPerspective;

// ---------- PCG random number generator ----------

const uint BVH_INVALID_INDEX = 0xffffffffu;
const int BVH_STACK_SIZE = 64;

uint rng_state;

uint pcg(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float rand01() {
    rng_state = pcg(rng_state);
    return float(rng_state) / 4294967296.0;
}

// ---------- Ray-sphere intersection ----------

// Returns t of closest hit, or -1.0 if no hit
float intersectSphere(vec3 ro, vec3 rd, vec3 center, float radius) {
    vec3 oc = ro - center;
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

// ---------- Ray-cylinder intersection ----------

// Finite cylinder from pa to pb with given radius. Returns t or -1.
float intersectCylinder(vec3 ro, vec3 rd, vec3 pa, vec3 pb, float radius) {
    vec3 ba = pb - pa;
    float baba = dot(ba, ba);
    if (baba < 1e-8) return -1.0;

    vec3 oc = ro - pa;
    float bard = dot(ba, rd);
    float baoc = dot(ba, oc);

    float k2 = baba - bard * bard;
    float k1 = baba * dot(oc, rd) - baoc * bard;
    float k0 = baba * dot(oc, oc) - baoc * baoc - radius * radius * baba;

    float disc = k1 * k1 - k2 * k0;
    if (disc < 0.0) return -1.0;

    float sqrtDisc = sqrt(disc);

    // Front hit
    float t = (-k1 - sqrtDisc) / k2;
    float y = baoc + t * bard;
    if (y > 0.0 && y < baba && t > 0.001) return t;

    // Back hit
    t = (-k1 + sqrtDisc) / k2;
    y = baoc + t * bard;
    if (y > 0.0 && y < baba && t > 0.001) return t;

    return -1.0;
}

// ---------- Scene traversal ----------
bool intersectAABB(vec3 ro, vec3 invRd, vec3 bmin, vec3 bmax, float tMax, out float tNearOut) {
    vec3 t0 = (bmin - ro) * invRd;
    vec3 t1 = (bmax - ro) * invRd;
    vec3 tMin = min(t0, t1);
    vec3 tMax3 = max(t0, t1);

    float tNear = max(max(tMin.x, tMin.y), max(tMin.z, 0.0));
    float tFar = min(min(tMax3.x, tMax3.y), tMax3.z);
    tNearOut = tNear;
    return (tFar >= tNear) && (tNear <= tMax);
}

// Test a BVH node's AABB only (fetches min/max, not meta).
bool testNodeAABB(int nodeIndex, vec3 ro, vec3 invRd, float tMax, out float tNear) {
    vec4 minData = texelFetch(uBVHNodeMinData, nodeIndex);
    vec4 maxData = texelFetch(uBVHNodeMaxData, nodeIndex);

    // BVH is built with base radii. Expand node AABBs conservatively when
    // runtime atom scale is larger than 1 to avoid misses.
    float expansion = max(uAtomScale - 1.0, 0.0) * minData.w;
    vec3 bmin = minData.xyz - vec3(expansion);
    vec3 bmax = maxData.xyz + vec3(expansion);
    return intersectAABB(ro, invRd, bmin, bmax, tMax, tNear);
}

void traceClosest(vec3 ro, vec3 rd, out float hitT, out int hitIndex) {
    hitT = 1e30;
    hitIndex = -1;
    if (uBVHNodeCount <= 0) return;

    vec3 invRd = 1.0 / rd;
    int stack[BVH_STACK_SIZE];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        int nodeIndex = stack[--sp];
        uvec4 meta = texelFetch(uBVHNodeMeta, nodeIndex);

        uint primCount = meta.w;
        if (primCount > 0u) {
            uint first = meta.z;
            int totalPrims = uAtomCount + uBondCount;
            for (uint i = 0u; i < primCount; ++i) {
                uint primIndex = texelFetch(uBVHPrimIndices, int(first + i)).x;
                if (primIndex >= uint(totalPrims)) continue;

                if (primIndex < uint(uAtomCount)) {
                    vec4 atom = texelFetch(uAtomPositions, int(primIndex));
                    float r = atom.w * uAtomScale;
                    float t = intersectSphere(ro, rd, atom.xyz, r);
                    if (t > 0.0 && t < hitT) {
                        hitT = t;
                        hitIndex = int(primIndex);
                    }
                } else if (uShowBonds) {
                    int bondIdx = int(primIndex) - uAtomCount;
                    vec3 pa = texelFetch(uBondStartPositions, bondIdx).xyz;
                    vec3 pb = texelFetch(uBondEndPositions, bondIdx).xyz;
                    float t = intersectCylinder(ro, rd, pa, pb, uBondRadius);
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
            hitLeft = testNodeAABB(int(left), ro, invRd, hitT, leftNear);
        }
        if (right != BVH_INVALID_INDEX) {
            hitRight = testNodeAABB(int(right), ro, invRd, hitT, rightNear);
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

// Any-hit test (shadow/AO) — early exit on first intersection
bool traceAnyHit(vec3 ro, vec3 rd, float maxDist) {
    if (uBVHNodeCount <= 0) return false;

    vec3 invRd = 1.0 / rd;
    int stack[BVH_STACK_SIZE];
    int sp = 0;
    stack[sp++] = 0;

    while (sp > 0) {
        int nodeIndex = stack[--sp];
        uvec4 meta = texelFetch(uBVHNodeMeta, nodeIndex);

        uint primCount = meta.w;
        if (primCount > 0u) {
            uint first = meta.z;
            int totalPrims = uAtomCount + uBondCount;
            for (uint i = 0u; i < primCount; ++i) {
                uint primIndex = texelFetch(uBVHPrimIndices, int(first + i)).x;
                if (primIndex >= uint(totalPrims)) continue;

                if (primIndex < uint(uAtomCount)) {
                    vec4 atom = texelFetch(uAtomPositions, int(primIndex));
                    float r = atom.w * uAtomScale;
                    vec3 oc = ro - atom.xyz;
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
                } else if (uShowBonds) {
                    int bondIdx = int(primIndex) - uAtomCount;
                    vec3 pa = texelFetch(uBondStartPositions, bondIdx).xyz;
                    vec3 pb = texelFetch(uBondEndPositions, bondIdx).xyz;
                    float t = intersectCylinder(ro, rd, pa, pb, uBondRadius);
                    if (t > 0.001 && t < maxDist) return true;
                }
            }
            continue;
        }

        uint left = meta.x;
        uint right = meta.y;
        float tNear;
        if (left != BVH_INVALID_INDEX && sp < BVH_STACK_SIZE) {
            if (testNodeAABB(int(left), ro, invRd, maxDist, tNear))
                stack[sp++] = int(left);
        }
        if (right != BVH_INVALID_INDEX && sp < BVH_STACK_SIZE) {
            if (testNodeAABB(int(right), ro, invRd, maxDist, tNear))
                stack[sp++] = int(right);
        }
    }

    return false;
}

// ---------- Hemisphere sampling for AO ----------

vec3 cosineWeightedHemisphere(vec3 normal) {
    float r1 = rand01();
    float r2 = rand01();

    // Cosine-weighted: cosTheta = sqrt(r1), sinTheta = sqrt(1 - r1)
    float cosTheta = sqrt(r1);
    float sinTheta = sqrt(1.0 - r1);
    float phi = 6.28318530718 * r2;

    // Build TBN from normal
    vec3 tangent;
    if (abs(normal.y) < 0.9)
        tangent = normalize(cross(normal, vec3(0.0, 1.0, 0.0)));
    else
        tangent = normalize(cross(normal, vec3(1.0, 0.0, 0.0)));
    vec3 bitangent = cross(normal, tangent);

    return normalize(
        tangent   * (sinTheta * cos(phi)) +
        bitangent * (sinTheta * sin(phi)) +
        normal    * cosTheta
    );
}

// ---------- Main ----------

void main() {
    // Initialize RNG with unique seed per pixel per frame
    rng_state = pcg(
        uint(gl_FragCoord.x) +
        uint(gl_FragCoord.y) * uint(uWidth) +
        uFrameCount * uint(uWidth) * uint(uHeight)
    );

    // Sub-pixel jitter for progressive anti-aliasing
    vec2 jitter = vec2(rand01(), rand01()) - 0.5;
    vec2 uv = (gl_FragCoord.xy + jitter) / vec2(float(uWidth), float(uHeight));

    // Generate camera ray
    vec4 ndc = vec4(uv * 2.0 - 1.0, -1.0, 1.0);
    vec4 viewTarget = uInvProjection * ndc;
    viewTarget.xyz /= viewTarget.w;

    vec3 rayDir;
    vec3 rayOrigin;
    if (uIsPerspective != 0) {
        rayDir = normalize((uInvView * vec4(viewTarget.xyz, 0.0)).xyz);
        rayOrigin = uCameraPos;
    } else {
        // Orthographic: origin varies per pixel, direction is constant
        rayOrigin = (uInvView * vec4(viewTarget.xyz, 1.0)).xyz;
        rayDir = normalize((uInvView * vec4(0.0, 0.0, -1.0, 0.0)).xyz);
    }

    // Trace primary ray
    float hitT;
    int hitIndex;
    traceClosest(rayOrigin, rayDir, hitT, hitIndex);

    if (hitIndex < 0) {
        fragColor = vec4(uBackgroundColor, 1.0);
        return;
    }

    // Shading
    vec3 hitPos = rayOrigin + rayDir * hitT;
    vec3 normal;
    vec3 surfaceColor;
    float biasRadius;

    if (hitIndex < uAtomCount) {
        // Sphere hit
        vec4 atomData = texelFetch(uAtomPositions, hitIndex);
        vec4 atomColor = texelFetch(uAtomColors, hitIndex);
        normal = normalize(hitPos - atomData.xyz);
        surfaceColor = atomColor.rgb;
        biasRadius = atomData.w * uAtomScale;
    } else {
        // Cylinder hit
        int bondIdx = hitIndex - uAtomCount;
        vec4 bondColor = texelFetch(uBondColors, bondIdx);
        vec3 pa = texelFetch(uBondStartPositions, bondIdx).xyz;
        vec3 pb = texelFetch(uBondEndPositions, bondIdx).xyz;
        vec3 ba = pb - pa;
        float h = dot(hitPos - pa, ba) / dot(ba, ba);
        normal = normalize(hitPos - pa - h * ba);
        surfaceColor = bondColor.rgb;
        biasRadius = uBondRadius;
    }

    // Light direction (world space, normalized)
    vec3 lightDir = normalize(uLightDir);
    vec3 viewDir = -rayDir;

    // Blinn-Phong shading
    float NdotL = max(dot(normal, lightDir), 0.0);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), uShininess);

    vec3 ambient  = uAmbient  * surfaceColor;
    vec3 diffuse  = uDiffuse  * NdotL * surfaceColor;
    vec3 specular = uSpecular * spec * vec3(1.0);

    // Bias origin along normal to avoid self-intersection (scale with radius)
    float bias = max(biasRadius * 0.01, 0.05);
    vec3 biasedOrigin = hitPos + normal * bias;

    // Shadow
    float shadow = 1.0;
    if (uEnableShadows) {
        if (traceAnyHit(biasedOrigin, lightDir, 10000.0)) {
            shadow = 1.0 - clamp(uShadowOpacity, 0.0, 1.0);
        }
    }

    // Ambient occlusion
    float ao = 1.0;
    if (uEnableAO && uAOSamples > 0) {
        float occluded = 0.0;
        for (int i = 0; i < uAOSamples; i++) {
            vec3 aoDir = cosineWeightedHemisphere(normal);
            if (traceAnyHit(biasedOrigin, aoDir, uAORadius)) {
                occluded += 1.0;
            }
        }
        ao = 1.0 - occluded / float(uAOSamples);
    }

    // AO only modulates ambient (indirect light); direct light uses shadow only
    vec3 result = ambient * ao + (diffuse + specular) * shadow;
    fragColor = vec4(result, 1.0);
}
)";

const char* displayVertexShader = R"(
#version 410 core

layout(location = 0) in vec2 aPosition;

out vec2 vTexCoord;

void main() {
    vTexCoord = aPosition * 0.5 + 0.5;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

const char* displayFragmentShader = R"(
#version 410 core

in vec2 vTexCoord;
out vec4 fragColor;

uniform sampler2D uAccumTexture;
uniform float uSampleCount;

void main() {
    vec3 accum = texture(uAccumTexture, vTexCoord).rgb;
    vec3 color = accum / max(uSampleCount, 1.0);

    fragColor = vec4(color, 1.0);
}
)";

} // namespace rt_shaders

// ---------------------------------------------------------------------------
// RayTracingRenderer implementation
// ---------------------------------------------------------------------------

RayTracingRenderer::RayTracingRenderer() = default;

RayTracingRenderer::~RayTracingRenderer() {
    cleanup();
}

bool RayTracingRenderer::initialize() {
    if (m_initialized) return true;

    initializeOpenGLFunctions();

    if (!compileShaders()) {
        qCritical() << "RayTracingRenderer: Failed to compile shaders";
        return false;
    }

    if (!m_overlayShaderManager.initialize()) {
        qCritical() << "RayTracingRenderer: Failed to initialize overlay shader manager";
        return false;
    }
    if (!m_viewportAxesRenderer.initialize(&m_overlayShaderManager)) {
        qCritical() << "RayTracingRenderer: Failed to initialize viewport axes renderer";
        return false;
    }

    createFullScreenQuad();

    // Query TBO size limit
    GLint maxTBOSize;
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maxTBOSize);
    qInfo() << "RayTracingRenderer: Max TBO texels:" << maxTBOSize;

    // Create TBO resources (buffers + textures)
    glGenBuffers(1, &m_atomPosBuf);
    glGenTextures(1, &m_atomPosTex);
    glGenBuffers(1, &m_atomColorBuf);
    glGenTextures(1, &m_atomColorTex);
    glGenBuffers(1, &m_bvhNodeMinBuf);
    glGenTextures(1, &m_bvhNodeMinTex);
    glGenBuffers(1, &m_bvhNodeMaxBuf);
    glGenTextures(1, &m_bvhNodeMaxTex);
    glGenBuffers(1, &m_bvhNodeMetaBuf);
    glGenTextures(1, &m_bvhNodeMetaTex);
    glGenBuffers(1, &m_bvhPrimBuf);
    glGenTextures(1, &m_bvhPrimTex);

    // Bond TBOs
    glGenBuffers(1, &m_bondStartBuf);
    glGenTextures(1, &m_bondStartTex);
    glGenBuffers(1, &m_bondEndBuf);
    glGenTextures(1, &m_bondEndTex);
    glGenBuffers(1, &m_bondColorBuf);
    glGenTextures(1, &m_bondColorTex);

    m_initialized = true;
    return true;
}

void RayTracingRenderer::cleanup() {
    if (!m_initialized) return;

    m_rtShader.reset();
    m_displayShader.reset();
    m_viewportAxesRenderer.cleanup();
    m_overlayShaderManager.cleanup();

    if (m_quadVAO) { glDeleteVertexArrays(1, &m_quadVAO); m_quadVAO = 0; }
    if (m_quadVBO) { glDeleteBuffers(1, &m_quadVBO); m_quadVBO = 0; }
    if (m_accumFBO) { glDeleteFramebuffers(1, &m_accumFBO); m_accumFBO = 0; }
    if (m_accumTexture) { glDeleteTextures(1, &m_accumTexture); m_accumTexture = 0; }
    if (m_atomPosBuf) { glDeleteBuffers(1, &m_atomPosBuf); m_atomPosBuf = 0; }
    if (m_atomPosTex) { glDeleteTextures(1, &m_atomPosTex); m_atomPosTex = 0; }
    if (m_atomColorBuf) { glDeleteBuffers(1, &m_atomColorBuf); m_atomColorBuf = 0; }
    if (m_atomColorTex) { glDeleteTextures(1, &m_atomColorTex); m_atomColorTex = 0; }
    if (m_bvhNodeMinBuf) { glDeleteBuffers(1, &m_bvhNodeMinBuf); m_bvhNodeMinBuf = 0; }
    if (m_bvhNodeMinTex) { glDeleteTextures(1, &m_bvhNodeMinTex); m_bvhNodeMinTex = 0; }
    if (m_bvhNodeMaxBuf) { glDeleteBuffers(1, &m_bvhNodeMaxBuf); m_bvhNodeMaxBuf = 0; }
    if (m_bvhNodeMaxTex) { glDeleteTextures(1, &m_bvhNodeMaxTex); m_bvhNodeMaxTex = 0; }
    if (m_bvhNodeMetaBuf) { glDeleteBuffers(1, &m_bvhNodeMetaBuf); m_bvhNodeMetaBuf = 0; }
    if (m_bvhNodeMetaTex) { glDeleteTextures(1, &m_bvhNodeMetaTex); m_bvhNodeMetaTex = 0; }
    if (m_bvhPrimBuf) { glDeleteBuffers(1, &m_bvhPrimBuf); m_bvhPrimBuf = 0; }
    if (m_bvhPrimTex) { glDeleteTextures(1, &m_bvhPrimTex); m_bvhPrimTex = 0; }
    if (m_bondStartBuf) { glDeleteBuffers(1, &m_bondStartBuf); m_bondStartBuf = 0; }
    if (m_bondStartTex) { glDeleteTextures(1, &m_bondStartTex); m_bondStartTex = 0; }
    if (m_bondEndBuf) { glDeleteBuffers(1, &m_bondEndBuf); m_bondEndBuf = 0; }
    if (m_bondEndTex) { glDeleteTextures(1, &m_bondEndTex); m_bondEndTex = 0; }
    if (m_bondColorBuf) { glDeleteBuffers(1, &m_bondColorBuf); m_bondColorBuf = 0; }
    if (m_bondColorTex) { glDeleteTextures(1, &m_bondColorTex); m_bondColorTex = 0; }

    m_structure = nullptr;
    m_bvhNodeCount = 0;
    m_bondCount = 0;
    m_initialized = false;
}

void RayTracingRenderer::resize(int width, int height) {
    if (width == m_width && height == m_height) return;
    m_width = width;
    m_height = height;

    // Recreate accumulation FBO at new size
    createAccumulationFBO();
    resetAccumulation();
}

void RayTracingRenderer::setStructure(const data::Structure* structure) {
    m_structure = structure;
    m_atomDataDirty = true;
    m_bondDataDirty = true;
    resetAccumulation();
}

void RayTracingRenderer::render(const Camera& camera, const RenderSettings& settings) {
    if (!m_initialized || m_width == 0 || m_height == 0) return;

    // Store settings for isConverged() and state hashing.
    m_settings = settings;

    // Upload scene data if dirty
    if (m_atomDataDirty || m_bondDataDirty) {
        uploadSceneData();
    }

    if (m_atomCount == 0) {
        // No atoms — just clear with background color
        QColor bg = m_settings.backgroundColor;
        glClearColor(bg.redF(), bg.greenF(), bg.blueF(), 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (m_settings.showViewportAxes) {
            glClear(GL_DEPTH_BUFFER_BIT);
            m_viewportAxesRenderer.render(camera, m_settings, m_width, m_height);
        }
        return;
    }

    // Check if state changed (camera, settings, etc.)
    uint64_t currentHash = computeRenderStateHash(camera, m_settings);
    if (currentHash != m_lastStateHash) {
        m_lastStateHash = currentHash;
        resetAccumulation();
    }

    // Save Qt's FBO binding
    GLint qtFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &qtFBO);

    // Ensure accumulation FBO exists
    if (!m_accumFBO) {
        createAccumulationFBO();
    }

    // Pass 1: Ray trace into accumulation FBO (additive) until convergence.
    if (!isConverged()) {
        m_sampleCount++;
        renderRTPass(camera);
    }

    // Pass 2: Display accumulated result into Qt's FBO
    glBindFramebuffer(GL_FRAMEBUFFER, qtFBO);
    glViewport(0, 0, m_width, m_height);
    renderDisplayPass(camera);
}

void RayTracingRenderer::invalidateAtomData() {
    m_atomDataDirty = true;
    resetAccumulation();
}

void RayTracingRenderer::invalidateBondData() {
    m_bondDataDirty = true;
    resetAccumulation();
}

void RayTracingRenderer::resetAccumulation() {
    m_sampleCount = 0;

    if (m_accumFBO && m_initialized) {
        GLint currentFBO;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFBO);

        glBindFramebuffer(GL_FRAMEBUFFER, m_accumFBO);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glBindFramebuffer(GL_FRAMEBUFFER, currentFBO);
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool RayTracingRenderer::compileShaders() {
    // RT shader
    m_rtShader = std::make_unique<QOpenGLShaderProgram>();
    if (!m_rtShader->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                              rt_shaders::quadVertexShader)) {
        qCritical() << "RT vertex shader failed:" << m_rtShader->log();
        return false;
    }
    if (!m_rtShader->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                              rt_shaders::rayTraceFragmentShader)) {
        qCritical() << "RT fragment shader failed:" << m_rtShader->log();
        return false;
    }
    if (!m_rtShader->link()) {
        qCritical() << "RT shader linking failed:" << m_rtShader->log();
        return false;
    }

    // Display shader
    m_displayShader = std::make_unique<QOpenGLShaderProgram>();
    if (!m_displayShader->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                                   rt_shaders::displayVertexShader)) {
        qCritical() << "Display vertex shader failed:" << m_displayShader->log();
        return false;
    }
    if (!m_displayShader->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                                   rt_shaders::displayFragmentShader)) {
        qCritical() << "Display fragment shader failed:" << m_displayShader->log();
        return false;
    }
    if (!m_displayShader->link()) {
        qCritical() << "Display shader linking failed:" << m_displayShader->log();
        return false;
    }

    return true;
}

void RayTracingRenderer::createFullScreenQuad() {
    // Two triangles covering [-1, 1] in clip space
    float vertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f
    };

    glGenVertexArrays(1, &m_quadVAO);
    glGenBuffers(1, &m_quadVBO);

    glBindVertexArray(m_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    glBindVertexArray(0);
}

void RayTracingRenderer::createAccumulationFBO() {
    // Clean up old FBO
    if (m_accumFBO) {
        glDeleteFramebuffers(1, &m_accumFBO);
        m_accumFBO = 0;
    }
    if (m_accumTexture) {
        glDeleteTextures(1, &m_accumTexture);
        m_accumTexture = 0;
    }

    if (m_width == 0 || m_height == 0) return;

    // Create RGBA32F texture for accumulation
    glGenTextures(1, &m_accumTexture);
    glBindTexture(GL_TEXTURE_2D, m_accumTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, m_width, m_height, 0,
                 GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Create FBO
    glGenFramebuffers(1, &m_accumFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_accumFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, m_accumTexture, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        qCritical() << "RayTracingRenderer: Accumulation FBO incomplete:" << status;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RayTracingRenderer::uploadSceneData() {
    if (!m_structure || m_structure->atomCount() == 0) {
        m_atomCount = 0;
        m_bondCount = 0;
        m_bvhNodeCount = 0;
        m_atomDataDirty = false;
        m_bondDataDirty = false;
        return;
    }

    m_atomCount = static_cast<int>(m_structure->atomCount());

    // ── Atom TBOs ──────────────────────────────────────────────

    // Pack positions + radii: vec4(x, y, z, radius) per atom
    auto posData = m_structure->packPositionsAndRadii();

    glBindBuffer(GL_TEXTURE_BUFFER, m_atomPosBuf);
    glBufferData(GL_TEXTURE_BUFFER,
                 static_cast<GLsizeiptr>(posData.size() * sizeof(float)),
                 posData.data(), GL_STATIC_DRAW);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, m_atomPosTex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, m_atomPosBuf);

    // Pack colors: vec4(r, g, b, a) per atom
    auto colorData = m_structure->packColors();

    glBindBuffer(GL_TEXTURE_BUFFER, m_atomColorBuf);
    glBufferData(GL_TEXTURE_BUFFER,
                 static_cast<GLsizeiptr>(colorData.size() * sizeof(float)),
                 colorData.data(), GL_STATIC_DRAW);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, m_atomColorTex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, m_atomColorBuf);

    // ── Bond TBOs ──────────────────────────────────────────────

    const auto& bonds = m_structure->bonds();
    m_bondCount = static_cast<int>(bonds.bondCount());

    // Temporary arrays for bond positions (also used for BVH)
    std::vector<float> bondStartData;
    std::vector<float> bondEndData;

    if (m_bondCount > 0) {
        bondStartData.resize(static_cast<size_t>(m_bondCount) * 4);
        bondEndData.resize(static_cast<size_t>(m_bondCount) * 4);
        std::vector<float> bondColorData(static_cast<size_t>(m_bondCount) * 4);

        const float* px = m_structure->positionsX();
        const float* py = m_structure->positionsY();
        const float* pz = m_structure->positionsZ();
        const float* cr = m_structure->colorsR();
        const float* cg = m_structure->colorsG();
        const float* cb = m_structure->colorsB();
        const auto& lattice = m_structure->lattice();
        const auto& mat = lattice.matrix;

        for (int i = 0; i < m_bondCount; ++i) {
            const auto& bond = bonds.bond(static_cast<size_t>(i));
            uint32_t a1 = bond.atomIndex1;
            uint32_t a2 = bond.atomIndex2;
            size_t idx = static_cast<size_t>(i);

            bondStartData[idx * 4 + 0] = px[a1];
            bondStartData[idx * 4 + 1] = py[a1];
            bondStartData[idx * 4 + 2] = pz[a1];
            bondStartData[idx * 4 + 3] = 0.0f;

            float ex = px[a2];
            float ey = py[a2];
            float ez = pz[a2];
            if (bond.imageX != 0 || bond.imageY != 0 || bond.imageZ != 0) {
                ex += static_cast<float>(bond.imageX * mat[0][0] + bond.imageY * mat[1][0] + bond.imageZ * mat[2][0]);
                ey += static_cast<float>(bond.imageX * mat[0][1] + bond.imageY * mat[1][1] + bond.imageZ * mat[2][1]);
                ez += static_cast<float>(bond.imageX * mat[0][2] + bond.imageY * mat[1][2] + bond.imageZ * mat[2][2]);
            }
            bondEndData[idx * 4 + 0] = ex;
            bondEndData[idx * 4 + 1] = ey;
            bondEndData[idx * 4 + 2] = ez;
            bondEndData[idx * 4 + 3] = 0.0f;

            bondColorData[idx * 4 + 0] = (cr[a1] + cr[a2]) * 0.5f;
            bondColorData[idx * 4 + 1] = (cg[a1] + cg[a2]) * 0.5f;
            bondColorData[idx * 4 + 2] = (cb[a1] + cb[a2]) * 0.5f;
            bondColorData[idx * 4 + 3] = 1.0f;
        }

        glBindBuffer(GL_TEXTURE_BUFFER, m_bondStartBuf);
        glBufferData(GL_TEXTURE_BUFFER,
                     static_cast<GLsizeiptr>(bondStartData.size() * sizeof(float)),
                     bondStartData.data(), GL_STATIC_DRAW);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_BUFFER, m_bondStartTex);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, m_bondStartBuf);

        glBindBuffer(GL_TEXTURE_BUFFER, m_bondEndBuf);
        glBufferData(GL_TEXTURE_BUFFER,
                     static_cast<GLsizeiptr>(bondEndData.size() * sizeof(float)),
                     bondEndData.data(), GL_STATIC_DRAW);
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_BUFFER, m_bondEndTex);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, m_bondEndBuf);

        glBindBuffer(GL_TEXTURE_BUFFER, m_bondColorBuf);
        glBufferData(GL_TEXTURE_BUFFER,
                     static_cast<GLsizeiptr>(bondColorData.size() * sizeof(float)),
                     bondColorData.data(), GL_STATIC_DRAW);
        glActiveTexture(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_BUFFER, m_bondColorTex);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, m_bondColorBuf);
    }

    // ── Unified BVH (atoms + bonds) ───────────────────────────

    size_t totalPrims = static_cast<size_t>(m_atomCount) + static_cast<size_t>(m_bondCount);
    std::vector<PrimitiveBounds> primBounds(totalPrims);

    // Atom bounds
    const float* ax = m_structure->positionsX();
    const float* ay = m_structure->positionsY();
    const float* az = m_structure->positionsZ();
    const float* ar = m_structure->radii();
    for (size_t i = 0; i < static_cast<size_t>(m_atomCount); ++i) {
        float r = ar[i];
        primBounds[i] = {
            ax[i] - r, ay[i] - r, az[i] - r,
            ax[i] + r, ay[i] + r, az[i] + r,
            ax[i], ay[i], az[i],
            r
        };
    }

    // Bond bounds (capsule AABB; maxRadius = 0 since bondRadius is baked in)
    float bondR = m_settings.bondRadius;
    for (size_t i = 0; i < static_cast<size_t>(m_bondCount); ++i) {
        float sx = bondStartData[i * 4 + 0];
        float sy = bondStartData[i * 4 + 1];
        float sz = bondStartData[i * 4 + 2];
        float ex = bondEndData[i * 4 + 0];
        float ey = bondEndData[i * 4 + 1];
        float ez = bondEndData[i * 4 + 2];
        size_t pi = static_cast<size_t>(m_atomCount) + i;
        primBounds[pi] = {
            std::min(sx, ex) - bondR, std::min(sy, ey) - bondR, std::min(sz, ez) - bondR,
            std::max(sx, ex) + bondR, std::max(sy, ey) + bondR, std::max(sz, ez) + bondR,
            (sx + ex) * 0.5f, (sy + ey) * 0.5f, (sz + ez) * 0.5f,
            0.0f
        };
    }

    BVHData bvh = buildBVH(primBounds.data(), totalPrims);
    assert(!bvh.nodes.empty() && "Expected non-empty BVH for non-empty structure");
    assert(!bvh.primitiveIndices.empty() && "Expected non-empty BVH primitive index list");


    m_bvhNodeCount = static_cast<int>(bvh.nodes.size());

    std::vector<float> nodeMinData(static_cast<size_t>(m_bvhNodeCount) * 4);
    std::vector<float> nodeMaxData(static_cast<size_t>(m_bvhNodeCount) * 4);
    std::vector<uint32_t> nodeMetaData(static_cast<size_t>(m_bvhNodeCount) * 4);

    for (int i = 0; i < m_bvhNodeCount; ++i) {
        const BVHNodeGPU& node = bvh.nodes[static_cast<size_t>(i)];
        for (int c = 0; c < 4; ++c) {
            nodeMinData[static_cast<size_t>(i) * 4 + c] = node.minAndMaxRadius[static_cast<size_t>(c)];
            nodeMaxData[static_cast<size_t>(i) * 4 + c] = node.maxAndPad[static_cast<size_t>(c)];
            nodeMetaData[static_cast<size_t>(i) * 4 + c] = node.meta[static_cast<size_t>(c)];
        }
    }

    glBindBuffer(GL_TEXTURE_BUFFER, m_bvhNodeMinBuf);
    glBufferData(GL_TEXTURE_BUFFER,
                 static_cast<GLsizeiptr>(nodeMinData.size() * sizeof(float)),
                 nodeMinData.data(),
                 GL_STATIC_DRAW);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_BUFFER, m_bvhNodeMinTex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, m_bvhNodeMinBuf);

    glBindBuffer(GL_TEXTURE_BUFFER, m_bvhNodeMaxBuf);
    glBufferData(GL_TEXTURE_BUFFER,
                 static_cast<GLsizeiptr>(nodeMaxData.size() * sizeof(float)),
                 nodeMaxData.data(),
                 GL_STATIC_DRAW);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_BUFFER, m_bvhNodeMaxTex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, m_bvhNodeMaxBuf);

    glBindBuffer(GL_TEXTURE_BUFFER, m_bvhNodeMetaBuf);
    glBufferData(GL_TEXTURE_BUFFER,
                 static_cast<GLsizeiptr>(nodeMetaData.size() * sizeof(uint32_t)),
                 nodeMetaData.data(),
                 GL_STATIC_DRAW);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_BUFFER, m_bvhNodeMetaTex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32UI, m_bvhNodeMetaBuf);

    glBindBuffer(GL_TEXTURE_BUFFER, m_bvhPrimBuf);
    glBufferData(GL_TEXTURE_BUFFER,
                 static_cast<GLsizeiptr>(bvh.primitiveIndices.size() * sizeof(uint32_t)),
                 bvh.primitiveIndices.data(),
                 GL_STATIC_DRAW);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_BUFFER, m_bvhPrimTex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R32UI, m_bvhPrimBuf);

    m_atomDataDirty = false;
    m_bondDataDirty = false;
}

void RayTracingRenderer::renderRTPass(const Camera& camera) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_accumFBO);
    glViewport(0, 0, m_width, m_height);

    // Additive blending for accumulation
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDisable(GL_DEPTH_TEST);

    m_rtShader->bind();

    // Camera uniforms
    QMatrix4x4 invView = camera.viewMatrix().inverted();
    QMatrix4x4 invProj = camera.projectionMatrix().inverted();
    QVector3D camPos = camera.position();

    m_rtShader->setUniformValue("uInvView", invView);
    m_rtShader->setUniformValue("uInvProjection", invProj);
    m_rtShader->setUniformValue("uCameraPos", camPos);
    m_rtShader->setUniformValue("uIsPerspective", camera.isPerspective() ? 1 : 0);

    // Viewport
    m_rtShader->setUniformValue("uWidth", m_width);
    m_rtShader->setUniformValue("uHeight", m_height);

    // Atom data TBOs
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_BUFFER, m_atomPosTex);
    m_rtShader->setUniformValue("uAtomPositions", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, m_atomColorTex);
    m_rtShader->setUniformValue("uAtomColors", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_BUFFER, m_bvhNodeMinTex);
    m_rtShader->setUniformValue("uBVHNodeMinData", 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_BUFFER, m_bvhNodeMaxTex);
    m_rtShader->setUniformValue("uBVHNodeMaxData", 3);

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_BUFFER, m_bvhNodeMetaTex);
    m_rtShader->setUniformValue("uBVHNodeMeta", 4);

    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_BUFFER, m_bvhPrimTex);
    m_rtShader->setUniformValue("uBVHPrimIndices", 5);

    // Bond data TBOs
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_BUFFER, m_bondStartTex);
    m_rtShader->setUniformValue("uBondStartPositions", 6);

    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_BUFFER, m_bondEndTex);
    m_rtShader->setUniformValue("uBondEndPositions", 7);

    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_BUFFER, m_bondColorTex);
    m_rtShader->setUniformValue("uBondColors", 8);

    m_rtShader->setUniformValue("uAtomCount", m_atomCount);
    m_rtShader->setUniformValue("uBondCount", m_bondCount);
    m_rtShader->setUniformValue("uBVHNodeCount", m_bvhNodeCount);
    m_rtShader->setUniformValue("uAtomScale", m_settings.atomScale);
    m_rtShader->setUniformValue("uBondRadius", m_settings.bondRadius);
    m_rtShader->setUniformValue("uShowBonds", m_settings.showBonds);

    // Light direction is already in world space
    m_rtShader->setUniformValue("uLightDir", m_settings.lightDirWorld());
    m_rtShader->setUniformValue("uAmbient", m_settings.ambientStrength);
    m_rtShader->setUniformValue("uDiffuse", m_settings.diffuseStrength);
    m_rtShader->setUniformValue("uSpecular", m_settings.specularStrength);
    m_rtShader->setUniformValue("uShininess", m_settings.shininess);

    // Background color
    QColor bg = m_settings.backgroundColor;
    m_rtShader->setUniformValue("uBackgroundColor",
                                 QVector3D(bg.redF(), bg.greenF(), bg.blueF()));

    // Progressive rendering
    m_rtShader->setUniformValue("uFrameCount", static_cast<GLuint>(m_sampleCount));

    // Feature toggles
    m_rtShader->setUniformValue("uEnableShadows", m_settings.enableShadows);
    m_rtShader->setUniformValue("uEnableAO", m_settings.enableAmbientOcclusion);
    m_rtShader->setUniformValue("uShadowOpacity", m_settings.shadowOpacity);
    m_rtShader->setUniformValue("uAOSamples", m_settings.aoSamples);
    m_rtShader->setUniformValue("uAORadius", m_settings.aoRadius);

    // Draw full-screen quad
    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    m_rtShader->release();
    glDisable(GL_BLEND);
}

void RayTracingRenderer::renderDisplayPass(const Camera& camera) {
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    m_displayShader->bind();

    // Bind accumulation texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_accumTexture);
    m_displayShader->setUniformValue("uAccumTexture", 0);
    m_displayShader->setUniformValue("uSampleCount", static_cast<float>(m_sampleCount));

    // Draw full-screen quad
    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    m_displayShader->release();

    if (m_settings.showViewportAxes) {
        // Fresh depth for overlay so it stays on top of the displayed RT image
        // while preserving true self-occlusion inside the gizmo geometry.
        glClear(GL_DEPTH_BUFFER_BIT);
        m_viewportAxesRenderer.render(camera, m_settings, m_width, m_height);
    }
}

} // namespace atom::render
