#include "PythonRuntimeModule.h"

// pybind11/embed.h must be included before any system headers
// that might indirectly pull in Python.h.
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>

#include <filesystem>
#include <iostream>

#ifdef _WIN32
  #include <windows.h>
  #include <process.h>
#else
  #include <cstdlib>
#endif

namespace py = pybind11;

PythonRuntimeModule::PythonRuntimeModule(const core::runtime::ConfigSection& cfg)
    : BaseModule("Python Runtime"),
      extLibPath_(cfg.value<std::string>("extLibPath", "python"))
{}

PythonRuntimeModule::~PythonRuntimeModule() {
    if (interpreter_) {
        interpreter_.reset();
    }
}

bool PythonRuntimeModule::onInitialize() {
    try {
        // 1. Resolve the bundled Python runtime path.
        std::filesystem::path libPath(extLibPath_);
        if (libPath.is_relative()) {
            libPath = std::filesystem::current_path() / libPath;
        }

        if (!std::filesystem::exists(libPath)) {
            std::cerr << "[PythonRuntime] bundle not found: "
                      << libPath.string() << "\n";
            return false;
        }

        // 2. Configure environment for embedded Python BEFORE Py_Initialize.
#ifdef _WIN32
        const std::string libStr = libPath.string();

        _putenv_s("PYTHONHOME",             libStr.c_str());
        _putenv_s("PYTHONDONTWRITEBYTECODE", "1");
        _putenv_s("PYTHONNOUSERSITE",        "1");

        // Help the DLL loader find python312.dll from the bundle.
        SetDllDirectoryA(libStr.c_str());

        // Prepend the bundle to PATH so dependent DLLs (libcrypto, etc.) are found.
        if (const char* oldPath = std::getenv("PATH")) {
            std::string newPath = libStr + ";" + oldPath;
            _putenv_s("PATH", newPath.c_str());
        }
#else
        const std::string libStr = libPath.string();
        setenv("PYTHONHOME",             libStr.c_str(), 1);
        setenv("PYTHONDONTWRITEBYTECODE", "1",            1);
        setenv("PYTHONNOUSERSITE",        "1",            1);
#endif

        // 3. Create the interpreter.
        interpreter_ = std::make_unique<py::scoped_interpreter>();

        py::gil_scoped_acquire gil;
        auto sys = py::module_::import("sys");

        // 4. Configure sys.path: bundle root + site-packages.
        sys.attr("path").attr("insert")(0, libStr);

        std::filesystem::path sitePackages = libPath / "site-packages";
        if (std::filesystem::exists(sitePackages)) {
            sys.attr("path").attr("insert")(0, sitePackages.string());
        }

        // 5. Verify importability of core modules.
        try {
            py::module_::import("mesh_crypto");
            py::module_::import("mesh_node_db");
        } catch (const py::error_already_set& e) {
            std::cerr << "[PythonRuntime] failed to import mesh_crypto / mesh_node_db: "
                      << e.what() << "\n";
            interpreter_.reset();
            return false;
        }

        std::cout << "[PythonRuntime] initialized (bundle: "
                  << libStr << ")\n";
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[PythonRuntime] init error: " << e.what() << "\n";
        interpreter_.reset();
        return false;
    }
}

void PythonRuntimeModule::onShutdown() {
    // interpreter_ is destroyed here; all Python objects in other modules
    // must already be released before this point (guaranteed by dependency order).
    interpreter_.reset();
}
