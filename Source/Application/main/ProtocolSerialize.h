#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "Protocol.h"

namespace Wyvern::Protocol {

    // ──────────────────── Вспомогательные функции ────────────────────

    inline void put(std::vector<std::byte>& buf, const void* src, size_t n) {
        auto* p = static_cast<const std::byte*>(src);
        buf.insert(buf.end(), p, p + n);
    }

    inline const std::byte* get(const std::byte* data, size_t& pos,
        size_t size, void* dst, size_t n) {
        if (pos + n > size)
            throw std::runtime_error("deserialize: buffer overflow");
        const std::byte* p = data + pos;
        if (dst) std::memcpy(dst, p, n);
        pos += n;
        return p;
    }

    // ──────────────────── Шаблоны: trivially copyable ────────────────────

    template <typename T>
        requires std::is_trivially_copyable_v<T>
    void serialize(std::vector<std::byte>& buf, const T& v) {
        put(buf, &v, sizeof(T));
    }

    template <typename T>
        requires std::is_trivially_copyable_v<T>
    void deserialize(const std::byte* data, size_t& pos, size_t size, T& v) {
        get(data, pos, size, &v, sizeof(T));
    }

    // ──────────────────── Шаблоны: std::string ────────────────────

    inline void serialize(std::vector<std::byte>& buf, const std::string& s) {
        uint32_t len = static_cast<uint32_t>(s.size());
        serialize(buf, len);
        if (len) put(buf, s.data(), len);
    }

    inline void deserialize(const std::byte* data, size_t& pos,
        size_t size, std::string& s) {
        uint32_t len = 0;
        deserialize(data, pos, size, len);
        if (len) {
            const auto* p = get(data, pos, size, nullptr, len);
            s.assign(reinterpret_cast<const char*>(p), len);
        }
        else {
            s.clear();
        }
    }

    // ──────────────────── ПРЕДВАРИТЕЛЬНЫЕ ОБЪЯВЛЕНИЯ СВОИХ ТИПОВ ────────────────────
    // MSVC требует видеть перегрузки ДО шаблона вектора

    // --- MsgType ---

    inline void serialize(std::vector<std::byte>& buf, MsgType v) {
        serialize(buf, static_cast<uint8_t>(v));
    }

    inline void deserialize(const std::byte* data, size_t& pos,
        size_t size, MsgType& v) {
        uint8_t raw = 0;
        deserialize(data, pos, size, raw);
        v = static_cast<MsgType>(raw);
    }

    // --- RelayInfo ---

    inline void serialize(std::vector<std::byte>& buf,
        const Packets::RelayInfo& v) {
        serialize(buf, v.id);
        serialize(buf, v.addr);
        serialize(buf, v.source);
        serialize(buf, v.load);
    }

    inline void deserialize(const std::byte* data, size_t& pos,
        size_t size, Packets::RelayInfo& v) {
        deserialize(data, pos, size, v.id);
        deserialize(data, pos, size, v.addr);
        deserialize(data, pos, size, v.source);
        deserialize(data, pos, size, v.load);
    }

    // ──────────────────── Шаблоны: std::vector ────────────────────
    // Теперь компилятор уже видел перегрузку RelayInfo выше

    template <typename T>
    void serialize(std::vector<std::byte>& buf, const std::vector<T>& v) {
        uint32_t n = static_cast<uint32_t>(v.size());
        serialize(buf, n);
        for (const auto& item : v)
            serialize(buf, item);
    }

    template <typename T>
    void deserialize(const std::byte* data, size_t& pos,
        size_t size, std::vector<T>& v) {
        uint32_t n = 0;
        deserialize(data, pos, size, n);
        v.resize(n);
        for (auto& item : v)
            deserialize(data, pos, size, item);
    }

    // ──────────────────── Оставшиеся перегрузки ────────────────────

    // --- Envelope ---

    inline void serialize(std::vector<std::byte>& buf, const Envelope& e) {
        serialize(buf, e.type);
        serialize(buf, e.seq);
        serialize(buf, e.reply_to);
        serialize(buf, e.node_id);
        serialize(buf, e.relay_id);
        serialize(buf, e.timestamp_ms);
        serialize(buf, e.payload_size);
    }

    inline void deserialize(const std::byte* data, size_t& pos,
        size_t size, Envelope& e) {
        deserialize(data, pos, size, e.type);
        deserialize(data, pos, size, e.seq);
        deserialize(data, pos, size, e.reply_to);
        deserialize(data, pos, size, e.node_id);
        deserialize(data, pos, size, e.relay_id);
        deserialize(data, pos, size, e.timestamp_ms);
        deserialize(data, pos, size, e.payload_size);
    }

    // --- InitPayload ---

    inline void serialize(std::vector<std::byte>& buf,
        const Packets::InitPayload& v) {
        serialize(buf, v.action);
        serialize(buf, v.since_ms);
    }

