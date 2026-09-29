#pragma once

#include "RelayServer.h"
#include "LowLevelConnections.h"



class NodeRuntime : public std::enable_shared_from_this<NodeRuntime> {
    std::shared_ptr<boost::asio::io_context> ioc;
    std::shared_ptr<Wyvern::Configuration> applicationConfig;

    std::unique_ptr<RelayConnection> relayConnection;
    std::unordered_map<std::string, std::shared_ptr<NodeConnection>> storedNodes;

public:
    NodeRuntime(std::shared_ptr<boost::asio::io_context> ioContext)
        : ioc(ioContext),
        applicationConfig(std::make_shared<Wyvern::Configuration>())
    {
        //relayConnection->setSignalCallback([this](const std::string& body) {
        //    onSignal(body);
        //    });
        //relayConnection->requestConnectToRelay();
    }

    // Инициация подключения к удаленному пиру (Пир A)
    void connectToPeer(const std::string& remoteID) {
        auto nodeCon = std::make_shared<NodeConnection>();
        storedNodes[remoteID] = nodeCon;

    }

private:
    // Парсинг входящего сигнала от Relay (Signaling Dispatcher)
    void onSignal(const std::string& body) {
        boost::system::error_code ec;
        auto jv = boost::json::parse(body, ec);
        if (ec || !jv.is_object()) return;

        const auto& obj = jv.as_object();
        std::string from = obj.at("from").as_string().c_str();
        std::string type = obj.at("type").as_string().c_str();
        const auto& payload = obj.at("payload").as_object();

        // 1. Если это Offer, а у нас нет этого пира — создаем Responder (Пир B)
        if (storedNodes.find(from) == storedNodes.end()) {
            if (type == "offer") {
                auto nodeCon = std::make_shared<NodeConnection>();
                storedNodes[from] = nodeCon;

                
            }
            else {
                return; // Пока игнорируем кандидаты/answer от неизвестных инициаторов
            }
        }

        auto nodeCon = storedNodes[from];

        // 2. Диспетчеризация по типам
        if (type == "offer" || type == "answer") {//TODO: Нужна более строгая типизация
            std::string sdp = payload.at("sdp").as_string().c_str();
            nodeCon->handleRemoteDescription(type, sdp);
        }
        else if (type == "candidate") {
            std::string cand = payload.at("candidate").as_string().c_str();
            std::string mid = payload.at("mid").as_string().c_str();
            nodeCon->handleRemoteCandidate(cand, mid);
        }
    }
};

class RuntimeAPI {
    std::shared_ptr < boost::asio::io_context > ioc;

    std::shared_ptr<NodeRuntime> runtime;
public:
    RuntimeAPI(std::shared_ptr < boost::asio::io_context > ioContext, std::shared_ptr<NodeRuntime> nodeRuntime) : ioc(ioContext), runtime(nodeRuntime) {

    }

    void callShutdown() {
        ioc->stop();//FIXME: В данный момент я не создал никаких механизмов корректного завершения, по этому ждём обращения к убитым указателям
        //TODO: Доделать нормальный shutdown
    }

    void callConnectToPeer(const std::string& remoteID) {//Автоматическое подключение к реле и попытка подключиться к ноде
        boost::asio::post(*ioc, [this, remoteID] {
            runtime->connectToPeer(remoteID);
            });
    }
};

class ConsoleIO {
    std::shared_ptr<RuntimeAPI> runtimeAPI;
    std::jthread consoleThread;

public:
    explicit ConsoleIO(std::shared_ptr<RuntimeAPI> api)
        : runtimeAPI(std::move(api))
    {
        consoleThread = std::jthread([this](std::stop_token st) {
            lineParseLoop(st);
            });
    }

    ~ConsoleIO() {
        if (consoleThread.joinable()) {
            consoleThread.request_stop();
        }
    }

private:
    void lineParseLoop(std::stop_token st) {
        std::string line;

        while (!st.stop_requested()) {
            if (!std::getline(std::cin, line)) {
                break; // EOF / cin закрыт
            }
            parseLine(line);
        }
    }

    static std::vector<std::string> tokenize(std::string_view line) {
        std::vector<std::string> tokens;
        std::istringstream iss{ std::string{line} };
        std::string tok;
        while (iss >> tok) {
            tokens.push_back(std::move(tok));
        }
        return tokens;
    }

    void parseLine(std::string_view line) {
        auto args = tokenize(line);
        if (args.empty()) {
            return;
        }

        const std::string& cmd = args [0];

        if (cmd == "connect") {
            handleConnect(args);
        }
        else if (cmd == "disconnect") {
            handleDisconnect(args);
        }
        else if (cmd == "help") {
            printHelp();
        }
        else if (cmd == "quit" || cmd == "exit") {
            runtimeAPI->callShutdown();
            consoleThread.request_stop();
        }
        else {
            std::cerr << "Unknown command: " << cmd << "\n"
                << "Type 'help' for list of commands.\n";
        }
    }

    void handleConnect(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            std::cerr << "Usage: connect <peer>\n";
            return;
        }
        runtimeAPI->callConnectToPeer(args [1]);
    }

    void handleDisconnect(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printWIPmsg();
            //std::cerr << "Usage: disconnect <peer>\n";
            return;
        }
        //runtimeAPI->callDisconnectFromPeer(args [1]);
    }

    static void printHelp() {
        std::cout
            << "Commands:\n"
            << "  connect <peer>      Connect to peer\n"
            << "  disconnect <peer>   Disconnect from peer [WIP]\n"
            << "  status              Show current status [WIP]\n"
            << "  help                This message\n"
            << "  quit | exit         Stop runtime [WIP]\n";
    }
    void printWIPmsg() {
        printf("WIP\n");
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
            auto console = ConsoleIO(api);// Умрет в деструкторе - как и надо.

            

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
