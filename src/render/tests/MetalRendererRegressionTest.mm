#include "common/Camera.h"
#include "metal/MetalRenderer.h"
#include "metal/MetalRayTracingRenderer.h"
#include "Structure.h"
#include "BondList.h"
#import <Metal/Metal.h>
#include <QElapsedTimer>
#include <QImage>
#include <QThread>
#include <iostream>

using namespace atom;
namespace {
constexpr int size = 256;
bool check(bool ok, const char* message) {
    if (!ok) std::cerr << message << '\n';
    return ok;
}
void* texture(render::metal::MetalRenderer& r, uint64_t& token) { return r.colorTexture(token); }
void* texture(render::metal::MetalRayTracingRenderer& r, uint64_t& token) { return r.outputTexture(token); }
QImage readImage(id<MTLTexture> t, id<MTLCommandQueue> queue) {
    if (!t) return {};
    const NSUInteger row = t.width * 4;
    id<MTLBuffer> pixels = [t.device newBufferWithLength:row * t.height options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> command = [queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
    [blit copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
              sourceSize:MTLSizeMake(t.width,t.height,1) toBuffer:pixels destinationOffset:0
  destinationBytesPerRow:row destinationBytesPerImage:row*t.height];
    [blit endEncoding]; [command commit]; [command waitUntilCompleted];
    if (command.status != MTLCommandBufferStatusCompleted) return {};
    return QImage(static_cast<const uchar*>(pixels.contents), int(t.width), int(t.height), int(row),
                  QImage::Format_ARGB32_Premultiplied).copy();
}
template<class Renderer>
QImage draw(Renderer& r, const render::Camera& camera, render::RenderSettings& settings,
            id<MTLCommandQueue> queue) {
    ++settings.frameRequestToken;
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < 10000) {
        r.render(camera, settings);
        uint64_t token = 0;
        auto t = (__bridge id<MTLTexture>)texture(r, token);
        if (t && token == settings.frameRequestToken) return readImage(t, queue);
        QThread::msleep(1);
    }
    return {};
}
QImage converge(render::metal::MetalRayTracingRenderer& r, const render::Camera& camera,
                 render::RenderSettings& settings, id<MTLCommandQueue> queue) {
    QImage image;
    do { image = draw(r, camera, settings, queue); }
    while (!image.isNull() && !r.isConverged());
    // sampleCount() counts submitted samples. Drain the final submission before
    // comparing images or asserting idleness; an earlier batch can have the same token.
    QElapsedTimer timer; timer.start();
    while (!image.isNull() && timer.elapsed() < 10000) {
        r.render(camera, settings);
        uint64_t token = 0;
        auto t = (__bridge id<MTLTexture>)r.outputTexture(token);
        if (!r.needsMoreFrames()) return readImage(t, queue);
        QThread::msleep(1);
    }
    return {};
}

bool checkOutlineBounds(id<MTLDevice> device, id<MTLCommandQueue> queue) {
    render::metal::MetalRayTracingRenderer rt;
    rt.setDevice((__bridge void*)device);
    if (!rt.initialize()) return false;
    rt.resize(512,512);
    render::Camera camera;
    camera.setPresetView(render::ViewDirection::PlusZ);
    camera.setProjection(false); camera.setAspectRatio(1); camera.setOrthoScale(3);
    data::Structure scene;
    for (int i=0; i<8; ++i) {
        const float x=(i%4-1.5f)*1.4f, y=(i/4-.5f)*2.f;
        scene.addAtom(x-.35f,y-.35f,0,6); scene.addAtom(x+.35f,y+.35f,0,6);
        scene.radii()[i*2]=scene.radii()[i*2+1]=.001f;
        scene.bonds().addBond(i*2,i*2+1); scene.bonds().setRadius(i,.01f);
    }
    auto geometry=render::prepareGeometry(render::captureGeometry(&scene));
    auto referenceGeometry=std::make_shared<render::PreparedGeometry>(*geometry);
    // Disable spatial rejection without changing the geometry or shader tests.
    // Any pixel difference exposes a nonconservative traversal bound.
    for (auto& node : referenceGeometry->bvh.nodes) for (int axis=0; axis<3; ++axis) {
        node.minAndMaxRadius[axis]=-10000; node.maxAndPad[axis]=10000;
    }
    render::RenderSettings settings;
    settings.maxRTSamples=1; settings.showAtoms=false; settings.showUnitCell=false;
    settings.showViewportAxes=false;
    for (bool perspective : {false,true}) for (bool selected : {false,true}) {
        camera.setProjection(perspective); camera.setDistance(10);
        scene.bonds().setSelected(0,selected);
        for (float width : {1.f,2.f,4.f,8.f}) {
            settings.outlineWidth=width;
            rt.setPreparedStructure(&scene,geometry);
            const auto image=converge(rt,camera,settings,queue);
            rt.setPreparedStructure(&scene,referenceGeometry);
            if (!check(!image.isNull() && image==converge(rt,camera,settings,queue),
                       "BVH bounds clipped a bond outline")) return false;
        }
    }
    return true;
}

bool checkCameraMotion(id<MTLDevice> device, id<MTLCommandQueue> queue) {
    render::metal::MetalRayTracingRenderer rt;
    rt.setDevice((__bridge void*)device);
    if (!rt.initialize()) return false;
    rt.resize(768,512);
    data::Structure scene;
    for (int i=0; i<12000; ++i)
        scene.addAtom((i%40)*1.2f, ((i/40)%30)*1.2f, (i/1200)*1.2f, 6);
    const auto prepared=render::prepareGeometry(render::captureGeometry(&scene));
    rt.setPreparedStructure(&scene,prepared);
    render::Camera camera;
    camera.setProjection(false); camera.setOrthoScale(25); camera.setAspectRatio(1.5f);
    camera.setTarget(QVector3D(23,17,5)); camera.setSceneExtent(60);
    render::RenderSettings settings;
    settings.maxRTSamples=1; settings.showBonds=false; settings.showUnitCell=false;
    settings.showViewportAxes=false; settings.enableAmbientOcclusion=true; settings.aoSamples=32;
    if (converge(rt,camera,settings,queue).isNull()) return false;

    uint64_t previous=0;
    rt.outputTexture(previous);
    int updates=0;
    QElapsedTimer timer; timer.start();
    // Submit newer camera requests while a slower RT sample is still in flight.
    // Finished intermediate views must remain presentable during continuous input.
    while (timer.elapsed() < 1000) {
        camera.orbit(.3f,.1f);
        ++settings.frameRequestToken;
        rt.render(camera,settings);
        uint64_t token=0;
        rt.outputTexture(token);
        if (token != previous) { previous=token; ++updates; }
        QThread::msleep(1);
    }
    if (!check(updates>0, "Continuous camera motion discarded every completed RT frame")) return false;

    // Once movement stops, accumulated samples must belong only to the latest camera.
    settings.maxRTSamples=4;
    const auto settled=converge(rt,camera,settings,queue);
    rt.resetAccumulation();
    if (!check(!settled.isNull() && settled==converge(rt,camera,settings,queue),
               "Camera motion mixed samples from different views")) return false;

    // Destroy resources while a slower sample is still running, then recreate
    // the renderer. Old completions must not publish into the new output state.
    for (int i=0; i<4; ++i) {
        rt.resetAccumulation();
        rt.render(camera,settings);
        rt.cleanup();
        if (!rt.initialize()) return false;
        rt.resize(768,512);
        rt.setPreparedStructure(&scene,prepared);
        if (!check(settled==converge(rt,camera,settings,queue),
                   "Renderer recreation reused an old submission")) return false;
    }
    return true;
}
}

int main() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return 77;
        id<MTLCommandQueue> queue = [device newCommandQueue];
        render::metal::MetalRayTracingRenderer rt;
        render::metal::MetalRenderer raster;
        rt.setDevice((__bridge void*)device); raster.setDevice((__bridge void*)device);
        if (!rt.initialize() || !raster.initialize()) return 1;
        // ARC must release replaced Metal resources. A small size change may
        // cross allocation alignment boundaries, but must not retain every target.
        NSUInteger firstAllocation = 0;
        for (int i=0; i<12; ++i) {
            @autoreleasepool { rt.resize(1024+i,768); raster.resize(1024+i,768); }
            if (i == 0) firstAllocation = device.currentAllocatedSize;
        }
        if (!check(device.currentAllocatedSize < firstAllocation + 16*1024*1024,
                   "Repeated resize leaked Metal targets")) return 1;
        rt.resize(size,size); raster.resize(size,size);
        render::Camera camera;
        camera.setPresetView(render::ViewDirection::PlusZ);
        camera.setProjection(false); camera.setOrthoScale(3); camera.setAspectRatio(1);
        render::RenderSettings settings;
        settings.showUnitCell=false; settings.showViewportAxes=false; settings.showBonds=false;
        settings.outlineEnabled=false; settings.maxRTSamples=8;
        settings.backgroundColor=QColor(20,50,100);
        data::Structure single; single.addAtom(0,0,0,6); single.radii()[0]=.7f;
        rt.setStructure(&single);
        if (!check(!converge(rt,camera,settings,queue).isNull(), "Bond-free RT frame failed")) return 1;

