#include "PythonRuntimeModule.h"

// pybind11/embed.h must be included before any system headers
// that might indirectly pull in Python.h.
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>

#include <filesystem>
#include <iostream>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace py = pybind11;

namespace {

// Returns the directory containing the running executable.
std::filesystem::path executableDir() {
#ifdef _WIN32
    wchar_t buf[32768] = {};
    GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return std::filesystem::path(buf).parent_path();
#else
    // Linux: /proc/self/exe; fallback to CWD
    std::error_code ec;
    auto p = std::filesystem::canonical("/proc/self/exe", ec);
    return ec ? std::filesystem::current_path() : p.parent_path();
#endif
}

// Resolves `relativePath` by walking up from `start`, returning the first
// ancestor directory where `ancestor / relativePath` exists.
// Returns empty path if not found within `maxLevels` steps.
std::filesystem::path findByWalkingUp(const std::filesystem::path& start,
                                       const std::filesystem::path& relativePath,
                                       int maxLevels = 8) {
    auto dir = start;
    for (int i = 0; i <= maxLevels; ++i) {
        auto candidate = dir / relativePath;
        if (std::filesystem::exists(candidate))
            return candidate;
        auto parent = dir.parent_path();
        if (parent == dir) break;  // reached filesystem root
        dir = parent;
    }
    return {};
}

} // namespace

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
    } catch (const std::exception& e) {
        std::cerr << "[PythonRuntime] failed to start Python interpreter: " << e.what() << "\n";
        return false;
    }

    // --- All Python work inside its own scope so the GIL is released ---
    // --- before we ever call interpreter_.reset() on failure.         ---
    std::string resolvedLibPath;
    bool initOk = [&]() -> bool {
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

        // Resolve the library path.
        // Priority:
        //  1. If absolute and exists — use as-is.
        //  2. Relative to CWD.
        //  3. Walk up from the executable's directory (handles running from
        //     out/build/<preset>/ without configuring an absolute path).
        std::filesystem::path libPath(extLibPath_);
        if (libPath.is_absolute()) {
            if (!std::filesystem::exists(libPath)) {
                std::cerr << "[PythonRuntime] absolute library path does not exist: "
                          << libPath.string() << "\n";
                return false;
            }
        } else {
            auto candidate = std::filesystem::current_path() / libPath;
            if (std::filesystem::exists(candidate)) {
                libPath = candidate;
            } else {
                auto found = findByWalkingUp(executableDir(), libPath);
                if (found.empty()) {
                    std::cerr << "[PythonRuntime] library path not found relative to CWD "
                              << "or executable directory: " << extLibPath_ << "\n";
                    return false;
                }
                libPath = found;
            }
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
            return false;
        }

        resolvedLibPath = libPath.string();
        return true;
    }(); // GIL released here when lambda returns

    if (!initOk) {
        // GIL is already released; safe to finalize the interpreter.
        interpreter_.reset();
        return false;
    }

    std::cout << "[PythonRuntime] initialized (lib: " << resolvedLibPath << ")\n";
    return true;
}

void PythonRuntimeModule::onShutdown() {
    // interpreter_ is destroyed here; all Python objects in other modules
    // must already be released before this point (guaranteed by dependency order).
    // Do NOT acquire the GIL here: scoped_interpreter destructor handles it
    // internally, and re-acquiring after Py_Finalize is undefined behaviour.
    interpreter_.reset();
}
