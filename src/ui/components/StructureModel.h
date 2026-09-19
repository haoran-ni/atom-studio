#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>
#include "StructureDocument.h"
#include <QFutureWatcher>
#include <atomic>
#include <vector>
#include <array>
#include <cstddef>
#include <memory>

namespace atom::data {
class Structure;
class BondList;
struct ConnectedSelection;
}

namespace atom::ui {

/**
 * @brief QML model for atomic structure data
 *
 * Exposes structure properties to QML for the sidebar.
 */
class StructureModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int structureCount READ structureCount NOTIFY structuresChanged)
    Q_PROPERTY(int atomRadiusType READ atomRadiusType WRITE setAtomRadiusType NOTIFY atomRadiusTypeChanged)
    Q_PROPERTY(bool editsLocked READ editsLocked NOTIFY editsLockedChanged)
    Q_PROPERTY(int activeIndex READ activeIndex WRITE setActiveIndex NOTIFY activeStructureChanged)
    Q_PROPERTY(qint64 activeId READ activeId NOTIFY activeStructureChanged)
    Q_PROPERTY(bool switchingLocked READ switchingLocked WRITE setSwitchingLocked NOTIFY switchingLockedChanged)
    Q_PROPERTY(bool hasStructure READ hasStructure NOTIFY structureChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY structureChanged)
    Q_PROPERTY(QString structureName READ structureName NOTIFY structureChanged)
    Q_PROPERTY(int atomCount READ atomCount NOTIFY structureChanged)
    Q_PROPERTY(int bondCount READ bondCount NOTIFY structureChanged)
    Q_PROPERTY(int atomTypeCount READ atomTypeCount NOTIFY structureChanged)
    Q_PROPERTY(QStringList elements READ elements NOTIFY structureChanged)
    Q_PROPERTY(bool hasUnitCell READ hasUnitCell NOTIFY structureChanged)
    Q_PROPERTY(bool hasReplicableStructures READ hasReplicableStructures NOTIFY structuresChanged)
    Q_PROPERTY(bool hasBonds READ hasBonds NOTIFY structureChanged)
    Q_PROPERTY(QString cellParameters READ cellParameters NOTIFY structureChanged)
    Q_PROPERTY(int replicationX READ replicationX NOTIFY replicationFactorsChanged)
    Q_PROPERTY(int replicationY READ replicationY NOTIFY replicationFactorsChanged)
    Q_PROPERTY(int replicationZ READ replicationZ NOTIFY replicationFactorsChanged)
    Q_PROPERTY(int selectionMode READ selectionMode WRITE setSelectionMode NOTIFY selectionModeChanged)
    Q_PROPERTY(bool selectionEnabled READ selectionEnabled NOTIFY selectionModeChanged)
    Q_PROPERTY(int selectedAtomCount READ selectedAtomCount NOTIFY selectionChanged)
    Q_PROPERTY(int selectedBondCount READ selectedBondCount NOTIFY selectionChanged)
    Q_PROPERTY(QColor selectedAtomColor READ selectedAtomColor NOTIFY structureStyleChanged)

public:
    explicit StructureModel(QObject* parent = nullptr);
    ~StructureModel() override;

    static StructureModel* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    static StructureModel* instance() { return s_instance; }

    /** Returns the working (possibly modified) structure shown in the viewport. */
    std::shared_ptr<data::Structure> structure() const { return m_active->current; }

    /** Returns the original structure as loaded from file (never modified). */
    std::shared_ptr<const data::Structure> originalStructure() const { return m_active->raw; }

    int structureCount() const { return static_cast<int>(m_documents.size()); }
    int activeIndex() const { return m_activeIndex; }
    qint64 activeId() const { return m_active->id; }
    bool switchingLocked() const { return m_switchingLocked; }
    qint64 addStructure(std::shared_ptr<data::Structure> structure, bool replicate = true);
    const std::vector<std::shared_ptr<StructureDocument>>& documents() const { return m_documents; }
    std::shared_ptr<StructureDocument> document(qint64 id) const;
    bool editsLocked() const { return m_editsLocked; }
    void setEditsLocked(bool locked);
    bool liveFramePending() const { return m_liveFramePending; }
    void finishLiveFrame() { m_liveFramePending = false; }
    bool applyShellStructure(qint64 id, quint64 revision, std::shared_ptr<data::Structure> structure);
    Q_INVOKABLE void setActiveIndex(int index);
    void setSwitchingLocked(bool locked);
    void applySharedAppearance(int colorScheme, float bondRadius);
    int atomRadiusType() const { return m_atomRadiusType; }
    void setAtomRadiusType(int type);
    void ensureBonds(float scale);
    bool isDetectingBonds() const { return m_bondRunning || m_bondPending; }
    bool hasStructure() const;
    QString fileName() const;
    QString structureName() const;
    int atomCount() const;
    int bondCount() const;
    int atomTypeCount() const;
    QStringList elements() const;
    bool hasUnitCell() const;
    bool hasReplicableStructures() const;
    float maximumViewExtent() const;
    bool hasBonds() const;
    QString cellParameters() const;
    int replicationX() const { return m_replication[0]; }
    int replicationY() const { return m_replication[1]; }
    int replicationZ() const { return m_replication[2]; }
    int selectionMode() const;
    bool selectionEnabled() const;
    int selectedAtomCount() const;
    int selectedBondCount() const;
    bool hasActiveAtomSelection() const;
    bool hasActiveBondSelection() const;
    QColor selectedAtomColor() const;