        uint64_t token=0; void* previous=rt.outputTexture(token);
        for (int i=0;i<8;++i) {
            rt.render(camera,settings);
            if (!check(!rt.needsMoreFrames(), "Converged unchanged RT must be idle")) return 1;
            if (!check(rt.outputTexture(token)==previous, "Idle RT replaced its output")) return 1;
        }
        settings.showAtoms=false;
        auto hidden=converge(rt,camera,settings,queue);
        if (!check(!hidden.isNull() && hidden.pixelColor(128,128)==settings.backgroundColor,
                   "Atom visibility must reset accumulation")) return 1;

        rt.setStructure(nullptr);
        for (int alpha : {0,64,255}) {
            settings.backgroundColor=QColor(20,50,100,alpha);
            for (bool axes : {false,true}) {
                settings.showViewportAxes=axes;
                auto empty=draw(rt,camera,settings,queue);
                if (!check(!empty.isNull() && std::abs(empty.pixelColor(245,245).alpha()-alpha)<=1,
                           "Empty RT background alpha changed with overlays")) return 1;
                rt.render(camera,settings);
                if (!check(!rt.needsMoreFrames(), "Empty RT must become idle")) return 1;
            }
        }
        // A lattice with no atoms exercises zero-count unit-cell shader bindings.
        data::Structure latticeOnly;
        latticeOnly.lattice().defined=true;
        latticeOnly.lattice().matrix={{{4,0,0},{0,4,0},{0,0,-4}}};
        rt.setStructure(&latticeOnly); settings.showUnitCell=true;
        if (!check(!draw(rt,camera,settings,queue).isNull(),"Lattice-only RT failed")) return 1;

