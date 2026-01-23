#include "Camera.h"
#include <QtMath>

namespace atom::render {

Camera::Camera() {
    reset();
}

void Camera::reset() {
    m_target = QVector3D(0, 0, 0);
    m_azimuth = 45.0f;
    m_elevation = 30.0f;
    m_distance = 50.0f;
    m_fov = 45.0f;
    m_orthoScale = 10.0f;
    m_viewDirty = true;
    m_projDirty = true;
}

void Camera::orbit(float deltaAzimuth, float deltaElevation) {
    m_azimuth += deltaAzimuth;
    m_elevation += deltaElevation;

    // Wrap azimuth
    while (m_azimuth > 360.0f) m_azimuth -= 360.0f;
    while (m_azimuth < 0.0f) m_azimuth += 360.0f;

    clampElevation();
    m_viewDirty = true;
}

void Camera::pan(float deltaX, float deltaY) {
    // Pan in screen space
    QVector3D right = rightVector();
    QVector3D up = upVector();

    // Scale pan amount by distance for consistent feel
    float scale = m_distance * 0.002f;
    m_target += right * (-deltaX * scale) + up * (deltaY * scale);
    m_viewDirty = true;
}

void Camera::zoom(float factor) {
    m_distance *= factor;
    m_distance = qBound(0.1f, m_distance, 100000.0f);

    // Also adjust ortho scale for orthographic mode
    m_orthoScale *= factor;
    m_orthoScale = qBound(0.1f, m_orthoScale, 10000.0f);

    m_viewDirty = true;
    m_projDirty = true;
}

void Camera::setDistance(float distance) {
    m_distance = qBound(0.1f, distance, 100000.0f);
    m_viewDirty = true;
}

void Camera::fitToView(const QVector3D& center, float extent) {
    m_target = center;

    if (extent > 0) {
        // Calculate distance to fit the extent in view
        if (m_perspective) {
            float fovRad = qDegreesToRadians(m_fov * 0.5f);
            m_distance = (extent * 0.5f) / qTan(fovRad) * 1.5f;
        } else {
            m_orthoScale = extent * 0.6f;
            m_distance = extent * 2.0f;
        }
    }

    // Update near/far planes based on extent
    m_near = qMax(0.01f, m_distance * 0.001f);
    m_far = m_distance * 10.0f;

    m_viewDirty = true;
    m_projDirty = true;
}

void Camera::setTarget(const QVector3D& target) {
    m_target = target;
    m_viewDirty = true;
}

void Camera::setAzimuth(float azimuth) {
    m_azimuth = azimuth;
    while (m_azimuth > 360.0f) m_azimuth -= 360.0f;
    while (m_azimuth < 0.0f) m_azimuth += 360.0f;
    m_viewDirty = true;
}

void Camera::setElevation(float elevation) {
    m_elevation = elevation;
    clampElevation();
    m_viewDirty = true;
}

void Camera::clampElevation() {
    m_elevation = qBound(-89.0f, m_elevation, 89.0f);
}

QVector3D Camera::position() const {
    float azimuthRad = qDegreesToRadians(m_azimuth);
    float elevationRad = qDegreesToRadians(m_elevation);

    float cosElev = qCos(elevationRad);
    float x = m_distance * cosElev * qSin(azimuthRad);
    float y = m_distance * qSin(elevationRad);
    float z = m_distance * cosElev * qCos(azimuthRad);

    return m_target + QVector3D(x, y, z);
}

QVector3D Camera::forwardVector() const {
    return (m_target - position()).normalized();
}

QVector3D Camera::rightVector() const {
    QVector3D forward = forwardVector();
    QVector3D worldUp(0, 1, 0);
    return QVector3D::crossProduct(forward, worldUp).normalized();
}

QVector3D Camera::upVector() const {
    QVector3D forward = forwardVector();
    QVector3D right = rightVector();
    return QVector3D::crossProduct(right, forward).normalized();
}

void Camera::setProjection(bool perspective) {
    m_perspective = perspective;
    m_projDirty = true;
}

void Camera::setFieldOfView(float fov) {
    m_fov = qBound(10.0f, fov, 170.0f);
    m_projDirty = true;
}

void Camera::setOrthoScale(float scale) {
    m_orthoScale = qBound(0.1f, scale, 10000.0f);
    m_projDirty = true;
}

void Camera::setAspectRatio(float aspect) {
    m_aspectRatio = qMax(0.1f, aspect);
    m_projDirty = true;
}

void Camera::setNearPlane(float near) {
    m_near = qMax(0.001f, near);
    m_projDirty = true;
}

void Camera::setFarPlane(float far) {
    m_far = far;
    m_projDirty = true;
}

QMatrix4x4 Camera::viewMatrix() const {
    if (m_viewDirty) {
        updateMatrices();
    }
    return m_viewMatrix;
}

QMatrix4x4 Camera::projectionMatrix() const {
    if (m_projDirty) {
        updateMatrices();
    }
    return m_projectionMatrix;
}

QMatrix4x4 Camera::viewProjectionMatrix() const {
    return projectionMatrix() * viewMatrix();
}

void Camera::updateMatrices() const {
    if (m_viewDirty) {
        m_viewMatrix.setToIdentity();
        m_viewMatrix.lookAt(position(), m_target, QVector3D(0, 1, 0));
        m_viewDirty = false;
    }

    if (m_projDirty) {
        m_projectionMatrix.setToIdentity();
        if (m_perspective) {
            m_projectionMatrix.perspective(m_fov, m_aspectRatio, m_near, m_far);
        } else {
            float halfWidth = m_orthoScale * m_aspectRatio;
            float halfHeight = m_orthoScale;
            m_projectionMatrix.ortho(-halfWidth, halfWidth,
                                     -halfHeight, halfHeight,
                                     m_near, m_far);
        }
        m_projDirty = false;
    }
}

QVector3D Camera::screenToWorld(float screenX, float screenY, float depth,
                                 int viewportWidth, int viewportHeight) const {
    // Convert screen coordinates to normalized device coordinates
    float ndcX = (2.0f * screenX / viewportWidth) - 1.0f;
    float ndcY = 1.0f - (2.0f * screenY / viewportHeight);

    QMatrix4x4 invVP = viewProjectionMatrix().inverted();
    QVector4D nearPoint = invVP * QVector4D(ndcX, ndcY, -1.0f, 1.0f);
    QVector4D farPoint = invVP * QVector4D(ndcX, ndcY, 1.0f, 1.0f);

    nearPoint /= nearPoint.w();
    farPoint /= farPoint.w();

    QVector3D rayDir = (farPoint.toVector3D() - nearPoint.toVector3D()).normalized();
    return nearPoint.toVector3D() + rayDir * depth;
}

QVector3D Camera::worldToScreen(const QVector3D& worldPos,
                                 int viewportWidth, int viewportHeight) const {
    QVector4D clipPos = viewProjectionMatrix() * QVector4D(worldPos, 1.0f);
    if (qFuzzyIsNull(clipPos.w())) {
        return QVector3D(0, 0, 0);
    }

    QVector3D ndc = clipPos.toVector3D() / clipPos.w();

    float screenX = (ndc.x() + 1.0f) * 0.5f * viewportWidth;
    float screenY = (1.0f - ndc.y()) * 0.5f * viewportHeight;
    float depth = (ndc.z() + 1.0f) * 0.5f;

    return QVector3D(screenX, screenY, depth);
}

} // namespace atom::render
