#pragma once

#include "Protocol.h"
#include "Serialize.h"
#include "SupportUtils.h"
#include "Encryption.h"

#include <memory>

#include "rtc/rtc.hpp"

namespace Wyvern::Protocol {

    class IMessageVisitor {
    public:
        virtual void visit(std::shared_ptr<rtc::WebSocket>, const Packets::ActionMsg&) = 0;
        virtual void visit(std::shared_ptr<rtc::WebSocket>, const Packets::RelaysActionAckMsg&) = 0;
        virtual void visit(std::shared_ptr<rtc::WebSocket>, const Packets::PublishMsg&) = 0;
        virtual void visit(std::shared_ptr<rtc::WebSocket>, const Packets::AckMsg&) = 0;
        virtual void visit(std::shared_ptr<rtc::WebSocket>, const Packets::NackMsg&) = 0;
        virtual ~IMessageVisitor() = default;
    };

    class MessageRouter : std::enable_shared_from_this<MessageRouter> {
    public:
        MessageRouter(IMessageVisitor& visitor) : visitor_(visitor) {}

        void route(std::shared_ptr<rtc::WebSocket> ws, std::vector<std::byte> body) {
            auto msg = Wyvern::Binary::deserialize(body);
            std::visit([&](const auto& m) { visitor_.visit(ws, m); }, msg);
        }

    private:
        IMessageVisitor& visitor_;
    };

    class ProtocolVisitor : public IMessageVisitor {
    public:
        ProtocolVisitor() = default;

        void visit(const std::shared_ptr<rtc::WebSocket> from,
            const Wyvern::Protocol::Packets::ActionMsg& req) override
        {
            using namespace Wyvern::Protocol;

            std::string status;

            switch (req.payload.action) {
            case ActionType::NoAction: { status = "ok"; break; }
            case ActionType::GetRelayList: { status = "Not implemented"; break; }
            }


            Packets::RelaysActionAckPayload payload{
                .status = status,
                .payload_size = UINT32_MAX,
                .relays = {},
            };

            auto ack = Wyvern::Binary::make_reply<Packets::RelaysActionAckMsg>(
                req.env,
                Packets::RelaysActionAckMsg::kType,
                static_cast<std::uint64_t>(outSeq_.newNum()),
                Wyvern::Identity::getSelfID(),
                Wyvern::Utilities::NowMs(),
                std::move(payload));

            from->send(Wyvern::Binary::serialize(Message{ std::move(ack) }));
        }

        void visit(const std::shared_ptr<rtc::WebSocket> from,
            const Wyvern::Protocol::Packets::RelaysActionAckMsg& req) override {
            
            printf("Спасибо за участие в тестировании! Статус ответа: %s\n", req.payload.status);
            return;
        }

        void visit(const std::shared_ptr<rtc::WebSocket> from,
            const Wyvern::Protocol::Packets::PublishMsg& req) override
        {
            return;
        }

        void visit(const std::shared_ptr<rtc::WebSocket> from,
            const Wyvern::Protocol::Packets::AckMsg& req) override {
            return;
        }
        void visit(const std::shared_ptr<rtc::WebSocket> from,
            const Wyvern::Protocol::Packets::NackMsg& req) override {
            return;
        }

        Wyvern::Utilities::ThreadSafeCounter outSeq_;
    };
}