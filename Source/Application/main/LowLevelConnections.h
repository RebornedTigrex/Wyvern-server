#pragma once

#include "rtc/rtc.hpp"
#include "Boost/asio.hpp"

#include <memory>
#include <iostream>
#include <functional>
#include <deque>
#include "boost/json.hpp"

#include "SupportUtils.h"

namespace Wyvern::Network
{
    class IConnection {
    public:
        IConnection() = default;

        virtual void setOnMessageCallback(std::function<void(std::vector<std::byte>)> callback) = 0;

        virtual void connect(std::shared_ptr<Wyvern::Endpoint> endpoint) = 0; //Присоединиться к конкретному реле
        virtual void connect(std::string PeerIdentification) = 0;

        virtual void disconnect() = 0;

        virtual void send(std::vector<std::byte> msg) = 0;
    };


    class RelayConnection : public IConnection, public std::enable_shared_from_this<RelayConnection> {

        std::shared_ptr<rtc::WebSocket> connection;
        std::shared_ptr<Wyvern::Configuration> applicationConfig;


        void setupCallbacks(std::function<void(std::vector<std::byte>)> onMessage_) {
            connection->onMessage([this, onMessage_](rtc::message_variant msg) {
                if (!std::holds_alternative<rtc::binary>(msg)) return;

                std::vector<std::byte> body = std::get<rtc::binary>(std::move(msg));
                try {
                    if (onMessage_)
                        onMessage_(std::move(body));
                }
                catch (const std::exception& e) {
                    //TODO: Намутить обработку
                }

                });
        }

    public:
        RelayConnection(
            std::function<void(std::vector<std::byte>)> onMsgCallback
        ) :
            connection(std::make_shared<rtc::WebSocket>())
        {
            setupCallbacks(onMsgCallback);
        }


        //Делаем реле подключение отдельно
        void connect(std::shared_ptr<Wyvern::Endpoint> endpoint) override {
            if (connection->isClosed())
                connection->open(endpoint->to_ws_url(Wyvern::Identity::getSelfID()));
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
        void send(std::vector<std::byte> msg) override {
            if (!connection || !connection->isOpen()) {
                throw std::runtime_error("Send without connection");//FIXME: Более явные ошибки? Исправить позже трудности с асинхронными throw
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
}