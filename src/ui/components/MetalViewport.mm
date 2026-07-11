#import <Metal/Metal.h>

#include "MetalViewport.h"
#include "StructureModel.h"
#include "ViewportSelection.h"
#include "../../render/common/Camera.h"
#include "../../render/common/RenderStateHash.h"
#include "../../render/metal/MetalRenderer.h"
#include "../../render/metal/MetalRayTracingRenderer.h"
#include "../../data/Structure.h"
#include "../../data/BondList.h"
#include "../../data/NeighborList.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGRendererInterface>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDateTime>
#include <QTimer>
#include <QDebug>
#include <QMetaObject>
#include <QRunnable>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <algorithm>
#include <vector>

// Qt 6 native interface for wrapping Metal textures
#include <QSGTexture>

namespace atom::ui {

namespace {

data::ElementColorScheme colorSchemeFromIndex(int index) {
    return index == 1 ? data::ElementColorScheme::Cpk
                      : data::ElementColorScheme::Jmol;
}

} // namespace

// ---------------------------------------------------------------------------
// PIMPL — holds Metal renderer instances
// ---------------------------------------------------------------------------

struct MetalViewport::Impl {
    struct TextureCacheEntry {
        void* nativeTexture = nullptr;
        QSize size;
        QQuickWindow* window = nullptr;
        QSGTexture* wrapper = nullptr;
        uint64_t lastUsedTick = 0;
    };

    static constexpr std::size_t kMaxCachedTextures = 8;

    std::unique_ptr<render::metal::MetalRenderer> rasterRenderer;
    std::unique_ptr<render::metal::MetalRayTracingRenderer> rtRenderer;
    render::Renderer* activeRenderer = nullptr;
    int currentMode = 0;

    std::vector<TextureCacheEntry> textureCache; // Owned by viewport cache
    uint64_t textureCacheTick = 0;

    QSGTexture* findCachedTexture(void* nativeTexture, const QSize& size, QQuickWindow* window) {
        for (auto& entry : textureCache) {
            if (entry.nativeTexture == nativeTexture &&
                entry.size == size &&
                entry.window == window) {
                entry.lastUsedTick = ++textureCacheTick;
                return entry.wrapper;
            }
        }
        return nullptr;
    }

    void insertCachedTexture(void* nativeTexture,
                             const QSize& size,
                             QQuickWindow* window,
                             QSGTexture* wrapper) {
        textureCache.push_back(TextureCacheEntry{
            nativeTexture,
            size,
            window,
            wrapper,
            ++textureCacheTick
        });
    }

    void pruneTextureCache(QSGTexture* keepTexture = nullptr) {
        while (textureCache.size() > kMaxCachedTextures) {
            std::size_t oldestIndex = textureCache.size();
            uint64_t oldestTick = UINT64_MAX;

            for (std::size_t i = 0; i < textureCache.size(); ++i) {
                const auto& entry = textureCache[i];
                if (!entry.wrapper || entry.wrapper == keepTexture) {
                    continue;
                }
                if (entry.lastUsedTick < oldestTick) {
                    oldestTick = entry.lastUsedTick;
                    oldestIndex = i;
                }
            }

            if (oldestIndex == textureCache.size()) {
                break;
            }

            delete textureCache[oldestIndex].wrapper;
            textureCache.erase(textureCache.begin() + static_cast<std::ptrdiff_t>(oldestIndex));
        }
    }

    std::vector<QSGTexture*> takeAllCachedTextures() {
        std::vector<QSGTexture*> textures;
        textures.reserve(textureCache.size());
        for (const auto& entry : textureCache) {
            if (entry.wrapper) {
                textures.push_back(entry.wrapper);
            }
        }
        textureCache.clear();
        return textures;
    }
};

class TextureCacheCleanupJob final : public QRunnable {
public:
    explicit TextureCacheCleanupJob(std::vector<QSGTexture*> textures)
        : m_textures(std::move(textures)) {}

    void run() override {
        for (QSGTexture* texture : m_textures) {
            delete texture;
        }
    }

private:
    std::vector<QSGTexture*> m_textures;
};

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

MetalViewport::MetalViewport(QQuickItem* parent)
    : QQuickItem(parent)
    , m_impl(std::make_unique<Impl>())
    , m_camera(std::make_unique<render::Camera>())
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);

    m_lastFrameTime = QDateTime::currentMSecsSinceEpoch();

    // Connect to StructureModel for file loads
    QTimer::singleShot(0, this, [this]() {
        if (auto* model = StructureModel::instance()) {
            connect(model, &StructureModel::structureUpdated,
                    this, &MetalViewport::setStructure);
            connect(model, &StructureModel::structureEdited,
                    this, &MetalViewport::setEditedStructure);
            connect(model, &StructureModel::structureStyleChanged,
                    this, &MetalViewport::onStructureStyleChanged);
            connect(model, &StructureModel::structureGeometryChanged,
                    this, &MetalViewport::onStructureGeometryChanged);
        }
    });
}

