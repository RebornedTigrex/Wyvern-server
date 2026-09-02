#include "Runtime.h"

#include <csignal>
#include <cstdlib>
#include <iostream>

#ifdef _WIN32
#include <crtdbg.h>
#include <windows.h>
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

    auto runtimeObj = Wyvern::Runtime(argc, argv);

    return 0;
}
