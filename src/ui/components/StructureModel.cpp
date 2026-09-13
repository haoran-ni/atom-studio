#include "StructureModel.h"
#include "../../data/Structure.h"
#include "../../data/BondList.h"
#include "../../data/ElementData.h"
#include "../../data/StructureOperations.h"

#include <QFileInfo>
#include <QtConcurrent>
#include "../../data/NeighborList.h"
#include <map>
#include <tuple>
#include <QQmlEngine>
#include <algorithm>
#include <cmath>
#include <utility>

namespace atom::ui {

StructureModel* StructureModel::s_instance = nullptr;

namespace {

std::shared_ptr<data::Structure> workingCopy(const data::Structure& raw, qint64 id,
                                           const std::array<int, 3>& replication) {
    auto current = raw.hasLattice() && replication != std::array<int, 3>{1, 1, 1}
        ? data::replicateCell(raw, replication[0], replication[1], replication[2])
        : raw.clone();
    if (current) current->setName("STRUCT_" + std::to_string(id) + "_CURRENT");
    return current;
}

void replaceWorkingCopy(StructureDocument& document, std::shared_ptr<data::Structure> current) {
    document.current = std::move(current);
    document.current->clearSelection();
    document.elements.clear();
    document.appliedColorScheme = -1;
    document.appliedBondRadius = -1;
    document.detectedBondScale = std::numeric_limits<float>::quiet_NaN();
}

data::ElementColorScheme colorSchemeFromIndex(int index) {
    return index == 1 ? data::ElementColorScheme::Cpk
                      : data::ElementColorScheme::Jmol;
}

data::Color colorFromQColor(const QColor& color) {
    QColor valid = color.isValid() ? color : QColor(255, 255, 255);
    return data::Color(
        static_cast<float>(valid.redF()),
        static_cast<float>(valid.greenF()),
        static_cast<float>(valid.blueF()));
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
                cr[bond.atomIndex1], cg[bond.atomIndex1], cb[bond.atomIndex1]), true);
        }
        if (bond.atomIndex2 < selectedAtoms.size() && selectedAtoms[bond.atomIndex2]) {
            bonds.setEndColor(i, data::Color(
                cr[bond.atomIndex2], cg[bond.atomIndex2], cb[bond.atomIndex2]), true);
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
    cancelBondDetection();
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
    return m_active->current != nullptr && m_active->current->atomCount() > 0;
}

QString StructureModel::fileName() const {
    if (!m_active->current) return tr("No file loaded");

    QString path = QString::fromStdString(m_active->current->sourcePath());
    if (path.isEmpty()) return tr("Untitled");

    return QFileInfo(path).fileName();
}

QString StructureModel::structureName() const {
    if (!m_active->current) return QString();
    return QString::fromStdString(m_active->current->name());
}

int StructureModel::atomCount() const {
    return m_active->current ? static_cast<int>(m_active->current->atomCount()) : 0;
}

int StructureModel::bondCount() const {
    return m_active->current ? static_cast<int>(m_active->current->bonds().bondCount()) : 0;
}

int StructureModel::atomTypeCount() const {
    return m_active->elements.count();
}

QStringList StructureModel::elements() const {
    return m_active->elements;
}

bool StructureModel::hasUnitCell() const {
    return m_active->current && m_active->current->hasLattice();
}

bool StructureModel::hasReplicableStructures() const {
    return std::any_of(m_documents.begin(), m_documents.end(), [](const auto& document) {
        return document->raw->hasLattice();
    });
}

float StructureModel::maximumViewExtent() const {
    float extent = 0.0f;
    for (const auto& document : m_documents) {
        extent = std::max(extent, document->current->computeViewBoundingBox().maxExtent());
    }
    return extent;
}

bool StructureModel::hasBonds() const {
    return m_active->current && !m_active->current->bonds().empty();
}

QString StructureModel::cellParameters() const {
    if (!hasUnitCell()) return QString();

    const auto& lattice = m_active->current->lattice();
    return QString("a=%1 b=%2 c=%3\n\u03B1=%4\u00B0 \u03B2=%5\u00B0 \u03B3=%6\u00B0")
           .arg(lattice.a(), 0, 'f', 3)
           .arg(lattice.b(), 0, 'f', 3)
           .arg(lattice.c(), 0, 'f', 3)
           .arg(lattice.alpha(), 0, 'f', 1)
           .arg(lattice.beta(), 0, 'f', 1)
           .arg(lattice.gamma(), 0, 'f', 1);
}

