#pragma once

#include <QColor>

namespace atom::render {

/**
 * @brief Render settings for visualization
 */
struct RenderSettings {
    // Background
    QColor backgroundColor = QColor(230, 230, 230);  // Light neutral default

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

    // Lighting
    float ambientStrength = 0.3f;
    float diffuseStrength = 0.7f;
    float specularStrength = 0.0f;
    float shininess = 32.0f;

    // Light direction (in view/camera space: X=right, Y=up, Z=toward viewer)
    float lightDirX = 0.3f;
    float lightDirY = 0.8f;
    float lightDirZ = 0.5f;

    // Quality
    bool useSmoothShading = true;
    int sphereSegments = 32;          // For tessellated spheres (fallback)
    int cylinderSegments = 16;        // For bond cylinders

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