        data::Structure bonds;
        bonds.addAtom(-1,.25f,1,6); bonds.addAtom(2,.25f,1,6);
        bonds.bonds().addBond(0,1); bonds.bonds().setRadius(0,.5f);
        bonds.lattice()=latticeOnly.lattice();
        rt.setStructure(&bonds);
        settings.showBonds=true; settings.showViewportAxes=false; settings.unitCellThickness=.04f;
        settings.backgroundColor=QColor(255,255,255); settings.maxRTSamples=1; settings.bondRadius=.1f;
        auto thin=draw(rt,camera,settings,queue);
        settings.bondRadius=.5f; auto thick=draw(rt,camera,settings,queue);
        if (!check(!thin.isNull() && thin==thick, "Unit-cell occlusion must use stored bond radii")) return 1;

        // Identical sample sequence split across requests vs an adaptive batch.
        settings.showAtoms=true; settings.showUnitCell=false; settings.showViewportAxes=true;
        settings.enableAmbientOcclusion=true; settings.enableShadows=true;
        settings.outlineEnabled=true; settings.backgroundColor=QColor(60,80,110,64);
        bonds.setAtomSelected(0,true); bonds.bonds().setSelected(0,true);
        rt.setStructure(&bonds); settings.maxRTSamples=32;
        auto batched=converge(rt,camera,settings,queue);
        rt.setStructure(&bonds);
        QImage individual;
        for(int i=1;i<=32;++i) { settings.maxRTSamples=i; individual=draw(rt,camera,settings,queue); }
        if (!check(!batched.isNull() && batched==individual, "RT batches changed the accumulated image")) return 1;

