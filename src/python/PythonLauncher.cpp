// A real, Qt-free Python executable for venv, pip and Python subprocesses.
// File readers continue to use the separate isolated embedded interpreter.
#include <Python.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
int main(int argc, char** argv) {
    try {
        fs::path physical;
#ifdef __APPLE__
        char buffer[4096]; uint32_t size = sizeof(buffer);
        if (_NSGetExecutablePath(buffer, &size)) return 1;
        physical = fs::canonical(buffer);
#elif defined(_WIN32)
        wchar_t buffer[32768];
        if (!GetModuleFileNameW(nullptr, buffer, 32768)) return 1;
        physical = fs::canonical(buffer);
#else
        physical = fs::canonical("/proc/self/exe");
#endif
        auto home = physical.parent_path().parent_path();
#ifdef _WIN32
        // Windows venvs copy executables rather than symlinking them.
        std::ifstream configuration(home / "pyvenv.cfg");
        for (std::string line; std::getline(configuration, line);) {
            if (line.rfind("home = ", 0) == 0) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                home = fs::path(line.substr(7)).parent_path();
                break;
            }
        }
#endif
        const auto certificates = home / "lib" / ("python" + std::to_string(PY_MAJOR_VERSION) + "." + std::to_string(PY_MINOR_VERSION))
            / "site-packages/pip/_vendor/certifi/cacert.pem";
        // OpenSSL's build-machine default CA path need not exist on the user's
        // machine. Supply bundled roots for urllib/model downloads, retaining
        // any explicit certificate configuration supplied by the user.
        if (!std::getenv("SSL_CERT_FILE") && fs::exists(certificates)) {
#ifdef _WIN32
            _putenv_s("SSL_CERT_FILE", certificates.string().c_str());
#else
            setenv("SSL_CERT_FILE", certificates.string().c_str(), 0);
#endif
        }
        PyConfig config;
        PyConfig_InitPythonConfig(&config);
        // Normal CLI behavior is needed by pip's isolated build subprocesses,
        // which use their own temporary PYTHONPATH. GUI workers pass -I; the
        // explicit home below always anchors the standard library to this app.
        config.use_environment = 1;
        config.user_site_directory = 0;
        config.write_bytecode = 0;
        const auto check = [&config](PyStatus status) {
            if (PyStatus_Exception(status)) {
                PyConfig_Clear(&config);
                Py_ExitStatusException(status);
            }
        };
        check(PyConfig_SetBytesString(&config, &config.home, home.string().c_str()));
#ifdef _WIN32
        const auto baseExecutable = home / "bin/python.exe";
#else
        const auto baseExecutable = home / "bin/python3";
#endif
        check(PyConfig_SetBytesString(&config, &config.base_executable, baseExecutable.string().c_str()));
        check(PyConfig_SetBytesArgv(&config, argc, argv));
        check(Py_InitializeFromConfig(&config));
        PyConfig_Clear(&config);
        return Py_RunMain();
    } catch (const std::exception& error) {
        std::cerr << "Atom Studio Python: " << error.what() << '\n';
        return 1;
    }
}
