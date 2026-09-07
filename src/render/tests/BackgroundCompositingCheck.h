#pragma once

#include <QImage>
#include <QPainter>
#include <iostream>

// Exercise a converged renderer without replacing its scene or resetting samples.
// draw() returns one completed output for the current settings.
template<class Renderer, class Draw>
bool checkBackgroundCompositing(Renderer& renderer, atom::render::RenderSettings& settings,
                               Draw draw) {
    const int samples = renderer.sampleCount();
    settings.backgroundColor = QColor(80, 160, 240, 0);
    const QImage foreground = draw().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (foreground.isNull() || renderer.sampleCount() != samples) {
        std::cerr << "Transparent export must preserve converged samples\n";
        return false;
    }
    int edgePixels = 0;
    int opaquePixels = 0;
    for (int y = 0; y < foreground.height(); ++y) {
        for (int x = 0; x < foreground.width(); ++x) {
            const int alpha = qAlpha(foreground.pixel(x, y));
            edgePixels += alpha > 0 && alpha < 255;
            opaquePixels += alpha == 255;
        }
    }
    if (edgePixels == 0 || opaquePixels == 0 || qAlpha(foreground.pixel(2, 2)) != 0) {
        std::cerr << "Expected transparent background, smooth edges, and opaque geometry: "
                  << "background alpha=" << qAlpha(foreground.pixel(2, 2))
                  << ", edge pixels=" << edgePixels << ", opaque pixels=" << opaquePixels << '\n';
        return false;
    }
    for (const QColor background : {QColor(230, 50, 100), QColor(30, 190, 70, 64),
                                    QColor(80, 160, 240), QColor(5, 10, 15, 0)}) {
        settings.backgroundColor = background;
        const QImage actual = draw().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (actual.size() != foreground.size() || renderer.sampleCount() != samples) {
            std::cerr << "Background edits must preserve converged samples\n";
            return false;
        }
        QImage expected(foreground.size(), QImage::Format_ARGB32_Premultiplied);
        expected.fill(background);
        {
            QPainter painter(&expected);
            painter.drawImage(0, 0, foreground);
        }
        for (int y = 0; y < actual.height(); ++y) {
            for (int x = 0; x < actual.width(); ++x) {
                const QRgb a = actual.pixel(x, y);
                const QRgb e = expected.pixel(x, y);
                if (std::abs(qRed(a) - qRed(e)) > 3 || std::abs(qGreen(a) - qGreen(e)) > 3
                    || std::abs(qBlue(a) - qBlue(e)) > 3 || std::abs(qAlpha(a) - qAlpha(e)) > 3) {
                    std::cerr << "Background composition changed foreground/edge coverage at "
                              << x << ", " << y << '\n';
                    return false;
                }
            }
        }
    }
    return true;
}
