.pragma library

function clamp(value, minimum, maximum) {
    return Math.max(minimum, Math.min(maximum, value))
}

function hex(color) {
    return [color.r, color.g, color.b].map(function(channel) {
        return Math.round(channel * 255).toString(16).padStart(2, "0")
    }).join("").toUpperCase()
}

function fromHex(text, alpha) {
    const value = text.trim().replace(/^#/, "")
    if (!/^[0-9a-fA-F]{6}$/.test(value))
        return null
    return Qt.rgba(parseInt(value.slice(0, 2), 16) / 255,
                   parseInt(value.slice(2, 4), 16) / 255,
                   parseInt(value.slice(4, 6), 16) / 255, alpha)
}

function sameRgb(first, second) {
    return hex(first) === hex(second)
}

function gridColor(index) {
    const column = index % 12
    const row = Math.floor(index / 12)
    if (row === 0) {
        const gray = 1 - column / 11
        return Qt.rgba(gray, gray, gray, 1)
    }
    // Cool hues at the left, warm hues in the middle, greens at the right.
    const hues = [0.53, 0.60, 0.69, 0.78, 0.91, 0.0, 0.055, 0.095, 0.135, 0.17, 0.22, 0.28]
    const lightness = [0, 0.13, 0.23, 0.33, 0.43, 0.53, 0.65, 0.76, 0.86, 0.93]
    return Qt.hsla(hues[column], 0.85, lightness[row], 1)
}
