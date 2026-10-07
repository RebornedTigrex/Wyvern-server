#pragma once

#include "Protocol.h"
#include "Serialize.h"
#include "SupportUtils.h"
#include "Encryption.h"

#include <memory>
#include <format>

#include "rtc/rtc.hpp"

namespace Wyvern::Protocol {

    using websocketPtr = const std::shared_ptr<rtc::WebSocket>&;

    template<typename T>
    concept MessageVisitorType = requires(T visitor, websocketPtr ws) {
        { visitor.visit(ws, std::declval<const Packets::ActionMsg&>()) };
        { visitor.visit(ws, std::declval<const Packets::RelaysActionAckMsg&>()) };
        { visitor.visit(ws, std::declval<const Packets::PublishMsg&>()) };
        { visitor.visit(ws, std::declval<const Packets::AckMsg&>()) };
        { visitor.visit(ws, std::declval<const Packets::NackMsg&>()) };
    };


    template<MessageVisitorType Visitor>
    class MessageRouter {
    public:
        MessageRouter() = default;

        void route(websocketPtr ws, std::vector<std::byte> body) {
            try {
                auto msg = Wyvern::Binary::deserialize(body);
                std::visit([&](const auto& m) { visitor_.visit(ws, m); }, msg);
            }
            catch(const std::exception& e){
                std::cout << std::format("Error in MessageRouter! {}\n", e.what());
            }
        }

        const Visitor& getVisitor() const { return visitor_; }

    private:
        Visitor visitor_;
    };

    class ProtocolVisitor {
    public:
        ProtocolVisitor() = default;

        void visit(websocketPtr from,
            const Packets::ActionMsg& req)
        {

            std::string status;

            switch (req.payload.action) {
                case ActionType::NoAction: { status = "ok"; break; }
                case ActionType::GetRelayList: { status = "Not implemented"; break; }
                default: status = "Not implemented";
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

        void visit(websocketPtr from,
            const Wyvern::Protocol::Packets::RelaysActionAckMsg& req) {
            
            std::cout << std::format("Спасибо за участие в тестировании! Статус ответа: {}\n", req.payload.status);
            return;
        }

        void visit(websocketPtr from,
            const Wyvern::Protocol::Packets::PublishMsg& req)
        {
            return;
        }

        void visit(websocketPtr from,
            const Wyvern::Protocol::Packets::AckMsg& req) {
            return;
        }
        void visit(websocketPtr from,
            const Wyvern::Protocol::Packets::NackMsg& req) {
            return;
        }

        Wyvern::Utilities::ThreadSafeCounter outSeq_;
    };
}