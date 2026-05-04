#include "ViewportSelection.h"

#include "StructureModel.h"
#include "../../render/common/Picking.h"

namespace atom::ui {

bool handleViewportSelectionClick(StructureModel& model,
                                  const data::Structure* structure,
                                  const render::Camera& camera,
                                  const render::RenderSettings& settings,
                                  const QPointF& position,
                                  int viewportWidth,
                                  int viewportHeight) {
    if (!model.selectionEnabled() || !structure) return false;

    const auto pick = render::pickStructureObject(
        structure, camera, settings,
        static_cast<float>(position.x()),
        static_cast<float>(position.y()),
        viewportWidth,
        viewportHeight);

    if (!pick.hit()) return false;

    if (model.selectionMode() == 2) {
        if (pick.type == render::PickObjectType::Atom) {
            return model.toggleMoleculeSelectionFromAtom(pick.index);
        }
        if (pick.type == render::PickObjectType::Bond) {
            return model.toggleMoleculeSelectionFromBond(pick.index);
        }
        return false;
    }

    if (pick.type == render::PickObjectType::Atom) {
        return model.toggleAtomSelection(pick.index);
    }
    if (pick.type == render::PickObjectType::Bond) {
        return model.toggleBondSelection(pick.index);
    }
    return false;
}

} // namespace atom::ui
