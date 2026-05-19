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
        py::gil_scoped_acquire gil;
        py::module_::import("sys").attr("path").attr("insert")(
            0, libPath.string());

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
    if (interpreter_) {
        py::gil_scoped_acquire gil;
        interpreter_.reset();
    }
}
