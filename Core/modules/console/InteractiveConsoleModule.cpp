#include "modules/console/InteractiveConsoleModule.h"

#include <boost/asio/post.hpp>

#include <iostream>
#include <sstream>
#include <utility>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace {

// LifecycleState -> человекочитаемая метка для list / status вывода.
const char* lifecycleStateLabel(core::contracts::LifecycleState s) {
    using S = core::contracts::LifecycleState;
    switch (s) {
        case S::Created:      return "created";
        case S::Registered:   return "registered";
        case S::Initializing: return "initializing";
        case S::Running:      return "running";
        case S::Stopping:     return "stopping";
        case S::Stopped:      return "stopped";
        case S::Failed:       return "failed";
    }
    return "unknown";
}

} // namespace

InteractiveConsoleModule::InteractiveConsoleModule(const core::runtime::ConfigSection& cfg,
                                                   ModuleRegistry& registry,
                                                   boost::asio::io_context& ioContext)
    : BaseModule("Interactive Console"),
      registry_(registry),
      ioContext_(ioContext),
      prompt_(cfg.value<std::string>("prompt", "wyvern> ")),
      banner_(cfg.value<bool>("banner", true)),
      running_(false) {
}

InteractiveConsoleModule::~InteractiveConsoleModule() {
    // Fallback: если onShutdown уже отработал, здесь будет no-op — thread
    // уже join'ed и handle закрыт. Если onShutdown не успел (например исключение
    // в lifecycle до initializeAll), рвём поток и ждём его окончания здесь.
    stopReaderAndJoin();
}

bool InteractiveConsoleModule::onInitialize() {
    if (running_.load()) {
        std::cerr << "[Console] already running\n";
        return false;
    }

    running_.store(true);

    if (banner_) {
        printBanner();
    }
    printPrompt();

    try {
        readerThread_ = std::thread([this]() { readerLoop(); });
    } catch (const std::exception& e) {
        std::cerr << "[Console] failed to start reader thread: " << e.what() << '\n';
        running_.store(false);
        return false;
    }

#ifdef _WIN32
    // Дублируем HANDLE потока: native_handle() становится невалидным после
    // join/detach, а нам нужно выжившее владение до вызова CancelSynchronousIo при shutdown.
    HANDLE current = static_cast<HANDLE>(readerThread_.native_handle());
    HANDLE dup = nullptr;
    if (current && DuplicateHandle(GetCurrentProcess(), current,
                                   GetCurrentProcess(), &dup,
                                   0, FALSE, DUPLICATE_SAME_ACCESS)) {
        readerThreadNativeHandle_ = dup;
    }
#endif

    return true;
}

void InteractiveConsoleModule::onShutdown() {
    stopReaderAndJoin();
}

void InteractiveConsoleModule::stopReaderAndJoin() {
    running_.store(false);

    // CancelSynchronousIo прерывает блокирующий ReadFile в reader-потоке.
    // Решающее отличие от предыдущего подхода: reader больше НЕ использует std::cin
    // и CRT-буферы. Чистый ReadFile не держит CRT-locks, поэтому КОРРЕКТНО
    // прерывается и CRT-деинициализация не виснет после main return.
#ifdef _WIN32
    if (readerThreadNativeHandle_) {
        CancelSynchronousIo(static_cast<HANDLE>(readerThreadNativeHandle_));
    }
#endif

    if (readerThread_.joinable()) {
        readerThread_.join();
    }

#ifdef _WIN32
    if (readerThreadNativeHandle_) {
        CloseHandle(static_cast<HANDLE>(readerThreadNativeHandle_));
        readerThreadNativeHandle_ = nullptr;
    }
#endif
}