int StructureModel::selectionMode() const {
    return m_active->selectionMode;
}

bool StructureModel::selectionEnabled() const {
    return m_active->selectionMode != 0;
}

int StructureModel::selectedAtomCount() const {
    return m_active->current ? static_cast<int>(m_active->current->selectedAtomCount()) : 0;
}

int StructureModel::selectedBondCount() const {
    return m_active->current ? static_cast<int>(m_active->current->selectedBondCount()) : 0;
}

bool StructureModel::hasActiveAtomSelection() const {
    return selectionEnabled() && selectedAtomCount() > 0;
}

bool StructureModel::hasActiveBondSelection() const {
    return selectionEnabled() && selectedBondCount() > 0;
}

QColor StructureModel::selectedAtomColor() const {
    if (!m_active->current) return QColor(255, 255, 255);

    const auto& selectedAtoms = m_active->current->atomSelectionMask();
    const float* cr = m_active->current->colorsR();
    const float* cg = m_active->current->colorsG();
    const float* cb = m_active->current->colorsB();
    for (size_t i = 0; i < selectedAtoms.size(); ++i) {
        if (selectedAtoms[i]) {
            return QColor::fromRgbF(cr[i], cg[i], cb[i], 1.0);
        }
    }
    return QColor(255, 255, 255);
}

// Compatibility entry point for replacing a session; file imports use addStructure.
void StructureModel::setStructure(std::shared_ptr<data::Structure> structure) {
    clear();
    addStructure(std::move(structure));
}

void StructureModel::addStructure(std::shared_ptr<data::Structure> structure) {
    if (!structure) return;
    auto document = std::make_shared<StructureDocument>();
    document->id = m_nextId;
    auto raw = structure->clone();
    raw->setName("STRUCT_" + std::to_string(document->id) + "_RAW");
    document->current = workingCopy(*raw, document->id, m_replication);
    if (!document->current) return;
    document->raw = std::move(raw);
    m_documents.push_back(std::move(document));
    ++m_nextId;
    emit structuresChanged();
    setActiveIndex(structureCount() - 1);
}

void StructureModel::setActiveIndex(int index) {
    if (index < 0 || index >= structureCount()) return;
    if (m_switchingLocked) {
        m_deferredIndex = index;
        return;
    }
    if (index == m_activeIndex) return;
    const bool firstStructure = m_activeIndex < 0;
    cancelBondDetection();
    m_activeIndex = index;
    m_active = m_documents[index];
    if (m_active->elements.isEmpty()) updateElementList();
    emit activeStructureChanged();
    emit structureChanged();
    emitSelectionResetSignals();
    emit structureActivated(m_active->current, firstStructure);
}

void StructureModel::setSwitchingLocked(bool locked) {
    if (locked == m_switchingLocked) return;
    m_switchingLocked = locked;
    emit switchingLockedChanged();
    if (!locked && m_deferredIndex >= 0) {
        const int index = std::exchange(m_deferredIndex, -1);
        setActiveIndex(index);
    }
}

void StructureModel::nameCurrentStructure() {
    if (m_active->current)
        m_active->current->setName("STRUCT_" + std::to_string(activeId()) + "_CURRENT");
}

void StructureModel::applySharedAppearance(int colorScheme, float bondRadius) {
    m_colorScheme = colorScheme;
    m_bondRadius = bondRadius;
    if (!m_active->current) return;
    if (m_active->appliedColorScheme != colorScheme) {
        const auto scheme = colorSchemeFromIndex(colorScheme);
        m_active->current->updateColorsFromElements(scheme, true);
        m_active->current->updateBondColorsFromElements(scheme, true);
        m_active->appliedColorScheme = colorScheme;
        emit structureStyleChanged();
    }
    if (m_active->appliedBondRadius != bondRadius) {
        m_active->current->bonds().setAllRadii(bondRadius, true);
        m_active->appliedBondRadius = bondRadius;
        emit structureGeometryChanged();
    }
}

