#pragma once

#include <QStringList>
#include <memory>
#include <limits>

namespace atom::data { class Structure; }
namespace atom::ui {

// CPU state for one import. Renderers only receive the active document's current copy.
struct StructureDocument {
    qint64 id = -1;
    std::shared_ptr<const data::Structure> raw;
    std::shared_ptr<data::Structure> current;
    QStringList elements;
    int selectionMode = 0;
    int appliedColorScheme = -1;
    float appliedBondRadius = -1;
    float detectedBondScale = std::numeric_limits<float>::quiet_NaN();
};

} // namespace atom::ui
