#include "ElementData.h"
#include "Structure.h"

#include <cmath>
#include <iostream>

namespace atom::data {
namespace {

bool nearlyEqual(float a, float b, float eps = 1e-6f) {
    return std::abs(a - b) <= eps;
}

bool checkColor(const Color& color, float r, float g, float b) {
    return nearlyEqual(color.r, r) &&
           nearlyEqual(color.g, g) &&
           nearlyEqual(color.b, b) &&
           nearlyEqual(color.a, 1.0f);
}

bool checkElementColorTables() {
    if (!checkColor(ElementData::colorForElement(8), 1.000f, 0.051f, 0.051f)) {
        std::cerr << "Default element colors should use ASE Jmol values\n";
        return false;
    }

    if (!checkColor(ElementData::colorForElement(8, ElementColorScheme::Cpk),
                    0.941f, 0.000f, 0.000f)) {
        std::cerr << "CPK element colors should use ASE CPK values\n";
        return false;
    }

    if (!checkColor(ElementData::colorForElement(104, ElementColorScheme::Cpk),
                    1.000f, 0.000f, 0.000f)) {
        std::cerr << "CPK colors above ASE coverage should fall back to ASE's unknown color\n";
        return false;
    }

    return true;
}

bool checkStructureColorPopulation() {
    Structure s;
    s.addAtom(0.0f, 0.0f, 0.0f, 8);
    s.addAtom(1.0f, 0.0f, 0.0f, 26);

    if (!checkColor(s.color(0), 1.000f, 0.051f, 0.051f)) {
        std::cerr << "New atoms should default to ASE Jmol colors\n";
        return false;
    }

    s.updateColorsFromElements(ElementColorScheme::Cpk);

    if (!checkColor(s.color(0), 0.941f, 0.000f, 0.000f) ||
        !checkColor(s.color(1), 1.000f, 0.647f, 0.000f)) {
        std::cerr << "Structure color refresh should honor the selected color scheme\n";
        return false;
    }

    return true;
}

} // namespace
} // namespace atom::data

int main() {
    bool ok = true;

    ok = atom::data::checkElementColorTables() && ok;
    ok = atom::data::checkStructureColorPopulation() && ok;

    if (!ok) return 1;

    std::cout << "Element color tests passed\n";
    return 0;
}