    inline void deserialize(const std::byte* data, size_t& pos,
        size_t size, Packets::InitPayload& v) {
        deserialize(data, pos, size, v.action);
        deserialize(data, pos, size, v.since_ms);
    }

    // --- InitAckPayload ---

    inline void serialize(std::vector<std::byte>& buf,
        const Packets::InitAckPayload& v) {
        serialize(buf, v.status);
        serialize(buf, v.relays_payload_size);
        serialize(buf, v.relays);
    }

    inline void deserialize(const std::byte* data, size_t& pos,
        size_t size, Packets::InitAckPayload& v) {
        deserialize(data, pos, size, v.status);
        deserialize(data, pos, size, v.relays_payload_size);
        deserialize(data, pos, size, v.relays);
    }

    // --- PublishPayload ---

    inline void serialize(std::vector<std::byte>& buf,
        const Packets::PublishPayload& v) {
        serialize(buf, v.relays_payload_size);
        serialize(buf, v.relays);
    }

    inline void deserialize(const std::byte* data, size_t& pos,
        size_t size, Packets::PublishPayload& v) {
        deserialize(data, pos, size, v.relays_payload_size);
        deserialize(data, pos, size, v.relays);
    }

    // --- AckPayload ---

    inline void serialize(std::vector<std::byte>& buf,
        const Packets::AckPayload& v) {
        serialize(buf, v.stored);
        serialize(buf, v.duplicates);
    }

    inline void deserialize(const std::byte* data, size_t& pos,
        size_t size, Packets::AckPayload& v) {
        deserialize(data, pos, size, v.stored);
        deserialize(data, pos, size, v.duplicates);
    }

    // --- NackPayload ---

    inline void serialize(std::vector<std::byte>& buf,
        const Packets::NackPayload& v) {
        serialize(buf, v.code);
        serialize(buf, v.reason);
    }

    inline void deserialize(const std::byte* data, size_t& pos,
        size_t size, Packets::NackPayload& v) {
        deserialize(data, pos, size, v.code);
        deserialize(data, pos, size, v.reason);
    }

    // --- Обёртки сообщений ---

    inline void serialize(std::vector<std::byte>& buf, const Packets::InitMsg& m) { serialize(buf, m.env); serialize(buf, m.payload); }
    inline void serialize(std::vector<std::byte>& buf, const Packets::InitAckMsg& m) { serialize(buf, m.env); serialize(buf, m.payload); }
    inline void serialize(std::vector<std::byte>& buf, const Packets::PublishMsg& m) { serialize(buf, m.env); serialize(buf, m.payload); }
    inline void serialize(std::vector<std::byte>& buf, const Packets::AckMsg& m) { serialize(buf, m.env); serialize(buf, m.payload); }
    inline void serialize(std::vector<std::byte>& buf, const Packets::NackMsg& m) { serialize(buf, m.env); serialize(buf, m.payload); }

    inline void deserialize(const std::byte* d, size_t& p, size_t s, Packets::InitMsg& m) { deserialize(d, p, s, m.env); deserialize(d, p, s, m.payload); }
    inline void deserialize(const std::byte* d, size_t& p, size_t s, Packets::InitAckMsg& m) { deserialize(d, p, s, m.env); deserialize(d, p, s, m.payload); }
    inline void deserialize(const std::byte* d, size_t& p, size_t s, Packets::PublishMsg& m) { deserialize(d, p, s, m.env); deserialize(d, p, s, m.payload); }
    inline void deserialize(const std::byte* d, size_t& p, size_t s, Packets::AckMsg& m) { deserialize(d, p, s, m.env); deserialize(d, p, s, m.payload); }
    inline void deserialize(const std::byte* d, size_t& p, size_t s, Packets::NackMsg& m) { deserialize(d, p, s, m.env); deserialize(d, p, s, m.payload); }

    // ──────────────────── Message (std::variant) ────────────────────

    inline std::vector<std::byte> serialize(const Message& msg) {
        std::vector<std::byte> buf;
        serialize(buf, static_cast<uint8_t>(msg.index()));
        std::visit([&](const auto& m) { serialize(buf, m); }, msg);
        return buf;
    }

    inline Message deserialize(const std::vector<std::byte>& buf) {
        const std::byte* data = buf.data();
        size_t pos = 0, size = buf.size();
        uint8_t idx = 0;
        deserialize(data, pos, size, idx);

        switch (idx) {
        case 0: { Packets::InitMsg    m; deserialize(data, pos, size, m); return m; }
        case 1: { Packets::InitAckMsg m; deserialize(data, pos, size, m); return m; }
        case 2: { Packets::PublishMsg m; deserialize(data, pos, size, m); return m; }
        case 3: { Packets::AckMsg     m; deserialize(data, pos, size, m); return m; }
        case 4: { Packets::NackMsg    m; deserialize(data, pos, size, m); return m; }
        default:
            throw std::runtime_error("deserialize: unknown message type");
        }
    }

} // namespace Wyvern::Protocol