void InteractiveConsoleModule::readerLoop() {
#ifdef _WIN32
    // Windows-путь: работаем напрямую с stdin HANDLE через ReadFile, без std::cin
    // и CRT-буферов. Это позволяет CancelSynchronousIo надёжно рвать блокирующее
    // чтение при shutdown без оставления повисших CRT lock-ов.
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    if (!hStdin || hStdin == INVALID_HANDLE_VALUE) {
        running_.store(false);
        boost::asio::post(ioContext_, [this]() {
            std::cerr << "[Console] stdin handle is invalid\n";
            ioContext_.stop();
        });
        return;
    }

    std::string line;
    char buffer[4096];

    while (running_.load()) {
        DWORD bytesRead = 0;
        const BOOL ok = ReadFile(hStdin, buffer, sizeof(buffer), &bytesRead, nullptr);
        if (!ok || bytesRead == 0) {
            // Ошибка / EOF / ERROR_OPERATION_ABORTED после CancelSynchronousIo.
            if (running_.exchange(false)) {
                boost::asio::post(ioContext_, [this]() {
                    std::cout << "\n[Console] stdin closed, stopping io_context\n";
                    ioContext_.stop();
                });
            }
            return;
        }

        for (DWORD i = 0; i < bytesRead; ++i) {
            const char c = buffer[i];
            if (c == '\n') {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (!running_.load()) {
                    return;
                }
                std::string captured = std::move(line);
                line.clear();
                boost::asio::post(ioContext_,
                    [this, captured = std::move(captured)]() mutable {
                        executeLine(std::move(captured));
                    });
            } else {
                line.push_back(c);
            }
        }
    }
#else
    // POSIX-путь: std::getline прерывается закрытием stdin (в stopReaderAndJoin
    // этот путь не покрыт — для Linux/macOS хорошо бы делать close(STDIN_FILENO)).
    std::string line;
    while (running_.load()) {
        if (!std::getline(std::cin, line)) {
            std::cin.clear();
            if (running_.exchange(false)) {
                boost::asio::post(ioContext_, [this]() {
                    std::cout << "\n[Console] stdin closed, stopping io_context\n";
                    ioContext_.stop();
                });
            }
            return;
        }
        if (!running_.load()) {
            return;
        }
        boost::asio::post(ioContext_,
            [this, captured = std::move(line)]() mutable {
                executeLine(std::move(captured));
            });
    }
#endif
}

void InteractiveConsoleModule::executeLine(std::string line) {
    auto tokens = tokenize(line);
    if (tokens.empty()) {
        printPrompt();
        return;
    }

    const std::string& head = tokens[0];

    if (head == "help") {
        printHelp();
        printPrompt();
        return;
    }
    if (head == "list") {
        printList();
        printPrompt();
        return;
    }
    if (head == "quit" || head == "exit") {
        std::cout << "[Console] bye\n";
        running_.store(false);
        ioContext_.stop();
        return;
    }

    if (tokens.size() < 2) {
        std::cout << "[Console] usage: <moduleKey> <commandName> [args...] | help | list | quit\n";
        printPrompt();
        return;
    }

    std::string moduleKey  = std::move(tokens[0]);
    std::string commandName = std::move(tokens[1]);
    core::contracts::CommandArgs args;
    args.reserve(tokens.size() > 2 ? tokens.size() - 2 : 0);
    for (std::size_t i = 2; i < tokens.size(); ++i) {
        args.push_back(std::move(tokens[i]));
    }

    auto result = registry_.invokeCommand(moduleKey, commandName, args);

    if (!result.output.empty()) {
        std::cout << result.output;
        if (result.output.back() != '\n') {
            std::cout << '\n';
        }
    }
    if (!result.status.ok) {
        std::cout << "[Console] error: " << result.status.message << '\n';
    } else if (result.output.empty()) {
        std::cout << "[Console] ok\n";
    }

    printPrompt();
}

void InteractiveConsoleModule::printBanner() const {
    std::cout
        << "----------------------------------------\n"
        << " Wyvern interactive console\n"
        << " type `help` for built-ins, `list` for modules,\n"
        << " `quit` or `exit` to shut down the server.\n"
        << "----------------------------------------\n";
}

void InteractiveConsoleModule::printPrompt() const {
    std::cout << prompt_ << std::flush;
}

void InteractiveConsoleModule::printHelp() const {
    std::cout
        << "Built-in commands:\n"
        << "  help                                  - show this help\n"
        << "  list                                  - list registered modules and their commands\n"
        << "  quit | exit                           - shut the server down\n"
        << "Module commands:\n"
        << "  <moduleKey> <commandName> [args...]   - invoke a published command\n"
        << "                                          (see `list` for available modules and commands)\n";
}

void InteractiveConsoleModule::printList() const {
    auto snapshots = registry_.snapshots();
    if (snapshots.empty()) {
        std::cout << "(no modules registered)\n";
        return;
    }

    std::cout << "Registered modules (" << snapshots.size() << "):\n";
    for (const auto& s : snapshots) {
        std::cout
            << "- " << s.moduleKey
            << " [" << s.name << "]"
            << " state=" << lifecycleStateLabel(s.state)
            << " enabled=" << (s.enabled ? "yes" : "no")
            << '\n';

        if (s.commands.empty()) {
            std::cout << "    (no commands)\n";
            continue;
        }
        for (const auto& c : s.commands) {
            std::cout << "    " << c.name;
            if (!c.summary.empty()) {
                std::cout << "  -  " << c.summary;
            }
            std::cout << '\n';
        }
    }
}

std::vector<std::string> InteractiveConsoleModule::tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string token;
    while (iss >> token) {
        tokens.push_back(std::move(token));
    }
    return tokens;
}
