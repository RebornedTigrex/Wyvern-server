include_guard()

function(BootstrapPythonRuntime)
    # cmake/BootstrapPythonRuntime.cmake
    include(FetchContent)

    # === Пин версии (обновляй когда нужно) ===
    set(PBS_RELEASE "20260510")                    # ← актуально на май 2026
    set(PBS_PYTHON_VERSION "3.14.5")               # или 3.13.x

    # Определяем нужный архив под платформу
    if(WYVERN_SYSTEM_FAMILY_WINDOWS)
        set(PBS_PLATFORM "x86_64-pc-windows-msvc")
        set(PBS_EXT "zip")
    elseif(WYVERN_SYSTEM_FAMILY_UNIX AND WYVERN_ARCHITECTURE_X86_64)
        set(PBS_PLATFORM "x86_64-unknown-linux-gnu")
        set(PBS_EXT "tar.gz")
    elseif(WYVERN_SYSTEM_FAMILY_UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(PBS_PLATFORM "aarch64-unknown-linux-gnu")
        set(PBS_EXT "tar.gz")
    else()
        message(FATAL_ERROR "Unsupported platform for python-build-standalone")
    endif()

    set(PBS_FILENAME "cpython-${PBS_PYTHON_VERSION}+${PBS_RELEASE}-${PBS_PLATFORM}-install_only_stripped.${PBS_EXT}")
    set(PBS_URL "https://github.com/astral-sh/python-build-standalone/releases/download/${PBS_RELEASE}/${PBS_FILENAME}")

    set(PYTHON_RUNTIME_DIR "${CMAKE_SOURCE_DIR}/EXTERNALS/python-standalone/${PBS_PLATFORM}")

    # Скачиваем и распаковываем один раз
    if(NOT EXISTS "${PYTHON_RUNTIME_DIR}/python")
        message(STATUS "Downloading portable Python runtime (${PBS_PLATFORM})...")
        FetchContent_Declare(python_standalone
            URL ${PBS_URL}
            SOURCE_DIR ${PYTHON_RUNTIME_DIR}
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(python_standalone)
        message(STATUS "Python runtime ready at: ${PYTHON_RUNTIME_DIR}")
    endif()

    # Передаём путь дальше
    set(PYTHON_RUNTIME_PATH "${PYTHON_RUNTIME_DIR}" CACHE PATH "Path to portable Python runtime" FORCE)
endfunction()