MetalViewport::~MetalViewport() = default;

// ---------------------------------------------------------------------------
// Properties (identical to OpenGLViewport)
// ---------------------------------------------------------------------------

int MetalViewport::atomCount() const {
    return m_structure ? static_cast<int>(m_structure->atomCount()) : 0;
}

int MetalViewport::bondCount() const {
    return m_structure ? static_cast<int>(m_structure->bonds().bondCount()) : 0;
}

float MetalViewport::fps() const { return m_fps; }
qulonglong MetalViewport::frameToken() const { return m_frameToken; }
QString MetalViewport::hoverStatus() const { return m_hoverStatus; }
bool MetalViewport::showBonds() const { return m_showBonds; }
QColor MetalViewport::backgroundColor() const { return m_backgroundColor; }
bool MetalViewport::showUnitCell() const { return m_showUnitCell; }
float MetalViewport::unitCellThickness() const { return m_unitCellThickness; }
QColor MetalViewport::unitCellColor() const { return m_unitCellColor; }
float MetalViewport::atomScale() const { return m_atomScale; }
int MetalViewport::atomColorScheme() const { return m_atomColorScheme; }
float MetalViewport::bondRadius() const { return m_bondRadius; }
int MetalViewport::rendererMode() const { return m_rendererMode; }
int MetalViewport::sampleCount() const { return m_sampleCount; }
int MetalViewport::maxRTSamples() const { return m_maxRTSamples; }
bool MetalViewport::enableAO() const { return m_enableAO; }
bool MetalViewport::enableShadows() const { return m_enableShadows; }
float MetalViewport::shadowOpacity() const { return m_shadowOpacity; }
int MetalViewport::aoSamples() const { return m_aoSamples; }
float MetalViewport::aoRadius() const { return m_aoRadius; }
float MetalViewport::ambientStrength() const { return m_ambientStrength; }
float MetalViewport::diffuseStrength() const { return m_diffuseStrength; }
float MetalViewport::specularStrength() const { return m_specularStrength; }
float MetalViewport::shininess() const { return m_shininess; }
float MetalViewport::lightAzimuth() const { return m_lightAzimuth; }
float MetalViewport::lightElevation() const { return m_lightElevation; }
bool MetalViewport::outlineEnabled() const { return m_outlineEnabled; }
float MetalViewport::outlineWidth() const { return m_outlineWidth; }
QColor MetalViewport::outlineColor() const { return m_outlineColor; }
bool MetalViewport::showViewportAxes() const { return m_showViewportAxes; }
float MetalViewport::viewportAxesX() const { return m_viewportAxesX; }
float MetalViewport::viewportAxesY() const { return m_viewportAxesY; }
float MetalViewport::viewportAxesScale() const { return m_viewportAxesScale; }
bool MetalViewport::isPerspective() const { return m_camera->isPerspective(); }
float MetalViewport::fieldOfView() const { return m_camera->fieldOfView(); }
int MetalViewport::rotationConstraint() const {
    return static_cast<int>(m_camera->rotationConstraint());
}

