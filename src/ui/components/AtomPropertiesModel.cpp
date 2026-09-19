#include "AtomPropertiesModel.h"
#include "StructureModel.h"
#include "../../data/Structure.h"
#include "../../data/ElementData.h"
#include <algorithm>
#include <numeric>
#include <unordered_set>

namespace atom::ui {
namespace {
QString speciesLabel(const data::Structure& structure, size_t index) {
    const auto& element = data::ElementData::byAtomicNumber(structure.atomicNumber(index));
    return QString::fromUtf8(element.name.data(), element.name.size())
        + " (" + QString::fromStdString(structure.symbol(index)) + ")";
}
}

AtomPropertiesModel::AtomPropertiesModel(StructureModel* owner)
    : QAbstractListModel(owner), m_owner(owner) {
    connect(owner, &StructureModel::structureChanged, this, &AtomPropertiesModel::refresh);
    connect(owner, &StructureModel::structureStyleChanged, this, &AtomPropertiesModel::refreshProperties);
    connect(owner, &StructureModel::structureGeometryChanged, this, &AtomPropertiesModel::refreshProperties);
}

int AtomPropertiesModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_indices.size());
}

QHash<int, QByteArray> AtomPropertiesModel::roleNames() const {
    return {{AtomIdentifier, "atomIdentifier"}, {AtomicNumber, "atomicNumber"},
            {SpeciesName, "speciesName"}, {AtomColor, "atomColor"}, {AtomRadius, "atomRadius"}};
}

QVariant AtomPropertiesModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount() || !m_structure) return {};
    const auto i = m_indices[index.row()];
    switch (role) {
    case AtomIdentifier: return QVariant::fromValue<qlonglong>(m_displayIds[i]);
    case AtomicNumber: return m_structure->atomicNumber(i);
    case SpeciesName: return speciesLabel(*m_structure, i);
    case AtomColor: {
        const auto color = m_structure->color(i);
        return QColor::fromRgbF(color.r, color.g, color.b);
    }
    case AtomRadius: return m_structure->radius(i);
    default: return {};
    }
}

void AtomPropertiesModel::refresh() {
    const auto structure = m_owner->structure();
    if (structure == m_structure && m_indices.size() == (structure ? structure->atomCount() : 0)) return;
    beginResetModel();
    m_structure = structure;
    m_indices.resize(structure ? structure->atomCount() : 0);
    m_displayIds.resize(m_indices.size());
    m_speciesLabels.clear();
    std::unordered_set<int> species;
    const auto document = m_owner->document(m_owner->activeId());
    for (size_t i = 0; i < m_indices.size(); ++i) {
        m_displayIds[i] = document->atomDisplayIds.at(structure->atomId(i));
        if (species.insert(structure->atomicNumber(i)).second)
            m_speciesLabels.append(speciesLabel(*structure, i));
    }
    std::iota(m_indices.begin(), m_indices.end(), size_t{0});
    const auto ordered = [&](size_t a, size_t b) { return m_displayIds[a] < m_displayIds[b]; };
    // Most frames are already in import order; avoid sorting those.
    if (!std::is_sorted(m_indices.begin(), m_indices.end(), ordered))
        std::sort(m_indices.begin(), m_indices.end(), ordered);
    endResetModel();
    emit columnLabelsChanged();
}

void AtomPropertiesModel::refreshProperties() {
    refresh();
    if (rowCount()) emit dataChanged(index(0), index(rowCount() - 1), {AtomColor, AtomRadius});
}
}
