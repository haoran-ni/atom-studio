#include "ElementData.h"
#include <algorithm>
#include <cctype>
#include <mutex>
#include <optional>

namespace atom::data {

namespace {

// CPK/Jmol color scheme element data
// Colors based on Jmol color scheme (similar to CPK)
// Radii: covalent from Cordero et al., VdW from Bondi/Rowland & Taylor
constexpr std::array<ElementInfo, ElementData::MAX_ELEMENTS> kElements = {{
    // Unknown/placeholder (index 0) — no covalent radius
    {0, "X", "Unknown", 0.0f, -1.0f, 1.5f, Color::fromRgb(255, 20, 147)},
    // Period 1 — covalent radii from covalent_radii.md
    {1, "H", "Hydrogen", 1.008f, 0.31f, 1.20f, Color::fromRgb(255, 255, 255)},
    {2, "He", "Helium", 4.003f, 0.28f, 1.40f, Color::fromRgb(217, 255, 255)},
    // Period 2
    {3, "Li", "Lithium", 6.941f, 1.28f, 1.82f, Color::fromRgb(204, 128, 255)},
    {4, "Be", "Beryllium", 9.012f, 0.96f, 1.53f, Color::fromRgb(194, 255, 0)},
    {5, "B", "Boron", 10.81f, 0.85f, 1.92f, Color::fromRgb(255, 181, 181)},
    {6, "C", "Carbon", 12.01f, 0.76f, 1.70f, Color::fromRgb(144, 144, 144)},
    {7, "N", "Nitrogen", 14.01f, 0.71f, 1.55f, Color::fromRgb(48, 80, 248)},
    {8, "O", "Oxygen", 16.00f, 0.66f, 1.52f, Color::fromRgb(255, 13, 13)},
    {9, "F", "Fluorine", 19.00f, 0.57f, 1.47f, Color::fromRgb(144, 224, 80)},
    {10, "Ne", "Neon", 20.18f, 0.58f, 1.54f, Color::fromRgb(179, 227, 245)},
    // Period 3
    {11, "Na", "Sodium", 22.99f, 1.66f, 2.27f, Color::fromRgb(171, 92, 242)},
    {12, "Mg", "Magnesium", 24.31f, 1.41f, 1.73f, Color::fromRgb(138, 255, 0)},
    {13, "Al", "Aluminum", 26.98f, 1.21f, 1.84f, Color::fromRgb(191, 166, 166)},
    {14, "Si", "Silicon", 28.09f, 1.11f, 2.10f, Color::fromRgb(240, 200, 160)},
    {15, "P", "Phosphorus", 30.97f, 1.07f, 1.80f, Color::fromRgb(255, 128, 0)},
    {16, "S", "Sulfur", 32.07f, 1.05f, 1.80f, Color::fromRgb(255, 255, 48)},
    {17, "Cl", "Chlorine", 35.45f, 1.02f, 1.75f, Color::fromRgb(31, 240, 31)},
    {18, "Ar", "Argon", 39.95f, 1.06f, 1.88f, Color::fromRgb(128, 209, 227)},
    // Period 4
    {19, "K", "Potassium", 39.10f, 2.03f, 2.75f, Color::fromRgb(143, 64, 212)},
    {20, "Ca", "Calcium", 40.08f, 1.76f, 2.31f, Color::fromRgb(61, 255, 0)},
    {21, "Sc", "Scandium", 44.96f, 1.70f, 2.11f, Color::fromRgb(230, 230, 230)},
    {22, "Ti", "Titanium", 47.87f, 1.60f, 2.00f, Color::fromRgb(191, 194, 199)},
    {23, "V", "Vanadium", 50.94f, 1.53f, 1.92f, Color::fromRgb(166, 166, 171)},
    {24, "Cr", "Chromium", 52.00f, 1.39f, 1.85f, Color::fromRgb(138, 153, 199)},
    {25, "Mn", "Manganese", 54.94f, 1.39f, 1.79f, Color::fromRgb(156, 122, 199)},
    {26, "Fe", "Iron", 55.85f, 1.32f, 1.83f, Color::fromRgb(224, 102, 51)},
    {27, "Co", "Cobalt", 58.93f, 1.26f, 1.79f, Color::fromRgb(240, 144, 160)},
    {28, "Ni", "Nickel", 58.69f, 1.24f, 1.63f, Color::fromRgb(80, 208, 80)},
    {29, "Cu", "Copper", 63.55f, 1.32f, 1.40f, Color::fromRgb(200, 128, 51)},
    {30, "Zn", "Zinc", 65.38f, 1.22f, 1.39f, Color::fromRgb(125, 128, 176)},
    {31, "Ga", "Gallium", 69.72f, 1.22f, 1.87f, Color::fromRgb(194, 143, 143)},
    {32, "Ge", "Germanium", 72.63f, 1.20f, 2.11f, Color::fromRgb(102, 143, 143)},
    {33, "As", "Arsenic", 74.92f, 1.19f, 1.85f, Color::fromRgb(189, 128, 227)},
    {34, "Se", "Selenium", 78.97f, 1.20f, 1.90f, Color::fromRgb(255, 161, 0)},
    {35, "Br", "Bromine", 79.90f, 1.20f, 1.85f, Color::fromRgb(166, 41, 41)},
    {36, "Kr", "Krypton", 83.80f, 1.16f, 2.02f, Color::fromRgb(92, 184, 209)},
    // Period 5
    {37, "Rb", "Rubidium", 85.47f, 2.20f, 3.03f, Color::fromRgb(112, 46, 176)},
    {38, "Sr", "Strontium", 87.62f, 1.95f, 2.49f, Color::fromRgb(0, 255, 0)},
    {39, "Y", "Yttrium", 88.91f, 1.90f, 2.32f, Color::fromRgb(148, 255, 255)},
    {40, "Zr", "Zirconium", 91.22f, 1.75f, 2.23f, Color::fromRgb(148, 224, 224)},
    {41, "Nb", "Niobium", 92.91f, 1.64f, 2.18f, Color::fromRgb(115, 194, 201)},
    {42, "Mo", "Molybdenum", 95.95f, 1.54f, 2.17f, Color::fromRgb(84, 181, 181)},
    {43, "Tc", "Technetium", 98.00f, 1.47f, 2.16f, Color::fromRgb(59, 158, 158)},
    {44, "Ru", "Ruthenium", 101.1f, 1.46f, 2.13f, Color::fromRgb(36, 143, 143)},
    {45, "Rh", "Rhodium", 102.9f, 1.42f, 2.10f, Color::fromRgb(10, 125, 140)},
    {46, "Pd", "Palladium", 106.4f, 1.39f, 1.63f, Color::fromRgb(0, 105, 133)},
    {47, "Ag", "Silver", 107.9f, 1.45f, 1.72f, Color::fromRgb(192, 192, 192)},
    {48, "Cd", "Cadmium", 112.4f, 1.44f, 1.58f, Color::fromRgb(255, 217, 143)},
    {49, "In", "Indium", 114.8f, 1.42f, 1.93f, Color::fromRgb(166, 117, 115)},
    {50, "Sn", "Tin", 118.7f, 1.39f, 2.17f, Color::fromRgb(102, 128, 128)},
    {51, "Sb", "Antimony", 121.8f, 1.39f, 2.06f, Color::fromRgb(158, 99, 181)},
    {52, "Te", "Tellurium", 127.6f, 1.38f, 2.06f, Color::fromRgb(212, 122, 0)},
    {53, "I", "Iodine", 126.9f, 1.39f, 1.98f, Color::fromRgb(148, 0, 148)},
    {54, "Xe", "Xenon", 131.3f, 1.40f, 2.16f, Color::fromRgb(66, 158, 176)},
    // Period 6
    {55, "Cs", "Cesium", 132.9f, 2.44f, 3.43f, Color::fromRgb(87, 23, 143)},
    {56, "Ba", "Barium", 137.3f, 2.15f, 2.68f, Color::fromRgb(0, 201, 0)},
    {57, "La", "Lanthanum", 138.9f, 2.07f, 2.40f, Color::fromRgb(112, 212, 255)},
    {58, "Ce", "Cerium", 140.1f, 2.04f, 2.35f, Color::fromRgb(255, 255, 199)},
    {59, "Pr", "Praseodymium", 140.9f, 2.03f, 2.39f, Color::fromRgb(217, 255, 199)},
    {60, "Nd", "Neodymium", 144.2f, 2.01f, 2.29f, Color::fromRgb(199, 255, 199)},
    {61, "Pm", "Promethium", 145.0f, 1.99f, 2.36f, Color::fromRgb(163, 255, 199)},
    {62, "Sm", "Samarium", 150.4f, 1.98f, 2.29f, Color::fromRgb(143, 255, 199)},
    {63, "Eu", "Europium", 152.0f, 1.98f, 2.33f, Color::fromRgb(97, 255, 199)},
    {64, "Gd", "Gadolinium", 157.3f, 1.96f, 2.37f, Color::fromRgb(69, 255, 199)},
    {65, "Tb", "Terbium", 158.9f, 1.94f, 2.21f, Color::fromRgb(48, 255, 199)},
    {66, "Dy", "Dysprosium", 162.5f, 1.92f, 2.29f, Color::fromRgb(31, 255, 199)},
    {67, "Ho", "Holmium", 164.9f, 1.92f, 2.16f, Color::fromRgb(0, 255, 156)},
    {68, "Er", "Erbium", 167.3f, 1.89f, 2.35f, Color::fromRgb(0, 230, 117)},
    {69, "Tm", "Thulium", 168.9f, 1.90f, 2.27f, Color::fromRgb(0, 212, 82)},
    {70, "Yb", "Ytterbium", 173.0f, 1.87f, 2.42f, Color::fromRgb(0, 191, 56)},
    {71, "Lu", "Lutetium", 175.0f, 1.87f, 2.21f, Color::fromRgb(0, 171, 36)},
    {72, "Hf", "Hafnium", 178.5f, 1.75f, 2.23f, Color::fromRgb(77, 194, 255)},
    {73, "Ta", "Tantalum", 180.9f, 1.70f, 2.22f, Color::fromRgb(77, 166, 255)},
    {74, "W", "Tungsten", 183.8f, 1.62f, 2.18f, Color::fromRgb(33, 148, 214)},
    {75, "Re", "Rhenium", 186.2f, 1.51f, 2.16f, Color::fromRgb(38, 125, 171)},
    {76, "Os", "Osmium", 190.2f, 1.44f, 2.16f, Color::fromRgb(38, 102, 150)},
    {77, "Ir", "Iridium", 192.2f, 1.41f, 2.13f, Color::fromRgb(23, 84, 135)},
    {78, "Pt", "Platinum", 195.1f, 1.36f, 1.75f, Color::fromRgb(208, 208, 224)},
    {79, "Au", "Gold", 197.0f, 1.36f, 1.66f, Color::fromRgb(255, 209, 35)},
    {80, "Hg", "Mercury", 200.6f, 1.32f, 1.55f, Color::fromRgb(184, 184, 208)},
    {81, "Tl", "Thallium", 204.4f, 1.45f, 1.96f, Color::fromRgb(166, 84, 77)},
    {82, "Pb", "Lead", 207.2f, 1.46f, 2.02f, Color::fromRgb(87, 89, 97)},
    {83, "Bi", "Bismuth", 209.0f, 1.48f, 2.07f, Color::fromRgb(158, 79, 181)},
    {84, "Po", "Polonium", 209.0f, 1.40f, 1.97f, Color::fromRgb(171, 92, 0)},
    {85, "At", "Astatine", 210.0f, 1.50f, 2.02f, Color::fromRgb(117, 79, 69)},
    {86, "Rn", "Radon", 222.0f, 1.50f, 2.20f, Color::fromRgb(66, 130, 150)},
    // Period 7
    {87, "Fr", "Francium", 223.0f, 2.60f, 3.48f, Color::fromRgb(66, 0, 102)},
    {88, "Ra", "Radium", 226.0f, 2.21f, 2.83f, Color::fromRgb(0, 125, 0)},
    {89, "Ac", "Actinium", 227.0f, 2.15f, 2.47f, Color::fromRgb(112, 171, 250)},
    {90, "Th", "Thorium", 232.0f, 2.06f, 2.45f, Color::fromRgb(0, 186, 255)},
    {91, "Pa", "Protactinium", 231.0f, 2.00f, 2.43f, Color::fromRgb(0, 161, 255)},
    {92, "U", "Uranium", 238.0f, 1.96f, 1.86f, Color::fromRgb(0, 143, 255)},
    {93, "Np", "Neptunium", 237.0f, 1.90f, 2.21f, Color::fromRgb(0, 128, 255)},
    {94, "Pu", "Plutonium", 244.0f, 1.87f, 2.43f, Color::fromRgb(0, 107, 255)},
    {95, "Am", "Americium", 243.0f, 1.80f, 2.44f, Color::fromRgb(84, 92, 242)},
    {96, "Cm", "Curium", 247.0f, 1.69f, 2.45f, Color::fromRgb(120, 92, 227)},
    // Z >= 97: no covalent radius defined (-1.0f)
    {97,  "Bk", "Berkelium",     247.0f, -1.0f, 2.44f, Color::fromRgb(138, 79, 227)},
    {98,  "Cf", "Californium",   251.0f, -1.0f, 2.45f, Color::fromRgb(161, 54, 212)},
    {99,  "Es", "Einsteinium",   252.0f, -1.0f, 2.45f, Color::fromRgb(179, 31, 212)},
    {100, "Fm", "Fermium",       257.0f, -1.0f, 2.45f, Color::fromRgb(179, 31, 186)},
    {101, "Md", "Mendelevium",   258.0f, -1.0f, 2.46f, Color::fromRgb(179, 13, 166)},
    {102, "No", "Nobelium",      259.0f, -1.0f, 2.46f, Color::fromRgb(189, 13, 135)},
    {103, "Lr", "Lawrencium",    262.0f, -1.0f, 2.46f, Color::fromRgb(199, 0, 102)},
    {104, "Rf", "Rutherfordium", 267.0f, -1.0f, 2.40f, Color::fromRgb(204, 0, 89)},
    {105, "Db", "Dubnium",       268.0f, -1.0f, 2.40f, Color::fromRgb(209, 0, 79)},
    {106, "Sg", "Seaborgium",    269.0f, -1.0f, 2.40f, Color::fromRgb(217, 0, 69)},
    {107, "Bh", "Bohrium",       270.0f, -1.0f, 2.40f, Color::fromRgb(224, 0, 56)},
    {108, "Hs", "Hassium",       269.0f, -1.0f, 2.40f, Color::fromRgb(230, 0, 46)},
    {109, "Mt", "Meitnerium",    278.0f, -1.0f, 2.40f, Color::fromRgb(235, 0, 38)},
    {110, "Ds", "Darmstadtium",  281.0f, -1.0f, 2.40f, Color::fromRgb(240, 0, 33)},
    {111, "Rg", "Roentgenium",   282.0f, -1.0f, 2.40f, Color::fromRgb(245, 0, 29)},
    {112, "Cn", "Copernicium",   285.0f, -1.0f, 2.40f, Color::fromRgb(250, 0, 25)},
    {113, "Nh", "Nihonium",      286.0f, -1.0f, 2.40f, Color::fromRgb(255, 0, 20)},
    {114, "Fl", "Flerovium",     289.0f, -1.0f, 2.40f, Color::fromRgb(255, 0, 15)},
    {115, "Mc", "Moscovium",     290.0f, -1.0f, 2.40f, Color::fromRgb(255, 0, 10)},
    {116, "Lv", "Livermorium",   293.0f, -1.0f, 2.40f, Color::fromRgb(255, 0, 5)},
    {117, "Ts", "Tennessine",    294.0f, -1.0f, 2.40f, Color::fromRgb(255, 0, 0)},
    {118, "Og", "Oganesson",     294.0f, -1.0f, 2.40f, Color::fromRgb(255, 0, 0)},
}};

std::unordered_map<std::string, int> g_symbolMap;
std::once_flag g_symbolMapInitFlag;

void initSymbolMap() {
    for (int i = 0; i < ElementData::MAX_ELEMENTS; ++i) {
        std::string symbol(kElements[i].symbol);
        // Convert to lowercase for case-insensitive lookup
        std::transform(symbol.begin(), symbol.end(), symbol.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        g_symbolMap[symbol] = i;
    }
}

} // namespace

const std::array<ElementInfo, ElementData::MAX_ELEMENTS>& ElementData::elements() {
    return kElements;
}

const std::unordered_map<std::string, int>& ElementData::symbolMap() {
    std::call_once(g_symbolMapInitFlag, initSymbolMap);
    return g_symbolMap;
}

const ElementInfo& ElementData::byAtomicNumber(int atomicNumber) {
    if (atomicNumber < 0 || atomicNumber >= MAX_ELEMENTS) {
        return kElements[0]; // Unknown
    }
    return kElements[atomicNumber];
}

const ElementInfo& ElementData::bySymbol(std::string_view symbol) {
    int num = atomicNumberFromSymbol(symbol);
    return byAtomicNumber(num);
}

int ElementData::atomicNumberFromSymbol(std::string_view symbol) {
    if (symbol.empty()) return 0;

    // Convert to lowercase
    std::string lower(symbol);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    const auto& map = symbolMap();
    auto it = map.find(lower);
    if (it != map.end()) {
        return it->second;
    }
    return 0; // Unknown
}

bool ElementData::isValidSymbol(std::string_view symbol) {
    return atomicNumberFromSymbol(symbol) > 0;
}

Color ElementData::colorForElement(int atomicNumber) {
    return byAtomicNumber(atomicNumber).cpkColor;
}

float ElementData::radiusForElement(int atomicNumber, bool useVdW) {
    const auto& elem = byAtomicNumber(atomicNumber);
    if (useVdW) return elem.vdwRadius;
    // Fall back to vdwRadius for elements without a defined covalent radius
    return (elem.covalentRadius >= 0.0f) ? elem.covalentRadius : elem.vdwRadius;
}

std::optional<float> ElementData::covalentRadius(int atomicNumber) {
    const auto& elem = byAtomicNumber(atomicNumber);
    if (elem.covalentRadius < 0.0f) return std::nullopt;
    return elem.covalentRadius;
}

} // namespace atom::data
