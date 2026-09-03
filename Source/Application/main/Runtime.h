#pragma once

#include "rtc/rtc.hpp"
#include "Boost/asio.hpp"

#include <memory>
#include <iostream>
#include <functional>
#include <map>
#include <queue>
#include "boost/json.hpp"

#include <cmath>

#include<csignal>

namespace Wyvern {
    class Runtime {
        std::shared_ptr < boost::asio::io_context > ioc;
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type> relayThreadHolder;
        std::unique_ptr<RelayServer> server;
        std::unique_ptr<NodeRuntime> nodeRuntime;

        bool isNeedSetupRelay;
        bool isNeedSetupNodeRuntime;

    public:

        Runtime(int argc, char* argv[]) : 
            ioc(std::make_shared<boost::asio::io_context>()), 
            relayThreadHolder(boost::asio::make_work_guard(*ioc)), 
            isNeedSetupRelay(false),
            isNeedSetupNodeRuntime(true)
        {
            parse_arguments(argc, argv);

            setupRelay(9005);
            setupNodeRuntime();

            ioc->run();//Не делаю ioc->stop(). Пусть умрет при SIGINT
        }

    private:

        void parse_arguments(int argc, char* argv[]) {
            for (int i = 1; i < argc; ++i) {
                if (std::string_view(argv[i]) == "--relay") {
                    isNeedSetupRelay = true;
                    return;
                }
                else if (std::string_view(argv[i]) == "--no-node") {
                    isNeedSetupNodeRuntime = false;
                    return;
                }
            }
        }

        inline void setupRelay(uint16_t port) {
            if (isNeedSetupRelay) server = std::make_unique<RelayServer> (port);
        }
        inline void setupNodeRuntime() {
            if (isNeedSetupNodeRuntime) nodeRuntime = std::make_unique<NodeRuntime>();
        }
    };
}

namespace Utilities {
    static inline std::string normalize_path(std::string p) {
        if (!p.empty() && p.front() == '/') p.erase(p.begin());
        return p;
    }
};


enum class NodeActivityStatus {
    Online,
    Offline
};

struct Node {
    NodeActivityStatus status;

    std::shared_ptr<rtc::WebSocket> connection;
    std::deque<boost::json::object> pendingMessages;
};

using Server = rtc::WebSocketServer;


class RelayServer {

    std::optional<Server> serverInstance;

	std::unordered_map<std::string ,Node> storedNodes;

	bool storeDelayedMessage(Node, boost::json::object);
	bool changeNodeActivity(Node, NodeActivityStatus);

	bool isNodeActive(Node);

public:
	explicit RelayServer(uint16_t port) {
		rtc::WebSocketServer::Configuration cfg;
		cfg.port = port;
		cfg.bindAddress = "0.0.0.0";
		cfg.enableTls = false;
		cfg.maxMessageSize = 256 * 1024; // SDP + trickle ICE спокойно влезут

		serverInstance.emplace(std::move(cfg));
		serverInstance->onClient([this](std::shared_ptr<rtc::WebSocket> ws) {
			onIncoming(std::move(ws));
			});
	}

	bool setupServer(boost::asio::ip::address endpoint);
	void serverWorker();

	void onReqest(std::function<void>);

private:
    void onIncoming(std::shared_ptr<rtc::WebSocket> ws) {//TODO: Вынести в cpp, проверить код
        // handshake ещё не закончен
        ws->onOpen([this, ws] {
            const std::string id = Utilities::normalize_path(ws->path().value_or(""));
            if (id.empty()) {
                ws->close();
                return;
            }
            {
                std::lock_guard lk{ mtx_ };
                if (auto it = storedNodes.find(id); it != storedNodes.end())
                    it->second.connection->close();//FIXME: Дедлок? Если да - вынести в pendingClose
                storedNodes[id].connection = ws;
            }
            });

        ws->onMessage([this, ws](rtc::message_variant msg) {
            if (!std::holds_alternative<rtc::string>(msg))
                return; // сигналинг — текст
            route(ws, std::get<rtc::string>(std::move(msg)));
            });

        ws->onClosed([this, ws] {
            std::lock_guard lk{ mtx_ };
            for (auto it = storedNodes.begin(); it != storedNodes.end(); ) {
                if (it->second.connection == ws) it = storedNodes.erase(it);
                else ++it;
            }
            });

        ws->onError([](std::string e) {
            // лог
            });
    }

