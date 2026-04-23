#pragma once
#include "modules/BaseModule.h"
#include "RequestHandler.h"

class PlatformRoutesModule : public BaseModule {
public:
    PlatformRoutesModule() : BaseModule("Platform Routes") {}

    std::string moduleKey() const override { return "wyvern.platformRoutes"; }
    std::vector<std::string> dependencies() const override { return {"wyvern.requestHandler"}; }

    void onInject(const std::string& depKey, core::contracts::IModule* dep) override {
        if (depKey == "wyvern.requestHandler")
            requestHandler_ = dynamic_cast<RequestHandler*>(dep);
    }

protected:
    bool onInitialize() override {
        if (!requestHandler_) return false;
        registerRoutes();
        return true;
    }

    void onShutdown() override {}

private:
    RequestHandler* requestHandler_ = nullptr;

    void registerRoutes() {
        using Request  = http::request<http::string_body>;
        using Response = http::response<http::string_body>;

        requestHandler_->addRouteHandler("/health", [](const Request& req, Response& res) {
            if (req.method() != http::verb::get) {
                res.result(http::status::method_not_allowed);
                res.set(http::field::content_type, "text/plain");
                res.body() = "Method Not Allowed. Use GET.";
                return;
            }
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"status":"ok"})";
            res.result(http::status::ok);
        });

        requestHandler_->addRouteHandler("/test", [](const Request& req, Response& res) {
            if (req.method() != http::verb::get) {
                res.result(http::status::method_not_allowed);
                res.set(http::field::content_type, "text/plain");
                res.body() = "Method Not Allowed. Use GET.";
                return;
            }
            res.set(http::field::content_type, "text/plain");
            res.body() = "RequestHandler Test.";
            res.result(http::status::ok);
        });

        requestHandler_->addRouteHandler("/*", [](const Request&, Response&) {});
    }
};
