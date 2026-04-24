#pragma once

#include "modules/BaseModule.h"
#include "FileCache.h"

#include <boost/beast/http.hpp>

#include <algorithm>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace beast = boost::beast;
namespace http = beast::http;

class RequestHandler : public BaseModule {
public:
    struct RequestFlowContext {
        std::string clientIp;
    };

    using RouteHandler = std::function<void(const http::request<http::string_body>&, http::response<http::string_body>&)>;
    using MiddlewareHandler = std::function<bool(const http::request<http::string_body>&, http::response<http::string_body>&, const RequestFlowContext&)>;

private:
    struct MiddlewareRegistration {
        std::string middlewareId;
        int order = 0;
        MiddlewareHandler handler;
    };

    FileCache* file_cache_ = nullptr;

    std::pair<std::string, std::string> parseTarget(const std::string& target) {
        size_t pos = target.find('?');
        if (pos == std::string::npos) {
            return { target, "" };
        }
        return { target.substr(0, pos), target.substr(pos + 1) };
    }

public:
    RequestHandler();

    std::string moduleKey() const override { return "wyvern.requestHandler"; }
    std::vector<std::string> dependencies() const override { return {"wyvern.fileCache"}; }
    void onInject(const std::string& depKey, core::contracts::IModule* dep) override {
        if (depKey == "wyvern.fileCache")
            file_cache_ = dynamic_cast<FileCache*>(dep);
    }

    void addDynamicRouteHandler(const std::string& regexPattern, RouteHandler handler);
    bool addMiddleware(std::string middlewareId, MiddlewareHandler handler, int order = 0);
    void addRouteHandler(const std::string& path, RouteHandler handler);

    template<class Send>
    void handleRequest(http::request<http::string_body>&& req, Send&& send, const RequestFlowContext& flowContext = {}) {
        http::response<http::string_body> res{ http::status::not_found, req.version() };
        res.set(http::field::server, "ModularServer");
        res.keep_alive(req.keep_alive());
        if (req.version() >= 11 && res.keep_alive()) {
            res.set(http::field::connection, "keep-alive");
        }

        for (const auto& middleware : middlewares_) {
            if (!middleware.handler(req, res, flowContext)) {
                if (res.body().empty()) {
                    res.result(http::status::forbidden);
                    res.set(http::field::content_type, "application/json");
                    res.body() = R"({"status":"blocked_by_middleware"})";
                }
                res.prepare_payload();
                send(std::move(res));
                return;
            }
        }

        const std::string target = std::string(req.target());
        auto [path, query] = parseTarget(target);

        auto wildcard_it = routeHandlers_.find("/*");
        if (wildcard_it != routeHandlers_.end() && file_cache_) {
            file_cache_->refresh_file(path);
            auto cached_file = file_cache_->get_file(path);
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

        auto it = routeHandlers_.find(path);
        if (it != routeHandlers_.end()) {
            it->second(req, res);
            res.prepare_payload();
            send(std::move(res));
            return;
        }

        if (target.find("../") != std::string::npos && file_cache_) {
            res.set(http::field::content_type, "text/html");
            file_cache_->refresh_file("/attention.html");
            const auto& cached = file_cache_->get_file("/attention.html");
            if (cached) {
                res.set(http::field::cache_control, "public, max-age=300");
                res.body() = cached.value().content;
            }
            res.prepare_payload();
            send(std::move(res));
            return;
        }

        if (!dynamicRouteHandlers_.empty()) {
            bool handled = false;
            for (const auto& [re, handler] : dynamicRouteHandlers_) {
                if (std::regex_match(path, re)) {
                    handler(req, res);
                    handled = true;
                    break;
                }
            }
            if (handled) {
                res.prepare_payload();
                send(std::move(res));
                return;
            }
        }

        if (target.find("api/") != std::string::npos) {
            res.set(http::field::content_type, "application/json");
            res.result(http::status::not_found);
            res.set(http::field::cache_control, "no-cache, must-revalidate");
            res.body() = R"({"status":"not_found"})";
            res.prepare_payload();
            send(std::move(res));
            return;
        }

        if (file_cache_) {
            res.set(http::field::content_type, "text/html");
            file_cache_->refresh_file("/errorNotFound.html");
            const auto& cached = file_cache_->get_file("/errorNotFound.html");
            if (cached) {
                res.set(http::field::cache_control, "public, max-age=300");
                res.body() = cached.value().content;
            }
            res.prepare_payload();
            send(std::move(res));
            return;
        }

        res.set(http::field::content_type, "text/plain");
        res.result(http::status::not_found);
        res.body() = "Not Found";
        res.prepare_payload();
        send(std::move(res));
    }

protected:
    bool onInitialize() override;
    bool onReady() override;
    void onShutdown() override;

private:
    std::vector<MiddlewareRegistration> middlewares_;
    std::vector<std::pair<std::regex, RouteHandler>> dynamicRouteHandlers_;
    std::unordered_map<std::string, RouteHandler> routeHandlers_;
    void setupDefaultRoutes();
};