QVariantList MetalViewport::getAxisDirections() const {
    QMatrix4x4 view = m_camera->viewMatrix();
    return {
        view(0, 0), -view(1, 0), view(2, 0),
        view(0, 1), -view(1, 1), view(2, 1),
        view(0, 2), -view(1, 2), view(2, 2)
    };
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void MetalViewport::setStructure(std::shared_ptr<data::Structure> structure) {
    m_structure = structure;
    setHoverStatus(QString());
    if (m_structure) {
        const auto scheme = colorSchemeFromIndex(m_atomColorScheme);
        m_structure->updateColorsFromElements(scheme);
        m_structure->updateBondColorsFromElements(scheme);
    }
    m_needsStructureUpdate = true;

    emit atomCountChanged();
    emit bondCountChanged();

    if (m_structure) {
        fitToView();
        startBondDetection();
    }
    update();
}

void MetalViewport::setEditedStructure(std::shared_ptr<data::Structure> structure) {
    m_structure = structure;
    m_bondTaskPending = false;
    m_pendingStructure.reset();
    setHoverStatus(QString());
    m_needsStructureUpdate = true;

    emit atomCountChanged();
    emit bondCountChanged();
    update();
}

void MetalViewport::fitToView() {
    if (!m_structure || m_structure->atomCount() == 0) return;

    const auto bbox = m_structure->computeViewBoundingBox();
    const auto centerData = m_structure->hasLattice()
        ? m_structure->unitCellCenter()
        : m_structure->geometricCenter();
    QVector3D center(centerData[0], centerData[1], centerData[2]);
    float extent = bbox.maxExtent();

    m_camera->fitToView(center, extent > 0 ? extent : 10.0f);
    emit cameraChanged();
    update();
}

void MetalViewport::resetCamera() {
    m_camera->reset();
    if (m_structure) {
        fitToView();
    } else {
        emit cameraChanged();
    }
    update();
}

void MetalViewport::setViewDirection(int direction) {
    if (direction < 0 || direction > 5) return;
    m_camera->setPresetView(static_cast<render::ViewDirection>(direction));
    emit cameraChanged();
    update();
}

void MetalViewport::setShowBonds(bool show) {
    if (m_showBonds != show) {
        m_showBonds = show;
        emit showBondsChanged();
        if (!m_hoverStatus.isEmpty()) {
            updateHoverStatus(m_lastMousePos);
        }
        update();
    }
}

void MetalViewport::setBackgroundColor(const QColor& color) {
    if (m_backgroundColor != color) {
        m_backgroundColor = color;
        emit backgroundColorChanged();
        update();
    }
}

void MetalViewport::setShowUnitCell(bool show) {
    if (m_showUnitCell != show) {
        m_showUnitCell = show;
        emit showUnitCellChanged();
        update();
    }
}

void MetalViewport::setUnitCellThickness(float thickness) {
    const float clamped = std::clamp(thickness, 0.01f, 2.0f);
    if (!qFuzzyCompare(m_unitCellThickness, clamped)) {
        m_unitCellThickness = clamped;
        emit unitCellThicknessChanged();
        update();
    }
}

void MetalViewport::setUnitCellColor(const QColor& color) {
    QColor opaque = color;
    opaque.setAlpha(255);
    if (m_unitCellColor != opaque) {
        m_unitCellColor = opaque;
        emit unitCellColorChanged();
        update();
    }
}

void MetalViewport::setAtomScale(float scale) {
    if (auto* model = StructureModel::instance()) {
        if (model->selectionEnabled()) {
            model->applyAtomScaleToSelection(scale, m_atomScale);
            return;
        }
    }
    if (!qFuzzyCompare(m_atomScale, scale)) {
        m_atomScale = scale;
        emit atomScaleChanged();
        if (!m_hoverStatus.isEmpty()) {
            updateHoverStatus(m_lastMousePos);
        }
        update();
    }
}

void MetalViewport::setAtomColorScheme(int scheme) {
    scheme = (scheme == 1) ? 1 : 0;
    if (auto* model = StructureModel::instance()) {
        if (model->selectionEnabled()) {
            model->applyAtomColorSchemeToSelection(scheme);
            return;
        }
    }
    if (m_atomColorScheme == scheme) return;

    m_atomColorScheme = scheme;
    emit atomColorSchemeChanged();

    if (m_structure) {
        const auto scheme = colorSchemeFromIndex(m_atomColorScheme);
        m_structure->updateColorsFromElements(scheme);
        m_structure->updateBondColorsFromElements(scheme);
        m_needsAppearanceUpdate = true;  // colors only — geometry unchanged
    }
    update();
}

float MetalViewport::bondScale() const {
    return m_bondScale;
}

void MetalViewport::setBondScale(float scale) {
    if (!std::isfinite(scale)) return;
    scale = std::clamp(scale, 0.1f, 5.0f);
    if (qFuzzyCompare(m_bondScale, scale)) return;
    m_bondScale = scale;
    emit bondScaleChanged();
    if (m_structure) {
        startBondDetection();
    }
}

void MetalViewport::setBondRadius(float radius) {
    if (!std::isfinite(radius)) return;
    const float clamped = std::clamp(radius, 0.01f, 0.6f);
    if (auto* model = StructureModel::instance()) {
        if (model->selectionEnabled()) {
            model->applyBondRadiusToSelection(clamped);
            return;
        }
    }
    if (!qFuzzyCompare(m_bondRadius, clamped)) {
        m_bondRadius = clamped;
        if (m_structure) {
            m_structure->bonds().setAllRadii(m_bondRadius);
            m_needsStructureUpdate = true;
        }
        emit bondRadiusChanged();
        if (!m_hoverStatus.isEmpty()) {
            updateHoverStatus(m_lastMousePos);
        }
        update();
    }
}

void MetalViewport::startBondDetection() {
    if (!m_structure) return;

    if (m_bondTaskRunning) {
        // Coalesce: remember the latest request; launchBondTask will pick it up.
        m_bondTaskPending  = true;
        m_pendingStructure = m_structure;
        m_pendingScale     = m_bondScale;
        return;
    }
    launchBondTask(m_structure, m_bondScale);
}

void MetalViewport::launchBondTask(
    std::shared_ptr<data::Structure> structure, float scale)
{
    if (!m_bondWatcher) {
        m_bondWatcher = new QFutureWatcher<BondResult>(this);
        connect(m_bondWatcher, &QFutureWatcher<BondResult>::finished,
                this, &MetalViewport::onBondsReady);
    }
    m_bondTaskRunning = true;
    m_bondTaskPending = false;

    m_bondWatcher->setFuture(
        QtConcurrent::run([structure, scale]() -> BondResult {
            data::NeighborList nl;
            nl.build(*structure, scale);
            return {nl.buildBondList(*structure, scale), structure};
        }));
}

void MetalViewport::onBondsReady() {
    m_bondTaskRunning = false;

    if (m_bondWatcher && m_structure) {
        BondResult result = m_bondWatcher->result();
        // Discard if the structure has changed since the task was launched.
        if (result.structure == m_structure && result.bonds) {
            result.bonds->setAllRadii(m_bondRadius);
            m_structure->setBondList(std::move(result.bonds));
            m_structure->updateBondColorsFromElements(colorSchemeFromIndex(m_atomColorScheme));
            m_needsStructureUpdate = true;
            emit bondCountChanged();
            if (auto* model = StructureModel::instance())
                model->notifyBondsUpdated();
            if (!m_hoverStatus.isEmpty()) {
                updateHoverStatus(m_lastMousePos);
            }
            update();
        }
    }

    if (m_bondTaskPending && m_pendingStructure) {
        auto s   = std::move(m_pendingStructure);
        float sc = m_pendingScale;
        launchBondTask(std::move(s), sc);
    }
}

void MetalViewport::setRendererMode(int mode) {
    if (m_rendererMode != mode) {
        m_rendererMode = mode;
        emit rendererModeChanged();
        update();
    }
}

void MetalViewport::setMaxRTSamples(int samples) {
    if (samples < 1 || samples > 10000) {
        qWarning() << "MetalViewport: maxRTSamples must be in [1, 10000], got" << samples;
        return;
    }

    if (m_maxRTSamples != samples) {
        m_maxRTSamples = samples;
        emit maxRTSamplesChanged();
        update();
    }
}

void MetalViewport::setEnableAO(bool enable) {
    if (m_enableAO != enable) {
        m_enableAO = enable;
        emit enableAOChanged();
        update();
    }
}

void MetalViewport::setEnableShadows(bool enable) {
    if (m_enableShadows != enable) {
        m_enableShadows = enable;
        emit enableShadowsChanged();
        update();
    }
}

void MetalViewport::setShadowOpacity(float opacity) {
    const float clamped = std::clamp(opacity, 0.0f, 1.0f);
    if (!qFuzzyCompare(m_shadowOpacity, clamped)) {
        m_shadowOpacity = clamped;
        emit shadowOpacityChanged();
        update();
    }
}

void MetalViewport::setAOSamples(int samples) {
    const int clamped = std::clamp(samples, 1, 16);
    if (m_aoSamples != clamped) {
        m_aoSamples = clamped;
        emit aoSamplesChanged();
        update();
    }
}

void MetalViewport::setAORadius(float radius) {
    const float clamped = std::clamp(radius, 1.0f, 10.0f);
    if (!qFuzzyCompare(m_aoRadius, clamped)) {
        m_aoRadius = clamped;
        emit aoRadiusChanged();
        update();
    }
}

void MetalViewport::setAmbientStrength(float strength) {
    const float clamped = std::clamp(strength, 0.0f, 1.0f);
    if (!qFuzzyCompare(m_ambientStrength, clamped)) {
        m_ambientStrength = clamped;
        emit ambientStrengthChanged();
        update();
    }
}

void MetalViewport::setDiffuseStrength(float strength) {
    const float clamped = std::clamp(strength, 0.0f, 1.0f);
    if (!qFuzzyCompare(m_diffuseStrength, clamped)) {
        m_diffuseStrength = clamped;
        emit diffuseStrengthChanged();
        update();
    }
}

void MetalViewport::setSpecularStrength(float strength) {
    const float clamped = std::clamp(strength, 0.0f, 1.0f);
    if (!qFuzzyCompare(m_specularStrength, clamped)) {
        m_specularStrength = clamped;
        emit specularStrengthChanged();
        update();
    }
}

void MetalViewport::setShininess(float shininess) {
    const float clamped = std::clamp(shininess, 1.0f, 128.0f);
    if (!qFuzzyCompare(m_shininess, clamped)) {
        m_shininess = clamped;
        emit shininessChanged();
        update();
    }
}

void MetalViewport::setLightAzimuth(float value) {
    const float clamped = std::clamp(value, -180.0f, 180.0f);
    if (!qFuzzyCompare(m_lightAzimuth, clamped)) {
        m_lightAzimuth = clamped;
        emit lightAzimuthChanged();
        update();
    }
}

void MetalViewport::setLightElevation(float value) {
    const float clamped = std::clamp(value, -90.0f, 90.0f);
    if (!qFuzzyCompare(m_lightElevation, clamped)) {
        m_lightElevation = clamped;
        emit lightElevationChanged();
        update();
    }
}

void MetalViewport::setOutlineEnabled(bool enable) {
    if (m_outlineEnabled != enable) {
        m_outlineEnabled = enable;
        emit outlineEnabledChanged();
        update();
    }
}

void MetalViewport::setOutlineWidth(float width) {
    const float clamped = std::clamp(width, 0.5f, 4.0f);
    if (!qFuzzyCompare(m_outlineWidth, clamped)) {
        m_outlineWidth = clamped;
        emit outlineWidthChanged();
        update();
    }
}

void MetalViewport::setOutlineColor(const QColor& color) {
    if (m_outlineColor != color) {
        m_outlineColor = color;
        emit outlineColorChanged();
        update();
    }
}

void MetalViewport::setShowViewportAxes(bool show) {
    if (m_showViewportAxes != show) {
        m_showViewportAxes = show;
        emit showViewportAxesChanged();
        update();
    }
}

void MetalViewport::setViewportAxesX(float value) {
    if (!std::isfinite(value)) return;
    if (!qFuzzyCompare(m_viewportAxesX, value)) {
        m_viewportAxesX = value;
        emit viewportAxesXChanged();
        update();
    }
}

void MetalViewport::setViewportAxesY(float value) {
    if (!std::isfinite(value)) return;
    if (!qFuzzyCompare(m_viewportAxesY, value)) {
        m_viewportAxesY = value;
        emit viewportAxesYChanged();
        update();
    }
}

void MetalViewport::setViewportAxesScale(float value) {
    if (!std::isfinite(value)) return;
    const float clamped = std::clamp(value, 0.1f, 10.0f);
    if (!qFuzzyCompare(m_viewportAxesScale, clamped)) {
        m_viewportAxesScale = clamped;
        emit viewportAxesScaleChanged();
        update();
    }
}

void MetalViewport::setIsPerspective(bool perspective) {
    if (m_camera->isPerspective() != perspective) {
        m_camera->setProjection(perspective);
        emit projectionChanged();
        update();
    }
}

void MetalViewport::setFieldOfView(float fov) {
    if (!qFuzzyCompare(m_camera->fieldOfView(), fov)) {
        m_camera->setFieldOfView(fov);
        emit projectionChanged();
        update();
    }
}

void MetalViewport::setRotationConstraint(int constraint) {
    if (constraint < static_cast<int>(render::RotationConstraint::None) ||
        constraint > static_cast<int>(render::RotationConstraint::XZPlane)) {
        return;
    }

    const auto value = static_cast<render::RotationConstraint>(constraint);
    if (m_camera->rotationConstraint() != value) {
        m_camera->setRotationConstraint(value);
        emit rotationConstraintChanged();
    }
}

// ---------------------------------------------------------------------------
// Scene graph: updatePaintNode
// ---------------------------------------------------------------------------

QSGNode* MetalViewport::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    // This runs on the render thread with the GUI thread blocked.
    QQuickWindow* renderWindow = window();
    if (!renderWindow) {
        return oldNode;
    }

    // 1. Get Metal device from Qt's scene graph
    QSGRendererInterface* ri = renderWindow->rendererInterface();
    void* devicePtr = ri->getResource(renderWindow, QSGRendererInterface::DeviceResource);
    if (!devicePtr) {
        qWarning() << "MetalViewport: could not get Metal device from Qt";
        return oldNode;
    }

    // 2. Initialize renderers on first call
    if (!m_metalInitialized) {
        m_impl->rasterRenderer = std::make_unique<render::metal::MetalRenderer>();
        m_impl->rasterRenderer->setDevice(devicePtr);

        if (!m_impl->rasterRenderer->initialize()) {
            qCritical() << "MetalViewport: failed to initialize Metal raster renderer";
            return oldNode;
        }

        m_impl->activeRenderer = m_impl->rasterRenderer.get();
        m_impl->currentMode = 0;
        m_metalInitialized = true;
    }

    // 3. Handle renderer mode switching
    if (m_rendererMode != m_impl->currentMode) {
        m_impl->currentMode = m_rendererMode;

        if (m_impl->currentMode == 1) {
            // Switch to ray tracing
            if (!m_impl->rtRenderer) {
                m_impl->rtRenderer = std::make_unique<render::metal::MetalRayTracingRenderer>();
                m_impl->rtRenderer->setDevice(devicePtr);
                if (!m_impl->rtRenderer->initialize()) {
                    qCritical() << "MetalViewport: failed to init RT renderer";
                    m_impl->currentMode = 0;
                    m_rendererMode = 0;
                    emit rendererModeChanged();
                }
            }
            if (m_impl->rtRenderer) {
                m_impl->activeRenderer = m_impl->rtRenderer.get();
                if (m_structure) {
                    m_impl->rtRenderer->setStructure(m_structure.get());
                }
            }
        } else {
            m_impl->activeRenderer = m_impl->rasterRenderer.get();
            m_sampleCount = 0;
            emit sampleCountChanged();
            // Scene/appearance may have changed while RT was active (the
            // raster renderer only got dirty flags); force one re-render.
            m_lastRasterFrameHash = 0;
        }
    }

    // 4. Build render settings from viewport properties
    m_renderSettings.backgroundColor = m_backgroundColor;
    m_renderSettings.showBonds = m_showBonds;
    m_renderSettings.showAtoms = !(m_showBonds && m_atomScale <= 0.1001f);
    m_renderSettings.showUnitCell = m_showUnitCell;
    m_renderSettings.unitCellThickness = m_unitCellThickness;
    m_renderSettings.unitCellColor = m_unitCellColor;
    m_renderSettings.atomScale = m_atomScale;
    m_renderSettings.bondRadius = m_bondRadius;
    m_renderSettings.maxRTSamples = m_maxRTSamples;
    m_renderSettings.enableAmbientOcclusion = m_enableAO;
    m_renderSettings.enableShadows = m_enableShadows;
    m_renderSettings.shadowOpacity = m_shadowOpacity;
    m_renderSettings.aoSamples = m_aoSamples;
    m_renderSettings.aoRadius = m_aoRadius;
    m_renderSettings.ambientStrength = m_ambientStrength;
    m_renderSettings.diffuseStrength = m_diffuseStrength;
    m_renderSettings.specularStrength = m_specularStrength;
    m_renderSettings.shininess = m_shininess;
    m_renderSettings.lightAzimuth = m_lightAzimuth;
    m_renderSettings.lightElevation = m_lightElevation;
    m_renderSettings.showRotationCenter = m_showRotationCenter;
    m_renderSettings.rotationCenterX = m_camera->target().x();
    m_renderSettings.rotationCenterY = m_camera->target().y();
    m_renderSettings.rotationCenterZ = m_camera->target().z();

    bool sceneChangedThisFrame = false;
    if (m_needsStructureUpdate) {
        m_impl->activeRenderer->setStructure(m_structure.get());
        // Also update the other renderer for instant switching
        if (m_impl->currentMode == 0 && m_impl->rtRenderer) {
            m_impl->rtRenderer->setStructure(m_structure.get());
        } else if (m_impl->currentMode == 1 && m_impl->rasterRenderer) {
            m_impl->rasterRenderer->setStructure(m_structure.get());
        }
        m_needsStructureUpdate = false;
        m_needsAppearanceUpdate = false;  // full update covers appearance
        sceneChangedThisFrame = true;
    } else if (m_needsAppearanceUpdate) {
        m_impl->activeRenderer->invalidateAppearance();
        if (m_impl->currentMode == 0 && m_impl->rtRenderer) {
            m_impl->rtRenderer->invalidateAppearance();
        } else if (m_impl->currentMode == 1 && m_impl->rasterRenderer) {
            m_impl->rasterRenderer->invalidateAppearance();
        }
        m_needsAppearanceUpdate = false;
        sceneChangedThisFrame = true;
    }

    // 5. Resize
    qreal dpr = renderWindow->devicePixelRatio();
    int pw = static_cast<int>(width() * dpr);
    int ph = static_cast<int>(height() * dpr);
    m_renderSettings.outlineEnabled = m_outlineEnabled;
    m_renderSettings.outlineWidth = m_outlineWidth * static_cast<float>(dpr);
    m_renderSettings.outlineColor = m_outlineColor;
    m_renderSettings.showViewportAxes = m_showViewportAxes;
    m_renderSettings.viewportAxesScreenX = m_viewportAxesX * static_cast<float>(dpr);
    m_renderSettings.viewportAxesScreenY = m_viewportAxesY * static_cast<float>(dpr);
    m_renderSettings.viewportAxesScale = m_viewportAxesScale;
    m_renderSettings.viewportAxesPixelRatio = static_cast<float>(dpr);
    if (pw > 0 && ph > 0) {
        m_impl->activeRenderer->resize(pw, ph);
        m_camera->setAspectRatio(static_cast<float>(width()) / static_cast<float>(height()));
    }

    // 6. Render
    // Raster mode renders on demand: only when the frame state (camera,
    // settings, size) or the scene changed, or a previous request was
    // dropped while a frame was in flight. RT mode keeps its own gating
    // (state hash + convergence) internally.
    if (m_impl->currentMode == 1) {
        m_impl->activeRenderer->render(*m_camera, m_renderSettings);
    } else {
        const quint64 frameHash =
            render::computeRasterFrameHash(*m_camera, m_renderSettings, pw, ph);
        const bool needsRender = sceneChangedThisFrame ||
                                 frameHash != m_lastRasterFrameHash ||
                                 m_impl->rasterRenderer->hasPendingRender();
        if (needsRender) {
            m_impl->rasterRenderer->render(*m_camera, m_renderSettings);
            m_lastRasterFrameHash = frameHash;
        }
    }

    // 7. Get the output texture
    void* mtlTexture = nullptr;
    bool needsMoreFrames = false;
    if (m_impl->currentMode == 1 && m_impl->rtRenderer) {
        const int newSampleCount = m_impl->rtRenderer->sampleCount();
        if (newSampleCount != m_sampleCount) {
            m_sampleCount = newSampleCount;
            emit sampleCountChanged();
        }
        needsMoreFrames = m_impl->rtRenderer->needsMoreFrames();
        mtlTexture = m_impl->rtRenderer->outputTexture();
    } else {
        mtlTexture = m_impl->rasterRenderer->colorTexture();
        needsMoreFrames = m_impl->rasterRenderer->needsMoreFrames();
        if (!mtlTexture && !needsMoreFrames) {
            // Safety net: no output and nothing in flight — force a render
            // on the next pass instead of stalling with no content.
            m_lastRasterFrameHash = 0;
            needsMoreFrames = true;
        }
    }

    if (!mtlTexture) {
        if (needsMoreFrames) {
            update();
        }
        return oldNode;
    }

    // 8. Wrap MTLTexture in QSGTexture and display via QSGSimpleTextureNode
    auto* node = static_cast<QSGSimpleTextureNode*>(oldNode);
    if (!node) {
        node = new QSGSimpleTextureNode();
        node->setTextureCoordinatesTransform(QSGSimpleTextureNode::NoTransform);
        node->setOwnsTexture(false); // Texture lifetime is managed by m_impl->textureCache.
    }

    const QSize textureSize(pw, ph);
    QSGTexture* tex = m_impl->findCachedTexture(mtlTexture, textureSize, renderWindow);
    if (!tex) {
        tex = QNativeInterface::QSGMetalTexture::fromNative(
            (__bridge id<MTLTexture>)mtlTexture,
            renderWindow,
            textureSize);
        if (tex) {
            m_impl->insertCachedTexture(mtlTexture, textureSize, renderWindow, tex);
        }
    }

    if (tex) {
        node->setTexture(tex);
        m_impl->pruneTextureCache(tex);
    }

    node->setRect(boundingRect());
    node->markDirty(QSGNode::DirtyMaterial);

    // 9. Update FPS
    m_frameCount++;
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    if (currentTime - m_lastFrameTime >= 1000) {
        m_fps = m_frameCount * 1000.0f / (currentTime - m_lastFrameTime);
        m_lastFrameTime = currentTime;
        m_frameCount = 0;
        emit fpsChanged();
    }

    // 10. Request next frame only while work remains (in-flight frame,
    // dropped render request, or a completed frame not yet presented).
    // All interactive changes re-trigger update() via the property setters
    // and input handlers, so an idle viewport schedules no frames.
    if (needsMoreFrames) {
        update();
    }

    QMetaObject::invokeMethod(this, &MetalViewport::notifyFramePresented,
                              Qt::QueuedConnection);

    return node;
}

