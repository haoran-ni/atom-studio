#pragma once

#include <QOpenGLShaderProgram>
#include <memory>
#include <string>
#include <unordered_map>

namespace atom::render {

/**
 * @brief Manages OpenGL shader programs
 *
 * Handles shader compilation, linking, and caching.
 */
class ShaderManager {
public:
    ShaderManager();
    ~ShaderManager();

    /**
     * @brief Initialize shaders
     * @return true if all shaders compiled successfully
     */
    bool initialize();

    /**
     * @brief Clean up all shaders
     */
    void cleanup();

    /**
     * @brief Get the sphere rendering shader
     */
    QOpenGLShaderProgram* sphereShader() { return m_sphereShader.get(); }

    /**
     * @brief Get the bond/cylinder rendering shader
     */
    QOpenGLShaderProgram* bondShader() { return m_bondShader.get(); }

    /**
     * @brief Get the unit cell line shader
     */
    QOpenGLShaderProgram* lineShader() { return m_lineShader.get(); }

    /**
     * @brief Check if shaders are initialized
     */
    bool isInitialized() const { return m_initialized; }

private:
    bool compileShader(QOpenGLShaderProgram* program,
                       const QString& vertexSource,
                       const QString& fragmentSource);

    std::unique_ptr<QOpenGLShaderProgram> m_sphereShader;
    std::unique_ptr<QOpenGLShaderProgram> m_bondShader;
    std::unique_ptr<QOpenGLShaderProgram> m_lineShader;
    bool m_initialized = false;
};

// Embedded shader sources
namespace shaders {
    extern const char* sphereVertexShader;
    extern const char* sphereFragmentShader;
    extern const char* bondVertexShader;
    extern const char* bondFragmentShader;
    extern const char* lineVertexShader;
    extern const char* lineFragmentShader;
}

} // namespace atom::render
