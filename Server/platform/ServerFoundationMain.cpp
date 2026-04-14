#include "LambdaSenders.h"
#include "Session.h"
#include "plugin_runtime/IPlugin.h"
#include "plugin_runtime/PluginLifecycleManager.h"
#include "ServerRuntimeContext.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/thread.hpp>
#include <boost/make_shared.hpp>
#include <boost/program_options.hpp>

#include <iostream>
#include <memory>
#include <fstream>
#include <memory>
#include <sstream>
#include <string_view>
#include <vector>

using wyvern::plugin_runtime::IPlugin;
using wyvern::plugin_runtime::PluginLifecycleManager;
using wyvern::platform::runtime::ServerRuntimeContext;

void printConnectionInfo(tcp::socket& socket) {
    try {
        tcp::endpoint remote_ep = socket.remote_endpoint();
        boost::asio::ip::address client_address = remote_ep.address();
        unsigned short client_port = remote_ep.port();

        std::cout << "Client connected from: "
            << client_address.to_string()
            << ":" << client_port << std::endl;
    }
    catch (const boost::system::system_error& e) {
        std::cerr << "Error getting connection info: " << e.what() << std::endl;
    }
}

void CreatePlatformHandlers(RequestHandler* module) {
    module->addRouteHandler("/health", [](const sRequest& req, sResponce& res) {
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
    module->addRouteHandler("/test", [](const sRequest& req, sResponce& res) {
        if (req.method() != http::verb::get) {
            res.result(http::status::method_not_allowed);
            res.set(http::field::content_type, "text/plain");
            res.body() = "Method Not Allowed. Use GET.";
            return;
        }
        res.set(http::field::content_type, "text/plain");
        res.body() = "RequestHandler Module Scaling Test.\nAlso checking support for the Russian language.";
        res.result(http::status::ok);
        });

    module->addRouteHandler("/*", [](const sRequest& req, sResponce& res) {});
}

class RequestHandlerPlugin final : public IPlugin {
public:
    explicit RequestHandlerPlugin(ServerRuntimeContext& context)
        : context_(context) {
    }

    std::string_view pluginId() const override { return "transport.request-handler"; }
    std::string_view version() const override { return "1.0.0"; }
    std::string_view kind() const override { return "transport"; }
    std::vector<std::string> dependencies() const override { return {}; }

    bool registerPlugin() override { return true; }

    bool init() override {
        context_.requestHandler = context_.moduleRegistry->registerModule<RequestHandler>();
        return context_.requestHandler != nullptr;
    }

    bool start() override { return true; }
    void stop() override {}

private:
    ServerRuntimeContext& context_;
};

class FileCachePlugin final : public IPlugin {
public:
    explicit FileCachePlugin(ServerRuntimeContext& context)
        : context_(context) {
    }

    std::string_view pluginId() const override { return "transport.file-cache"; }
    std::string_view version() const override { return "1.0.0"; }
    std::string_view kind() const override { return "infrastructure"; }
    std::vector<std::string> dependencies() const override { return { "transport.request-handler" }; }

    bool registerPlugin() override { return true; }

    bool init() override {
        context_.fileCache = context_.moduleRegistry->registerModule<FileCache>(context_.config.directory.c_str(), true, 100);
        return context_.fileCache != nullptr;
    }

    bool start() override {
        if (!context_.requestHandler || !context_.fileCache) {
            return false;
        }
        context_.requestHandler->setFileCache(context_.fileCache);
        return true;
    }

    void stop() override {}

private:
    ServerRuntimeContext& context_;
};

class DoSProtectionPlugin final : public IPlugin {
public:
    explicit DoSProtectionPlugin(ServerRuntimeContext& context)
        : context_(context) {
    }

    std::string_view pluginId() const override { return "security.dos-protection"; }
    std::string_view version() const override { return "1.0.0"; }
    std::string_view kind() const override { return "security"; }
    std::vector<std::string> dependencies() const override { return { "transport.request-handler" }; }

    bool registerPlugin() override { return true; }

    bool init() override {
        context_.dosProtection = context_.moduleRegistry->registerModule<DoSProtectionModule>();
        return context_.dosProtection != nullptr;
    }

    bool start() override {
        if (!context_.requestHandler || !context_.dosProtection) {
            return false;
        }

        return context_.requestHandler->addMiddleware(
            "security.dos-protection",
            [this](const sRequest&, sResponce& res, const RequestHandler::RequestFlowContext& flowContext) {
                if (flowContext.clientIp.empty() || context_.dosProtection->isAllowed(flowContext.clientIp)) {
                    return true;
                }

                res.result(http::status::too_many_requests);
                res.set(http::field::content_type, "application/json");
                res.set(http::field::cache_control, "no-cache, must-revalidate");
                res.body() = R"({"status":"rate_limited"})";
                return false;
            },
            -100
        );
    }
    void stop() override {}

private:
    ServerRuntimeContext& context_;
};

class PlatformRoutesPlugin final : public IPlugin {
public:
    explicit PlatformRoutesPlugin(ServerRuntimeContext& context)
        : context_(context) {
    }

    std::string_view pluginId() const override { return "platform.routes"; }
    std::string_view version() const override { return "1.0.0"; }
    std::string_view kind() const override { return "feature"; }
    std::vector<std::string> dependencies() const override { return { "transport.request-handler" }; }

    bool registerPlugin() override { return true; }

    bool init() override { return context_.requestHandler != nullptr; }

    bool start() override {
        if (!context_.requestHandler) {
            return false;
        }
        CreatePlatformHandlers(context_.requestHandler);
        return true;
    }

    void stop() override {}

private:
    ServerRuntimeContext& context_;
};

int main(int argc, char* argv[]) {
    ServerRuntimeContext context;
    context.config = ServerConfig::parse(argc, argv);

#ifdef _WIN32
    std::cout << "Console CP: " << GetConsoleCP() << std::endl;
    std::cout << "Console Output CP: " << GetConsoleOutputCP() << std::endl;
    std::cout << "ACP: " << GetACP() << std::endl;
    std::cout << "OEMCP: " << GetOEMCP() << std::endl;
#endif //_WIN32

    //// Вывод конфигурации
    //std::cout << "Server configuration:\n"
    //    << " Address: " << address << "\n"
    //    << " Port: " << port << "\n"
    //    << " Directory: " << directory << "\n\n";
//////////////////////////////////////////////////////////
    PluginLifecycleManager pluginLifecycleManager;
    if (!pluginLifecycleManager.add(std::make_unique<RequestHandlerPlugin>(context))
        || !pluginLifecycleManager.add(std::make_unique<FileCachePlugin>(context))
        || !pluginLifecycleManager.add(std::make_unique<DoSProtectionPlugin>(context))
        || !pluginLifecycleManager.add(std::make_unique<PlatformRoutesPlugin>(context))) {
        std::cerr << "Failed to add plugins to plugin manager." << std::endl;
        return EXIT_FAILURE;
    }
    if (!pluginLifecycleManager.startAll()) {
        std::cerr << "Plugin lifecycle failed during register/init/start." << std::endl;
        if (const auto lastError = pluginLifecycleManager.lastError()) {
            std::cerr << "Plugin lifecycle error:"
                << " pluginId=" << lastError->pluginId
                << " stage=" << lastError->stage
                << " message=" << lastError->message << std::endl;
        }
        return EXIT_FAILURE;
    }
    if (!context.moduleRegistry || !context.moduleRegistry->initializeAll()) {
        std::cerr << "Module registry initialization failed." << std::endl;
        return EXIT_FAILURE;
    }


///////////////////////////////////////////////////////////

    try {
        auto const net_address = net::ip::make_address(context.config.address);
        auto const net_port = static_cast<unsigned short>(context.config.port);
        tcp::acceptor acceptor{ context.ioContext, {net_address, net_port} };
        std::cout << "Server started on http://" << context.config.address << ":" << context.config.port << std::endl;

        // UPDATED: Do_accept с std::function для safe recursive (avoid self-ref UB)
        std::function<void()> do_accept_func = [&acceptor, &context, &do_accept_func]() {
            auto socket = std::make_shared<tcp::socket>(context.ioContext);
            acceptor.async_accept(*socket,
                [socket_ptr = socket, &do_accept_func, &context](beast::error_code ec) {
                    if (!ec) {
                        printConnectionInfo(*socket_ptr);
                        std::make_shared<session>(std::move(*socket_ptr), context.requestHandler)->run();
                    }
                    else {
                        std::cerr << "Accept error: " << ec.message() << std::endl;
                    }
                    do_accept_func();  // Рекурсия via function call (safe)
                });
            };

        do_accept_func();
        context.ioContext.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        pluginLifecycleManager.stopAll();
        if (context.moduleRegistry) {
            context.moduleRegistry->shutdownAll();
        }
        return EXIT_FAILURE;
    }
    pluginLifecycleManager.stopAll();
    if (context.moduleRegistry) {
        context.moduleRegistry->shutdownAll();
    }
    return 0;
}