void MetalViewport::releaseResources() {
    if (!m_impl) {
        return;
    }

    std::vector<QSGTexture*> textures = m_impl->takeAllCachedTextures();
    if (textures.empty()) {
        return;
    }

    QQuickWindow* renderWindow = window();
    if (renderWindow) {
        renderWindow->scheduleRenderJob(
            new TextureCacheCleanupJob(std::move(textures)),
            QQuickWindow::BeforeSynchronizingStage);
    } else {
        for (QSGTexture* texture : textures) {
            delete texture;
        }
    }
}

// ---------------------------------------------------------------------------
// Geometry change
// ---------------------------------------------------------------------------

void MetalViewport::geometryChange(const QRectF& newGeometry,
                                    const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);

    if (newGeometry.size() != oldGeometry.size()) {
        float aspect = newGeometry.width() / newGeometry.height();
        if (aspect > 0) {
            m_camera->setAspectRatio(aspect);
        }
    }
}

// ---------------------------------------------------------------------------
// Mouse handling (identical to OpenGLViewport)
// ---------------------------------------------------------------------------

void MetalViewport::setHoverStatus(const QString& status) {
    if (m_hoverStatus == status) return;
    m_hoverStatus = status;
    emit hoverStatusChanged();
}

void MetalViewport::updateHoverStatus(const QPointF& position) {
    if (!m_structure) {
        setHoverStatus(QString());
        return;
    }

    render::RenderSettings pickSettings = m_renderSettings;
    pickSettings.showBonds = m_showBonds;
    pickSettings.showAtoms = !(m_showBonds && m_atomScale <= 0.1001f);
    pickSettings.atomScale = m_atomScale;
    pickSettings.bondRadius = m_bondRadius;

    setHoverStatus(viewportHoverStatus(m_structure.get(), *m_camera, pickSettings,
                                       position,
                                       static_cast<int>(width()),
                                       static_cast<int>(height())));
}

