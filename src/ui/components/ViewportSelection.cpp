#include "ViewportSelection.h"

#include "StructureModel.h"
#include "../../data/BondList.h"
#include "../../data/Structure.h"
#include "../../render/common/BondRenderData.h"
#include "../../render/common/Picking.h"

#include <cmath>

namespace atom::ui {

namespace {

QString formatValue(float value) {
    const double displayValue = std::abs(value) < 0.00005f
        ? 0.0
        : static_cast<double>(value);
    return QString::number(displayValue, 'f', 4);
}

QString formatPosition(float x, float y, float z) {
    return QStringLiteral("(%1, %2, %3)")
        .arg(formatValue(x))
        .arg(formatValue(y))
        .arg(formatValue(z));
}

QString atomSymbol(const data::Structure& structure, size_t atomIndex) {
    return QString::fromStdString(structure.symbol(atomIndex));
}

QString atomHoverStatus(const data::Structure& structure, size_t atomIndex) {
    if (atomIndex >= structure.atomCount()) return QString();

    qint64 displayId = static_cast<qint64>(atomIndex);
    if (const auto* model = StructureModel::instance(); model && model->structure().get() == &structure) {
        if (const auto document = model->document(model->activeId())) {
            // Use the same import IDs as the per-atom table, even after deletion or reordering.
            const auto id = document->atomDisplayIds.find(structure.atomId(atomIndex));
            if (id != document->atomDisplayIds.end()) displayId = id->second;
        }
    }
    const auto position = structure.position(atomIndex);
    return QStringLiteral("ID: %1    |    Element: %2    |    Position: %3")
        .arg(displayId)
        .arg(atomSymbol(structure, atomIndex))
        .arg(formatPosition(position[0], position[1], position[2]));
}

QString bondHoverStatus(const data::Structure& structure, size_t bondIndex) {
    const auto& bonds = structure.bonds();
    if (bondIndex >= bonds.bondCount()) return QString();

    const data::Bond& bond = bonds.bond(bondIndex);
    if (bond.atomIndex1 >= structure.atomCount() ||
        bond.atomIndex2 >= structure.atomCount()) {
        return QString();
    }

    const render::BondRenderSegment segment =
        render::makeBondRenderSegment(structure, bond, bondIndex);
    const float dx = segment.endX - segment.startX;
    const float dy = segment.endY - segment.startY;
    const float dz = segment.endZ - segment.startZ;
    const float length = std::sqrt(dx * dx + dy * dy + dz * dz);

    return QStringLiteral(
               "Bond length: %1    |    Connected atoms: (%2, %3)    |    Atom positions: %4, %5")
        .arg(formatValue(length))
        .arg(atomSymbol(structure, bond.atomIndex1))
        .arg(atomSymbol(structure, bond.atomIndex2))
        .arg(formatPosition(segment.startX, segment.startY, segment.startZ))
        .arg(formatPosition(segment.endX, segment.endY, segment.endZ));
}

} // namespace

bool handleViewportSelectionClick(StructureModel& model,
                                  const data::Structure* structure,
                                  const render::Camera& camera,
                                  const render::RenderSettings& settings,
                                  const QPointF& position,
                                  int viewportWidth,
                                  int viewportHeight,
                                  const render::PreparedGeometry* geometry) {
    if (!model.selectionEnabled() || !structure) return false;

    const auto pick = render::pickStructureObject(
        structure, camera, settings,
        static_cast<float>(position.x()),
        static_cast<float>(position.y()),
        viewportWidth,
        viewportHeight, geometry);

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

QString viewportHoverStatus(const data::Structure* structure,
                            const render::Camera& camera,
                            const render::RenderSettings& settings,
                            const QPointF& position,
                            int viewportWidth,
                            int viewportHeight,
                                  const render::PreparedGeometry* geometry) {
    if (!structure) return QString();

    const auto pick = render::pickStructureObject(
        structure, camera, settings,
        static_cast<float>(position.x()),
        static_cast<float>(position.y()),
        viewportWidth,
        viewportHeight, geometry);

    if (!pick.hit()) return QString();

    if (pick.type == render::PickObjectType::Atom) {
        return atomHoverStatus(*structure, pick.index);
    }
    if (pick.type == render::PickObjectType::Bond) {
        return bondHoverStatus(*structure, pick.index);
    }
    return QString();
}

} // namespace atom::ui
