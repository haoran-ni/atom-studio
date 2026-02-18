#include "StructureModel.h"
#include "../../data/Structure.h"
#include "../../data/BondList.h"
#include "../../data/ElementData.h"

#include <QFileInfo>
#include <QQmlEngine>
#include <QSet>

namespace atom::ui {

StructureModel* StructureModel::s_instance = nullptr;

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

void StructureModel::setStructure(std::shared_ptr<data::Structure> structure) {
    m_structure = structure;
    updateElementList();
    emit structureChanged();
    emit structureUpdated(structure);
}

void StructureModel::clear() {
    m_structure.reset();
    m_elements.clear();
    emit structureChanged();
}

void StructureModel::notifyBondsUpdated() {
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

} // namespace atom::ui
