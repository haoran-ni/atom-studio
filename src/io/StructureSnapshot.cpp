#include "StructureSnapshot.h"
#include "../data/Structure.h"
#include <QJsonArray>
#include <cmath>
#include <set>
#include <stdexcept>

namespace atom::io {
namespace {
double number(const QJsonValue& value) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()) || std::abs(value.toDouble()) > 1e15)
        throw std::runtime_error("Invalid or non-finite structure coordinate");
    return value.toDouble();
}
QJsonArray array(const QJsonValue& value, qsizetype size) {
    if (!value.isArray() || value.toArray().size() != size)
        throw std::runtime_error("Invalid structure array dimensions");
    return value.toArray();
}
}

QJsonObject structureSnapshot(const data::Structure& structure) {
    QJsonArray positions, numbers, ids, cell, pbc, edits;
    for (size_t i = 0; i < structure.atomCount(); ++i) {
        const auto p = structure.precisePosition(i);
        positions.append(QJsonArray{p[0], p[1], p[2]});
        numbers.append(structure.atomicNumber(i));
        ids.append(static_cast<qint64>(structure.atomId(i)));
    }
    for (int axis = 0; axis < 3; ++axis) {
        const auto row = structure.hasLattice() ? structure.lattice().matrix[axis] : std::array<double, 3>{};
        cell.append(QJsonArray{row[0], row[1], row[2]});
        pbc.append(structure.lattice().pbc[axis]);
    }
    for (const auto& edit : structure.aseEdits()) {
        QJsonArray values;
        if (edit.isRepeat) {
            for (int value : edit.repeat) values.append(value);
            edits.append(QJsonObject{{"repeat", values}});
        } else {
            for (size_t value : edit.indices) values.append(static_cast<qint64>(value));
            edits.append(QJsonObject{{"indices", values}});
        }
    }
    return {{"positions", positions}, {"numbers", numbers}, {"ids", ids},
            {"cell", cell}, {"pbc", pbc}, {"edits", edits},
            {"ase", QString::fromStdString(structure.asePayload())}};
}

std::shared_ptr<data::Structure> structureFromSnapshot(const QJsonObject& snapshot) {
    if (!snapshot["numbers"].isArray()) throw std::runtime_error("Missing atomic numbers");
    const auto numbers = snapshot["numbers"].toArray();
    const auto positions = array(snapshot["positions"], numbers.size());
    const auto ids = array(snapshot["ids"], numbers.size());
    const auto cell = array(snapshot["cell"], 3);
    const auto pbc = array(snapshot["pbc"], 3);
    auto result = std::make_shared<data::Structure>();
    result->reserve(numbers.size());
    std::set<qint64> seen;
    for (qsizetype i = 0; i < numbers.size(); ++i) {
        const double z = number(numbers[i]);
        if (z != std::floor(z) || z < 0 || z > 118) throw std::runtime_error("Invalid atomic number");
        const double id = number(ids[i]);
        if (id != std::floor(id) || id <= 0 || !seen.insert(static_cast<qint64>(id)).second)
            throw std::runtime_error("Atom identifiers must be unique positive integers");
        const auto p = array(positions[i], 3);
        const double x = number(p[0]), y = number(p[1]), zz = number(p[2]);
        result->addAtom(x, y, zz, static_cast<int>(z));
        result->setPrecisePosition(i, x, y, zz);
        result->setAtomId(i, static_cast<int64_t>(id));
    }
    for (int axis = 0; axis < 3; ++axis) {
        const auto row = array(cell[axis], 3);
        if (!pbc[axis].isBool()) throw std::runtime_error("Invalid periodic boundary flag");
        result->lattice().pbc[axis] = pbc[axis].toBool();
        for (int d = 0; d < 3; ++d) {
            const double value = number(row[d]);
            result->lattice().matrix[axis][d] = value;
            result->lattice().defined |= value != 0;
        }
    }
    if (!snapshot["ase"].isString()) throw std::runtime_error("Missing ASE metadata");
    result->setASEPayload(snapshot["ase"].toString().toStdString());
    if (snapshot.contains("energy")) result->setEnergy(number(snapshot["energy"]));
    if (snapshot.contains("forces")) {
        const auto forces = array(snapshot["forces"], numbers.size());
        std::vector<float> x, y, z;
        for (const auto& value : forces) {
            const auto f = array(value, 3);
            x.push_back(number(f[0])); y.push_back(number(f[1])); z.push_back(number(f[2]));
        }
        result->setForces(std::move(x), std::move(y), std::move(z));
    }
    return result;
}
}
