#pragma once

#include "rtc/rtc.hpp"
#include "Boost/asio.hpp"

#include <memory>
#include <iostream>
#include <functional>
#include <deque>
#include "boost/json.hpp"

#include "SupportUtils.h"
#include "ProtocolSerialize.h"

class IRelayConnection {
public:
    IRelayConnection() = default;

    virtual void connect(std::shared_ptr<Wyvern::Endpoint> endpoint) = 0; //Присоединиться к конкретному реле и начать диалог
    virtual void disconnect() = 0;

    virtual void sendSignal() = 0;
    virtual void sendICEseqSignal() = 0;

    virtual std::optional<std::vector<std::string>> getKnownIDs() = 0;
};


class RelayConnection : IRelayConnection{

    std::shared_ptr<rtc::WebSocket> connection;
    std::shared_ptr<Wyvern::Configuration> applicationConfig;


    void setupCallbacks() {
        connection->onMessage([this](rtc::message_variant msg) {
            if (!std::holds_alternative<rtc::string>(msg)) return;

            auto body = std::get<rtc::string>(std::move(msg));
            });
    }

public:
    RelayConnection(
        std::shared_ptr<boost::asio::io_context> ioContext,
        std::shared_ptr<Wyvern::Configuration> config
    ):
        applicationConfig(config),
        connection(std::make_shared<rtc::WebSocket>())
    {
        setupCallbacks();
    }


    //Делаем реле подключение отдельно
    void connect(std::shared_ptr<Wyvern::Endpoint> endpoint) override {//TODO: В private?
        if (connection->isClosed())
            connection->open(endpoint->to_ws_url(Wyvern::Utilities::getSelfID()));
        else
            throw std::runtime_error("Attempt to connect with open connection");
    }

    void disconnect() override {
        if (connection->isOpen())
            connection->close();
        else
            throw std::runtime_error("Attempt to disconnect without open connection");
    }

public:
    void sendSignal(Wyvern::Protocol::Message& msg) {
        if (!connection || !connection->isOpen()) {
            throw std::runtime_error("Send signal without connection");
            return;
        }

        connection->send(Wyvern::Protocol::serialize(msg));
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