include_guard()

# BootstrapPythonSDK
# Downloads python-build-standalone (install_only) to provide:
#   - include/Python.h  (build-time headers)
#   - libs/python312.lib (import library for linking)
#
# The SDK is cached in ${CMAKE_BINARY_DIR}/_deps/python_sdk/ and
# downloaded only once per build tree.
#
# After calling BootstrapPythonSDK(), the variable PYTHON_SDK_ROOT
# is set as a CACHE PATH pointing to the extracted python/ directory.

function(BootstrapPythonSDK)
    include(FetchContent)

    # === Pin version: must match EXTERNALS/Python ABI (3.12.x) ===
    set(PBS_RELEASE      "20250409")
    set(PBS_PYTHON_VER   "3.12.10")

    # === Platform selection ===
    if(WYVERN_SYSTEM_FAMILY_WINDOWS)
        set(PBS_PLATFORM "x86_64-pc-windows-msvc")
        set(PBS_EXT      "tar.gz")
    elseif(WYVERN_SYSTEM_FAMILY_UNIX AND WYVERN_ARCHITECTURE_X86_64)
        set(PBS_PLATFORM "x86_64-unknown-linux-gnu")
        set(PBS_EXT      "tar.gz")
    elseif(WYVERN_SYSTEM_FAMILY_UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(PBS_PLATFORM "aarch64-unknown-linux-gnu")
        set(PBS_EXT      "tar.gz")
    else()
        message(FATAL_ERROR
            "[BootstrapPythonSDK] Unsupported platform. "
            "Add your triple to cmake/BootstrapPythonSDK.cmake")
    endif()

    set(PBS_FILENAME "cpython-${PBS_PYTHON_VER}+${PBS_RELEASE}-${PBS_PLATFORM}-install_only.${PBS_EXT}")
    set(PBS_URL      "https://github.com/astral-sh/python-build-standalone/releases/download/${PBS_RELEASE}/${PBS_FILENAME}")

    # Cache outside build dir so -Clean doesn't trigger a re-download.
    set(_sdk_dir "${CMAKE_SOURCE_DIR}/../EXTERNALS/python_sdk/${PBS_PLATFORM}")

    # The archive extracts flat (include/, libs/, python312.dll, etc.).
    if(WYVERN_SYSTEM_FAMILY_WINDOWS)
        set(_marker "${_sdk_dir}/include/Python.h")
    else()
        set(_marker "${_sdk_dir}/include/python3.12/Python.h")
    endif()

    if(NOT EXISTS "${_marker}")
        message(STATUS "[BootstrapPythonSDK] Downloading Python ${PBS_PYTHON_VER} SDK (${PBS_PLATFORM})...")
        FetchContent_Declare(python_sdk
            URL                  "${PBS_URL}"
            SOURCE_DIR           "${_sdk_dir}"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(python_sdk)
    else()
        message(STATUS "[BootstrapPythonSDK] Python SDK already present at ${_sdk_dir}")
    endif()

    set(PYTHON_SDK_ROOT "${_sdk_dir}" CACHE PATH "Python build SDK root" FORCE)
    message(STATUS "[BootstrapPythonSDK] PYTHON_SDK_ROOT = ${PYTHON_SDK_ROOT}")
endfunction()
