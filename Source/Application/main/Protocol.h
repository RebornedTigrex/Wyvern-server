#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

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
        MsgType type;                               // Тип пакета
        std::uint64_t seq;                          // Порядковый номер пакета
        std::optional<std::uint64_t> reply_to;     // Ответ на порядковый номер выходящего пакета
        std::string node_id;
        std::string relay_id;
        std::int64_t timestamp_ms;
    };


    namespace Packets {

        struct RelayInfo { // Пакет нужный для обмена информацией о реле с нодами
            std::string id;
            std::string addr;
            std::optional<std::string> source; // "gossip", "config", ...
            double load = 0.0;
        };

        // INIT: запрос списка реле
        struct InitPayload {
            std::string action;                      // "GET_RELAY_LIST"
            std::optional<std::int64_t> since_ms;               // получить изменения с момента
        };

        // INIT_ACK: подтверждение + опционально список
        struct InitAckPayload {
            std::string status;                       // "accepted" | "ok"
            std::optional<uint32_t> expected_size;
            std::vector<RelayInfo> relays;            // если список небольшой
        };

        // PUBLISH: узел публикует свои реле
        struct PublishPayload {
            std::vector<RelayInfo> relays;
        };

        // ACK: успешное подтверждение
        struct AckPayload {
            std::optional<uint32_t> stored;
            std::optional<uint32_t> duplicates;
        };

        // NACK / ERROR
        struct NackPayload {
            std::string code;
            std::string reason;
        };

        struct InitMsg      { Envelope env; InitPayload     payload; };
        struct InitAckMsg   { Envelope env; InitAckPayload  payload; };
        struct PublishMsg   { Envelope env; PublishPayload  payload; };
        struct AckMsg       { Envelope env; AckPayload      payload; };
        struct NackMsg      { Envelope env; NackPayload     payload; };
    }
    using Message = std::variant<Packets::InitMsg, Packets::InitAckMsg, Packets::PublishMsg, Packets::AckMsg, Packets::NackMsg>;



}