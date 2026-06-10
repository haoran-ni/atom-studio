#include "StructureModel.h"
#include "../../data/Structure.h"
#include "../../data/BondList.h"
#include "../../data/ElementData.h"
#include "../../data/StructureOperations.h"

#include <QFileInfo>
#include <QQmlEngine>
#include <algorithm>
#include <cmath>

namespace atom::ui {

StructureModel* StructureModel::s_instance = nullptr;

namespace {

data::ElementColorScheme colorSchemeFromIndex(int index) {
    return index == 1 ? data::ElementColorScheme::Cpk
                      : data::ElementColorScheme::Jmol;
}

data::Color colorFromQColor(const QColor& color) {
    QColor valid = color.isValid() ? color : QColor(255, 255, 255);
    return data::Color(
        static_cast<float>(valid.redF()),
        static_cast<float>(valid.greenF()),
        static_cast<float>(valid.blueF()),
        1.0f);
}

int transparencyFromAlpha(float alpha) {
    const float clamped = std::clamp(alpha, 0.0f, 1.0f);
    return static_cast<int>(std::lround((1.0f - clamped) * 100.0f));
}

void syncSelectedBondEndpointColorsFromSelectedAtoms(data::Structure& structure) {
    auto& bonds = structure.bonds();
    const auto& selectedAtoms = structure.atomSelectionMask();
    const auto& selectedBonds = bonds.selectionMask();
    const float* cr = structure.colorsR();
    const float* cg = structure.colorsG();
    const float* cb = structure.colorsB();

    for (size_t i = 0; i < bonds.bondCount(); ++i) {
        if (!selectedBonds[i]) continue;

        const data::Bond& bond = bonds.bond(i);
        if (bond.atomIndex1 < selectedAtoms.size() && selectedAtoms[bond.atomIndex1]) {
            bonds.setStartColor(i, data::Color(
                cr[bond.atomIndex1], cg[bond.atomIndex1], cb[bond.atomIndex1]));
        }
        if (bond.atomIndex2 < selectedAtoms.size() && selectedAtoms[bond.atomIndex2]) {
            bonds.setEndColor(i, data::Color(
                cr[bond.atomIndex2], cg[bond.atomIndex2], cb[bond.atomIndex2]));
        }
    }
}

void syncSelectedBondAlphaFromSelectedAtoms(data::Structure& structure, float alpha) {
    auto& bonds = structure.bonds();
    const auto& selectedAtoms = structure.atomSelectionMask();
    const auto& selectedBonds = bonds.selectionMask();

    for (size_t i = 0; i < bonds.bondCount(); ++i) {
        if (!selectedBonds[i]) continue;

        const data::Bond& bond = bonds.bond(i);
        const bool startSelected = bond.atomIndex1 < selectedAtoms.size() &&
            selectedAtoms[bond.atomIndex1];
        const bool endSelected = bond.atomIndex2 < selectedAtoms.size() &&
            selectedAtoms[bond.atomIndex2];
        if (startSelected || endSelected) {
            bonds.setAlpha(i, alpha);
        }
    }
}

} // namespace

StructureModel::StructureModel(QObject* parent)
    : QObject(parent)
{
    s_instance = this;
}

StructureModel::~StructureModel() {
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

StructureModel* StructureModel::create(QQmlEngine* qmlEngine, QJSEngine* jsEngine) {
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)

    if (!s_instance) {
        s_instance = new StructureModel();
    }
    return s_instance;
}

bool StructureModel::hasStructure() const {
    return m_structure != nullptr && m_structure->atomCount() > 0;
}

QString StructureModel::fileName() const {
    if (!m_structure) return tr("No file loaded");

    QString path = QString::fromStdString(m_structure->sourcePath());
    if (path.isEmpty()) return tr("Untitled");

    return QFileInfo(path).fileName();
}

QString StructureModel::structureName() const {
    if (!m_structure) return QString();
    return QString::fromStdString(m_structure->name());
}

int StructureModel::atomCount() const {
    return m_structure ? static_cast<int>(m_structure->atomCount()) : 0;
}

int StructureModel::bondCount() const {
    return m_structure ? static_cast<int>(m_structure->bonds().bondCount()) : 0;
}

int StructureModel::atomTypeCount() const {
    return m_elements.count();
}

QStringList StructureModel::elements() const {
    return m_elements;
}

bool StructureModel::hasUnitCell() const {
    return m_structure && m_structure->hasLattice();
}

bool StructureModel::hasBonds() const {
    return m_structure && !m_structure->bonds().empty();
}

QString StructureModel::cellParameters() const {
    if (!hasUnitCell()) return QString();

    const auto& lattice = m_structure->lattice();
    return QString("a=%1 b=%2 c=%3\n\u03B1=%4\u00B0 \u03B2=%5\u00B0 \u03B3=%6\u00B0")
           .arg(lattice.a(), 0, 'f', 3)
           .arg(lattice.b(), 0, 'f', 3)
           .arg(lattice.c(), 0, 'f', 3)
           .arg(lattice.alpha(), 0, 'f', 1)
           .arg(lattice.beta(), 0, 'f', 1)
           .arg(lattice.gamma(), 0, 'f', 1);
}

int StructureModel::selectionMode() const {
    return m_selectionMode;
}

bool StructureModel::selectionEnabled() const {
    return m_selectionMode != 0;
}

int StructureModel::selectedAtomCount() const {
    return m_structure ? static_cast<int>(m_structure->selectedAtomCount()) : 0;
}

int StructureModel::selectedBondCount() const {
    return m_structure ? static_cast<int>(m_structure->selectedBondCount()) : 0;
}

bool StructureModel::hasActiveAtomSelection() const {
    return selectionEnabled() && selectedAtomCount() > 0;
}

bool StructureModel::hasActiveBondSelection() const {
    return selectionEnabled() && selectedBondCount() > 0;
}

QColor StructureModel::selectedAtomColor() const {
    if (!m_structure) return QColor(255, 255, 255);

    const auto& selectedAtoms = m_structure->atomSelectionMask();
    const float* cr = m_structure->colorsR();
    const float* cg = m_structure->colorsG();
    const float* cb = m_structure->colorsB();
    for (size_t i = 0; i < selectedAtoms.size(); ++i) {
        if (selectedAtoms[i]) {
            return QColor::fromRgbF(cr[i], cg[i], cb[i], 1.0);
        }
    }
    return QColor(255, 255, 255);
}

int StructureModel::selectedAtomTransparency() const {
    if (!m_structure) return 0;

    const auto& selectedAtoms = m_structure->atomSelectionMask();
    const float* ca = m_structure->colorsA();
    for (size_t i = 0; i < selectedAtoms.size(); ++i) {
        if (selectedAtoms[i]) {
            return transparencyFromAlpha(ca[i]);
        }
    }
    return 0;
}

void StructureModel::setStructure(std::shared_ptr<data::Structure> structure) {
    m_originalStructure = structure;
    m_structure = structure->clone();
    setSelectionModeInternal(0, false);
    updateElementList();
    emit structureChanged();
    emitSelectionResetSignals();
    emit structureUpdated(m_structure);
}

void StructureModel::resetToOriginal() {
    if (!m_originalStructure) return;
    m_structure = m_originalStructure->clone();
    setSelectionModeInternal(0, false);
    updateElementList();
    emit structureChanged();
    emitSelectionResetSignals();
    emit structureUpdated(m_structure);
}

void StructureModel::replicateCell(int nx, int ny, int nz) {
    if (!m_originalStructure || !m_originalStructure->hasLattice()) return;
    if (nx < 1 || ny < 1 || nz < 1) return;

    auto replicated = data::replicateCell(*m_originalStructure, nx, ny, nz);
    if (!replicated) return;

    m_structure = std::move(replicated);
    clearSelection();
    updateElementList();
    emit structureChanged();
    emit structureUpdated(m_structure);
}

void StructureModel::unwrapMolecules() {
    if (!m_structure || !m_structure->hasLattice()) return;
    if (m_structure->bonds().empty()) return;

    data::unwrapMolecules(*m_structure);
    clearSelection();
    emit structureChanged();
    emit structureUpdated(m_structure);
}

void StructureModel::setSelectionMode(int mode) {
    mode = std::clamp(mode, 0, 2);
    const bool modeChanged = mode != m_selectionMode;
    setSelectionModeInternal(mode, modeChanged);
    clearSelection();
}

void StructureModel::clearSelection() {
    if (m_structure) {
        m_structure->clearSelection();
    }
    emit selectionChanged();
    emit structureStyleChanged();
}

bool StructureModel::toggleAtomSelection(size_t atomIndex) {
    if (!m_structure || atomIndex >= m_structure->atomCount()) return false;
    m_structure->toggleAtomSelected(atomIndex);
    emit selectionChanged();
    emit structureStyleChanged();
    return true;
}

bool StructureModel::toggleBondSelection(size_t bondIndex) {
    if (!m_structure || bondIndex >= m_structure->bonds().bondCount()) return false;
    m_structure->bonds().toggleSelected(bondIndex);
    emit selectionChanged();
    emit structureStyleChanged();
    return true;
}

bool StructureModel::toggleMoleculeSelectionFromAtom(size_t atomIndex) {
    if (!m_structure || atomIndex >= m_structure->atomCount()) return false;
    const data::ConnectedSelection component = data::connectedSelectionFromAtom(*m_structure, atomIndex);
    const bool deselect = componentFullySelected(component);
    return setComponentSelection(component, !deselect);
}

bool StructureModel::toggleMoleculeSelectionFromBond(size_t bondIndex) {
    if (!m_structure || bondIndex >= m_structure->bonds().bondCount()) return false;
    const data::ConnectedSelection component = data::connectedSelectionFromBond(*m_structure, bondIndex);
    const bool deselect = componentFullySelected(component);
    return setComponentSelection(component, !deselect);
}

bool StructureModel::applyAtomScaleToSelection(float scale, float globalAtomScale) {
    if (!m_structure || !selectionEnabled() || selectedAtomCount() == 0) return false;
    if (!std::isfinite(scale) || !std::isfinite(globalAtomScale)) return false;

    const float safeGlobalScale = std::max(globalAtomScale, 0.0001f);
    float* radii = m_structure->radii();
    const int* atomicNumbers = m_structure->atomicNumbers();
    const auto& selectedAtoms = m_structure->atomSelectionMask();
    for (size_t i = 0; i < m_structure->atomCount(); ++i) {
        if (!selectedAtoms[i]) continue;
        radii[i] = data::ElementData::radiusForElement(atomicNumbers[i], false) * scale / safeGlobalScale;
    }

    // Radii feed BVH bounds — geometry, not just appearance.
    emit structureGeometryChanged();
    return true;
}

bool StructureModel::applyAtomColorSchemeToSelection(int scheme) {
    if (!m_structure || !selectionEnabled() || selectedAtomCount() == 0) return false;

    const auto colorScheme = colorSchemeFromIndex(scheme);
    float* cr = m_structure->colorsR();
    float* cg = m_structure->colorsG();
    float* cb = m_structure->colorsB();
    const int* atomicNumbers = m_structure->atomicNumbers();
    const auto& selectedAtoms = m_structure->atomSelectionMask();

    for (size_t i = 0; i < m_structure->atomCount(); ++i) {
        if (!selectedAtoms[i]) continue;
        const auto color = data::ElementData::colorForElement(atomicNumbers[i], colorScheme);
        cr[i] = color.r;
        cg[i] = color.g;
        cb[i] = color.b;
    }

    syncSelectedBondEndpointColorsFromSelectedAtoms(*m_structure);
    emit structureStyleChanged();
    return true;
}

bool StructureModel::applyAtomColorToSelection(const QColor& color) {
    if (!m_structure || !selectionEnabled() || selectedAtomCount() == 0) return false;

    const data::Color target = colorFromQColor(color);
    float* cr = m_structure->colorsR();
    float* cg = m_structure->colorsG();
    float* cb = m_structure->colorsB();
    const auto& selectedAtoms = m_structure->atomSelectionMask();

    for (size_t i = 0; i < m_structure->atomCount(); ++i) {
        if (!selectedAtoms[i]) continue;
        cr[i] = target.r;
        cg[i] = target.g;
        cb[i] = target.b;
    }

    syncSelectedBondEndpointColorsFromSelectedAtoms(*m_structure);
    emit structureStyleChanged();
    return true;
}

bool StructureModel::applyAtomTransparencyToSelection(int transparency) {
    if (!m_structure || !selectionEnabled() || selectedAtomCount() == 0) return false;

    const int clampedTransparency = std::clamp(transparency, 0, 100);
    const float alpha = 1.0f - static_cast<float>(clampedTransparency) / 100.0f;
    float* ca = m_structure->colorsA();
    const auto& selectedAtoms = m_structure->atomSelectionMask();

    for (size_t i = 0; i < m_structure->atomCount(); ++i) {
        if (!selectedAtoms[i]) continue;
        ca[i] = alpha;
    }

    syncSelectedBondAlphaFromSelectedAtoms(*m_structure, alpha);
    emit structureStyleChanged();
    return true;
}

bool StructureModel::applyBondRadiusToSelection(float radius) {
    if (!m_structure || !selectionEnabled() || selectedBondCount() == 0) return false;
    if (!std::isfinite(radius)) return false;

    auto& bonds = m_structure->bonds();
    const auto& selectedBonds = bonds.selectionMask();
    for (size_t i = 0; i < bonds.bondCount(); ++i) {
        if (selectedBonds[i]) {
            bonds.setRadius(i, radius);
        }
    }

    // Bond radii are baked into BVH bounds — geometry, not just appearance.
    emit structureGeometryChanged();
    return true;
}

bool StructureModel::resetSelectedObjects(float defaultBondRadius, int colorScheme) {
    if (!m_structure || !selectionEnabled() || !m_structure->hasSelection()) return false;
    if (!std::isfinite(defaultBondRadius)) return false;

    const auto scheme = colorSchemeFromIndex(colorScheme);
    float* radii = m_structure->radii();
    float* cr = m_structure->colorsR();
    float* cg = m_structure->colorsG();
    float* cb = m_structure->colorsB();
    float* ca = m_structure->colorsA();
    const int* atomicNumbers = m_structure->atomicNumbers();
    const auto& selectedAtoms = m_structure->atomSelectionMask();

    for (size_t i = 0; i < m_structure->atomCount(); ++i) {
        if (!selectedAtoms[i]) continue;
        radii[i] = data::ElementData::radiusForElement(atomicNumbers[i], false);
        const auto color = data::ElementData::colorForElement(atomicNumbers[i], scheme);
        cr[i] = color.r;
        cg[i] = color.g;
        cb[i] = color.b;
        ca[i] = 1.0f;
    }

    auto& bonds = m_structure->bonds();
    const auto& selectedBonds = bonds.selectionMask();
    const float clampedBondRadius = std::clamp(defaultBondRadius, 0.01f, 0.6f);
    for (size_t i = 0; i < bonds.bondCount(); ++i) {
        if (!selectedBonds[i]) continue;

        const data::Bond& bond = bonds.bond(i);
        if (bond.atomIndex1 >= m_structure->atomCount() ||
            bond.atomIndex2 >= m_structure->atomCount()) {
            continue;
        }

        bonds.setRadius(i, clampedBondRadius);
        bonds.setEndpointColors(
            i,
            data::ElementData::colorForElement(atomicNumbers[bond.atomIndex1], scheme),
            data::ElementData::colorForElement(atomicNumbers[bond.atomIndex2], scheme));
        bonds.setAlpha(i, 1.0f);
    }

    // Resets both radii (geometry) and colors (appearance; also drives the
    // selectedAtomColor/Transparency QML NOTIFY).
    emit structureGeometryChanged();
    emit structureStyleChanged();
    return true;
}

void StructureModel::clear() {
    m_originalStructure.reset();
    m_structure.reset();
    m_elements.clear();
    setSelectionModeInternal(0, false);
    emit structureChanged();
    emitSelectionResetSignals();
}

void StructureModel::notifyBondsUpdated() {
    clearSelection();
    emit structureChanged();
}

void StructureModel::updateElementList() {
    m_elements.clear();

    if (!m_structure || m_structure->atomCount() == 0) return;

    // Count atoms per element
    QMap<int, int> counts;
    const int* atomicNums = m_structure->atomicNumbers();
    size_t n = m_structure->atomCount();

    for (size_t i = 0; i < n; ++i) {
        counts[atomicNums[i]]++;
    }

    // Build element list with counts
    for (auto it = counts.begin(); it != counts.end(); ++it) {
        const auto& elem = data::ElementData::byAtomicNumber(it.key());
        QString symbol = QString::fromUtf8(elem.symbol.data(), elem.symbol.size());
        m_elements.append(QString("%1: %2").arg(symbol).arg(it.value()));
    }
}

void StructureModel::setSelectionModeInternal(int mode, bool emitChange) {
    m_selectionMode = std::clamp(mode, 0, 2);
    if (emitChange) {
        emit selectionModeChanged();
    }
}

bool StructureModel::componentFullySelected(const data::ConnectedSelection& component) const {
    if (!m_structure) return false;
    if (component.atoms.empty() && component.bonds.empty()) return false;

    for (size_t atomIndex : component.atoms) {
        if (atomIndex >= m_structure->atomCount() || !m_structure->atomSelected(atomIndex)) {
            return false;
        }
    }

    const auto& bonds = m_structure->bonds();
    for (size_t bondIndex : component.bonds) {
        if (bondIndex >= bonds.bondCount() || !bonds.selected(bondIndex)) {
            return false;
        }
    }

    return true;
}

bool StructureModel::setComponentSelection(const data::ConnectedSelection& component, bool selected) {
    if (!m_structure) return false;
    bool changed = false;

    for (size_t atomIndex : component.atoms) {
        if (atomIndex >= m_structure->atomCount()) continue;
        if (m_structure->atomSelected(atomIndex) != selected) {
            m_structure->setAtomSelected(atomIndex, selected);
            changed = true;
        }
    }

    auto& bonds = m_structure->bonds();
    for (size_t bondIndex : component.bonds) {
        if (bondIndex >= bonds.bondCount()) continue;
        if (bonds.selected(bondIndex) != selected) {
            bonds.setSelected(bondIndex, selected);
            changed = true;
        }
    }

    if (changed) {
        emit selectionChanged();
        emit structureStyleChanged();
    }
    return changed;
}

void StructureModel::emitSelectionResetSignals() {
    emit selectionModeChanged();
    emit selectionChanged();
    emit structureStyleChanged();
}

} // namespace atom::ui
