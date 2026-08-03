#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Низкоуровневые типы UDP-транспорта Wyvern.
//
// Содержит:
//  - константы общего бинарного заголовка пакета (формат "MASH v1");
//  - перечисление причин дропа (RX и TX);
//  - типы событий EventBus, через которые транспорт общается с верхним слоем;
//  - утилиты сериализации/десериализации заголовка в network byte order.
//
// Никакой бизнес-логики и интерпретации поля type[1] здесь нет — это задача
// вышестоящих агентов. Транспорт прокидывает type/flags/meta насквозь.
namespace wyvern::transport::udp {

// Сигнатура "MASH" — санити-фильтр против чужих/сканирующих датаграмм
// на нашем порту. Не безопасность, а быстрая ранняя отсечка.
inline constexpr std::array<std::uint8_t, 4> kMagic = {
    static_cast<std::uint8_t>('M'),
    static_cast<std::uint8_t>('A'),
    static_cast<std::uint8_t>('S'),
    static_cast<std::uint8_t>('H')
};

// Версия проводного формата. Меняем при breaking-изменении layout.
inline constexpr std::uint8_t kProtocolVersion = 0x01;

// magic(4) + version(1) + type(1) + flags(1) + length(2) + meta(8) = 17 байт.
inline constexpr std::size_t kHeaderSize = 4 + 1 + 1 + 1 + 2 + 8;

// Причины, по которым датаграмма не дошла до подписчиков EventBus.
enum class DropReason : std::uint8_t {
    InvalidMagic,         // первые 4 байта != "MASH"
    UnsupportedVersion,   // version[1] не равен kProtocolVersion
    TruncatedHeader,      // принято меньше kHeaderSize байт
    LengthMismatch,       // length[2] не совпал с реальным размером payload в датаграмме
    OversizedDatagram,    // размер датаграммы > maxDatagramBytes
    TxQueueOverflow,      // очередь отправки переполнена
    SocketError           // ошибка сокета на RX/TX (см. лог)
};

inline const char* toString(DropReason reason) {
    switch (reason) {
        case DropReason::InvalidMagic:        return "InvalidMagic";
        case DropReason::UnsupportedVersion:  return "UnsupportedVersion";
        case DropReason::TruncatedHeader:     return "TruncatedHeader";
        case DropReason::LengthMismatch:      return "LengthMismatch";
        case DropReason::OversizedDatagram:   return "OversizedDatagram";
        case DropReason::TxQueueOverflow:     return "TxQueueOverflow";
        case DropReason::SocketError:         return "SocketError";
    }
    return "Unknown";
}

// Платформенно-нейтральное представление сетевого адреса для событий.
// Транспорт сериализует boost::asio::ip::udp::endpoint -> PacketEndpoint
// перед публикацией события, чтобы подписчикам не нужно было знать про Asio.
struct PacketEndpoint {
    std::string   address;   // строковое представление IPv4/IPv6
    std::uint16_t port = 0;
};

// Заголовок в распарсенном виде. Поле sender в события добавляется отдельно.
struct PacketHeader {
    std::uint8_t  version = 0;
    std::uint8_t  type    = 0;
    std::uint8_t  flags   = 0;
    std::uint16_t length  = 0;
    std::uint64_t meta    = 0;
};

// Событие EventBus: на нас пришёл валидный UDP-пакет.
// payload — owning vector, чтобы подписчики могли свободно с ним работать
// (читать, перемещать дальше). Транспорт move-нёт его сюда из приёмного буфера.
struct UdpPacketReceived {
    PacketEndpoint            sender;
    std::uint8_t              type        = 0;
    std::uint8_t              flags       = 0;
    std::uint64_t             meta        = 0;
    std::vector<std::uint8_t> payload;
    std::int64_t              receivedAtNs = 0; // steady_clock, наносекунды от старта
};

// Событие EventBus: верхний слой просит отправить пакет.
// Транспорт подписан на это событие, валидирует размер,
// сериализует заголовок и кладёт датаграмму в очередь отправки.
struct UdpSendRequested {
    PacketEndpoint            target;
    std::uint8_t              type  = 0;
    std::uint8_t              flags = 0;
    std::uint64_t             meta  = 0;
    std::vector<std::uint8_t> payload;
};

// Событие EventBus: пакет (RX или TX) был отброшен.
// sender пустой для TX-причин (TxQueueOverflow и т.п.).
struct UdpPacketDropped {
    PacketEndpoint sender;
    DropReason     reason = DropReason::InvalidMagic;
    std::size_t    size   = 0;
    std::int64_t   atNs   = 0;
};

// Сериализация uint16/uint64 в big-endian (network byte order)
// без зависимостей и без выравнивания.
inline void writeBigEndian16(std::uint8_t* out, std::uint16_t value) noexcept {
    out[0] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    out[1] = static_cast<std::uint8_t>(value & 0xFF);
}

inline void writeBigEndian64(std::uint8_t* out, std::uint64_t value) noexcept {
    for (int i = 0; i < 8; ++i) {
        out[i] = static_cast<std::uint8_t>((value >> (56 - 8 * i)) & 0xFF);
    }
}

inline std::uint16_t readBigEndian16(const std::uint8_t* in) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(in[0]) << 8) |
         static_cast<std::uint16_t>(in[1]));
}

inline std::uint64_t readBigEndian64(const std::uint8_t* in) noexcept {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | static_cast<std::uint64_t>(in[i]);
    }
    return v;
}

// Записывает заголовок в буфер (минимум kHeaderSize байт).
inline void writeHeader(std::uint8_t* out,
                        std::uint8_t  type,
                        std::uint8_t  flags,
                        std::uint16_t payloadLength,
                        std::uint64_t meta) noexcept {
    std::memcpy(out, kMagic.data(), kMagic.size());
    out[4] = kProtocolVersion;
    out[5] = type;
    out[6] = flags;
    writeBigEndian16(out + 7, payloadLength);
    writeBigEndian64(out + 9, meta);
}

// Парсит заголовок из buffer длиной bufferSize.
// Возвращает true и заполняет outHeader/outDropReason=unused при успехе.
// При ошибке возвращает false и кладёт причину в outDropReason.
inline bool parseHeader(const std::uint8_t* buffer,
                        std::size_t         bufferSize,
                        PacketHeader&       outHeader,
                        DropReason&         outDropReason) noexcept {
    if (bufferSize < kHeaderSize) {
        outDropReason = DropReason::TruncatedHeader;
        return false;
    }
    if (std::memcmp(buffer, kMagic.data(), kMagic.size()) != 0) {
        outDropReason = DropReason::InvalidMagic;
        return false;
    }
    if (buffer[4] != kProtocolVersion) {
        outDropReason = DropReason::UnsupportedVersion;
        return false;
    }

    outHeader.version = buffer[4];
    outHeader.type    = buffer[5];
    outHeader.flags   = buffer[6];
    outHeader.length  = readBigEndian16(buffer + 7);
    outHeader.meta    = readBigEndian64(buffer + 9);

    const std::size_t expectedTotal = kHeaderSize + static_cast<std::size_t>(outHeader.length);
    if (expectedTotal != bufferSize) {
        outDropReason = DropReason::LengthMismatch;
        return false;
    }
    return true;
}

} // namespace wyvern::transport::udp
