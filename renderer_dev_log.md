# Ray Tracing Renderer Development Log

## 2026-02-07: Initial Ray Tracing Renderer Implementation

### Summary
Added a full-screen fragment-shader ray tracing renderer alongside the existing OpenGL rasterization renderer. The RT renderer traces primary rays, shadow rays, and ambient occlusion rays per pixel using brute-force traversal, with progressive accumulation for noise-free results. Both renderers are kept alive simultaneously for instant switching via a sidebar toggle.

### Files Created
| File | Purpose |
|------|---------|
| `src/render/opengl/RayTracingRenderer.h` | RT renderer class declaration, implements `Renderer` interface |
| `src/render/opengl/RayTracingRenderer.cpp` | Full implementation (~640 lines): shaders, TBOs, accumulation FBO, render pipeline |

### Files Modified
| File | Change |
|------|--------|
| `src/render/RenderSettings.h` | Added `aoSamples` (4), `aoRadius` (3.0 Angstroms), `maxRTSamples` (4096) |
| `src/render/CMakeLists.txt` | Added `opengl/RayTracingRenderer.cpp/h` to source list |
| `src/ui/components/OpenGLViewport.h` | Added `rendererMode`, `sampleCount`, `enableAO`, `enableShadows` Q_PROPERTYs; forward-declared `RayTracingRenderer` |
| `src/ui/components/OpenGLViewport.cpp` | Dual-renderer architecture in `RendererImpl`: holds both `OpenGLRenderer` + `RayTracingRenderer`, lazy RT init, mode switching, FBO MSAA toggle |
| `src/ui/qml/Sidebar.qml` | Wired Render Mode ComboBox, AO checkbox, Shadows checkbox to viewport properties |
| `src/ui/qml/ViewportPanel.qml` | Added "Mode: Ray Tracing" and "Samples: N" labels in bottom-left info overlay |

### Architecture Decisions

#### 1. OpenGL 4.1 Fragment-Shader Ray Tracing (not Vulkan)
macOS only supports OpenGL 4.1 (Apple deprecated GL, no compute shaders). Vulkan RT extensions aren't available via MoltenVK either. Fragment-shader RT with texture buffer objects (TBOs) is the only viable approach on the current dev platform. The core algorithms (ray-sphere intersection, BVH traversal, shading) are API-independent and can be ported to Vulkan compute later.

#### 2. Texture Buffer Objects for Atom Data
Atom positions+radii packed as `vec4(x,y,z,r)` per atom into a TBO (`samplerBuffer` + `texelFetch`). Colors in a second TBO. TBOs are core in OpenGL 3.1+, support millions of texels, and are the most efficient way to pass large arrays to fragment shaders in GL 4.1 (no SSBOs available).

#### 3. Additive Accumulation (not Running Average)
Each frame's RT output is additively blended (`GL_ONE, GL_ONE`) into a `GL_RGBA32F` accumulation texture. The display shader divides by `sampleCount` to produce the true arithmetic mean. This avoids the exponential-weighting problem of constant-alpha blending (`result = new/N + old*(N-1)/N` is NOT a true average when chained — it over-weights recent samples).

#### 4. State Hash for Change Detection
A hash of camera parameters (azimuth, elevation, distance, target, FOV, aspect, projection type, ortho scale) + render settings (atomScale, enableShadows, enableAO, AO params, lighting) is computed each frame. If the hash changes, the accumulation buffer is cleared and `sampleCount` resets to 0.

#### 5. PCG Hash for Shader RNG
Uses a PCG (Permuted Congruential Generator) hash function for random number generation in the fragment shader, seeded by `gl_FragCoord.xy + frameCount * viewportSize`. PCG has better statistical properties than `fract(sin(...))` hashes (which produce visible patterns on some GPUs). Each pixel in each frame gets a unique, deterministic random sequence.

