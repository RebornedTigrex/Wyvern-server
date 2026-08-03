#include "Core.h"
#include "StaticModuleRegistration.h"

#include <csignal>
#include <cstdlib>
#include <iostream>

#ifdef _WIN32
#include <crtdbg.h>
#endif

static void setupAbortHandling() {
#ifdef _WIN32
    // Suppress the Windows CRT assertion dialog and error-reporting popup.
    // Any abort() / assert() failure will now write to stderr and exit immediately
    // without requiring user interaction.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR,  _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR,  _CRTDBG_FILE_STDERR);

    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);
#endif
    std::signal(SIGABRT, [](int) {
        fputs("[fatal] abort() called - exiting\n", stderr);
        _exit(EXIT_FAILURE);
    });
}

int main(int argc, char* argv[]) {
    setupAbortHandling();
    auto core = Core::instance();

    if (!core->bootstrap(argc, argv)) {
        return EXIT_FAILURE;
    }

    StaticModuleRegistration(*core);

    if (!core->initializeModules()) {
        std::cerr << "[main] module initialization failed\n";
        core->shutdown();
        return EXIT_FAILURE;
    }
    core->readyModules();
    core->commitConfig();

    try {
        core->ioContext().run();
    } catch (const std::exception& e) {
        std::cerr << "[main] runtime error: " << e.what() << "\n";
        core->shutdown();
        return EXIT_FAILURE;
    }

    core->shutdown();
    return 0;
}
