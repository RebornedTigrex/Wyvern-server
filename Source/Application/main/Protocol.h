#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace Wyvern::Protocol {

    enum class MsgType {
        Action = 0,
        Publish = 1,
        Ack = 2,
        Nack = 3,
        Error = 4,
        RelaysActionAck = 5,
    };

    enum class ActionType {
        NoAction = 0,
        GetRelayList = 1,
    };

    struct Envelope { // Конверт для всех остальных пакетов. //TODO: Возможно позже стоит сделать пакет попроще, где метаданные будут в другом месте.
        MsgType type;
        std::uint64_t seq;                          // Порядковый номер пакета
        std::uint64_t reply_to{UINT64_MAX};     // Ответ на порядковый номер выходящего пакета // Нет значение = UINT64_MAX
        std::string from;
        std::string to;
        std::int64_t timestamp_ms;
        uint32_t payload_size {UINT32_MAX}; // Нет значение = UINT32_MAX
    };


    namespace Packets {

        struct RelayInfo { // Пакет нужный для обмена информацией о реле с нодами
            std::string id;
            std::string addr;
            std::string source{""}; // "gossip", "config", ... иначе = "". Отсутствие допустимо
            double load = 0.0;
        };

        // ACTION: запрос действия
        struct ActionPayload {
            ActionType action;                       // "GET_RELAY_LIST"
            std::int64_t since_ms{INT64_MAX};         // получить изменения с момента \\ Отсутствие значения = INT64_MAX
        };

        // ACTION_ACK: подтверждение + опционально список
        struct RelaysActionAckPayload {
            std::string status;                       // "accepted" | "ok"
            uint32_t payload_size;             // Отсутствие значения = UINT32_MAX
            std::vector<RelayInfo> relays;            // если список небольшой
        };

        // PUBLISH: узел публикует свои реле
        struct PublishPayload {
            uint32_t payload_size;
            std::vector<RelayInfo> relays;
        };

        // ACK: успешное подтверждение
        struct AckPayload {//FIXME: Бестолковый пакет. Неуниверсальный, хотя имя говорит об обратном
            uint32_t stored{UINT32_MAX};            // Отсутствие значения = UINT32_MAX
            uint32_t duplicates{UINT32_MAX};        // Отсутствие значения = UINT32_MAX
        };

        // NACK / ERROR
        struct NackPayload {
            std::string code;
            std::string reason;
        };

        struct ActionMsg            { static constexpr MsgType kType = MsgType::Action;             Envelope env; ActionPayload             payload; };
        struct PublishMsg           { static constexpr MsgType kType = MsgType::Publish;            Envelope env; PublishPayload            payload; };
        struct AckMsg               { static constexpr MsgType kType = MsgType::Ack;                Envelope env; AckPayload                payload; };
        struct NackMsg              { static constexpr MsgType kType = MsgType::Nack;               Envelope env; NackPayload               payload; };
        struct RelaysActionAckMsg   { static constexpr MsgType kType = MsgType::RelaysActionAck;    Envelope env; RelaysActionAckPayload    payload; };

    }
    using Message = std::variant<Packets::ActionMsg, Packets::RelaysActionAckMsg, Packets::PublishMsg, Packets::AckMsg, Packets::NackMsg>;



}