#include "ElementData.h"
#include <algorithm>
#include <cctype>
#include <mutex>
#include <optional>

namespace atom::data {

namespace {

// Element data
// Colors: Jmol values from ASE; separate CPK table below
// Radii: covalent from Cordero et al., VdW from Bondi/Rowland & Taylor
constexpr std::array<ElementInfo, ElementData::MAX_ELEMENTS> kElements = {{
    // Unknown/placeholder (index 0) — no covalent radius
    {0, "X", "Unknown", 0.0f, -1.0f, 1.5f, Color(1.000f, 0.000f, 0.000f)},
    // Period 1 — covalent radii from covalent_radii.md
    {1, "H", "Hydrogen", 1.008f, 0.31f, 1.20f, Color(1.000f, 1.000f, 1.000f)},
    {2, "He", "Helium", 4.003f, -1.0f, 1.40f, Color(0.851f, 1.000f, 1.000f)},
    // Period 2
    {3, "Li", "Lithium", 6.941f, 1.28f, 1.82f, Color(0.800f, 0.502f, 1.000f)},
    {4, "Be", "Beryllium", 9.012f, 0.96f, 1.53f, Color(0.761f, 1.000f, 0.000f)},
    {5, "B", "Boron", 10.81f, 0.85f, 1.92f, Color(1.000f, 0.710f, 0.710f)},
    {6, "C", "Carbon", 12.01f, 0.76f, 1.70f, Color(0.565f, 0.565f, 0.565f)},
    {7, "N", "Nitrogen", 14.01f, 0.71f, 1.55f, Color(0.188f, 0.314f, 0.973f)},
    {8, "O", "Oxygen", 16.00f, 0.66f, 1.52f, Color(1.000f, 0.051f, 0.051f)},
    {9, "F", "Fluorine", 19.00f, 0.57f, 1.47f, Color(0.565f, 0.878f, 0.314f)},
    {10, "Ne", "Neon", 20.18f, -1.0f, 1.54f, Color(0.702f, 0.890f, 0.961f)},
    // Period 3
    {11, "Na", "Sodium", 22.99f, 1.66f, 2.27f, Color(0.671f, 0.361f, 0.949f)},
    {12, "Mg", "Magnesium", 24.31f, 1.41f, 1.73f, Color(0.541f, 1.000f, 0.000f)},
    {13, "Al", "Aluminum", 26.98f, 1.21f, 1.84f, Color(0.749f, 0.651f, 0.651f)},
    {14, "Si", "Silicon", 28.09f, 1.11f, 2.10f, Color(0.941f, 0.784f, 0.627f)},
    {15, "P", "Phosphorus", 30.97f, 1.07f, 1.80f, Color(1.000f, 0.502f, 0.000f)},
    {16, "S", "Sulfur", 32.07f, 1.05f, 1.80f, Color(1.000f, 1.000f, 0.188f)},
    {17, "Cl", "Chlorine", 35.45f, 1.02f, 1.75f, Color(0.122f, 0.941f, 0.122f)},
    {18, "Ar", "Argon", 39.95f, -1.0f, 1.88f, Color(0.502f, 0.820f, 0.890f)},
    // Period 4
    {19, "K", "Potassium", 39.10f, 2.03f, 2.75f, Color(0.561f, 0.251f, 0.831f)},
    {20, "Ca", "Calcium", 40.08f, 1.76f, 2.31f, Color(0.239f, 1.000f, 0.000f)},
    {21, "Sc", "Scandium", 44.96f, 1.70f, 2.11f, Color(0.902f, 0.902f, 0.902f)},
    {22, "Ti", "Titanium", 47.87f, 1.60f, 2.00f, Color(0.749f, 0.761f, 0.780f)},
    {23, "V", "Vanadium", 50.94f, 1.53f, 1.92f, Color(0.651f, 0.651f, 0.671f)},
    {24, "Cr", "Chromium", 52.00f, 1.39f, 1.85f, Color(0.541f, 0.600f, 0.780f)},
    {25, "Mn", "Manganese", 54.94f, 1.39f, 1.79f, Color(0.612f, 0.478f, 0.780f)},
    {26, "Fe", "Iron", 55.85f, 1.32f, 1.83f, Color(0.878f, 0.400f, 0.200f)},
    {27, "Co", "Cobalt", 58.93f, 1.26f, 1.79f, Color(0.941f, 0.565f, 0.627f)},
    {28, "Ni", "Nickel", 58.69f, 1.24f, 1.63f, Color(0.314f, 0.816f, 0.314f)},
    {29, "Cu", "Copper", 63.55f, 1.32f, 1.40f, Color(0.784f, 0.502f, 0.200f)},
    {30, "Zn", "Zinc", 65.38f, 1.22f, 1.39f, Color(0.490f, 0.502f, 0.690f)},
    {31, "Ga", "Gallium", 69.72f, 1.22f, 1.87f, Color(0.761f, 0.561f, 0.561f)},
    {32, "Ge", "Germanium", 72.63f, 1.20f, 2.11f, Color(0.400f, 0.561f, 0.561f)},
    {33, "As", "Arsenic", 74.92f, 1.19f, 1.85f, Color(0.741f, 0.502f, 0.890f)},
    {34, "Se", "Selenium", 78.97f, 1.20f, 1.90f, Color(1.000f, 0.631f, 0.000f)},
    {35, "Br", "Bromine", 79.90f, 1.20f, 1.85f, Color(0.651f, 0.161f, 0.161f)},
    {36, "Kr", "Krypton", 83.80f, -1.0f, 2.02f, Color(0.361f, 0.722f, 0.820f)},
    // Period 5
    {37, "Rb", "Rubidium", 85.47f, 2.20f, 3.03f, Color(0.439f, 0.180f, 0.690f)},
    {38, "Sr", "Strontium", 87.62f, 1.95f, 2.49f, Color(0.000f, 1.000f, 0.000f)},
    {39, "Y", "Yttrium", 88.91f, 1.90f, 2.32f, Color(0.580f, 1.000f, 1.000f)},
    {40, "Zr", "Zirconium", 91.22f, 1.75f, 2.23f, Color(0.580f, 0.878f, 0.878f)},
    {41, "Nb", "Niobium", 92.91f, 1.64f, 2.18f, Color(0.451f, 0.761f, 0.788f)},
    {42, "Mo", "Molybdenum", 95.95f, 1.54f, 2.17f, Color(0.329f, 0.710f, 0.710f)},
    {43, "Tc", "Technetium", 98.00f, 1.47f, 2.16f, Color(0.231f, 0.620f, 0.620f)},
    {44, "Ru", "Ruthenium", 101.1f, 1.46f, 2.13f, Color(0.141f, 0.561f, 0.561f)},
    {45, "Rh", "Rhodium", 102.9f, 1.42f, 2.10f, Color(0.039f, 0.490f, 0.549f)},
    {46, "Pd", "Palladium", 106.4f, 1.39f, 1.63f, Color(0.000f, 0.412f, 0.522f)},
    {47, "Ag", "Silver", 107.9f, 1.45f, 1.72f, Color(0.753f, 0.753f, 0.753f)},
    {48, "Cd", "Cadmium", 112.4f, 1.44f, 1.58f, Color(1.000f, 0.851f, 0.561f)},
    {49, "In", "Indium", 114.8f, 1.42f, 1.93f, Color(0.651f, 0.459f, 0.451f)},
    {50, "Sn", "Tin", 118.7f, 1.39f, 2.17f, Color(0.400f, 0.502f, 0.502f)},
    {51, "Sb", "Antimony", 121.8f, 1.39f, 2.06f, Color(0.620f, 0.388f, 0.710f)},
    {52, "Te", "Tellurium", 127.6f, 1.38f, 2.06f, Color(0.831f, 0.478f, 0.000f)},
    {53, "I", "Iodine", 126.9f, 1.39f, 1.98f, Color(0.580f, 0.000f, 0.580f)},
    {54, "Xe", "Xenon", 131.3f, -1.0f, 2.16f, Color(0.259f, 0.620f, 0.690f)},
    // Period 6
    {55, "Cs", "Cesium", 132.9f, 2.44f, 3.43f, Color(0.341f, 0.090f, 0.561f)},
    {56, "Ba", "Barium", 137.3f, 2.15f, 2.68f, Color(0.000f, 0.788f, 0.000f)},
    {57, "La", "Lanthanum", 138.9f, 2.07f, 2.40f, Color(0.439f, 0.831f, 1.000f)},
    {58, "Ce", "Cerium", 140.1f, 2.04f, 2.35f, Color(1.000f, 1.000f, 0.780f)},
    {59, "Pr", "Praseodymium", 140.9f, 2.03f, 2.39f, Color(0.851f, 1.000f, 0.780f)},
    {60, "Nd", "Neodymium", 144.2f, 2.01f, 2.29f, Color(0.780f, 1.000f, 0.780f)},
    {61, "Pm", "Promethium", 145.0f, 1.99f, 2.36f, Color(0.639f, 1.000f, 0.780f)},
    {62, "Sm", "Samarium", 150.4f, 1.98f, 2.29f, Color(0.561f, 1.000f, 0.780f)},
    {63, "Eu", "Europium", 152.0f, 1.98f, 2.33f, Color(0.380f, 1.000f, 0.780f)},
    {64, "Gd", "Gadolinium", 157.3f, 1.96f, 2.37f, Color(0.271f, 1.000f, 0.780f)},
    {65, "Tb", "Terbium", 158.9f, 1.94f, 2.21f, Color(0.188f, 1.000f, 0.780f)},
    {66, "Dy", "Dysprosium", 162.5f, 1.92f, 2.29f, Color(0.122f, 1.000f, 0.780f)},
    {67, "Ho", "Holmium", 164.9f, 1.92f, 2.16f, Color(0.000f, 1.000f, 0.612f)},
    {68, "Er", "Erbium", 167.3f, 1.89f, 2.35f, Color(0.000f, 0.902f, 0.459f)},
    {69, "Tm", "Thulium", 168.9f, 1.90f, 2.27f, Color(0.000f, 0.831f, 0.322f)},
    {70, "Yb", "Ytterbium", 173.0f, 1.87f, 2.42f, Color(0.000f, 0.749f, 0.220f)},
    {71, "Lu", "Lutetium", 175.0f, 1.87f, 2.21f, Color(0.000f, 0.671f, 0.141f)},
    {72, "Hf", "Hafnium", 178.5f, 1.75f, 2.23f, Color(0.302f, 0.761f, 1.000f)},
    {73, "Ta", "Tantalum", 180.9f, 1.70f, 2.22f, Color(0.302f, 0.651f, 1.000f)},
    {74, "W", "Tungsten", 183.8f, 1.62f, 2.18f, Color(0.129f, 0.580f, 0.839f)},
    {75, "Re", "Rhenium", 186.2f, 1.51f, 2.16f, Color(0.149f, 0.490f, 0.671f)},
    {76, "Os", "Osmium", 190.2f, 1.44f, 2.16f, Color(0.149f, 0.400f, 0.588f)},
    {77, "Ir", "Iridium", 192.2f, 1.41f, 2.13f, Color(0.090f, 0.329f, 0.529f)},
    {78, "Pt", "Platinum", 195.1f, 1.36f, 1.75f, Color(0.816f, 0.816f, 0.878f)},
    {79, "Au", "Gold", 197.0f, 1.36f, 1.66f, Color(1.000f, 0.820f, 0.137f)},
    {80, "Hg", "Mercury", 200.6f, 1.32f, 1.55f, Color(0.722f, 0.722f, 0.816f)},
    {81, "Tl", "Thallium", 204.4f, 1.45f, 1.96f, Color(0.651f, 0.329f, 0.302f)},
    {82, "Pb", "Lead", 207.2f, 1.46f, 2.02f, Color(0.341f, 0.349f, 0.380f)},
    {83, "Bi", "Bismuth", 209.0f, 1.48f, 2.07f, Color(0.620f, 0.310f, 0.710f)},
    {84, "Po", "Polonium", 209.0f, 1.40f, 1.97f, Color(0.671f, 0.361f, 0.000f)},
    {85, "At", "Astatine", 210.0f, 1.50f, 2.02f, Color(0.459f, 0.310f, 0.271f)},
    {86, "Rn", "Radon", 222.0f, -1.0f, 2.20f, Color(0.259f, 0.510f, 0.588f)},
    // Period 7
    {87, "Fr", "Francium", 223.0f, 2.60f, 3.48f, Color(0.259f, 0.000f, 0.400f)},
    {88, "Ra", "Radium", 226.0f, 2.21f, 2.83f, Color(0.000f, 0.490f, 0.000f)},
    {89, "Ac", "Actinium", 227.0f, 2.15f, 2.47f, Color(0.439f, 0.671f, 0.980f)},
    {90, "Th", "Thorium", 232.0f, 2.06f, 2.45f, Color(0.000f, 0.729f, 1.000f)},
    {91, "Pa", "Protactinium", 231.0f, 2.00f, 2.43f, Color(0.000f, 0.631f, 1.000f)},
    {92, "U", "Uranium", 238.0f, 1.96f, 1.86f, Color(0.000f, 0.561f, 1.000f)},
    {93, "Np", "Neptunium", 237.0f, 1.90f, 2.21f, Color(0.000f, 0.502f, 1.000f)},
    {94, "Pu", "Plutonium", 244.0f, 1.87f, 2.43f, Color(0.000f, 0.420f, 1.000f)},
    {95, "Am", "Americium", 243.0f, 1.80f, 2.44f, Color(0.329f, 0.361f, 0.949f)},
    {96, "Cm", "Curium", 247.0f, 1.69f, 2.45f, Color(0.471f, 0.361f, 0.890f)},
    // Z >= 97: no covalent radius defined (-1.0f)
    {97,  "Bk", "Berkelium",     247.0f, -1.0f, 2.44f, Color(0.541f, 0.310f, 0.890f)},
    {98,  "Cf", "Californium",   251.0f, -1.0f, 2.45f, Color(0.631f, 0.212f, 0.831f)},
    {99,  "Es", "Einsteinium",   252.0f, -1.0f, 2.45f, Color(0.702f, 0.122f, 0.831f)},
    {100, "Fm", "Fermium",       257.0f, -1.0f, 2.45f, Color(0.702f, 0.122f, 0.729f)},
    {101, "Md", "Mendelevium",   258.0f, -1.0f, 2.46f, Color(0.702f, 0.051f, 0.651f)},
    {102, "No", "Nobelium",      259.0f, -1.0f, 2.46f, Color(0.741f, 0.051f, 0.529f)},
    {103, "Lr", "Lawrencium",    262.0f, -1.0f, 2.46f, Color(0.780f, 0.000f, 0.400f)},
    {104, "Rf", "Rutherfordium", 267.0f, -1.0f, 2.40f, Color(0.800f, 0.000f, 0.349f)},
    {105, "Db", "Dubnium",       268.0f, -1.0f, 2.40f, Color(0.820f, 0.000f, 0.310f)},
    {106, "Sg", "Seaborgium",    269.0f, -1.0f, 2.40f, Color(0.851f, 0.000f, 0.271f)},
    {107, "Bh", "Bohrium",       270.0f, -1.0f, 2.40f, Color(0.878f, 0.000f, 0.220f)},
    {108, "Hs", "Hassium",       269.0f, -1.0f, 2.40f, Color(0.902f, 0.000f, 0.180f)},
    {109, "Mt", "Meitnerium",    278.0f, -1.0f, 2.40f, Color(0.922f, 0.000f, 0.149f)},
    {110, "Ds", "Darmstadtium",  281.0f, -1.0f, 2.40f, Color(1.000f, 0.000f, 0.000f)},
    {111, "Rg", "Roentgenium",   282.0f, -1.0f, 2.40f, Color(1.000f, 0.000f, 0.000f)},
    {112, "Cn", "Copernicium",   285.0f, -1.0f, 2.40f, Color(1.000f, 0.000f, 0.000f)},
    {113, "Nh", "Nihonium",      286.0f, -1.0f, 2.40f, Color(1.000f, 0.000f, 0.000f)},
    {114, "Fl", "Flerovium",     289.0f, -1.0f, 2.40f, Color(1.000f, 0.000f, 0.000f)},
    {115, "Mc", "Moscovium",     290.0f, -1.0f, 2.40f, Color(1.000f, 0.000f, 0.000f)},
    {116, "Lv", "Livermorium",   293.0f, -1.0f, 2.40f, Color(1.000f, 0.000f, 0.000f)},
    {117, "Ts", "Tennessine",    294.0f, -1.0f, 2.40f, Color(1.000f, 0.000f, 0.000f)},
    {118, "Og", "Oganesson",     294.0f, -1.0f, 2.40f, Color(1.000f, 0.000f, 0.000f)},
}};

// ASE CPK colors. ASE only defines entries up to Z=103; higher Z fall back to the scheme's unknown color.
constexpr std::array<Color, ElementData::MAX_ELEMENTS> kCpkColors = {{
    Color(1.000f, 0.000f, 0.000f), //   0 X
    Color(1.000f, 1.000f, 1.000f), //   1 H
    Color(1.000f, 0.753f, 0.796f), //   2 He
    Color(0.698f, 0.133f, 0.133f), //   3 Li
    Color(1.000f, 0.078f, 0.576f), //   4 Be
    Color(0.000f, 1.000f, 0.000f), //   5 B
    Color(0.784f, 0.784f, 0.784f), //   6 C
    Color(0.561f, 0.561f, 1.000f), //   7 N
    Color(0.941f, 0.000f, 0.000f), //   8 O
    Color(0.855f, 0.647f, 0.125f), //   9 F
    Color(1.000f, 0.078f, 0.576f), //  10 Ne
    Color(0.000f, 0.000f, 1.000f), //  11 Na
    Color(0.133f, 0.545f, 0.133f), //  12 Mg
    Color(0.502f, 0.502f, 0.565f), //  13 Al
    Color(0.855f, 0.647f, 0.125f), //  14 Si
    Color(1.000f, 0.647f, 0.000f), //  15 P
    Color(1.000f, 0.784f, 0.196f), //  16 S
    Color(0.000f, 1.000f, 0.000f), //  17 Cl
    Color(1.000f, 0.078f, 0.576f), //  18 Ar
    Color(1.000f, 0.078f, 0.576f), //  19 K
    Color(0.502f, 0.502f, 0.565f), //  20 Ca
    Color(1.000f, 0.078f, 0.576f), //  21 Sc
    Color(0.502f, 0.502f, 0.565f), //  22 Ti
    Color(1.000f, 0.078f, 0.576f), //  23 V
    Color(0.502f, 0.502f, 0.565f), //  24 Cr
    Color(0.502f, 0.502f, 0.565f), //  25 Mn
    Color(1.000f, 0.647f, 0.000f), //  26 Fe
    Color(1.000f, 0.078f, 0.576f), //  27 Co
    Color(0.647f, 0.165f, 0.165f), //  28 Ni
    Color(0.647f, 0.165f, 0.165f), //  29 Cu
    Color(0.647f, 0.165f, 0.165f), //  30 Zn
    Color(1.000f, 0.078f, 0.576f), //  31 Ga
    Color(1.000f, 0.078f, 0.576f), //  32 Ge
    Color(1.000f, 0.078f, 0.576f), //  33 As
    Color(1.000f, 0.078f, 0.576f), //  34 Se
    Color(0.647f, 0.165f, 0.165f), //  35 Br
    Color(1.000f, 0.078f, 0.576f), //  36 Kr
    Color(1.000f, 0.078f, 0.576f), //  37 Rb
    Color(1.000f, 0.078f, 0.576f), //  38 Sr
    Color(1.000f, 0.078f, 0.576f), //  39 Y
    Color(1.000f, 0.078f, 0.576f), //  40 Zr
    Color(1.000f, 0.078f, 0.576f), //  41 Nb
    Color(1.000f, 0.078f, 0.576f), //  42 Mo
    Color(1.000f, 0.078f, 0.576f), //  43 Tc
    Color(1.000f, 0.078f, 0.576f), //  44 Ru
    Color(1.000f, 0.078f, 0.576f), //  45 Rh
    Color(1.000f, 0.078f, 0.576f), //  46 Pd
    Color(0.502f, 0.502f, 0.565f), //  47 Ag
    Color(1.000f, 0.078f, 0.576f), //  48 Cd
    Color(1.000f, 0.078f, 0.576f), //  49 In
    Color(1.000f, 0.078f, 0.576f), //  50 Sn
    Color(1.000f, 0.078f, 0.576f), //  51 Sb
    Color(1.000f, 0.078f, 0.576f), //  52 Te
    Color(0.627f, 0.125f, 0.941f), //  53 I
    Color(1.000f, 0.078f, 0.576f), //  54 Xe
    Color(1.000f, 0.078f, 0.576f), //  55 Cs
    Color(1.000f, 0.647f, 0.000f), //  56 Ba
    Color(1.000f, 0.078f, 0.576f), //  57 La
    Color(1.000f, 0.078f, 0.576f), //  58 Ce
    Color(1.000f, 0.078f, 0.576f), //  59 Pr
    Color(1.000f, 0.078f, 0.576f), //  60 Nd
    Color(1.000f, 0.078f, 0.576f), //  61 Pm
    Color(1.000f, 0.078f, 0.576f), //  62 Sm
    Color(1.000f, 0.078f, 0.576f), //  63 Eu
    Color(1.000f, 0.078f, 0.576f), //  64 Gd
    Color(1.000f, 0.078f, 0.576f), //  65 Tb
    Color(1.000f, 0.078f, 0.576f), //  66 Dy
    Color(1.000f, 0.078f, 0.576f), //  67 Ho
    Color(1.000f, 0.078f, 0.576f), //  68 Er
    Color(1.000f, 0.078f, 0.576f), //  69 Tm
    Color(1.000f, 0.078f, 0.576f), //  70 Yb
    Color(1.000f, 0.078f, 0.576f), //  71 Lu
    Color(1.000f, 0.078f, 0.576f), //  72 Hf
    Color(1.000f, 0.078f, 0.576f), //  73 Ta
    Color(1.000f, 0.078f, 0.576f), //  74 W
    Color(1.000f, 0.078f, 0.576f), //  75 Re
    Color(1.000f, 0.078f, 0.576f), //  76 Os
    Color(1.000f, 0.078f, 0.576f), //  77 Ir
    Color(1.000f, 0.078f, 0.576f), //  78 Pt
    Color(0.855f, 0.647f, 0.125f), //  79 Au
    Color(1.000f, 0.078f, 0.576f), //  80 Hg
    Color(1.000f, 0.078f, 0.576f), //  81 Tl
    Color(1.000f, 0.078f, 0.576f), //  82 Pb
    Color(1.000f, 0.078f, 0.576f), //  83 Bi
    Color(1.000f, 0.078f, 0.576f), //  84 Po
    Color(1.000f, 0.078f, 0.576f), //  85 At
    Color(1.000f, 1.000f, 1.000f), //  86 Rn
    Color(1.000f, 1.000f, 1.000f), //  87 Fr
    Color(1.000f, 1.000f, 1.000f), //  88 Ra
    Color(1.000f, 1.000f, 1.000f), //  89 Ac
    Color(1.000f, 0.078f, 0.576f), //  90 Th
    Color(1.000f, 1.000f, 1.000f), //  91 Pa
    Color(1.000f, 0.078f, 0.576f), //  92 U
    Color(1.000f, 1.000f, 1.000f), //  93 Np
    Color(1.000f, 1.000f, 1.000f), //  94 Pu
    Color(1.000f, 1.000f, 1.000f), //  95 Am
    Color(1.000f, 1.000f, 1.000f), //  96 Cm
    Color(1.000f, 1.000f, 1.000f), //  97 Bk
    Color(1.000f, 1.000f, 1.000f), //  98 Cf
    Color(1.000f, 1.000f, 1.000f), //  99 Es
    Color(1.000f, 1.000f, 1.000f), // 100 Fm
    Color(1.000f, 1.000f, 1.000f), // 101 Md
    Color(1.000f, 1.000f, 1.000f), // 102 No
    Color(1.000f, 1.000f, 1.000f), // 103 Lr
    Color(1.000f, 0.000f, 0.000f), // 104 Rf
    Color(1.000f, 0.000f, 0.000f), // 105 Db
    Color(1.000f, 0.000f, 0.000f), // 106 Sg
    Color(1.000f, 0.000f, 0.000f), // 107 Bh
    Color(1.000f, 0.000f, 0.000f), // 108 Hs
    Color(1.000f, 0.000f, 0.000f), // 109 Mt
    Color(1.000f, 0.000f, 0.000f), // 110 Ds
    Color(1.000f, 0.000f, 0.000f), // 111 Rg
    Color(1.000f, 0.000f, 0.000f), // 112 Cn
    Color(1.000f, 0.000f, 0.000f), // 113 Nh
    Color(1.000f, 0.000f, 0.000f), // 114 Fl
    Color(1.000f, 0.000f, 0.000f), // 115 Mc
    Color(1.000f, 0.000f, 0.000f), // 116 Lv
    Color(1.000f, 0.000f, 0.000f), // 117 Ts
    Color(1.000f, 0.000f, 0.000f), // 118 Og
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

Color ElementData::colorForElement(int atomicNumber, ElementColorScheme scheme) {
    const auto& elem = byAtomicNumber(atomicNumber);
    if (scheme == ElementColorScheme::Cpk) {
        return kCpkColors[elem.atomicNumber];
    }
    return elem.jmolColor;
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
