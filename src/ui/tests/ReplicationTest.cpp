#include "components/FileController.h"
#include "components/StructureModel.h"
#include "Structure.h"
#include <QtQuickTest/quicktest.h>
#include <QQmlContext>
#include <QQmlEngine>

class ReplicationSetup : public QObject {
    Q_OBJECT

public:
    Q_INVOKABLE void load(bool periodic = true) {
        m_model.clear();
        add(periodic);
    }

    Q_INVOKABLE void add(bool periodic = true) {
        auto structure = std::make_shared<atom::data::Structure>();
        structure->addAtom(0.2f, 0.3f, 0.4f, 6);
        structure->addAtom(1.2f, 1.3f, 1.4f, 8);
        auto& cell = structure->lattice();
        cell.defined = periodic;
        cell.matrix = {{{3, 0.5, 0}, {0, 4, 0.25}, {0, 0, 5}}};
        cell.pbc = {periodic, periodic, periodic};
        m_model.addStructure(structure);
    }

    Q_INVOKABLE double latticeComponent(int axis, int component) const {
        return m_model.structure()->lattice().matrix[axis][component];
    }

    Q_INVOKABLE bool deleteFirstAtom() {
        m_model.setSelectionMode(1);
        m_model.toggleAtomSelection(0);
        return m_model.deleteSelectedObjects();
    }

public slots:
    void qmlEngineAvailable(QQmlEngine* engine) {
        qmlRegisterSingletonInstance("AtomStudio", 1, 0, "StructureModel", &m_model);
        qmlRegisterSingletonInstance("AtomStudio", 1, 0, "FileController", &m_files);
        engine->rootContext()->setContextProperty("ReplicationFixture", this);
    }

private:
    atom::ui::StructureModel m_model;
    atom::ui::FileController m_files;
};

QUICK_TEST_MAIN_WITH_SETUP(replication, ReplicationSetup)

#include "ReplicationTest.moc"