    void route(const std::shared_ptr<rtc::WebSocket>& from, std::string body) {
        boost::json::value j;
        try {
            j = boost::json::parse(body);
        }
        catch (...) {
            return;
        }
        auto* obj = j.if_object();
        if (!obj) return;

        auto* to_v = obj->if_contains("to");
        if (!to_v || !to_v->is_string()) return;
        const std::string to{ to_v->as_string() };

        std::shared_ptr<rtc::WebSocket> dest;
        {
            std::lock_guard lk{ mtx_ };
            auto it = storedNodes.find(to);
            if (it == storedNodes.end()) return;
            dest = it->second.connection;
        }
        dest->send(std::move(body));
    }

    std::mutex mtx_;
    std::deque<std::shared_ptr<rtc::WebSocket>> pendingCloseConnection;//TODO: Посмотреть: а надо ли
};

enum class RelaySpecific {
    Closest,
    Fastest
};
enum class P2PConnectionType{
    PreferRelay,
    PreferDirect
};

namespace Wyvern {
    class Configuration {
    public:
        RelaySpecific getPreferRelaySpecific();
        P2PConnectionType getPreferConnectionType();
        
        int getRelayTimeoutMS() { return 500; } // TODO: заглушка
        int getMaxReconnectAttempts() { return 5; }

    };

    class ConnectionInformationService {//Работает в отдельном потоке и по кд опрашивает всё, что может понадобиться нам (Подключения, доступность, имена)
    public:


        std::vector<std::string> getRelayNames();//TODO:Уточнить цель функции и изменить имя
        std::string chooseRelayBy(RelaySpecific);
        std::string chooseNodeType();

        std::vector < std::string> requestKnownNodesFromRelay();
    };
}


class NodeConnection {

	rtc::Configuration config;

	std::shared_ptr<rtc::PeerConnection> pc;
	std::shared_ptr<rtc::DataChannel> dc;




	void loop();

public:
    NodeConnection() :
            pc(std::make_shared<rtc::PeerConnection>(config))
    {};

    inline void connect() {
        pc->createDataChannel("dataChannelLabel");//TODO: заглушка названия
    }

};

class NodeRuntime {//Сюда писать бизнес логику?

    std::shared_ptr<Wyvern::Configuration> applicationConfig;
    std::shared_ptr<Wyvern::ConnectionInformationService> InformationService;

    std::shared_ptr<boost::asio::io_context> ioc;
    boost::asio::steady_timer timeoutTimer;

    std::shared_ptr<rtc::WebSocket> connection;

    std::unordered_map<std::string, std::shared_ptr<NodeConnection>> storedNodes;

    bool stopping = false;// Вроде как бестолковая вещь, а вроде как и полезная


    std::shared_ptr<std::promise<void>> relayUp;
    std::shared_future<void> relayReady;

    void setupCallbacks() {
        connection->onOpen([this] {
            boost::asio::post(*ioc, [this] {
                isConnectedToRelay = true;
                timeoutTimer.cancel();
                if (relayUp) relayUp->set_value();
                });

            });
        connection->onClosed([this] {
            boost::asio::post(*ioc, [this] {
                isConnectedToRelay = false;
                });
            });
        //connection->onError([this](std::string) {
        //    boost::asio::post(*ioc, [this] { failOrRetry(); });
        //    });
        //connection->onClosed([this] {
        //    boost::asio::post(*ioc, [this] {
        //        relayOpen = false;
        //        failOrRetry();
        //        });
        //    });
        //connection->onMessage([this](rtc::message_variant msg) {
        //    if (!std::holds_alternative<rtc::string>(msg)) return;
        //    auto body = std::get<rtc::string>(std::move(msg));
        //    boost::asio::post(*ioc, [this, body = std::move(body)] { onSignal(body); });
        //    });
    }

