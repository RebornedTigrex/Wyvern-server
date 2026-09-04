#pragma once

#include "rtc/rtc.hpp"
#include "Boost/asio.hpp"

#include <memory>
#include <iostream>
#include <functional>
#include <deque>
#include "boost/json.hpp"

#include <random>

class RelayServer;
class NodeRuntime;

namespace Wyvern {
    class Runtime : public std::enable_shared_from_this<Runtime> {
        std::shared_ptr < boost::asio::io_context > ioc;
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type> relayThreadHolder;
        std::shared_ptr<RelayServer> server;
        std::shared_ptr<NodeRuntime> nodeRuntime;

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
            setupNodeRuntime(ioc);

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

            if (isNeedSetupRelay) server = std::make_shared<RelayServer> (port);
        }
        inline void setupNodeRuntime(auto ioc) {
            if (isNeedSetupNodeRuntime) nodeRuntime = std::make_shared<NodeRuntime>(ioc);
        }
    };
}

namespace Wyvern::Utilities {
    static inline std::string normalize_path(std::string p) {
        if (!p.empty() && p.front() == '/') p.erase(p.begin());
        return p;
    }

    static inline std::string generateRandNumSeq(int seqSize) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<int> digit(0, 9);
        static std::string s;

        s = "";

        for (int i = 0; i < seqSize; i++) {
            s += char(digit(gen));
        }

        return s;
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

	bool storeDelayedMessage(Node, boost::json::object) { return true; };
	bool changeNodeActivity(Node, NodeActivityStatus) { return true; };

