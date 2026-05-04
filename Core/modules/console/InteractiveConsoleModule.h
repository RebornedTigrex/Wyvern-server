#pragma once

#include "modules/BaseModule.h"
#include "managers/ModuleRegistry.h"
#include "runtime/ConfigSection.h"

#include <boost/asio/io_context.hpp>
#include <boost/json.hpp>

#include <atomic>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// InteractiveConsoleModule — Core-инфраструктурный модуль REPL.
//
// Обязанности (и только они):
//  - читать stdin построчно в фоновом потоке;
//  - постить исполнение каждой команды в общий io_context Core, чтобы вызов
//    handler-ов модулей шёл в том же executor, что и остальная асинхронная
//    логика (нет необходимости в дополнительной синхронизации в модулях);
//  - реализовывать встроенные команды help / list / quit / exit;
//  - делегировать пользовательские команды в ModuleRegistry::invokeCommand.
//
// Чего модуль НЕ делает (по контракту):
//  - не знает про конкретные прикладные модули (UDP / HTTP / ...);
//  - не парсит формат payload-ов прикладных команд;
//  - не публикует команды через свой commands() — встроенные команды
//    обрабатываются внутри REPL и не светятся в реестре.
//
// Связь с архитектурой:
//  - "горизонтальная связность": общается с модулями через единственный
//    источник истины — ModuleRegistry (snapshots + invokeCommand).
//  - "вертикальная связность": зависит от ModuleRegistry и io_context,
//    которые приходят через конструктор. Никаких dependency-ключей.
// ---------------------------------------------------------------------------
class InteractiveConsoleModule : public BaseModule {
public:
    static std::string moduleType() { return "wyvern.interactiveConsole"; }

    static boost::json::object defaults() {
        boost::json::object obj;
        obj["prompt"] = "wyvern> ";
        obj["banner"] = true;
        return obj;
    }

    InteractiveConsoleModule(const core::runtime::ConfigSection& cfg,
                             ModuleRegistry& registry,
                             boost::asio::io_context& ioContext);

    ~InteractiveConsoleModule() override;

    InteractiveConsoleModule(const InteractiveConsoleModule&) = delete;
    InteractiveConsoleModule& operator=(const InteractiveConsoleModule&) = delete;

    std::string moduleKey() const override { return moduleType(); }

protected:
    bool onInitialize() override;
    void onShutdown() override;

private:
    void readerLoop();
    void executeLine(std::string line);

    // Идемпотентный stop+join работы reader-потока. Вызывается из onShutdown
    // и из деструктора. На Windows прерывает блокирующий std::getline через
    // CancelSynchronousIo, чтобы join всегда возвращался оперативно.
    void stopReaderAndJoin();

    void printBanner() const;
    void printPrompt() const;
    void printHelp() const;
    void printList() const;

    static std::vector<std::string> tokenize(const std::string& line);

    ModuleRegistry& registry_;
    boost::asio::io_context& ioContext_;

    std::string prompt_;
    bool banner_;

    std::atomic<bool> running_;
    std::thread readerThread_;

    // Дубликат native HANDLE reader-потока. Храним как void*, чтобы не тащить
    // <windows.h> в заголовок. Нужен, чтобы при shutdown рвать synchronous I/O
    // в std::getline через CancelSynchronousIo и гарантированно делать join.
    // 0 / nullptr = поток не запущен или хэндл уже закрыт.
    void* readerThreadNativeHandle_ = nullptr;
};
