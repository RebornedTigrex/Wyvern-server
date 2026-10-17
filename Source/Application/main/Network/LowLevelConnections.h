#pragma once

#include <rtc/rtc.hpp>
#include <Boost/asio.hpp>

#include <memory>
#include <iostream>
#include <functional>
#include <deque>

#include "SupportUtils.h"

#include <Network/NetMessageBus.h>

namespace Wyvern::Network
{
    class IConnection {
    public:
        virtual ~IConnection() = default;
        virtual void connect() = 0;
        virtual void disconnect() = 0;
        virtual void send(std::vector<std::byte> msg) = 0;
    };

    class IRelayConnection : public IConnection {
    public:
        //virtual void connect(const Wyvern::Endpoint& endpoint) = 0;
        virtual void setOnMessageCallback(
            std::function<void(std::shared_ptr<rtc::WebSocket>&, std::vector<std::byte>)>) = 0;
        virtual void setOnOpenCallback(std::function<void()>) = 0;
        virtual void setOnErrorCallback(std::function<void(std::string)>) = 0;
    };

    class INodeConnection : public IConnection {
    public:
        //virtual void connect(std::string peerId) = 0;
        virtual void handleRemoteDescription(const std::string& type, const std::string& sdp) = 0;
        virtual void handleRemoteCandidate(const std::string& candidate, const std::string& mid) = 0;
    };


    class RelayConnection : public IRelayConnection {

        std::shared_ptr<rtc::WebSocket> connection;
        std::shared_ptr<Wyvern::Configuration> applicationConfig;
        NetMessageBus& networkBus;

        const Wyvern::Endpoint endpoint;

        std::function<void(std::shared_ptr<rtc::WebSocket>&, std::vector<std::byte>)> onMsgCallback;


    public:
        RelayConnection( Wyvern::Endpoint Endpoint, NetMessageBus& bus) : endpoint(Endpoint), 
            networkBus(bus),
            connection(std::make_shared<rtc::WebSocket>())
        {
            setOnMessageCallback(onMsgCallback);
        }

        void giveMsgCallback(std::function<void(std::shared_ptr<rtc::WebSocket>&, std::vector<std::byte>)> _onMsgCallback) {
            onMsgCallback = _onMsgCallback;
        }

        //Делаем реле подключение отдельно
        void connect() override {
            if (connection->isClosed())
                connection->open(endpoint.to_ws_url());
            else
                throw std::runtime_error("Attempt to connect with open connection");//FIXME: Более явные ошибки? Исправить позже трудности с асинхронными throw
        }

        void disconnect() override {
            if (connection->isOpen())
                connection->close();
            else
                throw std::runtime_error("Attempt to disconnect without open connection");//FIXME: Более явные ошибки? Исправить позже трудности с асинхронными throw
        }

        void setOnMessageCallback(std::function<void(std::shared_ptr<rtc::WebSocket>&, std::vector<std::byte>)> onMessage_) override {
            connection->onMessage([this, onMessage_](rtc::message_variant msg) {
                if (!std::holds_alternative<rtc::binary>(msg)) return;

                std::vector<std::byte> body = std::get<rtc::binary>(std::move(msg));
                try {
                    if (onMessage_) onMessage_(connection, std::move(body));
                }
                catch (const std::exception& e) {
                    //TODO: Намутить обработку
                }

                });
        }

        void setOnOpenCallback(std::function<void()> onOpen_) override {
            connection->onOpen(std::move(onOpen_));
        }

        void setOnErrorCallback(std::function<void(std::string)> onError_) override {
            connection->onError(std::move(onError_));
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
}