void MetalViewport::hoverMoveEvent(QHoverEvent* event) {
    m_lastMousePos = event->position();
    updateHoverStatus(event->position());
    event->accept();
}

void MetalViewport::hoverLeaveEvent(QHoverEvent* event) {
    setHoverStatus(QString());
    event->accept();
}

void MetalViewport::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->position();
    m_mousePressPos = event->position();
    m_pressedButtons = event->buttons();
    if (m_pressedButtons & Qt::LeftButton) {
        m_showRotationCenter = true;
        update();
    }
    event->accept();
}

void MetalViewport::mouseMoveEvent(QMouseEvent* event) {
    QPointF delta = event->position() - m_lastMousePos;
    m_lastMousePos = event->position();

    if (m_pressedButtons & Qt::LeftButton) {
        m_camera->orbit(-delta.x() * 0.5f, -delta.y() * 0.5f);
    } else if (m_pressedButtons & Qt::RightButton) {
        m_camera->pan(delta.x(), delta.y());
    } else if (m_pressedButtons & Qt::MiddleButton) {
        m_camera->zoom(1.0f - delta.y() * 0.01f);
    }

    emit cameraChanged();
    updateHoverStatus(event->position());
    update();
    event->accept();
}

void MetalViewport::mouseReleaseEvent(QMouseEvent* event) {
    const bool leftReleased = (event->button() == Qt::LeftButton);
    const QPointF releasePos = event->position();
    m_pressedButtons = event->buttons();
    if (!(m_pressedButtons & Qt::LeftButton)) {
        m_showRotationCenter = false;
        update();
    }
    if (leftReleased) {
        const QPointF delta = releasePos - m_mousePressPos;
        if (delta.manhattanLength() <= 4.0) {
            if (auto* model = StructureModel::instance()) {
                if (model->selectionEnabled() && m_structure) {
                    render::RenderSettings pickSettings = m_renderSettings;
                    pickSettings.showBonds = m_showBonds;
                    pickSettings.showAtoms = !(m_showBonds && m_atomScale <= 0.1001f);
                    pickSettings.atomScale = m_atomScale;
                    pickSettings.bondRadius = m_bondRadius;
                    handleViewportSelectionClick(*model, m_structure.get(), *m_camera,
                                                 pickSettings, releasePos,
                                                 static_cast<int>(width()),
                                                 static_cast<int>(height()));
                }
            }
        }
    }
    updateHoverStatus(releasePos);
    event->accept();
}

void MetalViewport::wheelEvent(QWheelEvent* event) {
    float delta = event->angleDelta().y();
    float factor = (delta > 0) ? 0.9f : 1.1f;
    m_camera->zoom(factor);
    emit cameraChanged();
    updateHoverStatus(event->position());
    update();
    event->accept();
}

void MetalViewport::mouseDoubleClickEvent(QMouseEvent* event) {
    resetCamera();
    event->accept();
}

void MetalViewport::notifyFramePresented() {
    ++m_frameToken;
    emit frameTokenChanged();
}

void MetalViewport::onStructureStyleChanged() {
    // Appearance only (colors, transparency, selection styling) — the
    // renderers refresh color buffers without rebuilding geometry or BVH.
    m_needsAppearanceUpdate = true;
    update();
}

void MetalViewport::onStructureGeometryChanged() {
    m_needsStructureUpdate = true;
    update();
}

} // namespace atom::ui
