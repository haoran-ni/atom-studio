#pragma once

#include <QAbstractListModel>
#include <memory>
#include <vector>

namespace atom::data { class Structure; }
namespace atom::ui {
class StructureModel;

// A lightweight view of the active structure; QML instantiates only visible rows.
class AtomPropertiesModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QStringList speciesLabels READ speciesLabels NOTIFY columnLabelsChanged)
    Q_PROPERTY(QString largestDisplayId READ largestDisplayId NOTIFY columnLabelsChanged)
public:
    enum Role { AtomIdentifier = Qt::UserRole + 1, AtomicNumber, SpeciesName, AtomColor, AtomRadius };
    explicit AtomPropertiesModel(StructureModel* owner);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QStringList speciesLabels() const { return m_speciesLabels; }
    QString largestDisplayId() const { return m_indices.empty() ? QStringLiteral("0") : QString::number(m_displayIds[m_indices.back()]); }

signals:
    void columnLabelsChanged();

private:
    void refresh();
    void refreshProperties();
    StructureModel* m_owner;
    std::shared_ptr<data::Structure> m_structure;
    std::vector<size_t> m_indices;
    std::vector<qint64> m_displayIds;
    QStringList m_speciesLabels;
};
}
