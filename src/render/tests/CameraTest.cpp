#include "common/Camera.h"

#include <QQuaternion>
#include <QVector2D>
#include <QVector3D>

#include <array>
#include <cmath>
#include <iostream>

namespace {

bool sameOrientation(const QQuaternion& lhs, const QQuaternion& rhs) {
    // Unit quaternions q and -q represent the same orientation.
    return std::abs(QQuaternion::dotProduct(lhs.normalized(), rhs.normalized())) > 0.99999f;
}

} // namespace

int main() {
    using atom::render::Camera;
    using atom::render::RotationConstraint;
    using atom::render::ViewDirection;

    struct ConstraintCase {
        RotationConstraint constraint;
        QVector3D axis;
        const char* name;
    };

    constexpr float rotationAngle = 18.0f;
    const std::array<ConstraintCase, 3> cases{{
        {RotationConstraint::XYPlane, {0, 0, 1}, "XY plane"},
        {RotationConstraint::YZPlane, {1, 0, 0}, "YZ plane"},
        {RotationConstraint::XZPlane, {0, 1, 0}, "XZ plane"},
    }};

    for (const ConstraintCase& testCase : cases) {
        Camera camera;
        camera.setRotationConstraint(testCase.constraint);

        QVector2D projectedAxis(
            QVector3D::dotProduct(testCase.axis, camera.rightVector()),
            -QVector3D::dotProduct(testCase.axis, camera.upVector()));
        if (projectedAxis.lengthSquared() < 1.0e-4f) {
            std::cerr << testCase.name << " test axis unexpectedly has no projection\n";
            return 1;
        }
        projectedAxis.normalize();

        const QQuaternion initial = camera.orientation();
        camera.orbit(projectedAxis.x() * rotationAngle,
                     projectedAxis.y() * rotationAngle);
        if (!sameOrientation(camera.orientation(), initial)) {
            std::cerr << testCase.name << " responded to drag parallel to its projected axis\n";
            return 1;
        }

        const QVector2D perpendicular(-projectedAxis.y(), projectedAxis.x());
        camera.orbit(perpendicular.x() * rotationAngle,
                     perpendicular.y() * rotationAngle);

        const QQuaternion expected =
            (QQuaternion::fromAxisAndAngle(testCase.axis, rotationAngle) * initial)
                .normalized();
        if (!sameOrientation(camera.orientation(), expected)) {
            std::cerr << testCase.name << " did not rotate around its world-space normal\n";
            return 1;
        }
    }

    Camera camera;
    camera.setRotationConstraint(RotationConstraint::YZPlane);
    camera.reset();
    if (camera.rotationConstraint() != RotationConstraint::YZPlane) {
        std::cerr << "Reset Camera cleared the rotation constraint\n";
        return 1;
    }

    Camera fallbackCamera;
    fallbackCamera.setPresetView(ViewDirection::PlusZ);
    fallbackCamera.setRotationConstraint(RotationConstraint::XYPlane);
    const QQuaternion fallbackInitial = fallbackCamera.orientation();
    fallbackCamera.orbit(rotationAngle, 42.0f);
    const QQuaternion fallbackExpected =
        (QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), rotationAngle)
         * fallbackInitial).normalized();
    if (!sameOrientation(fallbackCamera.orientation(), fallbackExpected)) {
        std::cerr << "View-aligned rotation axis did not use horizontal fallback\n";
        return 1;
    }

    return 0;
}
