// Qt's offscreen platform skips this test when it cannot create a GL context.
// On macOS, run the executable with QT_QPA_PLATFORM=cocoa for native GPU checks.
#include "common/Camera.h"
#include "opengl/OpenGLRenderer.h"
#include "opengl/RayTracingRenderer.h"
#include "Structure.h"
#include "BondList.h"
#include "BackgroundCompositingCheck.h"
#include "GizmoRenderingCheck.h"

#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <iostream>

using namespace atom;

namespace {
constexpr int size = 128;

bool checkRenderer(render::Renderer& renderer, QOpenGLFunctions& gl, bool rayTracing) {
    if (!renderer.initialize()) return false;
    renderer.resize(size, size);
    QOpenGLFramebufferObject fbo(size, size, QOpenGLFramebufferObject::CombinedDepthStencil);
    if (!fbo.isValid()) return false;

    render::Camera camera;
    camera.setPresetView(render::ViewDirection::PlusZ);
    camera.setProjection(false);
    camera.setOrthoScale(3.0f);
    camera.setAspectRatio(1.0f);

    data::Structure scene;
    scene.addAtom(-1.0f, 0.0f, 0.0f, 6);
    scene.addAtom(1.0f, 0.0f, 0.0f, 8);
    scene.radii()[0] = scene.radii()[1] = 0.55f;
    scene.bonds().addBond(0, 1);
    scene.bonds().setRadius(0, 0.22f);
    scene.bonds().setEndpointColors(0, data::Color(0.2f, 0.5f, 0.8f),
                                   data::Color(0.2f, 0.5f, 0.8f));

    render::RenderSettings settings;
    settings.showUnitCell = false;
    settings.showViewportAxes = false;
    settings.outlineEnabled = false;
    settings.specularStrength = 0.0f;
    settings.maxRTSamples = 8;

    auto snapshot = [&](const data::Structure* structure) {
        renderer.setStructure(structure);
        for (int i = 0; i < (rayTracing ? settings.maxRTSamples : 1); ++i) {
            fbo.bind();
            gl.glViewport(0, 0, size, size);
            renderer.render(camera, settings);
        }
        gl.glFinish();
        return fbo.toImage();
    };
    const QVector3D atomScreen = camera.worldToScreen(QVector3D(-1, 0, 0), size, size);
    const QPoint atomPixel(qRound(atomScreen.x()), size / 2);
    const QPoint bondPixel(size / 2, size / 2);
    QColor atomReference;
    QColor bondReference;
    for (int alpha : {255, 64, 0}) {
        settings.backgroundColor = QColor(80, 160, 240, alpha);
        const QImage image = snapshot(&scene);
        const QColor atom = image.pixelColor(atomPixel);
        const QColor bond = image.pixelColor(bondPixel);
        if (std::abs(image.pixelColor(2, 2).alpha() - alpha) > 1 ||
            atom.alpha() != 255 || bond.alpha() != 255) {
            std::cerr << "Background must retain alpha while atoms and bonds stay opaque\n";
            return false;
        }
        if (alpha == 255) {
            atomReference = atom;
            bondReference = bond;
        } else if (atom != atomReference || bond != bondReference) {
            std::cerr << "Object colors must not blend with the background\n";
            return false;
        }
        const QImage empty = snapshot(nullptr);
        if (std::abs(empty.pixelColor(2, 2).alpha() - alpha) > 1) {
            std::cerr << "An empty scene must retain background alpha\n";
            return false;
        }
    }

    if (rayTracing) {
        settings.backgroundColor = QColor(80, 160, 240);
        snapshot(&scene);
        if (!checkBackgroundCompositing(
                static_cast<render::RayTracingRenderer&>(renderer), settings, [&]() {
                    fbo.bind();
                    renderer.render(camera, settings);
                    gl.glFinish();
                    return fbo.toImage();
                })) return false;

        // The second sphere blocks the light at the first sphere's front face.
        scene.bonds().clear();
        scene.setPosition(1, 0.0f, 0.0f, 1.55f);
        const QColor lit = snapshot(&scene).pixelColor(atomPixel);
        settings.enableShadows = true;
        const QColor shadowed = snapshot(&scene).pixelColor(atomPixel);
        if (shadowed.alpha() != 255 || lit.red() - shadowed.red() < 20) {
            std::cerr << "Opaque occluders must still cast shadows\n";
            return false;
        }
        settings.enableAmbientOcclusion = true;
        if (snapshot(&scene).pixelColor(atomPixel).alpha() != 255) {
            std::cerr << "Ambient occlusion must preserve object opacity\n";
            return false;
        }
    }
    if (rayTracing) {
        auto& rt = static_cast<render::RayTracingRenderer&>(renderer);
        rt.releaseStructure();
        if (rt.sampleCount() != 0) return false;
    } else {
        static_cast<render::OpenGLRenderer&>(renderer).releaseStructure();
    }
    // GPU scene buffers can be released and reused for another structure.
    if (snapshot(&scene).pixelColor(atomPixel).alpha() != 255) return false;
    if (!checkGizmoRendering(camera, settings, [&]() { return snapshot(&scene); })) return false;
    renderer.cleanup();
    return gl.glGetError() == GL_NO_ERROR;
}
} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QSurfaceFormat format;
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setAlphaBufferSize(8);
    QOpenGLContext context;
    context.setFormat(format);
    QOffscreenSurface surface;
    surface.setFormat(format);
    surface.create();
    if (!context.create() || !context.makeCurrent(&surface)) {
        std::cerr << "OpenGL 4.1 context unavailable; skipping GPU checks\n";
        return 77;
    }
    auto* gl = context.functions();
    render::OpenGLRenderer raster;
    if (!checkRenderer(raster, *gl, false)) return 1;
    render::RayTracingRenderer rayTracing;
    return checkRenderer(rayTracing, *gl, true) ? 0 : 1;
}
