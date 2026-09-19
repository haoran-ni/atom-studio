#pragma once

#include <QStringList>
#include <memory>
#include <limits>
#include <set>
#include <tuple>

namespace atom::data { class Structure; }
namespace atom::ui {

// CPU state for one import. Renderers only receive the active document's current copy.
struct StructureDocument {
    qint64 id = -1;
    quint64 revision = 0;
    using BondIdentity = std::tuple<int64_t, int64_t, int, int, int>;
    std::set<BondIdentity> deletedBonds;
    std::shared_ptr<const data::Structure> raw;
    std::shared_ptr<data::Structure> current;
    QStringList elements;
    int selectionMode = 0;
    int appliedColorScheme = -1;
    int appliedAtomRadiusType = 0; // Incoming structures use covalent radii.
    float appliedBondRadius = -1;
    float detectedBondScale = std::numeric_limits<float>::quiet_NaN();
};

} // namespace atom::ui