        // Cached preparation and narrow appearance updates must match a fresh upload.
        settings.maxRTSamples=4;
        auto prepared=render::prepareGeometry(render::captureGeometry(&bonds));
        rt.setPreparedStructure(&bonds,prepared);
        auto cached=converge(rt,camera,settings,queue);
        rt.setStructure(&bonds);
        if (!check(cached==converge(rt,camera,settings,queue), "Prepared RT geometry changed pixels")) return 1;
        raster.setPreparedStructure(&bonds,prepared);
        auto rasterCached=draw(raster,camera,settings,queue);
        raster.setStructure(&bonds);
        if (!check(rasterCached==draw(raster,camera,settings,queue), "Prepared raster geometry changed pixels")) return 1;
        bonds.setAtomSelected(0,false); bonds.bonds().setSelected(0,false);
        bonds.colorsR()[0]=.1f; bonds.colorsG()[0]=.9f;
        bonds.bonds().setEndpointColors(0,{.2f,.8f,.3f},{.9f,.2f,.4f});
        rt.invalidateAppearance(); raster.invalidateAppearance();
        auto updatedRT=converge(rt,camera,settings,queue);
        auto updatedRaster=draw(raster,camera,settings,queue);
        rt.setStructure(&bonds); raster.setStructure(&bonds);
        if (!check(updatedRT==converge(rt,camera,settings,queue) &&
                   updatedRaster==draw(raster,camera,settings,queue), "Appearance update differs from full upload")) return 1;

        // Resize and replace a scene while an earlier frame may still be in flight.
        for (int i=0;i<12;++i) {
            rt.render(camera,settings);
            rt.resize(size-i,size-i);
            rt.setStructure(i%2 ? &single : &bonds);
            auto resized=draw(rt,camera,settings,queue);
            if (!check(!resized.isNull() && resized.width()==size-i,"Resize published an invalid frame")) return 1;
        }

        settings=render::RenderSettings{};
        settings.showBonds=false; settings.showUnitCell=false; settings.showViewportAxes=false;
        settings.outlineEnabled=false; settings.backgroundColor=QColor(20,50,100);
        camera.setProjection(true); camera.setFieldOfView(120); camera.setDistance(10);
        data::Structure offAxis; offAxis.addAtom(8,0,0,6); offAxis.radii()[0]=2;
        raster.setStructure(&offAxis);
        auto sphere=draw(raster,camera,settings,queue);
        if (sphere.isNull()) return 1;
        auto inverse=camera.viewProjectionMatrix().inverted(); auto origin=camera.position();
        for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
            QVector4D target=inverse*QVector4D(2*(x+.5f)/size-1,1-2*(y+.5f)/size,-1,1); target/=target.w();
            auto direction=(target.toVector3D()-origin).normalized(); auto oc=origin-QVector3D(8,0,0);
            auto perpendicular=oc-QVector3D::dotProduct(oc,direction)*direction;
            if (4-perpendicular.lengthSquared()>.4f && sphere.pixelColor(x,y)==settings.backgroundColor)
                return check(false,"Raster billboard clipped an interior sphere pixel") ? 0 : 1;
        }
        // A sphere containing the camera must retain coverage when the proxy
        // crosses the near/camera planes and conservative early-Z is disabled.
        camera.setDistance(.1f);
        raster.setStructure(&single);
        auto inside=draw(raster,camera,settings,queue);
        if (!check(!inside.isNull() && inside.pixelColor(128,128)!=settings.backgroundColor &&
                   inside.pixelColor(10,10)!=settings.backgroundColor,
                   "Camera-plane sphere proxy lost coverage")) return 1;
        settings.showAtoms=false;
        const auto emptyRaster=draw(raster,camera,settings,queue);
        data::Structure behind;
        for (int i=0; i<256; ++i) behind.addAtom(0,0,20,6);
        raster.setStructure(&behind); settings.showAtoms=true;
        if (!check(emptyRaster==draw(raster,camera,settings,queue),
                   "Spheres behind the camera changed visible pixels")) return 1;
        // Cleanup and recreate the raster renderer with a command still pending.
        ++settings.frameRequestToken; raster.render(camera,settings); raster.cleanup();
        if (!raster.initialize()) return 1;
        raster.resize(size,size); raster.setStructure(&behind);
        if (!check(emptyRaster==draw(raster,camera,settings,queue),
                   "Raster recreation reused an old submission")) return 1;
        if (!checkOutlineBounds(device,queue) || !checkCameraMotion(device,queue)) return 1;
        std::cout << "Metal renderer image, validation, motion, idle, batch, appearance, memory, and resize regressions passed\n";
        return 0;
    }
}
