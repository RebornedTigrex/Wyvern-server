#pragma once

#include "modules/BaseModule.h"
#include "runtime/ConfigSection.h"

#include <boost/json.hpp>
#include <memory>
#include <string>

// Forward-declare pybind11 types to avoid polluting all headers
// that include this one.
namespace pybind11 { class scoped_interpreter; }

// PythonRuntimeModule — owns the embedded CPython interpreter.
//
// Responsibilities (only these):
//  - Create and destroy pybind11::scoped_interpreter (Py_Initialize / Py_Finalize).
//  - Add the configured external library path to sys.path so that
//    mesh_crypto and mesh_node_db are importable.
//
// Must be shut down LAST (after all modules that use Python objects).
// The dependency ordering in ModuleRegistry guarantees this because all
// Python-using modules declare "p2p.python_runtime" as a dependency.
class PythonRuntimeModule : public BaseModule {
public:
    static std::string moduleType() { return "p2p.python_runtime"; }

#if !defined(PYTHON_HOME_PATH)
    static boost::json::object defaults() {
        boost::json::object obj;
        // Relative path from CWD to the external Python library root.
        // Override in config to use an absolute path.
        obj["extLibPath"] = "python";
        return obj;
    }

    explicit PythonRuntimeModule(const core::runtime::ConfigSection& cfg);
#else
    explicit PythonRuntimeModule();
#endif

    ~PythonRuntimeModule() override;

    std::string moduleKey() const override { return moduleType(); }

protected:
    bool onInitialize() override;
    void onShutdown() override;

private:
    std::string extLibPath_;
    std::unique_ptr<pybind11::scoped_interpreter> interpreter_;
};
