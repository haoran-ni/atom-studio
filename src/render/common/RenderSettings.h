#pragma once

#include <QColor>
#include <QVector3D>
#include <cmath>
#include <cstdint>

namespace atom::render {

/**
 * @brief Render settings for visualization
 */
struct RenderSettings {
    // Identifies an explicit frame request (for export). Carried with the
    // completed output; does not affect ray-tracing accumulation.
    uint64_t frameRequestToken = 0;

    // Background
    QColor backgroundColor = QColor(255, 255, 255);  // Pure white default

    // Atom rendering
    float atomScale = 1.0f;           // Scale factor for atom radii
    bool showAtoms = true;

    // Bond rendering
    float bondRadius = 0.1f;          // Bond cylinder radius in Angstroms
    bool showBonds = true;

    // Unit cell
    bool showUnitCell = true;
    QColor unitCellColor = QColor(0, 0, 0);
    float unitCellThickness = 0.06f;  // Cylinder/sphere radius in Angstroms

    // Viewport-corner XYZ axes overlay (screen-space 3D gizmo; not scene geometry)
    bool showViewportAxes = true;
    float viewportAxesScreenX = 60.0f;   // center position in rendered pixels
    float viewportAxesScreenY = 60.0f;   // center position in rendered pixels (y-down)
    float viewportAxesScale = 1.0f;      // includes hover enlargement from UI
    float viewportAxesPixelRatio = 1.0f; // renderer pixels per QML logical pixel; also sizes the center gizmo

    // Lighting
    float ambientStrength = 0.35f;
    float diffuseStrength = 0.7f;
    float specularStrength = 0.05f;
    float shininess = 60.0f;

    // Light direction (world space, defined by spherical angles in degrees)
    // Azimuth: angle in XY plane, 0° = +X, 90° = +Y
    // Elevation: angle from XY plane toward +Z, 0° = horizontal, 90° = +Z
    float lightAzimuth = 0.0f;     // degrees
    float lightElevation = 45.0f;  // degrees

    // Compute world-space light direction unit vector from azimuth/elevation
    QVector3D lightDirWorld() const {
        float az = qDegreesToRadians(lightAzimuth);
        float el = qDegreesToRadians(lightElevation);
        float cosEl = std::cos(el);
        return QVector3D(cosEl * std::cos(az),   // X
                         cosEl * std::sin(az),   // Y
                         std::sin(el));           // Z
    }

    // Quality
    bool useSmoothShading = true;
    int sphereSegments = 32;          // For tessellated spheres (fallback)
    int cylinderSegments = 20;        // For mesh bond cylinders

    // Stroke outlines (inverted-hull shells around atoms and bonds)
    bool outlineEnabled = true;
    float outlineWidth = 1.0f;               // Stroke width in rendered pixels
    QColor outlineColor = QColor(0, 0, 0);

    // Effects
    bool enableAmbientOcclusion = false;
    bool enableShadows = false;
    int msaaSamples = 4;

    // Ray tracing
    int aoSamples = 4;            // AO rays per pixel per frame
    float aoRadius = 3.0f;        // AO sampling radius in Angstroms
    int maxRTSamples = 1000;      // Max progressive samples before stopping

    // Rotation center gizmo (transient — set by viewport during active rotation)
    bool showRotationCenter = false;
    float rotationCenterX = 0.0f;
    float rotationCenterY = 0.0f;
    float rotationCenterZ = 0.0f;
};

} // namespace atom::render
