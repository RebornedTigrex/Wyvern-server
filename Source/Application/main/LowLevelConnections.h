#pragma once

#include "rtc/rtc.hpp"
#include "Boost/asio.hpp"

#include <memory>
#include <iostream>
#include <functional>
#include <deque>
#include "boost/json.hpp"

#include "SupportUtils.h"

namespace Wyvern::Protocol {

    enum class MsgType {
        Init,
        InitAck,
        Publish,
        Ack,
        Nack,
        Error,
    };

    struct Envelope {
        MsgType type;                           // Тип пакета
        std::uint64_t seq;                      // Порядковый номер пакета
        std::optional<std::uint64_t> reply_to;  // Ответ на порядковый номер выходящего пакета
        std::string node_id;                    // 
        std::string relay_id;
        std::int64_t timestamp_ms;
    };

    boost::json::object serialize(Envelope createdMsg) {
        boost::json::object returnObj{
            {"type", createdMsg.type},
            {"seq", createdMsg.seq},
            {"reply_to", createdMsg.reply_to},
            {"node_id", createdMsg.node_id},
            {"relay_id", createdMsg.relay_id},
            {"timestamp_ms", createdMsg.timestamp_ms}
        };

        return returnObj;
    }
}

class RelaySequence{
    static Wyvern::Utilities::ThreadSafeCounter sequenceNumber;

public:
    void initSeq(std::shared_ptr<rtc::WebSocket> connection) {
        Wyvern::Protocol::Envelope msg{ Wyvern::Protocol::MsgType::Init, sequenceNumber.newNum(), std::nullopt,};


        //connection->send();
    }


    
};

class IRelayConnection {
public:
    IRelayConnection() = default;

    virtual void connect(std::shared_ptr<Wyvern::Endpoint> endpoint) = 0; //Присоединиться к конкретному реле и начать диалог
    virtual void disconnect() = 0;

    virtual void sendSignal(std::function<void(std::shared_ptr<rtc::WebSocket>)> signalSequence) = 0;
    virtual void sendICEseqSignal() = 0;

    virtual std::optional<std::vector<std::string>> getKnownIDs() = 0;
};


class RelayConnection : IRelayConnection{
    std::shared_ptr<boost::asio::io_context> ioc;

    std::shared_ptr<rtc::WebSocket> connection;
    std::shared_ptr<Wyvern::Configuration> applicationConfig;

    void setupCallbacks() {
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
        ioc(ioContext)
    {
        setupCallbacks();
    }


    //Делаем реле подключение отдельно
    void connect(std::shared_ptr<Wyvern::Endpoint> endpoint) override {//TODO: В private?
        if (connection->isClosed())
            connection->open(endpoint->to_ws_url(Wyvern::Utilities::getSelfID()));
    }

    void disconnect() override {
        if (connection->isOpen())
            connection->close();
    }

public:
    void sendSignal(boost::json::object msg) {
        if (!connection || !connection->isOpen()) return;

        std::string serialized = boost::json::serialize(msg);
        connection->send(serialized);
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