void StructureModel::resetToOriginal() {
    if (!m_active->raw) return;
    auto current = workingCopy(*m_active->raw, activeId(), m_replication);
    if (!current) return;
    cancelBondDetection();
    replaceWorkingCopy(*m_active, std::move(current));
    setSelectionModeInternal(0, false);
    updateElementList();
    emit structureChanged();
    emitSelectionResetSignals();
    emit structureUpdated(m_active->current);
}

void StructureModel::replicateCell(int nx, int ny, int nz) {
    if (!hasReplicableStructures()) return;
    if (nx < 1 || ny < 1 || nz < 1 || nx > 99 || ny > 99 || nz > 99) return;
    const std::array<int, 3> factors{nx, ny, nz};
    if (m_replication == factors) return;

    // Stage every result before replacing any document. Hidden structures keep
    // only CPU data; appearance, bonds and GPU preparation remain lazy.
    std::vector<std::shared_ptr<data::Structure>> replicated(m_documents.size());
    for (size_t i = 0; i < m_documents.size(); ++i) {
        const auto& document = m_documents[i];
        if (!document->raw->hasLattice()) continue;
        replicated[i] = workingCopy(*document->raw, document->id, factors);
        if (!replicated[i]) return;
    }

    cancelBondDetection();
    for (size_t i = 0; i < m_documents.size(); ++i) {
        if (replicated[i]) replaceWorkingCopy(*m_documents[i], std::move(replicated[i]));
    }
    setReplicationFactors(nx, ny, nz);
    updateElementList();
    emit structureChanged();
    emitSelectionResetSignals();
    // Notify the single viewport once, after the entire collection is updated.
    emit structureUpdated(m_active->current);
}

void StructureModel::unwrapMolecules() {
    if (!m_active->current || !m_active->current->hasLattice()) return;
    if (m_active->current->bonds().empty()) return;

    cancelBondDetection();
    data::unwrapMolecules(*m_active->current);
    m_active->detectedBondScale = std::numeric_limits<float>::quiet_NaN();
    clearSelection();
    emit structureChanged();
    emit structureUpdated(m_active->current);
}

void StructureModel::setReplicationFactors(int nx, int ny, int nz) {
    const std::array<int, 3> factors{nx, ny, nz};
    if (m_replication == factors) return;
    m_replication = factors;
    emit replicationFactorsChanged();
}

void StructureModel::setSelectionMode(int mode) {
    mode = std::clamp(mode, 0, 2);
    const bool modeChanged = mode != m_active->selectionMode;
    setSelectionModeInternal(mode, modeChanged);
    if (modeChanged && mode == 0) {
        clearSelection();
    }
}

void StructureModel::clearSelection() {
    if (m_active->current) {
        m_active->current->clearSelection();
    }
    emit selectionChanged();
    emit structureStyleChanged();
}

bool StructureModel::toggleAtomSelection(size_t atomIndex) {
    if (!m_active->current || atomIndex >= m_active->current->atomCount()) return false;
    m_active->current->toggleAtomSelected(atomIndex);
    emit selectionChanged();
    emit structureStyleChanged();
    return true;
}

bool StructureModel::toggleBondSelection(size_t bondIndex) {
    if (!m_active->current || bondIndex >= m_active->current->bonds().bondCount()) return false;
    m_active->current->bonds().toggleSelected(bondIndex);
    emit selectionChanged();
    emit structureStyleChanged();
    return true;
}

bool StructureModel::toggleMoleculeSelectionFromAtom(size_t atomIndex) {
    if (!m_active->current || atomIndex >= m_active->current->atomCount()) return false;
    const data::ConnectedSelection component = data::connectedSelectionFromAtom(*m_active->current, atomIndex);
    const bool deselect = componentFullySelected(component);
    return setComponentSelection(component, !deselect);
}

bool StructureModel::toggleMoleculeSelectionFromBond(size_t bondIndex) {
    if (!m_active->current || bondIndex >= m_active->current->bonds().bondCount()) return false;
    const data::ConnectedSelection component = data::connectedSelectionFromBond(*m_active->current, bondIndex);
    const bool deselect = componentFullySelected(component);
    return setComponentSelection(component, !deselect);
}

