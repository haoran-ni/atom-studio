#pragma once

#include <QColor>

namespace atom::render {

/**
 * @brief Render settings for visualization
 */
struct RenderSettings {
    // Background
    QColor backgroundColor = QColor(26, 26, 46);  // Dark blue-ish

    // Atom rendering
    float atomScale = 1.0f;           // Scale factor for atom radii
    bool showAtoms = true;

    // Bond rendering
    float bondRadius = 0.1f;          // Bond cylinder radius in Angstroms
    bool showBonds = true;

    // Unit cell
    bool showUnitCell = true;
    QColor unitCellColor = QColor(255, 255, 255, 128);
    float unitCellLineWidth = 1.5f;

    // Lighting
    float ambientStrength = 0.3f;
    float diffuseStrength = 0.7f;
    float specularStrength = 0.5f;
    float shininess = 32.0f;

    // Light direction (in view space, pointing towards light)
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
};

} // namespace atom::render
