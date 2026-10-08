#pragma once

#include <RelayServer.h>
#include <LowLevelConnections.h>
#include <ConsoleIO.h>
#include <IdentityStore.h>
#include <ApplicationEvents.h>
#include <ChatDirectory.h>

#include <filesystem>
#include <optional>

class NodeRuntime {

    boost::asio::io_context& ioc;
    std::shared_ptr<Wyvern::ApplicationEvents> events;
    Wyvern::ChatDirectory chats;

    Wyvern::Protocol::MessageRouter<Wyvern::Protocol::ProtocolVisitor> router;

    std::unique_ptr<Wyvern::Network::RelayConnection> relayConnection;
    std::unordered_map<std::string, std::shared_ptr<Wyvern::Network::NodeConnection>> storedNodes;
    std::optional<Wyvern::Endpoint> lastRelay;

public:
    NodeRuntime(boost::asio::io_context& ioContext,
        std::shared_ptr<Wyvern::ApplicationEvents> bus)
        : ioc(ioContext)
        , events(std::move(bus))
        , chats(*events)
    {
        relayConnection = std::make_unique<Wyvern::Network::RelayConnection>(handleLambda);
        relayConnection->setOnOpenCallback([this] {
            boost::asio::post(ioc, [this] {
                if (lastRelay)
                    events->relayOpen(*lastRelay);
                });
            });
        relayConnection->setOnErrorCallback([this](std::string reason) {
            boost::asio::post(ioc, [this, reason = std::move(reason)] {
                events->relayFailed(reason);
                });
            });
    }

    // Инициация подключения к удаленному пиру (Пир A). Чат = id пира.
    void connectToPeer(const std::string& remoteID){
        auto nodeCon = std::make_shared<Wyvern::Network::NodeConnection>();
        storedNodes[remoteID] = nodeCon;
        events->peerDialed(remoteID);
        chats.ensure(remoteID);
    }

    void connectToRelay(const Wyvern::Endpoint& relay) {
        lastRelay = relay;
        events->relayDialed(relay);
        try {
            relayConnection->connect(relay);
        }
        catch (const std::exception& e) {
            events->relayFailed(e.what());
        }
    }
    void connectToRelay(const std::string& relayID) {
        //TODO: резолв id реле в endpoint
        (void)relayID;
    }

    void publishSnapshot() {
        chats.publishSnapshot();
    }

    // Шов для DataChannel, когда он начнёт отдавать текст.
    void notePeerMessage(std::string peerId, std::string body) {
        chats.append(Wyvern::ChatMessage{
            .peerId = std::move(peerId),
            .body = std::move(body),
            .timestampMs = Wyvern::Utilities::NowMs(),
            });
    }


private:
    std::function<void(std::shared_ptr<rtc::WebSocket>&,
        std::vector<std::byte>)> handleLambda{
        [this](std::shared_ptr < rtc::WebSocket >& ws, std::vector<std::byte> msg) {router.route(ws, msg); }
    };
    
};

class RuntimeAPI : public IRuntimeAPI {
    boost::asio::io_context& ioc;

    std::shared_ptr<NodeRuntime> runtime;
public:
    RuntimeAPI(boost::asio::io_context& ioContext,
        std::shared_ptr<NodeRuntime> nodeRuntime) : ioc(ioContext), runtime(nodeRuntime) {

    }

    void callShutdown() {
        ioc.stop();//FIXME: В данный момент я не создал никаких механизмов корректного завершения, по этому ждём обращения к убитым указателям
        //TODO: Доделать нормальный shutdown
    }

    void callConnectToRelay(const Wyvern::Endpoint& relay) override{
        boost::asio::post(ioc, [this, relay] {
            runtime->connectToRelay(relay);
            });
    }
    void callConnectToRelay(const std::string& relayID) override {
        boost::asio::post(ioc, [this, relayID] {
            runtime->connectToRelay(relayID);
            });
    }

    void callConnectToPeer(const std::string& remoteID) override {//Автоматическое подключение к реле и попытка подключиться к ноде
        boost::asio::post(ioc, [this, remoteID] {
            runtime->connectToPeer(remoteID);
            });
    }

    void callPublishSnapshot() override {
        boost::asio::post(ioc, [this] {
            runtime->publishSnapshot();
            });
    }
};


namespace Wyvern {
    class Runtime : public std::enable_shared_from_this<Runtime> {
        std::unique_ptr < boost::asio::io_context > ioc;
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type> relayThreadHolder;

        std::unique_ptr<RelayServer> server;
        std::shared_ptr<NodeRuntime> nodeRuntime;
        std::shared_ptr<Wyvern::ApplicationEvents> events;

        std::filesystem::path dataDir;
        std::shared_ptr<Storage::IRecordStore> records;
        std::shared_ptr<IdentityEntity> idEntity;


        bool isNeedSetupRelay;
        bool isNeedSetupNodeRuntime;

    public:

        Runtime(int argc, char* argv []) :
            ioc(std::make_unique<boost::asio::io_context>()),
            relayThreadHolder(boost::asio::make_work_guard(*ioc)),
            dataDir("WyvernData"),
            isNeedSetupRelay(false),
            isNeedSetupNodeRuntime(true)
        {
            parse_arguments(argc, argv);
            mainRuntimeFunc();
        }

    private:

        void mainRuntimeFunc() {

            records = std::make_shared<Storage::FileRecordStore>(dataDir);
            idEntity = IdentityStore(*records).loadOrCreate();
            Identity::publish(idEntity);
            events = std::make_shared<Wyvern::ApplicationEvents>();

            setupRelay(selfEndpoint);
            setupNodeRuntime(*ioc);

            auto api = std::make_shared< RuntimeAPI >(*ioc, nodeRuntime);
            auto console = ConsoleIO(
                static_cast<std::shared_ptr<IRuntimeAPI>>(api),
                events,
                Identity::getSelfID());// Умрет в деструкторе - как и надо.


            ioc->run();//Не делаю ioc->stop(). Пусть умрет при SIGINT. Этот чел будет держать объект, пока я не вызову ctrl+c

        }

        void parse_arguments(int argc, char* argv []) {
            for (int i = 1; i < argc; ++i) {
                if (std::string_view(argv [i]) == "--relay") {
                    isNeedSetupRelay = true;
                }
                else if (std::string_view(argv [i]) == "--no-node") {
                    isNeedSetupNodeRuntime = false;
                }
                else if (std::string_view(argv[i]) == "--data-dir" && i + 1 < argc) {
                    dataDir = argv[++i];
                }

            }
        }

        inline void setupRelay(const Endpoint& relayEndpoint) {

            if (isNeedSetupRelay) server = std::make_unique<RelayServer>(relayEndpoint);
        }
        inline void setupNodeRuntime(boost::asio::io_context& ioc) {
            if (isNeedSetupNodeRuntime) nodeRuntime = std::make_shared<NodeRuntime>(ioc, events);
        }
    };
}
