#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace atom::data {

enum class ElementColorScheme {
    Jmol,
    Cpk,
};

/**
 * @brief RGB color representation for element colors
 */
struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}

    // Factory method for creating colors from byte values (0-255)
    static constexpr Color fromRgb(int r, int g, int b, int a = 255) {
        return Color(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    }
};

/**
 * @brief Element data including symbol, name, radii, and colors
 */
struct ElementInfo {
    uint8_t atomicNumber;
    std::string_view symbol;
    std::string_view name;
    float mass;            // atomic mass in amu
    float covalentRadius;  // in Angstroms; -1.0f if not defined for this element
    float vdwRadius;       // Van der Waals radius in Angstroms
    Color jmolColor;       // Jmol color scheme from ASE
};

/**
 * @brief Static element database with ASE-aligned colors and radii
 *
 * Provides fast lookup of element properties by atomic number or symbol.
 * Uses ASE Jmol colors by default, and also exposes ASE CPK colors.
 */
class ElementData {
public:
    static constexpr int MAX_ELEMENTS = 119;

    /**
     * @brief Get element info by atomic number
     * @param atomicNumber Atomic number (1-118)
     * @return Element information, or unknown element for invalid numbers
     */
    static const ElementInfo& byAtomicNumber(int atomicNumber);

    /**
     * @brief Get element info by symbol
     * @param symbol Element symbol (case-insensitive)
     * @return Element information, or unknown element if not found
     */
    static const ElementInfo& bySymbol(std::string_view symbol);

    /**
     * @brief Get atomic number from symbol
     * @param symbol Element symbol (case-insensitive)
     * @return Atomic number, or 0 if not found
     */
    static int atomicNumberFromSymbol(std::string_view symbol);

    /**
     * @brief Check if a symbol is a valid element
     * @param symbol Element symbol to check
     * @return true if valid element symbol
     */
    static bool isValidSymbol(std::string_view symbol);

    /**
     * @brief Get color for an element type/atomic number
     * @param atomicNumber Atomic number
     * @param scheme Color regime to use; defaults to ASE Jmol colors
     * @return Element color for the selected scheme
     */
    static Color colorForElement(int atomicNumber,
                                 ElementColorScheme scheme = ElementColorScheme::Jmol);

    /**
     * @brief Get default radius for an element (always returns a value)
     * @param atomicNumber Atomic number
     * @param useVdW If true, use Van der Waals radius; otherwise covalent (falls back to vdwRadius if undefined)
     * @return Radius in Angstroms
     */
    static float radiusForElement(int atomicNumber, bool useVdW = false);

    /**
     * @brief Get covalent radius for bond detection, or nullopt if element cannot form covalent bonds
     * @param atomicNumber Atomic number
     * @return Covalent radius in Angstroms, or nullopt
     */
    static std::optional<float> covalentRadius(int atomicNumber);

private:
    static const std::array<ElementInfo, MAX_ELEMENTS>& elements();
    static const std::unordered_map<std::string, int>& symbolMap();
    static void buildSymbolMap();
};

} // namespace atom::data
