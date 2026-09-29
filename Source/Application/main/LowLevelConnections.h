#pragma once

#include "rtc/rtc.hpp"
#include "Boost/asio.hpp"

#include <memory>
#include <iostream>
#include <functional>
#include <deque>
#include "boost/json.hpp"

#include "SupportUtils.h"

class ISignalingConnection {
public:
    ISignalingConnection() = default;

    virtual void connect(std::shared_ptr<Wyvern::Endpoint> endpoint) = 0; //Присоединиться к конкретному реле
    virtual void disconnect() = 0;

    virtual void sendSignal(std::string msg) = 0;
};

class IChannelConnection {//FIXME: Тут есть проблема. Как я могу разделить канал и реле, если один зависит от другого?
public:
    IChannelConnection() = default;

    virtual void connect(std::string PeerIdentification) = 0; // Присоединиться к конкретному пиру, после подключение к реле
    virtual void disconnect() = 0;

    virtual void send(std::string msg) = 0;
};


class RelayConnection : public ISignalingConnection, public std::enable_shared_from_this<RelayConnection> {

    std::shared_ptr<rtc::WebSocket> connection;
    std::shared_ptr<Wyvern::Configuration> applicationConfig;


    void setupCallbacks(std::function<void(std::string)> onMessage_) {
        connection->onMessage([this, onMessage_](rtc::message_variant msg) {
            if (!std::holds_alternative<rtc::string>(msg)) return;

            auto body = std::get<rtc::string>(std::move(msg));
            try{
                if (onMessage_)
                    onMessage_(std::move(body));
            }
            catch(const std::exception &e){
                //TODO: Намутить обработку
            }
            
            });
    }

public:
    RelayConnection(
        std::function<void(std::string)> onMsgCallback
    ):
        connection(std::make_shared<rtc::WebSocket>())
    {
        setupCallbacks(onMsgCallback);
    }


    //Делаем реле подключение отдельно
    void connect(std::shared_ptr<Wyvern::Endpoint> endpoint) override {
        if (connection->isClosed())
            connection->open(endpoint->to_ws_url(Wyvern::Utilities::getSelfID()));
        else
            throw std::runtime_error("Attempt to connect with open connection");//FIXME: Более явные ошибки? Исправить позже трудности с асинхронными throw
    }

    void disconnect() override {
        if (connection->isOpen())
            connection->close();
        else
            throw std::runtime_error("Attempt to disconnect without open connection");//FIXME: Более явные ошибки? Исправить позже трудности с асинхронными throw
    }

public:
    void sendSignal(std::string msg) override{
        if (!connection || !connection->isOpen()) {
            throw std::runtime_error("Send signal without connection");//FIXME: Более явные ошибки? Исправить позже трудности с асинхронными throw
            return;
        }
        connection->send(msg);
    }
};


class NodeConnection : public std::enable_shared_from_this<NodeConnection> { //TODO: Пока не реализовано 
    rtc::Configuration config{};
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::DataChannel> dc;

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