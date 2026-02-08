#pragma once

#include <QMatrix4x4>
#include <QVector3D>
#include <QQuaternion>

namespace atom::render {

/**
 * @brief Orbit camera for 3D visualization
 *
 * Implements a trackball-style camera that orbits around a target point.
 * Supports:
 * - Orbit rotation (azimuth/elevation)
 * - Pan (move target)
 * - Zoom (change distance)
 * - Fit to bounding box
 */
class Camera {
public:
    Camera();

    // Camera manipulation
    void orbit(float deltaAzimuth, float deltaElevation);
    void pan(float deltaX, float deltaY);
    void zoom(float factor);
    void setDistance(float distance);

    // Reset
    void reset();

    /**
     * @brief Fit camera to view a bounding box
     * @param center Center of the bounding box
     * @param extent Maximum extent of the bounding box
     */
    void fitToView(const QVector3D& center, float extent);

    // Target point
    void setTarget(const QVector3D& target);
    QVector3D target() const { return m_target; }

    // View parameters
    float azimuth() const { return m_azimuth; }
    float elevation() const { return m_elevation; }
    float distance() const { return m_distance; }

    void setAzimuth(float azimuth);
    void setElevation(float elevation);

    // Camera position (computed from target, azimuth, elevation, distance)
    QVector3D position() const;
    QVector3D upVector() const;
    QVector3D rightVector() const;
    QVector3D forwardVector() const;

    // Projection parameters
    void setProjection(bool perspective);
    bool isPerspective() const { return m_perspective; }

    void setFieldOfView(float fov);
    float fieldOfView() const { return m_fov; }

    void setOrthoScale(float scale);
    float orthoScale() const { return m_orthoScale; }

    void setAspectRatio(float aspect);
    float aspectRatio() const { return m_aspectRatio; }

    void setNearPlane(float near);
    void setFarPlane(float far);
    float nearPlane() const { return m_near; }
    float farPlane() const { return m_far; }

    // Matrices
    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projectionMatrix() const;
    QMatrix4x4 viewProjectionMatrix() const;

    // Utility
    QVector3D screenToWorld(float screenX, float screenY, float depth,
                            int viewportWidth, int viewportHeight) const;
    QVector3D worldToScreen(const QVector3D& worldPos,
                            int viewportWidth, int viewportHeight) const;

private:
    void updateMatrices() const;
    void clampElevation();

    // Orbit parameters
    QVector3D m_target = {0, 0, 0};
    float m_azimuth = 45.0f;     // Horizontal angle in degrees
    float m_elevation = 30.0f;   // Vertical angle in degrees
    float m_distance = 50.0f;

    // Projection parameters
    bool m_perspective = true;
    float m_fov = 45.0f;         // Field of view in degrees
    float m_orthoScale = 10.0f;  // Orthographic scale
    float m_aspectRatio = 1.0f;
    float m_near = 0.1f;
    float m_far = 10000.0f;

    // Cached matrices
    mutable QMatrix4x4 m_viewMatrix;
    mutable QMatrix4x4 m_projectionMatrix;
    mutable bool m_viewDirty = true;
    mutable bool m_projDirty = true;
};

} // namespace atom::render
