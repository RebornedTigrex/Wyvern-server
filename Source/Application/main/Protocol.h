#include <cstdint>
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
        std::uint64_t reply_to{UINT64_MAX};     // Ответ на порядковый номер выходящего пакета // Нет значение = UINT64_MAX
        std::string node_id;
        std::string relay_id;
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

        // INIT: запрос списка реле
        struct InitPayload {
            std::string action;                       // "GET_RELAY_LIST"
            std::int64_t since_ms{INT64_MAX};         // получить изменения с момента \\ Отсутствие значения = INT64_MAX
        };

        // INIT_ACK: подтверждение + опционально список
        struct InitAckPayload {
            std::string status;                       // "accepted" | "ok"
            uint32_t relays_payload_size;             // Отсутствие значения = UINT32_MAX
            std::vector<RelayInfo> relays;            // если список небольшой
        };

        // PUBLISH: узел публикует свои реле
        struct PublishPayload {
            uint32_t relays_payload_size;
            std::vector<RelayInfo> relays;
        };

        // ACK: успешное подтверждение
        struct AckPayload {
            uint32_t stored{UINT32_MAX};            // Отсутствие значения = UINT32_MAX
            uint32_t duplicates{UINT32_MAX};        // Отсутствие значения = UINT32_MAX
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