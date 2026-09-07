#include "common/Camera.h"
#include "metal/MetalRayTracingRenderer.h"
#include "Structure.h"
#include "BondList.h"
#include "BackgroundCompositingCheck.h"

#import <Metal/Metal.h>
#include <QElapsedTimer>
#include <QThread>

using namespace atom;

int main() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            std::cerr << "Metal device unavailable; skipping GPU checks\n";
            return 77;
        }
        constexpr int size = 128;
        render::metal::MetalRayTracingRenderer renderer;
        renderer.setDevice((__bridge void*)device);
        if (!renderer.initialize()) return 1;
        renderer.resize(size, size);
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
        renderer.setStructure(&scene);
        render::RenderSettings settings;
        settings.showUnitCell = false;
        settings.showViewportAxes = false;
        settings.outlineEnabled = false;
        settings.specularStrength = 0.0f;
        settings.enableShadows = true;
        settings.enableAmbientOcclusion = true;
        settings.maxRTSamples = 16;

        id<MTLCommandQueue> queue = [device newCommandQueue];
        auto draw = [&]() -> QImage {
            ++settings.frameRequestToken;
            QElapsedTimer timer;
            timer.start();
            id<MTLTexture> texture = nil;
            while (timer.elapsed() < 10000) {
                renderer.render(camera, settings);
                uint64_t token = 0;
                texture = (__bridge id<MTLTexture>)renderer.outputTexture(token);
                if (texture && token == settings.frameRequestToken) break;
                QThread::msleep(1);
                texture = nil;
            }
            if (!texture) return {};
            id<MTLBuffer> pixels = [device newBufferWithLength:size * size * 4
                                                     options:MTLResourceStorageModeShared];
            id<MTLCommandBuffer> command = [queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
            [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(size, size, 1)
                        toBuffer:pixels destinationOffset:0 destinationBytesPerRow:size * 4
         destinationBytesPerImage:size * size * 4];
            [blit endEncoding];
            [command commit];
            [command waitUntilCompleted];
            if (command.status != MTLCommandBufferStatusCompleted) return {};
            return QImage(static_cast<const uchar*>(pixels.contents), size, size, size * 4,
                          QImage::Format_ARGB32_Premultiplied).copy();
        };
        for (int i = 0; i < settings.maxRTSamples && !renderer.isConverged(); ++i) {
            if (draw().isNull()) return 1;
        }
        if (!renderer.isConverged()) return 1;
        if (!checkBackgroundCompositing(renderer, settings, draw)) return 1;
        // Also exercise the multisampled display path used when axes are visible.
        settings.showViewportAxes = true;
        return checkBackgroundCompositing(renderer, settings, draw) ? 0 : 1;
    }
}
