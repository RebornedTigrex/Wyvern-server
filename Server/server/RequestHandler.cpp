#include "RequestHandler.h"
#include <iostream>

RequestHandler::RequestHandler()
    : BaseModule("HTTP Request Handler") {
}

void RequestHandler::addDynamicRouteHandler(const std::string& regexPattern,
    RouteHandler handler) {
    try {
        std::regex re(regexPattern);
        dynamicRouteHandlers_.emplace_back(re, handler);
    }
    catch (const std::regex_error& e) {
        std::cerr << "Invalid regex pattern: " << regexPattern << " - " << e.what() << std::endl;
    }
}

bool RequestHandler::addMiddleware(std::string middlewareId, MiddlewareHandler handler, int order) {
    if (middlewareId.empty() || !handler) {
        return false;
    }

    auto exists = std::find_if(
        middlewares_.begin(),
        middlewares_.end(),
        [&middlewareId](const MiddlewareRegistration& registration) {
            return registration.middlewareId == middlewareId;
        }
    );
    if (exists != middlewares_.end()) {
        return false;
    }

    MiddlewareRegistration registration;
    registration.middlewareId = std::move(middlewareId);
    registration.order = order;
    registration.handler = std::move(handler);
    middlewares_.push_back(std::move(registration));

    std::sort(
        middlewares_.begin(),
        middlewares_.end(),
        [](const MiddlewareRegistration& lhs, const MiddlewareRegistration& rhs) {
            if (lhs.order == rhs.order) {
                return lhs.middlewareId < rhs.middlewareId;
            }
            return lhs.order < rhs.order;
        }
    );

    return true;
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
    middlewares_.clear();
    dynamicRouteHandlers_.clear();
    routeHandlers_.clear();
    std::cout << "RequestHandler shutdown" << std::endl;
}

void RequestHandler::addRouteHandler(const std::string& path,
    RouteHandler handler) {
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