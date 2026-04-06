#include "RequestHandler.h"
#include <iostream>

RequestHandler::RequestHandler()
    : BaseModule("HTTP Request Handler") {
}

void RequestHandler::addDynamicRouteHandler(const std::string& regexPattern,
    std::function<void(const http::request<http::string_body>&, http::response<http::string_body>&)> handler) {
    try {
        std::regex re(regexPattern);  // Компилируем regex заранее для эффективности
        dynamicRouteHandlers_.emplace_back(re, handler);
    }
    catch (const std::regex_error& e) {
        std::cerr << "Invalid regex pattern: " << regexPattern << " - " << e.what() << std::endl;
        // Для MVP: не добавляем, но не крашим
    }
}

bool RequestHandler::onInitialize() {
    setupDefaultRoutes();
    std::cout << "RequestHandler initialized with " << routeHandlers_.size() << " routes" << std::endl;
    if (file_cache_) {
        std::cout << "FileCache linked successfully." << std::endl;  // NEW: Лог для отладки
    }
    return true;
}

void RequestHandler::onShutdown() {
    routeHandlers_.clear();
    std::cout << "RequestHandler shutdown" << std::endl;
}

void RequestHandler::addRouteHandler(const std::string& path,
    std::function<void(const http::request<http::string_body>&, http::response<http::string_body>&)> handler) {
    routeHandlers_[path] = handler;
}

void RequestHandler::setupDefaultRoutes() { //Придумать какую-нибудь штуку для замены стандартного обработчика
    // Обработчик для корневого пути
    /*addRouteHandler("/", [](const http::request<http::string_body>& req, http::response<http::string_body>& res) {
        res.set(http::field::content_type, "text/plain");
        res.body() = "Hello from RequestHandler module!";
        });*/
    // Обработчик для /status
    addRouteHandler("/status", [](const http::request<http::string_body>& req, http::response<http::string_body>& res) {
        res.set(http::field::content_type, "application/json");
        res.result(http::status::ok);
        res.set(http::field::cache_control, "no-cache, must-revalidate");
        res.body() = R"({"status": "ok", "service": "modular_http_server"})";
        });
}