public slots:
    void setStructure(std::shared_ptr<atom::data::Structure> structure);
    Q_INVOKABLE void resetToOriginal();
    Q_INVOKABLE void replicateCell(int nx, int ny, int nz);
    Q_INVOKABLE void unwrapMolecules();
    Q_INVOKABLE void setSelectionMode(int mode);
    Q_INVOKABLE void clearSelection();
    bool toggleAtomSelection(size_t atomIndex);
    bool toggleBondSelection(size_t bondIndex);
    bool toggleMoleculeSelectionFromAtom(size_t atomIndex);
    bool toggleMoleculeSelectionFromBond(size_t bondIndex);
    bool applyAtomScaleToSelection(float scale, float globalAtomScale);
    bool applyAtomColorSchemeToSelection(int scheme);
    Q_INVOKABLE bool applyAtomColorToSelection(const QColor& color);
    bool applyBondRadiusToSelection(float radius);
    Q_INVOKABLE bool resetSelectedObjects(float defaultBondRadius, int colorScheme);
    Q_INVOKABLE bool deleteSelectedObjects();
    void clear();
    void notifyBondsUpdated();

signals:
    void atomRadiusTypeChanged();
    void editsLockedChanged();
    void documentGeometryChanged(qint64 id);
    void structuresChanged();
    void activeStructureChanged();
    void switchingLockedChanged();
    void structureActivated(std::shared_ptr<data::Structure> structure, bool firstStructure);
    void structureChanged();
    void replicationFactorsChanged();
    void selectionModeChanged();
    void selectionChanged();
    /// Appearance-only change (colors, selection styling).
    /// Renderers may refresh color data without rebuilding geometry/BVH.
    void structureStyleChanged();
    /// Geometry-affecting style change (atom radii, bond radii). Renderers
    /// must repack geometry and rebuild acceleration structures.
    void structureGeometryChanged();
    void structureUpdated(std::shared_ptr<data::Structure> structure);
    void structureEdited(std::shared_ptr<data::Structure> structure);

private:
    void applyAtomRadiusType(StructureDocument& document);
    void updateElementList();
    void setSelectionModeInternal(int mode, bool emitChange);
    bool setComponentSelection(const data::ConnectedSelection& component, bool selected);
    bool componentFullySelected(const data::ConnectedSelection& component) const;
    void emitSelectionResetSignals();
    void setReplicationFactors(int nx, int ny, int nz);

    std::vector<std::shared_ptr<StructureDocument>> m_documents;
    std::array<int, 3> m_replication{1, 1, 1};
    std::shared_ptr<StructureDocument> m_active = std::make_shared<StructureDocument>();
    qint64 m_nextId = 0;
    int m_activeIndex = -1;
    bool m_switchingLocked = false;
    bool m_editsLocked = false;
    bool m_liveFramePending = false;
    int m_deferredIndex = -1;

    struct BondResult {
        std::shared_ptr<data::BondList> bonds;
        std::shared_ptr<data::Structure> source;
        quint64 revision;
        float scale;
    };
    QFutureWatcher<BondResult>* m_bondWatcher = nullptr;
    std::shared_ptr<std::atomic_bool> m_bondCancellation;
    quint64 m_bondRevision = 0;
    bool m_bondRunning = false;
    bool m_bondPending = false;
    float m_requestedBondScale = 1.1f;
    int m_colorScheme = 0;
    int m_atomRadiusType = 0; // 0 = covalent, 1 = Alvarez vdW (with legacy fallbacks).
    float m_bondRadius = 0.1f;
    void cancelBondDetection();
    void launchBondDetection();
    void onBondsReady();
    void nameCurrentStructure();

    static StructureModel* s_instance;
};

} // namespace atom::ui
