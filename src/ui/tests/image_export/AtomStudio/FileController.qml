pragma Singleton
import QtQml

QtObject {
    signal saveImagePathSelected(string filePath, string format, bool includeAxes, bool transparentBackground)
}
