#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>
#include <cstddef>
#include <memory>

namespace atom::data {
class Structure;
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

    Q_PROPERTY(bool hasStructure READ hasStructure NOTIFY structureChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY structureChanged)
    Q_PROPERTY(QString structureName READ structureName NOTIFY structureChanged)
    Q_PROPERTY(int atomCount READ atomCount NOTIFY structureChanged)
    Q_PROPERTY(int bondCount READ bondCount NOTIFY structureChanged)
    Q_PROPERTY(int atomTypeCount READ atomTypeCount NOTIFY structureChanged)
    Q_PROPERTY(QStringList elements READ elements NOTIFY structureChanged)
    Q_PROPERTY(bool hasUnitCell READ hasUnitCell NOTIFY structureChanged)
    Q_PROPERTY(bool hasBonds READ hasBonds NOTIFY structureChanged)
    Q_PROPERTY(QString cellParameters READ cellParameters NOTIFY structureChanged)
    Q_PROPERTY(int selectionMode READ selectionMode WRITE setSelectionMode NOTIFY selectionModeChanged)
    Q_PROPERTY(bool selectionEnabled READ selectionEnabled NOTIFY selectionModeChanged)
    Q_PROPERTY(int selectedAtomCount READ selectedAtomCount NOTIFY selectionChanged)
    Q_PROPERTY(int selectedBondCount READ selectedBondCount NOTIFY selectionChanged)
    Q_PROPERTY(QColor selectedAtomColor READ selectedAtomColor NOTIFY structureStyleChanged)
    Q_PROPERTY(int selectedAtomTransparency READ selectedAtomTransparency NOTIFY structureStyleChanged)

public:
    explicit StructureModel(QObject* parent = nullptr);
    ~StructureModel() override;

    static StructureModel* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    static StructureModel* instance() { return s_instance; }

    /** Returns the working (possibly modified) structure shown in the viewport. */
    std::shared_ptr<data::Structure> structure() const { return m_structure; }

    /** Returns the original structure as loaded from file (never modified). */
    std::shared_ptr<data::Structure> originalStructure() const { return m_originalStructure; }

    bool hasStructure() const;
    QString fileName() const;
    QString structureName() const;
    int atomCount() const;
    int bondCount() const;
    int atomTypeCount() const;
    QStringList elements() const;
    bool hasUnitCell() const;
    bool hasBonds() const;
    QString cellParameters() const;
    int selectionMode() const;
    bool selectionEnabled() const;
    int selectedAtomCount() const;
    int selectedBondCount() const;
    bool hasActiveAtomSelection() const;
    bool hasActiveBondSelection() const;
    QColor selectedAtomColor() const;
    int selectedAtomTransparency() const;

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
    Q_INVOKABLE bool applyAtomTransparencyToSelection(int transparency);
    bool applyBondRadiusToSelection(float radius);
    Q_INVOKABLE bool resetSelectedObjects(float defaultBondRadius, int colorScheme);
    void clear();
    void notifyBondsUpdated();

signals:
    void structureChanged();
    void selectionModeChanged();
    void selectionChanged();
    /// Appearance-only change (colors, transparency, selection highlight).
    /// Renderers may refresh color data without rebuilding geometry/BVH.
    void structureStyleChanged();
    /// Geometry-affecting style change (atom radii, bond radii). Renderers
    /// must repack geometry and rebuild acceleration structures.
    void structureGeometryChanged();
    void structureUpdated(std::shared_ptr<data::Structure> structure);

private:
    void updateElementList();
    void setSelectionModeInternal(int mode, bool emitChange);
    bool setComponentSelection(const data::ConnectedSelection& component, bool selected);
    bool componentFullySelected(const data::ConnectedSelection& component) const;
    void emitSelectionResetSignals();

    std::shared_ptr<data::Structure> m_originalStructure;  // immutable — set once on load
    std::shared_ptr<data::Structure> m_structure;          // working copy shown in viewport
    QStringList m_elements;
    int m_selectionMode = 0;

    static StructureModel* s_instance;
};

} // namespace atom::ui
