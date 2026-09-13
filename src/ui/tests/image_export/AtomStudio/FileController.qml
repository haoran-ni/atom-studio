pragma Singleton
import QtQml

QtObject {
    property var loadedUrls: []
    function loadFileUrls(urls) { loadedUrls = urls }
    signal saveImagePathSelected(string filePath, string format, bool includeAxes, bool transparentBackground)
}
