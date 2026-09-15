#pragma once
#include <QJsonObject>
#include <memory>
namespace atom::data { class Structure; }
namespace atom::io {
// Python-free transport boundary: validates messages before model mutation.
QJsonObject structureSnapshot(const data::Structure& structure);
std::shared_ptr<data::Structure> structureFromSnapshot(const QJsonObject& snapshot);
}
