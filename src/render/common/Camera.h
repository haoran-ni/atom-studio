#pragma once

#include <QMatrix4x4>
#include <QVector3D>
#include <QQuaternion>

namespace atom::render {

enum class ViewDirection { PlusX, MinusX, PlusY, MinusY, PlusZ, MinusZ };
enum class RotationConstraint { None, XYPlane, YZPlane, XZPlane };

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
    void setSceneExtent(float extent);

    // Orbit constraint. Plane constraints rotate around the corresponding
    // world-space plane normal (XY -> Z, YZ -> X, XZ -> Y).
    void setRotationConstraint(RotationConstraint constraint);
    RotationConstraint rotationConstraint() const { return m_rotationConstraint; }

    // Preset views
    void setPresetView(ViewDirection dir);

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
    // Move the scene's orbit center, retaining pan relative to that center.
    void setSceneCenter(const QVector3D& center);

    // View parameters
    QQuaternion orientation() const { return m_orientation; }
    void setOrientation(const QQuaternion& q);
    float distance() const { return m_distance; }
    float perspectiveDistance() const { return m_perspectiveDistance; }
    // Scene-scale reference used for size-in-world overlays (e.g. gizmos).
    // Returns m_distance in perspective (tracks zoom) and m_orthoScale in
    // orthographic (distance is fixed; orthoScale tracks zoom instead).
    float viewScale() const { return m_perspective ? m_distance : m_orthoScale; }

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

    // Orbit parameters
    QVector3D m_target = {0, 0, 0};
    QVector3D m_sceneCenter = {0, 0, 0};
    QQuaternion m_orientation;   // Camera orientation (replaces azimuth + elevation)
    RotationConstraint m_rotationConstraint = RotationConstraint::None;
    float m_distance = 50.0f;
    float m_perspectiveDistance = 50.0f;

    // Projection parameters
    bool m_perspective = true;
    float m_fov = 45.0f;         // Field of view in degrees
    float m_orthoScale  = 10.0f;  // Orthographic scale
    float m_sceneExtent =  0.0f;  // Current scene extent; used for clipping and
                                  // to set ortho distance on mode switch
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
