include_guard()

# BootstrapPythonRuntime
# Validates that the Python runtime bundle exists at EXTERNALS/Python.
# The bundle contains python312.dll, python312.zip, site-packages/
# (mesh_crypto, mesh_node_db, cryptography, etc.) and is used at runtime.
#
# Currently the bundle is placed manually (or will be downloaded from the
# submodule's GitHub Releases as a RAR archive in the future).
#
# After calling BootstrapPythonRuntime(), the variable PYTHON_RUNTIME_DIR
# is set as a CACHE PATH pointing to the runtime directory.
#
# Set WYVERN_PYTHON_RUNTIME_URL to enable automatic download
# (not yet implemented - placeholder for submodule Releases integration).

function(BootstrapPythonRuntime)
    # Configurable URL for future auto-download from submodule releases.
    set(WYVERN_PYTHON_RUNTIME_URL "" CACHE STRING
        "URL to Python runtime bundle (RAR/ZIP). Leave empty to use local EXTERNALS/Python.")

    set(_runtime_dir "${CMAKE_SOURCE_DIR}/../EXTERNALS/Python")

    if(WYVERN_SYSTEM_FAMILY_WINDOWS)
        set(_marker "${_runtime_dir}/python312.dll")
    else()
        set(_marker "${_runtime_dir}/bin/python3")
    endif()

    if(EXISTS "${_marker}")
        message(STATUS "[BootstrapPythonRuntime] Runtime found at ${_runtime_dir}")
    elseif(WYVERN_PYTHON_RUNTIME_URL STREQUAL "")
        message(WARNING
            "[BootstrapPythonRuntime] Runtime not found at ${_runtime_dir} "
            "and WYVERN_PYTHON_RUNTIME_URL is not set.\n"
            "  Place the Python runtime bundle into EXTERNALS/Python/ or set the URL.\n"
            "  The p2p_messenger will build but will not run without the runtime.")
    else()
        # Future: download and extract from WYVERN_PYTHON_RUNTIME_URL
        message(STATUS "[BootstrapPythonRuntime] Downloading runtime from ${WYVERN_PYTHON_RUNTIME_URL}...")
        include(FetchContent)
        FetchContent_Declare(python_runtime
            URL                  "${WYVERN_PYTHON_RUNTIME_URL}"
            SOURCE_DIR           "${_runtime_dir}"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(python_runtime)
    endif()

    set(PYTHON_RUNTIME_DIR "${_runtime_dir}" CACHE PATH "Python runtime bundle directory" FORCE)
endfunction()
