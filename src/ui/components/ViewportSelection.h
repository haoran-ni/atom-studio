#pragma once

#include <QPointF>

namespace atom::data {
class Structure;
}

namespace atom::render {
class Camera;
struct RenderSettings;
}

namespace atom::ui {

class StructureModel;

bool handleViewportSelectionClick(StructureModel& model,
                                  const data::Structure* structure,
                                  const render::Camera& camera,
                                  const render::RenderSettings& settings,
                                  const QPointF& position,
                                  int viewportWidth,
                                  int viewportHeight);

} // namespace atom::ui
