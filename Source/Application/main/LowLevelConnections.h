#include "rtc/rtc.hpp"
#include "Boost/asio.hpp"

#include <memory>
#include <iostream>
#include <functional>
#include <deque>
#include "boost/json.hpp"


class IRelayConnection {
    virtual void connect();
public:
    IRelayConnection() = default;

    virtual void disconnect();

    virtual std::optional<std::vector<std::string>> getKnownIDs();
};


class RelayConnection {
    std::shared_ptr<boost::asio::io_context> ioc;

    std::shared_ptr<rtc::WebSocket> connection;

    bool isConnectedToRelay = false;


    std::shared_ptr<Wyvern::Configuration> applicationConfig;

    void setupCallbacks() {
        connection->onOpen([this] {
            boost::asio::post(*ioc, [this] {
                isConnectedToRelay = true;
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
        std::shared_ptr<Wyvern::Configuration> config
    )
        :
        applicationConfig(config),
        connection(std::make_shared<rtc::WebSocket>()),
        ioc(ioContext),
        isConnectedToRelay(false)
    {
        setupCallbacks();
    }


    //Делаем реле подключение отдельно
    void requestConnectToRelay() {//TODO: В private?
        if (!isConnectedToRelay)
            connection->open("ws://" + Wyvern::Utilities::getRelayBy(applicationConfig->getPreferRelaySpecific())->host + "/" + Wyvern::Utilities::getSelfID());
    }


    void requestInfoAboutRelay() {};//Запрос ближайшего известного relay у подключенных пиров. (А надо ли это? Есть ли такая ситуация, когда подключение есть, а реле нет?)

public:
    void sendSignal(const std::string& toNodeId, boost::json::object msg) {
        if (!connection || !connection->isOpen()) return;

        msg ["from"] = Wyvern::Utilities::getSelfID();
        msg ["to"] = toNodeId;

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
    NodeConnection() : pc(std::make_shared<rtc::PeerConnection>(config)) {}


    // Обработка удаленного SDP (Offer или Answer)
    void handleRemoteDescription(const std::string& type, const std::string& sdp) {
        pc->setRemoteDescription(rtc::Description(sdp, type));
    }

    // Обработка удаленного ICE кандидата
    void handleRemoteCandidate(const std::string& candidate, const std::string& mid) {
        pc->addRemoteCandidate(rtc::Candidate(candidate, mid));
    }

};