#### 6. Cosine-Weighted Hemisphere Sampling for AO
AO rays are sampled from a cosine-weighted hemisphere around the surface normal. This naturally weights directions closer to the normal more heavily (matching the rendering equation's cos(theta) term), converging faster than uniform hemisphere sampling. 4 AO rays per pixel per frame, accumulating over progressive frames.

#### 7. Dual-Renderer Architecture
`RendererImpl` holds both `std::unique_ptr<OpenGLRenderer>` and `std::unique_ptr<RayTracingRenderer>` simultaneously. Raster renderer is initialized eagerly (on first `synchronize`). RT renderer is initialized lazily (on first switch to RT mode). Both share the same `Structure` data. Switching is instant — just a pointer swap + FBO recreation (MSAA on for raster, off for RT).

#### 8. No Gamma Correction
The display shader outputs linear colors without gamma correction, matching the existing raster renderer behavior. Both renderers use the same Blinn-Phong shading parameters from `RenderSettings`.

### Shader Architecture

**RT Fragment Shader** (`rayTraceFragmentShader`):
- Reconstructs camera ray from `gl_FragCoord` via inverse view/projection matrices
- Sub-pixel jitter for progressive anti-aliasing
- Brute-force closest-hit traversal over all atoms (O(N) per ray)
- Shadow ray with any-hit early termination toward light direction
- 4 cosine-weighted AO rays per pixel, configurable radius
- Blinn-Phong shading with ambient, diffuse, specular components

**Display Fragment Shader** (`displayFragmentShader`):
- Reads RGBA32F accumulation texture
- Divides by `sampleCount` for true average
- Outputs to Qt's FBO for display

### Performance Characteristics (Brute Force)
- ~30 fps at 1K atoms (M1 Pro, 1080p, all features)
- ~6 fps at 5K atoms
- ~3 fps at 10K atoms
- <1 fps at 50K+ atoms (accumulation-only mode)
- Progressive accumulation makes even slow frame rates usable — a clean image emerges after a few seconds

### Known Limitations / Future Work
- **No BVH acceleration** — brute-force O(N) per ray limits interactive use to ~10K atoms. Phase 2 should add a CPU-built BVH encoded in TBOs for O(log N) traversal.
- **No bond rendering in RT mode** — only spheres are ray-traced. Bonds could be added as ray-cylinder intersections.
- **No unit cell rendering in RT mode** — would need ray-line intersection or composited raster overlay.
- **No depth output** — can't composite rasterized overlays (axes gizmo, selection highlights) with correct depth ordering.
- **Single light source** — hard shadows from one directional light. Soft shadows would require area light sampling.

---

## 2026-02-08: Bug Fixes

### Summary
Fixed two rendering issues: viewport size mismatch on Retina displays and noisy AO. Partially addressed noisy shadow black points.

### Changes

#### 1. Retina Display Fix (OpenGLViewport.cpp)
The RT renderer's accumulation FBO and `glViewport` calls used logical pixel sizes from `QQuickItem::size()`, but Qt's FBO is created at physical pixel size (2x on Retina). The rendered image only filled the bottom-left quarter of the viewport.

**Fix**: Multiply logical size by `window()->devicePixelRatio()` before passing to `resize()`. Camera aspect ratio still uses logical size (ratio is the same).

#### 2. AO Shading Fix (RayTracingRenderer.cpp)
AO was multiplied into both ambient and direct lighting: `ambient * ao + (diffuse + specular) * shadow * ao`. With only 4 AO samples per frame, `ao` can be 0 (all rays occluded), making pixels completely black.

**Fix**: AO only modulates ambient (indirect light): `ambient * ao + (diffuse + specular) * shadow`. Direct lighting is only affected by shadow rays.

#### 3. Increased Ray Origin Bias (RayTracingRenderer.cpp)
Shadow/AO ray origins were offset by a fixed `0.01` along the normal. This was too small for larger atoms, causing self-intersection artifacts.

**Fix**: Bias scales with atom radius: `max(atomRadius * 0.01, 0.05)`.

### Known Bug: Noisy Black Points with Shadows
When shadows are enabled, noisy black points appear, especially at shadow boundaries. Root cause: **binary hard shadows** (0 or 1) from a single directional light, combined with sub-pixel jitter that shifts hit positions at shadow edges between frames. The contrast between shadowed pixels (`ambient * ao ≈ 0.3 * color`) and lit pixels (`+ diffuse + specular ≈ 1.0 * color`) is very high, making per-frame noise highly visible during early accumulation.

Attempted fixes that were reverted (produced unwanted visual artifacts):
- `skipIndex` in `traceAnyHit` to exclude source atom
- Jittered shadow ray direction (soft shadow via area light simulation)
- Secondary fill light from opposite direction

Potential approaches not yet tried:
- Multiple shadow rays per frame (reduces variance but costs performance)
- Distance-based soft shadow falloff instead of binary 0/1
- Temporal denoising / bilateral filter on the accumulation buffer
