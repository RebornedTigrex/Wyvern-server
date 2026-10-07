#pragma once

#include "RelayServer.h"
#include "LowLevelConnections.h"
#include "ConsoleIO.h"


class NodeRuntime : public std::enable_shared_from_this<NodeRuntime> {
    Wyvern::Protocol::ProtocolVisitor visitor{};

    std::shared_ptr<boost::asio::io_context> ioc;
    std::shared_ptr<Wyvern::Configuration> applicationConfig;

    std::shared_ptr<Wyvern::Protocol::MessageRouter> router;

    std::unique_ptr<Wyvern::Network::RelayConnection> relayConnection;
    std::unordered_map<std::string, std::shared_ptr<Wyvern::Network::NodeConnection>> storedNodes;

public:
    NodeRuntime(std::shared_ptr<boost::asio::io_context> ioContext)
        : ioc(ioContext),
        applicationConfig(std::make_shared<Wyvern::Configuration>())
    {
        router = std::make_shared<Wyvern::Protocol::MessageRouter>(visitor);
    }

    // Инициация подключения к удаленному пиру (Пир A)
    void connectToPeer(const std::string& remoteID){
        auto nodeCon = std::make_shared<Wyvern::Network::NodeConnection>();
        storedNodes[remoteID] = nodeCon;

    }

    void connectToRelay(const Wyvern::Endpoint& relay) {}
    void connectToRelay(const std::string& relayID) {}


private:
    // Парсинг входящего сигнала от Relay (Signaling Dispatcher)
    
};

class RuntimeAPI : public IRuntimeAPI {
    std::shared_ptr < boost::asio::io_context > ioc;

    std::shared_ptr<NodeRuntime> runtime;
public:
    RuntimeAPI(std::shared_ptr < boost::asio::io_context > ioContext,
        std::shared_ptr<NodeRuntime> nodeRuntime) : ioc(ioContext), runtime(nodeRuntime) {

    }

    void callShutdown() {
        ioc->stop();//FIXME: В данный момент я не создал никаких механизмов корректного завершения, по этому ждём обращения к убитым указателям
        //TODO: Доделать нормальный shutdown
    }

    void callConnectToRelay(const Wyvern::Endpoint& relay) override{
        boost::asio::post(*ioc, [this, relay] {
            runtime->connectToRelay(relay);
            });
    }
    void callConnectToRelay(const std::string& relayID) override {
        boost::asio::post(*ioc, [this, relayID] {
            runtime->connectToRelay(relayID);
            });
    }

    void callConnectToPeer(const std::string& remoteID) override {//Автоматическое подключение к реле и попытка подключиться к ноде
        boost::asio::post(*ioc, [this, remoteID] {
            runtime->connectToPeer(remoteID);
            });
    }
};


namespace Wyvern {
    class Runtime : public std::enable_shared_from_this<Runtime> {
        std::shared_ptr < boost::asio::io_context > ioc;
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type> relayThreadHolder;
        std::shared_ptr<RelayServer> server;
        std::shared_ptr<NodeRuntime> nodeRuntime;

        bool isNeedSetupRelay;
        bool isNeedSetupNodeRuntime;

    public:

        Runtime(int argc, char* argv []) :
            ioc(std::make_shared<boost::asio::io_context>()),
            relayThreadHolder(boost::asio::make_work_guard(*ioc)),
            isNeedSetupRelay(false),
            isNeedSetupNodeRuntime(true)
        {

            parse_arguments(argc, argv);

            setupRelay(selfEndpoint);
            setupNodeRuntime(ioc);

            auto api = std::make_shared< RuntimeAPI >(ioc, nodeRuntime);
            auto console = ConsoleIO(static_cast<std::shared_ptr<IRuntimeAPI>>(api));// Умрет в деструкторе - как и надо.

            

            ioc->run();//Не делаю ioc->stop(). Пусть умрет при SIGINT. Этот чел будет держать объект, пока я не вызову ctrl+c
        }

    private:

        void parse_arguments(int argc, char* argv []) {
            for (int i = 1; i < argc; ++i) {
                if (std::string_view(argv [i]) == "--relay") {
                    isNeedSetupRelay = true;
                    return;
                }
                else if (std::string_view(argv [i]) == "--no-node") {
                    isNeedSetupNodeRuntime = false;
                    return;
                }
            }
        }

        inline void setupRelay(std::shared_ptr<Endpoint> relayEndpoint) {

            if (isNeedSetupRelay) server = std::make_shared<RelayServer>(relayEndpoint);
        }
        inline void setupNodeRuntime(auto ioc) {
            if (isNeedSetupNodeRuntime) nodeRuntime = std::make_shared<NodeRuntime>(ioc);
        }
    };
}