    bool isNodeActive(Node) { return true; };

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

private:
    void onIncoming(std::shared_ptr<rtc::WebSocket> ws) {//TODO: Вынести в cpp, проверить код
        // handshake ещё не закончен
        ws->onOpen([this, ws] {
            const std::string id = Wyvern::Utilities::normalize_path(ws->path().value_or(""));
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

        ws->onClosed([this, ws] {//TODO: Вынести это в отдельную логику?
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
        Configuration() {};

        RelaySpecific getPreferRelaySpecific() { return RelaySpecific::Closest; };
        P2PConnectionType getPreferConnectionType() { return P2PConnectionType::PreferRelay; };
        
        int getRelayTimeoutMS() { return 500; } // TODO: заглушка
        int getMaxReconnectAttempts() { return 5; }

    };

    class ConnectionInformationService {//Работает в отдельном потоке и по кд опрашивает всё, что может понадобиться нам (Подключения, доступность, имена)
    public:


        std::string getSelfID() {
            static std::string localID = "SOME-KIND-OF-ID-" + Wyvern::Utilities::generateRandNumSeq(4);//TODO: Заглушка
            return localID;
        }

        //std::vector<std::string> getRelayNames();//TODO:Уточнить цель функции и изменить имя
        std::string getRelayBy(RelaySpecific) { return "0.0.0.0"; };//Выбор ссылки не реле по какому-то признаку
        std::string getNodeType() { return ""; };//Надо уточнить

        std::vector < std::string> requestKnownNodesFromRelay() { return std::vector<std::string> {}; };
    };
}

class RelayConnection {
    std::shared_ptr<boost::asio::io_context> ioc;
    //boost::asio::steady_timer timeoutTimer; // TODO: На будущее для реконекта

    std::shared_ptr<rtc::WebSocket> connection;

    bool isConnectedToRelay = false;


    std::shared_ptr<Wyvern::Configuration> applicationConfig;
    std::shared_ptr <Wyvern::ConnectionInformationService> InformationService;


    void setupCallbacks() {
        connection->onOpen([this] {
            boost::asio::post(*ioc, [this] {
                isConnectedToRelay = true;
                //timeoutTimer.cancel();
                });

            });
        connection->onClosed([this] {
            boost::asio::post(*ioc, [this] {
                isConnectedToRelay = false;
                });
            });
        connection->onMessage([this](rtc::message_variant msg) {
            if (!std::holds_alternative<rtc::string>(msg)) return;
            auto body = std::get<rtc::string>(std::move(msg));
            boost::asio::post(*ioc, [this, body = std::move(body)]
                {
                    onSignal(body);
                });
            });
    }

public:
    RelayConnection(
        std::shared_ptr<boost::asio::io_context> ioContext,
        std::shared_ptr<Wyvern::Configuration> config, 
        std::shared_ptr <Wyvern::ConnectionInformationService> infoService
    ) 
        :
        InformationService(infoService),
        applicationConfig(config),
        connection(std::make_shared<rtc::WebSocket>()),
        ioc(ioContext),
        //timeoutTimer(*ioc, boost::asio::chrono::mileseconds(applicationConfig->getRelayTimeoutMS())),
        isConnectedToRelay(false) 
    {
        setupCallbacks();
    }


    //Делаем реле подключение отдельно
    void requestConnectToRelay() {//TODO: В private?
        if (!isConnectedToRelay)
            connection->open("ws://" + InformationService->getRelayBy(applicationConfig->getPreferRelaySpecific()) + "/" + InformationService->getSelfID());
    }


    void requestInfoAboutRelay() {};//Запрос ближайшего известного relay у подключенных пиров. (А надо ли это? Есть ли такая ситуация, когда подключение есть, а реле нет?)

public:
    void sendSignal(const std::string& toNodeId, boost::json::object msg) {
        if (!connection || !connection->isOpen()) return;

        msg["from"] = InformationService->getSelfID();
        msg["to"] = toNodeId;

        std::string serialized = boost::json::serialize(msg);
        connection->send(serialized);
    }

    // Передаем incoming сообщения наверх в NodeRuntime
    void setSignalCallback(std::function<void(const std::string&)> cb) {
        onSignalCb = std::move(cb);
    }

private:
    std::function<void(const std::string&)> onSignalCb;

    void onSignal(std::string body) {
        if (onSignalCb) onSignalCb(body);
    }
};





class NodeConnection : public std::enable_shared_from_this<NodeConnection> {
    rtc::Configuration config;
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::DataChannel> dc;

    inline std::string generateName() {
        return "TEST-NAME-" + Wyvern::Utilities::generateRandNumSeq(4);
    }

public:
    NodeConnection() : pc(std::make_shared<rtc::PeerConnection>(config)) 
    {

    }


    // Инициатор (Peer A): создает offer
    void initAsOffer(std::function<void(const boost::json::object&)> sendSignalCb) {
        setupCallbacks(sendSignalCb);
        dc = pc->createDataChannel(generateName());
        setupChannelCallbacks(dc);
    }

    // Принимающий (Peer B): ожидает offer и создает answer
    void initAsAnswer(std::function<void(const boost::json::object&)> sendSignalCb) {
        setupCallbacks(sendSignalCb);
        pc->onDataChannel([this](std::shared_ptr<rtc::DataChannel> incomingDc) {
            dc = incomingDc;
            setupChannelCallbacks(dc);
            });
    }

    // Обработка удаленного SDP (Offer или Answer)
    void handleRemoteDescription(const std::string& type, const std::string& sdp) {
        pc->setRemoteDescription(rtc::Description(sdp, type));
    }

    // Обработка удаленного ICE кандидата
    void handleRemoteCandidate(const std::string& candidate, const std::string& mid) {
        pc->addRemoteCandidate(rtc::Candidate(candidate, mid));
    }

private:
    void setupCallbacks(std::function<void(const boost::json::object&)> sendSignalCb) {
        // Локальное описания SDP готов (создан offer или answer)
        pc->onLocalDescription([sendSignalCb](rtc::Description desc) {
            boost::json::object payload;
            payload["sdp"] = std::string(desc);
            payload["type"] = desc.typeString();

            boost::json::object msg;
            msg["type"] = desc.typeString(); // "offer" или "answer"
            msg["payload"] = payload;

            sendSignalCb(msg);
            });

        // Найден новый локальный ICE кандидат (Trickle ICE)
        pc->onLocalCandidate([sendSignalCb](rtc::Candidate cand) {
            boost::json::object payload;
            payload["candidate"] = cand.candidate();
            payload["mid"] = cand.mid();

            boost::json::object msg;
            msg["type"] = "candidate";
            msg["payload"] = payload;

            sendSignalCb(msg);
            });
    }

    void setupChannelCallbacks(std::shared_ptr<rtc::DataChannel> channel) {
        channel->onOpen([]() {
            std::cout << "[P2P] DataChannel успешно открыт!" << std::endl;
            });

        channel->onMessage([](rtc::message_variant msg) {
            if (std::holds_alternative<rtc::string>(msg)) {
                std::cout << "[P2P Msg] " << std::get<rtc::string>(msg) << std::endl;
            }
            });
    }
};

class NodeRuntime : public std::enable_shared_from_this<NodeRuntime> {
    std::shared_ptr<boost::asio::io_context> ioc;
    std::shared_ptr<Wyvern::Configuration> applicationConfig;
    std::shared_ptr<Wyvern::ConnectionInformationService> InformationService;

    std::unique_ptr<RelayConnection> relayConnection;
    std::unordered_map<std::string, std::shared_ptr<NodeConnection>> storedNodes;

public:
    NodeRuntime(std::shared_ptr<boost::asio::io_context> ioContext)
        : ioc(ioContext),
        InformationService(std::make_shared<Wyvern::ConnectionInformationService>()),
        applicationConfig(std::make_shared<Wyvern::Configuration>())
    {
        relayConnection = std::make_unique<RelayConnection>(ioc, applicationConfig, InformationService);
        relayConnection->setSignalCallback([this](const std::string& body) {
            onSignal(body);
            });
        relayConnection->requestConnectToRelay();
    }

    // Инициация подключения к удаленному пиру (Пир A)
    void connectToPeer(const std::string& remoteID) {
        auto nodeCon = std::make_shared<NodeConnection>();
        storedNodes[remoteID] = nodeCon;

        nodeCon->initAsOffer([this, remoteID](const boost::json::object& signalMsg) {
            relayConnection->sendSignal(remoteID, signalMsg);
            });
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

                nodeCon->initAsAnswer([this, from](const boost::json::object& signalMsg) {
                    relayConnection->sendSignal(from, signalMsg);
                    });
            }
            else {
                return; // Игнорируем кандидаты/answer от неизвестных инициаторов
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