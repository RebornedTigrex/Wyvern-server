#pragma once
#include "BaseModule.h"
#include "FileCache.h"

#include <boost/beast/http.hpp>
#include <sstream>
#include <fstream>
#include <regex>
#include <vector>
#include <unordered_map>

namespace beast = boost::beast;
namespace http = beast::http;

class RequestHandler : public BaseModule {
    FileCache* file_cache_ = nullptr;  // Указатель на кэш (инжектируется в main)


    // Парсинг target на path и query (простой split по ?)
    std::pair<std::string, std::string> parseTarget(const std::string& target) {
        size_t pos = target.find('?');
        if (pos == std::string::npos) {
            return { target, "" };  // Нет query
        }
        return { target.substr(0, pos), target.substr(pos + 1) };  // path, query
    }

public:
    RequestHandler();
    // Метод для инжекции кэша (только из main)
    void setFileCache(FileCache* cache) {
        file_cache_ = cache;
        std::string base_dir = file_cache_->get_base_directory();

    }

    // Новый метод для динамических роутов (regex-паттерн)
    void addDynamicRouteHandler(const std::string& regexPattern,
        std::function<void(const http::request<http::string_body>&, http::response<http::string_body>&)> handler);

    // Методы для регистрации обработчиков конкретных путей
    void addRouteHandler(const std::string& path, std::function<void(const http::request<http::string_body>&, http::response<http::string_body>&)> handler);

    template<class Body, class Allocator, class Send>
    void handleRequest(http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send) {
        http::response<http::string_body> res{ http::status::not_found, req.version() };
        res.set(http::field::server, "ModularServer");
        res.keep_alive(req.keep_alive());
        // Explicit Connection header для force keep-alive (если !reиq.keep_alive(), но для MVP — всегда true для 1.1)
        if (req.version() >= 11 && res.keep_alive()) {
            res.set(http::field::connection, "keep-alive");
        }

        std::string target = std::string(req.target());
        auto [path, query] = parseTarget(target);

        // Проверяем wildcard /* для динамического поиска в кэше (только по path!)
        auto wildcard_it = routeHandlers_.find("/*"); //FIXME: Повышает время отклика
        if (wildcard_it != routeHandlers_.end() && file_cache_) {
            file_cache_->refresh_file(path);
            auto cached_file = file_cache_->get_file(path);  // Ищем по чистому path
            if (cached_file) {
                res.set(http::field::content_type, cached_file->mime_type.c_str());
                res.set(http::field::cache_control, "public, max-age=300");
                res.body() = std::move(cached_file->content);
                res.result(http::status::ok);
                res.prepare_payload();
                send(std::move(res));
                return;
            }
        }

		// Добавлена динамика по regex-паттернам
        auto it = routeHandlers_.find(path);
        if (it != routeHandlers_.end()) {
            // Передаём query в handler (если lambda ожидает — расширь signature)
            // Для MVP: если handler статический, игнорируем query
            it->second(req, res); 
            res.prepare_payload();
            send(std::move(res));
            return;
        }
        else if (target.find("../") != std::string::npos) {
            res.set(http::field::content_type, "text/html");
            file_cache_->refresh_file("/attention.html");
            const auto& cached = file_cache_->get_file("/attention.html");
            res.set(http::field::cache_control, "public, max-age=300");
            res.body() = cached.value().content;
            res.prepare_payload();
            send(std::move(res));
            return;

        }
        if (it == routeHandlers_.end() && !dynamicRouteHandlers_.empty()) { //FIXME: Съедает 404 страничку (Уже нет, но переработать стоит). Сделать нормальную валидацию
            bool handled = false;
            for (const auto& [re, handler] : dynamicRouteHandlers_) {
                if (std::regex_match(path, re)) {  // Матчим весь path с regex
                    handler(req, res);
                    handled = true;
                    break;  // Первый матч — обрабатываем (порядок в векторе важен: более конкретные выше)
                }
            }
            if (handled) {
                res.prepare_payload();
                send(std::move(res));
                return;
            }
            else if (target.find("api/") != std::string::npos) {
                res.set(http::field::content_type, "application/json");
                res.result(http::status::not_found);
                res.set(http::field::cache_control, "no-cache, must-revalidate");
                res.body() = R"({"status": "not_found"})";
                res.prepare_payload();
                send(std::move(res));
                return;
            }
            else {
                res.set(http::field::content_type, "text/html");
                file_cache_->refresh_file("/errorNotFound.html");
                const auto& cached = file_cache_->get_file("/errorNotFound.html");
                res.set(http::field::cache_control, "public, max-age=300");
                res.body() = cached.value().content;
                res.prepare_payload();
                send(std::move(res));
            }
        }
    }

protected:
    bool onInitialize() override;
    void onShutdown() override;

private:
    std::vector<std::pair<std::regex,
        std::function
        <void(const http::request<http::string_body>&, http::response<http::string_body>&)>
    >> dynamicRouteHandlers_;

    std::unordered_map<
        std::string,
        std::function<void(const http::request<http::string_body>&, http::response<http::string_body>&)>
    > routeHandlers_;
    void setupDefaultRoutes();
};