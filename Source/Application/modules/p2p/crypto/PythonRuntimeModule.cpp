// === MUST BE FIRST ===
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>

#include <Python.h>          // нужен для PyConfig
#include <filesystem>
#include <iostream>

#include "PythonRuntimeModule.h"

#ifdef _WIN32
  #include <windows.h>
#endif

namespace py = pybind11;

#if defined(PYTHON_HOME_PATH)
PythonRuntimeModule::PythonRuntimeModule()
    : BaseModule("Python Runtime"),
        extLibPath_(PYTHON_HOME_PATH){}
#else

PythonRuntimeModule::PythonRuntimeModule(const core::runtime::ConfigSection& cfg)
    : BaseModule("Python Runtime"),
        extLibPath_(cfg.value<std::string>("extLibPath", "python")){}
#endif

PythonRuntimeModule::~PythonRuntimeModule() {
    interpreter_.reset();
}

bool PythonRuntimeModule::onInitialize() {
    try {
        // === 1. Определяем директорию exe ===
        std::filesystem::path baseDir;
#ifdef _WIN32
        wchar_t exePathW[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
        baseDir = std::filesystem::path(exePathW).parent_path();
    

#else
        baseDir = std::filesystem::current_path();
#endif

        std::filesystem::path libPath(extLibPath_);
        if (libPath.is_relative()) {
            libPath = baseDir / libPath;
        }

        if (!std::filesystem::exists(libPath)) {
            std::cerr << "[PythonRuntime] bundle not found: " << libPath.string() << "\n";
            return false;
        }

        const std::string libStr = libPath.string();
        std::filesystem::path sitePkgs  = libPath / "site-packages";
        std::filesystem::path zipFile   = libPath / "python312.zip";
        std::filesystem::path pythonDll = libPath / "python312.dll";

        // === 2. Собираем PYTHONPATH вручную ===
        std::string pythonPath;
        if (std::filesystem::exists(sitePkgs))   pythonPath += sitePkgs.string() + ";";
        if (std::filesystem::exists(zipFile))    pythonPath += zipFile.string() + ";";
        if (std::filesystem::exists(pythonDll))  pythonPath += pythonDll.string() + ";";
        pythonPath += libStr;

#ifdef _WIN32
        // === 3. Настраиваем Python ДО инициализации ===
        _wputenv_s(L"PYTHONHOME", std::wstring(libStr.begin(), libStr.end()).c_str());
        _wputenv_s(L"PYTHONPATH", std::wstring(pythonPath.begin(), pythonPath.end()).c_str());
        _wputenv_s(L"PYTHONDONTWRITEBYTECODE", L"1");
        _wputenv_s(L"PYTHONNOUSERSITE", L"1");

        SetDllDirectoryA(libStr.c_str());
#endif

        // === 4. Явно задаём home и path через C API (самое важное) ===
        Py_SetPythonHome(Py_DecodeLocale(libStr.c_str(), nullptr));
        Py_SetPath(Py_DecodeLocale(pythonPath.c_str(), nullptr));

        // === 5. Создаём интерпретатор ===
        interpreter_ = std::make_unique<py::scoped_interpreter>();

        py::gil_scoped_acquire gil;
        auto sys = py::module_::import("sys");

        // // На всякий случай чистим ещё раз
        // sys.attr("path").attr("clear");
        // if (std::filesystem::exists(sitePkgs)) sys.attr("path").attr("append")(sitePkgs.string());
        // if (std::filesystem::exists(zipFile))  sys.attr("path").attr("append")(zipFile.string());
        // sys.attr("path").attr("append")(libStr);

        // sys.attr("prefix") = libStr;
        // sys.attr("exec_prefix") = libStr;

        // Проверка
        py::module_::import("mesh_crypto");
        py::module_::import("mesh_node_db");

        std::cout << "[PythonRuntime] initialized successfully\n";
        return true;

    } catch (const std::exception& e) {
        std::cerr << "[PythonRuntime] init error: " << e.what() << "\n";
        interpreter_.reset();
        return false;
    }
}

void PythonRuntimeModule::onShutdown() {
    interpreter_.reset();
}