bool StructureModel::applyAtomScaleToSelection(float scale, float globalAtomScale) {
    if (!m_active->current || !selectionEnabled() || selectedAtomCount() == 0) return false;
    if (!std::isfinite(scale) || !std::isfinite(globalAtomScale)) return false;

    const float safeGlobalScale = std::max(globalAtomScale, 0.0001f);
    float* radii = m_active->current->radii();
    const int* atomicNumbers = m_active->current->atomicNumbers();
    const auto& selectedAtoms = m_active->current->atomSelectionMask();
    for (size_t i = 0; i < m_active->current->atomCount(); ++i) {
        if (!selectedAtoms[i]) continue;
        radii[i] = data::ElementData::radiusForElement(atomicNumbers[i], false) * scale / safeGlobalScale;
    }

    // Radii feed BVH bounds — geometry, not just appearance.
    emit structureGeometryChanged();
    return true;
}

bool StructureModel::applyAtomColorSchemeToSelection(int scheme) {
    if (!m_active->current || !selectionEnabled() || selectedAtomCount() == 0) return false;

    const auto colorScheme = colorSchemeFromIndex(scheme);
    float* cr = m_active->current->colorsR();
    float* cg = m_active->current->colorsG();
    float* cb = m_active->current->colorsB();
    const int* atomicNumbers = m_active->current->atomicNumbers();
    const auto& selectedAtoms = m_active->current->atomSelectionMask();

    for (size_t i = 0; i < m_active->current->atomCount(); ++i) {
        if (!selectedAtoms[i]) continue;
        const auto color = data::ElementData::colorForElement(atomicNumbers[i], colorScheme);
        m_active->current->setColorOverride(i, true);
        cr[i] = color.r;
        cg[i] = color.g;
        cb[i] = color.b;
    }

    syncSelectedBondEndpointColorsFromSelectedAtoms(*m_active->current);
    emit structureStyleChanged();
    return true;
}

bool StructureModel::applyAtomColorToSelection(const QColor& color) {
    if (!m_active->current || !selectionEnabled() || selectedAtomCount() == 0) return false;

    const data::Color target = colorFromQColor(color);
    float* cr = m_active->current->colorsR();
    float* cg = m_active->current->colorsG();
    float* cb = m_active->current->colorsB();
    const auto& selectedAtoms = m_active->current->atomSelectionMask();

    for (size_t i = 0; i < m_active->current->atomCount(); ++i) {
        if (!selectedAtoms[i]) continue;
        m_active->current->setColorOverride(i, true);
        cr[i] = target.r;
        cg[i] = target.g;
        cb[i] = target.b;
    }

    syncSelectedBondEndpointColorsFromSelectedAtoms(*m_active->current);
    emit structureStyleChanged();
    return true;
}

bool StructureModel::applyBondRadiusToSelection(float radius) {
    if (!m_active->current || !selectionEnabled() || selectedBondCount() == 0) return false;
    if (!std::isfinite(radius)) return false;

    auto& bonds = m_active->current->bonds();
    const auto& selectedBonds = bonds.selectionMask();
    for (size_t i = 0; i < bonds.bondCount(); ++i) {
        if (selectedBonds[i]) {
            bonds.setRadius(i, radius, true);
        }
    }

    // Bond radii are baked into BVH bounds — geometry, not just appearance.
    emit structureGeometryChanged();
    return true;
}

bool StructureModel::resetSelectedObjects(float defaultBondRadius, int colorScheme) {
    if (!m_active->current || !selectionEnabled() || !m_active->current->hasSelection()) return false;
    if (!std::isfinite(defaultBondRadius)) return false;

    const auto scheme = colorSchemeFromIndex(colorScheme);
    float* radii = m_active->current->radii();
    float* cr = m_active->current->colorsR();
    float* cg = m_active->current->colorsG();
    float* cb = m_active->current->colorsB();
    const int* atomicNumbers = m_active->current->atomicNumbers();
    const auto& selectedAtoms = m_active->current->atomSelectionMask();

    for (size_t i = 0; i < m_active->current->atomCount(); ++i) {
        if (!selectedAtoms[i]) continue;
        radii[i] = data::ElementData::radiusForElement(atomicNumbers[i], false);
        m_active->current->setColorOverride(i, false);
        const auto color = data::ElementData::colorForElement(atomicNumbers[i], scheme);
        cr[i] = color.r;
        cg[i] = color.g;
        cb[i] = color.b;
    }

    auto& bonds = m_active->current->bonds();
    const auto& selectedBonds = bonds.selectionMask();
    const float clampedBondRadius = std::clamp(defaultBondRadius, 0.01f, 0.6f);
    for (size_t i = 0; i < bonds.bondCount(); ++i) {
        if (!selectedBonds[i]) continue;

        const data::Bond& bond = bonds.bond(i);
        if (bond.atomIndex1 >= m_active->current->atomCount() ||
            bond.atomIndex2 >= m_active->current->atomCount()) {
            continue;
        }

        bonds.setRadius(i, clampedBondRadius);
        bonds.setEndpointColors(
            i,
            data::ElementData::colorForElement(atomicNumbers[bond.atomIndex1], scheme),
            data::ElementData::colorForElement(atomicNumbers[bond.atomIndex2], scheme));
    }

    // Resets both radii (geometry) and colors (appearance; also drives the
    // selectedAtomColor QML NOTIFY).
    emit structureGeometryChanged();
    emit structureStyleChanged();
    return true;
}

