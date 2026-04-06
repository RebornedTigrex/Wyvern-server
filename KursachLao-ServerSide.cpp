#include "LambdaSenders.h"
#include "RequestHandler.h"
#include "ModuleRegistry.h"
#include "FileCache.h"
#include "macros.h"
#include "Session.h"

#include "DatabaseModule.h"
#include "ApiProcessor.h"
#include "DoSProtectionModule.h"
#include "ServerConfig.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/thread.hpp>
#include <boost/make_shared.hpp>
#include <boost/program_options.hpp>

#include <iostream>
#include <memory>
#include <fstream>
#include <sstream>

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

void CreateAPIHandlers(RequestHandler* module, ApiProcessor* apiProcessor) {
    // Основной эндпоинт для всех данных — как ожидает фронт
    module->addRouteHandler("/api/all-data", [apiProcessor](const sRequest& req, sResponce& res) {
        apiProcessor->handleGetAllData(req, res);
        });

    // Список сотрудников (можно оставить как есть, но лучше сделать отдельный обработчик позже)
    module->addRouteHandler("/api/employees", [apiProcessor](const sRequest& req, sResponce& res) {
        if (req.method() == http::verb::post) {
            apiProcessor->handleAddEmployee(req, res);
        }
        else if (req.method() == http::verb::get) {
            apiProcessor->handleGetAllData(req, res); // временно ок — фронт пока не использует отдельно
        }
        else {
            res.result(http::status::method_not_allowed);
        }
        });

    module->addDynamicRouteHandler("/api/employees/\\d+(?:/)?", [apiProcessor](const sRequest& req, sResponce& res) {
        if (req.method() == http::verb::put) {
            apiProcessor->handleUpdateEmployee(req, res);
        }
        else {
            res.result(http::status::method_not_allowed);
        }
        });
    module->addDynamicRouteHandler("/api/hours/\\d+(?:/)?", [apiProcessor](const sRequest& req, sResponce& res) {
        if (req.method() == http::verb::post) {
            apiProcessor->handleAddHours(req, res);
        }
        else {
            res.result(http::status::method_not_allowed);
        }
        });
    module->addDynamicRouteHandler("/api/employees/\\d+/penalties(?:/)?", [apiProcessor](const sRequest& req, sResponce& res) {
        if (req.method() == http::verb::post) {
            apiProcessor->handleAddPenalty(req, res);
        }
        else {
            res.result(http::status::method_not_allowed);
        }
        });
    module->addDynamicRouteHandler("/api/employees/\\d+/bonuses(?:/)?", [apiProcessor](const sRequest& req, sResponce& res) {
        if (req.method() == http::verb::post) {
            apiProcessor->handleAddBonus(req, res);
        }
        else {
            res.result(http::status::method_not_allowed);
        }
        });
}

void CreateNewHandlers(RequestHandler* module, std::string staticFolder) {
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

int main(int argc, char* argv[]) {
    const ServerConfig config = ServerConfig::parse(argc, argv);

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
    net::io_context ioc;

    const char* databaseStr = "dbname=postgres user=postgres password=postgres host=127.0.0.1 port=54855";//TODO: Перенести хардкод в параметры

    ModuleRegistry registry;
    auto* cacheModule = registry.registerModule<FileCache>(config.directory.c_str(), true, 100);
    auto* requestModule = registry.registerModule<RequestHandler>();
    auto* dosProtectionModule = registry.registerModule<DoSProtectionModule>();
    auto* dbModule = registry.registerModule<DatabaseModule>(ioc, databaseStr);

    ApiProcessor apiProcessor(dbModule); //TODO: Не совсем подходит моей идеологии управления жизнью через реестр модулей. Однако это по сути обёртка

    CreateAPIHandlers(requestModule, &apiProcessor);

    CreateNewHandlers(requestModule, config.directory);

    registry.initializeAll();

    static_cast<RequestHandler*>(requestModule)->setFileCache(cacheModule);


///////////////////////////////////////////////////////////

    try {
        auto const net_address = net::ip::make_address(config.address);
        auto const net_port = static_cast<unsigned short>(config.port);
        tcp::acceptor acceptor{ ioc, {net_address, net_port} };
        std::cout << "Server started on http://" << config.address << ":" << config.port << std::endl;

        // UPDATED: Do_accept с std::function для safe recursive (avoid self-ref UB)
        std::function<void()> do_accept_func = [&acceptor, &ioc, requestModule, &do_accept_func, &dosProtectionModule]() {  // NEW: Explicit function, self-capture by ref
            auto socket = std::make_shared<tcp::socket>(ioc);
            acceptor.async_accept(*socket,
                [socket_ptr = socket, &do_accept_func, requestModule, &dosProtectionModule](beast::error_code ec) {
                    if (!ec) {
                        printConnectionInfo(*socket_ptr);
                        std::string ip = (*socket_ptr).remote_endpoint().address().to_string();
                        if (dosProtectionModule->isAllowed(ip)) {
                            std::make_shared<session>(std::move(*socket_ptr), requestModule)->run();
                        }
                        else {
                            std::cout << "[" << ip << "] Connection terminated: DoS protection triggered (rate limit exceeded)\n";
                        }
                    }
                    else {
                        std::cerr << "Accept error: " << ec.message() << std::endl;
                    }
                    do_accept_func();  // Рекурсия via function call (safe)
                });
            };

        do_accept_func();
        ioc.run();  // Блокирует, обрабатывает все async
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return 0;
}