    //void failOrRetry() {
    //    //TODO: Что-то?
    //}

    //void onSignal(std::string body) {
    //    //TODO: Что-то?
    //}

    std::string getSelfID() {
        static std::string localID = "SOME-KIND-OF-ID";//TODO: Заглушка
        return localID;
    }

    bool isConnectedToRelay = false;

    

public:
    NodeRuntime(std::shared_ptr<boost::asio::io_context> ioContext) :
        connection(std::make_shared<rtc::WebSocket>()),
        ioc(ioContext),
        timeoutTimer(*ioc, boost::asio::chrono::seconds(applicationConfig->getRelayTimeoutMS())), 
        isConnectedToRelay(false)

    {
        setupCallbacks();
    };

    void stopConnection() {// Простая штука, чтоб отменить подключение и не ждать таймаут
        stopping = true;
        timeoutTimer.cancel();
    }

    //Делаем реле подключение отдельно
    void requestConnectToRelay() {//TODO: В private? 
        connection->open("ws://" + InformationService->chooseRelayBy(applicationConfig->getPreferRelaySpecific()) + "/" + getSelfID());

        timeoutTimer.expires_after(std::chrono::seconds(
            applicationConfig->getRelayTimeoutMS()));
        timeoutTimer.async_wait([this](boost::system::error_code ec) {
            if (ec || stopping) return;          // cancel
            if (!connection->isOpen()) {
                connection->close();
                scheduleReconnect();
            }
            });
    }

    std::function<void()> connectAttemptCallback;//TODO: В private
    std::function<void()> reconnectFailedCallback;//TODO: В private


    void onReconnectAttempt(std::function<void()> callback = NULL) {
        connectAttemptCallback = std::move(callback);
    }

    void scheduleReconnect() {
        static int attempt = 0;

        if (connectAttemptCallback) connectAttemptCallback();
        printf("Попытка подключиться...\n");// TODO: заглушка

        if (stopping) return;
        if (reconnectFailedCallback && attempt >= applicationConfig->getMaxReconnectAttempts()) {
            reconnectFailedCallback();
            return;
        }
        if (connectAttemptCallback) connectAttemptCallback();

        auto delay = std::chrono::milliseconds(500 * (1 << std::min(attempt, 5)));
        ++attempt;
        timeoutTimer.expires_after(delay);
        timeoutTimer.async_wait([this](boost::system::error_code ec) {
            if (!ec) requestConnectToRelay();
            });
    }

    void onReconnectFailed(std::function<void()> callback = NULL) {
        reconnectFailedCallback = std::move(callback);
    }


    bool requestDirectConnection(std::string remoteID, std::string remoteAddres, std::string port) {//TODO: А надо ли вообще это?
        connection->open("ws://" + remoteAddres + port + "/" + remoteID);// Прокидывать конфиг сразу без каких-то значений?
        return connection->isOpen();
    }


    void requestInfoAboutRelay() {};//Запрос ближайшего известного relay у подключенных пиров. (А надо ли это? Есть ли такая ситуация, когда подключение есть, а реле нет?)


    bool requestConnectionToNode(std::string remoteID) {

        //TODO: Сделать какой-то await для функции ниже
        if (!isConnectedToRelay) {
            requestConnectToRelay();// Наверное его надо всё таки сделать на async_await с каким-то повторением. Как это сделать и надо ли?
        }
        //TODO: Что-то здесь должно происходить. Какое-то ожидание?

        static int num = 0;
        
        std::string someName = "SOME-ID-" + num++;//TODO: Заглушка
        auto nodeCon = std::make_shared<NodeConnection>();//Тут начинается проставление колбеков

        if (true) {
            storedNodes.insert(std::make_pair(someName, nodeCon));

            nodeCon->connect();// Тут тоже должен быть какой-то механизм с реконнектом и таймаутом. Как его сделать?
        }
        
        

    }
};