bool StructureModel::deleteSelectedObjects() {
    if (!m_active->current || !selectionEnabled() || !m_active->current->hasSelection()) return false;

    auto editedStructure = m_active->current->clone();
    if (!editedStructure->deleteSelectedObjects()) return false;

    cancelBondDetection();
    m_active->current = std::move(editedStructure);
    // A valid edited bond list stays valid, including explicit deletions.
    // If the import/cutoff calculation is unfinished, detect the surviving atoms.
    const bool needsBonds = m_active->detectedBondScale != m_requestedBondScale;
    updateElementList();

    emit structureChanged();
    emit selectionChanged();
    emit structureStyleChanged();
    emit structureEdited(m_active->current);
    if (needsBonds) ensureBonds(m_requestedBondScale);
    return true;
}

void StructureModel::clear() {
    cancelBondDetection();
    m_documents.clear();
    m_active = std::make_shared<StructureDocument>();
    m_activeIndex = -1;
    m_deferredIndex = -1;
    m_replication = {1, 1, 1};
    emit structuresChanged();
    emit activeStructureChanged();
    emit replicationFactorsChanged();
    emit structureChanged();
    emitSelectionResetSignals();
    emit structureActivated(nullptr, false);
}

void StructureModel::cancelBondDetection() {
    ++m_bondRevision;
    m_bondPending = false;
    if (m_bondCancellation) m_bondCancellation->store(true);
}

void StructureModel::ensureBonds(float scale) {
    m_requestedBondScale = scale;
    // Reverting a cutoff to cached data must also cancel an unfinished request
    // for the previous cutoff, otherwise its result can overwrite the cache.
    cancelBondDetection();
    if (!m_active->current || m_active->detectedBondScale == scale) return;
    m_bondPending = true;
    if (!m_bondRunning) launchBondDetection();
}

void StructureModel::launchBondDetection() {
    m_bondPending = false;
    if (!m_active->current) return;
    if (!m_bondWatcher) {
        m_bondWatcher = new QFutureWatcher<BondResult>(this);
        connect(m_bondWatcher, &QFutureWatcher<BondResult>::finished,
                this, &StructureModel::onBondsReady);
    }
    // Copy only the input needed by neighbor detection. Never let a worker read
    // live positions while the user unwraps, deletes, or switches structures.
    auto snapshot = std::make_shared<data::Structure>();
    snapshot->resize(m_active->current->atomCount());
    const auto n = m_active->current->atomCount();
    std::copy_n(m_active->current->positionsX(), n, snapshot->positionsX());
    std::copy_n(m_active->current->positionsY(), n, snapshot->positionsY());
    std::copy_n(m_active->current->positionsZ(), n, snapshot->positionsZ());
    std::copy_n(m_active->current->atomicNumbers(), n, snapshot->atomicNumbers());
    snapshot->lattice() = m_active->current->lattice();
    auto cancellation = std::make_shared<std::atomic_bool>(false);
    m_bondCancellation = cancellation;
    m_bondRunning = true;
    const auto source = m_active->current;
    const auto revision = m_bondRevision;
    const float scale = m_requestedBondScale;
    m_bondWatcher->setFuture(QtConcurrent::run([snapshot, source, revision, scale, cancellation]() {
        data::NeighborList neighbors;
        neighbors.build(*snapshot, scale, cancellation.get());
        auto bonds = cancellation->load() ? nullptr
            : neighbors.buildBondList(*snapshot, scale, cancellation.get());
        return BondResult{std::move(bonds), source, revision, scale};
    }));
}

