#pragma once

#include "BaseModule.h"
#include <boost/asio.hpp>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>

// Простой модуль для защиты от DoS-атак на основе rate limiting по IP.
// Алгоритм:
// 1. Отслеживаем количество запросов от каждого IP в скользящем окне времени (например, 1 минута).
// 2. Если количество запросов превышает порог (threshold), IP блокируется на заданное время (ban_duration).
// 3. Периодическая очистка старых записей для экономии памяти.
// 4. Интеграция: модуль может быть вызван в обработчике входящих соединений (например, в accept handler Boost Asio).
//    Перед обработкой запроса проверяем isAllowed(ip), если нет - закрываем соединение.
// Масштабируемость: в будущем можно добавить Redis для распределенного хранения, или более сложные алгоритмы (token bucket).

class DoSProtectionModule : public BaseModule {
private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = Clock::duration;

    struct ClientInfo {
        int request_count = 0;
        TimePoint last_request;
        TimePoint ban_until;
    };

    std::unordered_map<std::string, ClientInfo> clients_; // IP -> info
    std::mutex mutex_; // Для thread-safety
    std::thread cleanup_thread_; // Для периодической очистки
    std::atomic<bool> running_; // Флаг для остановки cleanup

    // Настройки (можно вынести в конфиг в будущем)
    const int max_requests_per_minute_ = 100; // Порог запросов в минуту
    const Duration window_duration_ = std::chrono::minutes(1);
    const Duration ban_duration_ = std::chrono::minutes(5);
    const Duration cleanup_interval_ = std::chrono::minutes(10);

    void cleanupLoop() {
        while (running_) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto now = Clock::now();
                for (auto it = clients_.begin(); it != clients_.end(); ) {
                    if (now - it->second.last_request > cleanup_interval_) {
                        it = clients_.erase(it);
                    }
                    else {
                        ++it;
                    }
                }
            }
            std::this_thread::sleep_for(cleanup_interval_);
        }
    }

public:
    DoSProtectionModule(const std::string& name = "DoSProtection", const int& id = -1)
        : BaseModule(name, id), running_(false) {
    }

    ~DoSProtectionModule() {
        if (running_) {
            running_ = false;
            if (cleanup_thread_.joinable()) {
                cleanup_thread_.join();
            }
        }
    }

protected:
    bool onInitialize() override {
        // Инициализация: запуск cleanup потока
        running_ = true;
        cleanup_thread_ = std::thread(&DoSProtectionModule::cleanupLoop, this);
        return true;
    }

    void onShutdown() override {
        // Остановка cleanup потока
        running_ = false;
        if (cleanup_thread_.joinable()) {
            cleanup_thread_.join();
        }
        clients_.clear();
    }

public:
    // Основной метод: проверить, разрешен ли запрос от этого IP
    // Вызывать в обработчике соединений, перед обработкой запроса.
    // Возвращает true, если разрешено; false, если заблокировано.
    bool isAllowed(const std::string& ip) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = Clock::now();

        auto& info = clients_[ip];

        // Если забанен
        if (now < info.ban_until) {
            return false;
        }

        // Обновляем счетчик
        if (now - info.last_request > window_duration_) {
            // Окно истекло, сброс
            info.request_count = 1;
        }
        else {
            ++info.request_count;
        }
        info.last_request = now;

        // Проверяем порог
        if (info.request_count > max_requests_per_minute_) {
            info.ban_until = now + ban_duration_;
            return false;
        }

        return true;
    }
};