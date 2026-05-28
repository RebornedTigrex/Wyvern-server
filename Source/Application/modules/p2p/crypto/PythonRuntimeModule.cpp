#include "PythonRuntimeModule.h"

// pybind11/embed.h must be included before any system headers
// that might indirectly pull in Python.h.
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>

#include <filesystem>
#include <iostream>

namespace py = pybind11;

PythonRuntimeModule::PythonRuntimeModule(const core::runtime::ConfigSection& cfg)
    : BaseModule("Python Runtime"),
      extLibPath_(cfg.value<std::string>("extLibPath", "EXTERNALS/kyrcach2_help_tools"))
{}

PythonRuntimeModule::~PythonRuntimeModule() {
    // Ensure the interpreter is destroyed here rather than at static
    // destruction time to avoid ordering issues.
    if (interpreter_) {
        interpreter_.reset();
    }
}

bool PythonRuntimeModule::onInitialize() {
    try {
        interpreter_ = std::make_unique<py::scoped_interpreter>();

        py::gil_scoped_acquire gil;
        auto sys = py::module_::import("sys");

        // Ensure user site-packages (where pip installs by default on Windows)
        // are reachable. The embedded Python may not add them automatically
        // when PYTHONHOME is not set.
        try {
            auto userSite = py::module_::import("site")
                                .attr("getusersitepackages")();
            sys.attr("path").attr("insert")(0, userSite);
        } catch (...) {} // non-critical

        // Resolve the library path: if relative, anchor it to CWD.
        std::filesystem::path libPath(extLibPath_);
        if (libPath.is_relative()) {
            libPath = std::filesystem::current_path() / libPath;
        }

        if (!std::filesystem::exists(libPath)) {
            std::cerr << "[PythonRuntime] library path does not exist: "
                      << libPath.string() << "\n";
            interpreter_.reset();
            return false;
        }

        // Add the library directory to sys.path.
        sys.attr("path").attr("insert")(0, libPath.string());

        // Verify importability.
        try {
            py::module_::import("mesh_crypto");
            py::module_::import("mesh_node_db");
        } catch (const py::error_already_set& e) {
            std::cerr << "[PythonRuntime] failed to import mesh_crypto / mesh_node_db: "
                      << e.what() << "\n";
            interpreter_.reset();
            return false;
        }

        std::cout << "[PythonRuntime] initialized (lib: "
                  << libPath.string() << ")\n";
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
    // Do NOT acquire the GIL here: scoped_interpreter destructor handles it
    // internally, and re-acquiring after Py_Finalize is undefined behaviour.
    interpreter_.reset();
}
