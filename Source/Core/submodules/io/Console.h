#pragma once

#include "objects/BaseModule.h"
#include "managers/ModuleRegistry.h"
#include "runtime/ConfigSection.h"

#include <boost/asio/io_context.hpp>
#include <boost/json.hpp>

#include <atomic>
#include <string>
#include <thread>
#include <functional>
#include <deque>
#include <chrono>
#include <mutex>
#include <future>

/*
    Модуль который полностью управляет выводом/вводом в консоль или же в любой другой интерфейс.
    Главное:
        1. Синхронизация многопоточных вызовов
        2. Универсальный интерфейс взаимодействия с любым модулем, где нужно последовательное отображение данных в реальном времени.
*/

enum class ConsoleIODefine{
    NeedRequest,
    ThrowAway
};

enum class ConsoleIOStatus : uint8_t {
    Pending,          // только создан
    Queued,           // лежит в очереди
    Waiting,          // ждёт ответа (актуально для input)
    Satisfied,        // успешно завершён
    TimedOut,
    Cancelled,
    Failed
};

struct ConsoleIODeclaration {
    // --- только перемещение ---
    ConsoleIODeclaration(const ConsoleIODeclaration&) = delete;
    ConsoleIODeclaration& operator=(const ConsoleIODeclaration&) = delete;

    ConsoleIODeclaration(ConsoleIODeclaration&&) noexcept = default;
    ConsoleIODeclaration& operator=(ConsoleIODeclaration&&) noexcept = default;

    // --- данные ---
    int64_t                     id = 0;
    std::string                 from;
    std::string                 to;
    std::string                 payload;               // сообщение живёт здесь
    std::chrono::steady_clock::time_point created_at;
    std::chrono::milliseconds   timeout{0};            // 0 = без таймаута

    // --- состояние ---
    std::atomic<ConsoleIOStatus> status{ConsoleIOStatus::Pending};

    // --- гарантия ответа ---
    std::promise<std::string>   response;              // для input-запросов

    // --- контролируемые переходы ---
    void mark_queued() noexcept {
        status.store(ConsoleIOStatus::Queued, std::memory_order_release);
    }

    void mark_waiting() noexcept {
        status.store(ConsoleIOStatus::Waiting, std::memory_order_release);
    }

    // Единственные способы завершить запрос
    void complete(std::string result) {
        ConsoleIOStatus expected = ConsoleIOStatus::Waiting;
        if (status.compare_exchange_strong(expected, ConsoleIOStatus::Satisfied)) {
            try {
                response.set_value(std::move(result));
            } catch (...) {
                // promise уже был установлен — это ошибка дизайна
            }
        }
    }

    void fail(std::string_view reason = {}) {
        auto expected = status.load();
        if (expected == ConsoleIOStatus::Satisfied ||
            expected == ConsoleIOStatus::TimedOut  ||
            expected == ConsoleIOStatus::Cancelled ||
            expected == ConsoleIOStatus::Failed)
            return;

        status.store(ConsoleIOStatus::Failed, std::memory_order_release);
        try {
            response.set_exception(std::make_exception_ptr(
                std::runtime_error(std::string(reason))));
        } catch (...) {}
    }

    void timeout() {
        ConsoleIOStatus expected = ConsoleIOStatus::Waiting;
        if (status.compare_exchange_strong(expected, ConsoleIOStatus::TimedOut)) {
            try {
                response.set_exception(std::make_exception_ptr(
                    std::runtime_error("timeout")));
            } catch (...) {}
        }
    }

    void cancel() {
        auto expected = status.load();
        if (expected >= ConsoleIOStatus::Satisfied) return; // уже терминальный

        status.store(ConsoleIOStatus::Cancelled, std::memory_order_release);
        try {
            response.set_exception(std::make_exception_ptr(
                std::runtime_error("cancelled")));
        } catch (...) {}
    }

    // Удобно проверять
    [[nodiscard]] bool is_terminal() const noexcept {
        auto s = status.load(std::memory_order_acquire);
        return s == ConsoleIOStatus::Satisfied ||
               s == ConsoleIOStatus::TimedOut  ||
               s == ConsoleIOStatus::Cancelled ||
               s == ConsoleIOStatus::Failed;
    }
};

class Console : public BaseModule {
public:
    Console(){
        moduleKey_ = "core.console";
        outputQuery.assign({});
        inputQuery.assign({});
    }

    ~Console() override;

    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;

protected:
    bool onInitialize() override;
    void onShutdown() override;

public:
    void postOutput(std::string_view, ConsoleIODeclaration);
    void postInput(std::string_view, ConsoleIODeclaration);

    void onDeclarationTimeout(std::function<void(const ConsoleIODeclaration&)>);
    void onDeclarationSatisfied(std::function<void(const ConsoleIODeclaration&)>);

    void onOutput(std::function<const ConsoleIODeclaration&>);
    void onInput(std::function<const ConsoleIODeclaration&>);

private:
    void* processor();

    void processDeclaration();

    std::mutex outputMutex;

    std::deque<ConsoleIODeclaration> outputQuery;
    std::deque<ConsoleIODeclaration> inputQuery;

};