void StructureModel::onBondsReady() {
    m_bondRunning = false;
    const auto result = m_bondWatcher->result();
    if (result.revision == m_bondRevision && result.source == m_active->current && result.bonds) {
        auto& bonds = *result.bonds;
        bonds.setAllRadii(m_bondRadius);
        // Restore surviving per-bond edits after an explicit cutoff change.
        using Key = std::tuple<uint32_t, uint32_t, int, int, int>;
        const auto key = [](const data::Bond& b) { return Key{b.atomIndex1, b.atomIndex2, b.imageX, b.imageY, b.imageZ}; };
        const auto& previous = m_active->current->bonds();
        std::map<Key, size_t> edits;
        for (size_t i = 0; i < previous.bondCount(); ++i)
            if (previous.radiusOverridden(i) || previous.startColorOverridden(i) || previous.endColorOverridden(i) || previous.selected(i))
                edits.emplace(key(previous.bond(i)), i);
        for (size_t i = 0; i < bonds.bondCount(); ++i) {
            auto found = edits.find(key(bonds.bond(i)));
            if (found == edits.end()) continue;
            const size_t old = found->second;
            if (previous.radiusOverridden(old)) bonds.setRadius(i, previous.radius(old), true);
            if (previous.startColorOverridden(old)) bonds.setStartColor(i, previous.startColor(old), true);
            if (previous.endColorOverridden(old)) bonds.setEndColor(i, previous.endColor(old), true);
            bonds.setSelected(i, previous.selected(old));
        }
        m_active->current->setBondList(result.bonds);
        m_active->current->updateBondColorsFromElements(colorSchemeFromIndex(m_colorScheme), true);
        m_active->detectedBondScale = result.scale;
        emit structureChanged();
        emit selectionChanged();
        emit structureGeometryChanged();
    }
    if (m_bondPending) launchBondDetection();
}

void StructureModel::notifyBondsUpdated() {
    clearSelection();
    emit structureChanged();
}

void StructureModel::updateElementList() {
    m_active->elements.clear();

    if (!m_active->current || m_active->current->atomCount() == 0) return;

    // Count atoms per element
    QMap<int, int> counts;
    const int* atomicNums = m_active->current->atomicNumbers();
    size_t n = m_active->current->atomCount();

    for (size_t i = 0; i < n; ++i) {
        counts[atomicNums[i]]++;
    }

    // Build element list with counts
    for (auto it = counts.begin(); it != counts.end(); ++it) {
        const auto& elem = data::ElementData::byAtomicNumber(it.key());
        QString symbol = QString::fromUtf8(elem.symbol.data(), elem.symbol.size());
        m_active->elements.append(QString("%1: %2").arg(symbol).arg(it.value()));
    }
}

void StructureModel::setSelectionModeInternal(int mode, bool emitChange) {
    m_active->selectionMode = std::clamp(mode, 0, 2);
    if (emitChange) {
        emit selectionModeChanged();
    }
}

bool StructureModel::componentFullySelected(const data::ConnectedSelection& component) const {
    if (!m_active->current) return false;
    if (component.atoms.empty() && component.bonds.empty()) return false;

    for (size_t atomIndex : component.atoms) {
        if (atomIndex >= m_active->current->atomCount() || !m_active->current->atomSelected(atomIndex)) {
            return false;
        }
    }

    const auto& bonds = m_active->current->bonds();
    for (size_t bondIndex : component.bonds) {
        if (bondIndex >= bonds.bondCount() || !bonds.selected(bondIndex)) {
            return false;
        }
    }

    return true;
}

bool StructureModel::setComponentSelection(const data::ConnectedSelection& component, bool selected) {
    if (!m_active->current) return false;
    bool changed = false;

    for (size_t atomIndex : component.atoms) {
        if (atomIndex >= m_active->current->atomCount()) continue;
        if (m_active->current->atomSelected(atomIndex) != selected) {
            m_active->current->setAtomSelected(atomIndex, selected);
            changed = true;
        }
    }

    auto& bonds = m_active->current->bonds();
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
