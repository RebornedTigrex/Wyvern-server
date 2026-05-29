include_guard()
function(CompilerFlags)
# Set C/C++ compiler flags based on build environment...

  if(WYVERN_COMPILER_GNU)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -rdynamic -Wall -Wextra -Wno-unused -Wno-implicit-fallthrough -no-pie")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++21 -rdynamic -Wall -Wextra -Wno-unused -Wno-implicit-fallthrough -no-pie")

    if(WYVERN_SYSTEM_FAMILY_WINDOWS)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mthreads")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mthreads")
    else()
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread -D_REENTRANT")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -D_REENTRANT")
    endif()

    if(WYVERN_ENABLE_STATIC_LIBGCC_LIBSTDCXX)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static-libgcc")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
    endif()

    if(WYVERN_ENABLE_GCC_PROFILING)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pg")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg")
    endif()

    set(CMAKE_C_FLAGS_DEBUG "-g -Og")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -Og")

    set(CMAKE_C_FLAGS_RELWITHASSERTS "-g -O3 -ffast-math")
    set(CMAKE_CXX_FLAGS_RELWITHASSERTS "-g -O3 -ffast-math")

    set(CMAKE_C_FLAGS_RELWITHDEBINFO "-g -DNDEBUG -O3 -ffast-math")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-g -DNDEBUG -O3 -ffast-math")

    set(CMAKE_C_FLAGS_RELEASE "-DNDEBUG -O3 -ffast-math")
    set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O3 -ffast-math")

    set(CMAKE_SKIP_BUILD_RPATH TRUE)

  elseif(WYVERN_COMPILER_CLANG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wuninitialized -Wno-parentheses-equality -Wno-deprecated-declarations")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++21 -Wall -Wextra -Wuninitialized -Wno-parentheses-equality -Wno-deprecated-declarations")

    if(WYVERN_SYSTEM_MACOS)
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
      set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-export_dynamic")
      set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-export_dynamic")
      set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++21")
      set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++")
    else()
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread -D_REENTRANT")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -D_REENTRANT")
      set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--export-dynamic")
      set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--export-dynamic")
    endif()

    set(CMAKE_C_FLAGS_DEBUG "-g")
    set(CMAKE_CXX_FLAGS_DEBUG "-g")

    set(CMAKE_C_FLAGS_RELWITHASSERTS "-g -O3 -ffast-math")
    set(CMAKE_CXX_FLAGS_RELWITHASSERTS "-g -O3 -ffast-math")

    set(CMAKE_C_FLAGS_RELWITHDEBINFO "-gline-tables-only -gz=zlib -DNDEBUG -O3 -ffast-math")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-gline-tables-only -gz=zlib -DNDEBUG -O3 -ffast-math")

    set(CMAKE_C_FLAGS_RELEASE "-DNDEBUG -O3 -ffast-math")
    set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O3 -ffast-math")

    set(CMAKE_SKIP_BUILD_RPATH TRUE)

  elseif(WYVERN_COMPILER_MSVC)
    # /MP      - Multi-processor building
    # /EHsc    - Enable normal C++ exception handling
    # /bigobj  - More sections in .obj files (Cannot build in Debug without it)
    # /MT      - Use multi-threaded statically linked C runtime library
    # /GA      - Optimize for windows application
    # /Ox      - Full optimization
    # /fp:fast - Equivalent to -ffast-math
    # /GS-     - Disable buffers security check
    # /Zi      - Generates debugging information without Edit and Continue
    # /Z7      - Like the above, but debugging information is stored per-object
    # /Gy      - Use function-level linking
    # /wd4996  - Disable warnings about unsafe C functions
    # /wd4351  - Disable warnings about new behavior of default initialization of
    #            arrays (which is the correct behavior anyway)
    # /wd4800  - Disable warnings about using non-bool as true or false (useless
    #            performance warning)
    # /wd4244  - Disable warnings about type conversion loss of data, it's a nice
    #            warning, but it triggers on lots and lots of harmless things that no
    #            other compiler warns about, like passing an int as a float parameter
    # /wd4305  - Disable warnings about truncation from double to float
    # /wd4267  - Disable warnings about 64 - 32 bit truncation
    # /wd4456  - Disable warnings about hiding previous local declaration
    # /wd4503  - Silence warnings about MSVC generating a name so long it has to
    #            truncate it
    # /wd4250  - Silence "XX inherits YY via dominance"
    # /wd4624  - Silence implicitly deleted destructor warnings that show up when
    #            using unions in interesting ways.

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /nologo /MP /EHsc /bigobj /wd4996 /wd4351 /wd4800 /wd4244 /wd4305 /wd4267 /wd4456 /wd4503 /wd4250 /wd4624")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /nologo /std:c++21 /MP /EHsc /bigobj /wd4996 /wd4351 /wd4800 /wd4244 /wd4305 /wd4267 /wd4456 /wd4503 /wd4250 /wd4624")

    if(WYVERN_ENABLE_STATIC_MSVC_RUNTIME)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MT")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MT")
    endif()

    set(CMAKE_C_FLAGS_DEBUG "/Z7 /Od")
    set(CMAKE_CXX_FLAGS_DEBUG "/Z7 /Od")

    set(CMAKE_C_FLAGS_RELWITHASSERTS "/Ox /fp:fast /GA /GS- /Z7 /Gy")
    set(CMAKE_CXX_FLAGS_RELWITHASSERTS "/Ox /fp:fast /GA /GS- /Z7 /Gy")

    set(CMAKE_C_FLAGS_RELWITHDEBINFO "/Ox /fp:fast /GA /GS- /Z7 /Gy /DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/Ox /fp:fast /GA /GS- /Z7 /Gy /DNDEBUG")

    set(CMAKE_C_FLAGS_RELEASE "/Ox /fp:fast /GA /GS- /Gy /DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "/Ox /fp:fast /GA /GS- /Gy /DNDEBUG")

    if(WYVERN_ARCHITECTURE_I386)
      # Assume all 32 bit target cpus support MMX, SSE, and SSE2

      set(CMAKE_C_FLAGS_RELWITHASSERTS "${CMAKE_C_FLAGS_RELWITHASSERTS} /arch:SSE2")
      set(CMAKE_CXX_FLAGS_RELWITHASSERTS "${CMAKE_CXX_FLAGS_RELWITHASSERTS} /arch:SSE2")

      set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} /arch:SSE2")
      set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /arch:SSE2")

      set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /arch:SSE2")
      set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /arch:SSE2")
    endif()

    add_definitions(/DUNICODE)
    add_definitions(/D_UNICODE)
    add_definitions(/DNOMINMAX)

  else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -pthread -D_REENTRANT")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++21 -Wall -pthread -D_REENTRANT")

    set(CMAKE_C_FLAGS_DEBUG "-g")
    set(CMAKE_CXX_FLAGS_DEBUG "-g")

    set(CMAKE_C_FLAGS_RELWITHASSERTS "-g -O2")
    set(CMAKE_CXX_FLAGS_RELWITHASSERTS "-g -O2")

    set(CMAKE_C_FLAGS_RELWITHDEBINFO "-DNDEBUG -g -O2")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-DNDEBUG -g -O2")

    set(CMAKE_C_FLAGS_RELEASE "$-DNDEBUG -O2")
    set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O2")

  endif()
endfunction()