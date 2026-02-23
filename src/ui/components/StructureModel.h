#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace atom::data {
class Structure;
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

public slots:
    void setStructure(std::shared_ptr<atom::data::Structure> structure);
    Q_INVOKABLE void resetToOriginal();
    Q_INVOKABLE void replicateCell(int nx, int ny, int nz);
    Q_INVOKABLE void unwrapMolecules();
    void clear();
    void notifyBondsUpdated();

signals:
    void structureChanged();
    void structureUpdated(std::shared_ptr<data::Structure> structure);

private:
    void updateElementList();

    std::shared_ptr<data::Structure> m_originalStructure;  // immutable — set once on load
    std::shared_ptr<data::Structure> m_structure;          // working copy shown in viewport
    QStringList m_elements;

    static StructureModel* s_instance;
};

} // namespace atom::ui
