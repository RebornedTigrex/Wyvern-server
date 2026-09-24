#pragma once

#include <boost/json.hpp>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <Protocol.h>


namespace Wyvern::Protocol{
    inline std::string toString(MsgType t) {
        switch (t) {
        case MsgType::Init:    return "INIT";
        case MsgType::InitAck: return "INIT_ACK";
        case MsgType::Publish: return "PUBLISH";
        case MsgType::Ack:     return "ACK";
        case MsgType::Nack:    return "NACK";
        case MsgType::Error:   return "ERROR";
        }
        return "UNKNOWN";
    }

    inline MsgType msgTypeFromString(std::string_view s) {
        if (s == "INIT")      return MsgType::Init;
        if (s == "INIT_ACK")  return MsgType::InitAck;
        if (s == "PUBLISH")   return MsgType::Publish;
        if (s == "ACK")       return MsgType::Ack;
        if (s == "NACK")      return MsgType::Nack;
        if (s == "ERROR")     return MsgType::Error;
        throw std::runtime_error("unknown msg type: " + std::string(s));
    }

    inline boost::json::object toJson(const Envelope& e) {
        boost::json::object o{
            {"type",      toString(e.type)},
            {"seq",       e.seq},
            {"node_id",   e.node_id},
            {"relay_id",  e.relay_id},
            {"timestamp", e.timestamp_ms},
        };
        if (e.reply_to) o ["reply_to"] = *e.reply_to;
        return o;
    }

    inline boost::json::object toJson(const Packets::RelayInfo& r) {
        boost::json::object o{
            {"id",   r.id},
            {"addr", r.addr},
            {"load", r.load},
        };

        if (r.source) o ["source"] = *r.source;

        return o;
    }

    inline boost::json::object toJson(const Packets::InitMsg& m) {
        auto o = toJson(m.env);
        boost::json::object p{ {"action", m.payload.action} };
        if (m.payload.since_ms) p ["since"] = *m.payload.since_ms;
        o ["payload"] = std::move(p);
        return o;
    }

    inline boost::json::object toJson(const Packets::InitAckMsg& m) {
        auto o = toJson(m.env);
        boost::json::array relays;
        for (const auto& r : m.payload.relays) relays.push_back(toJson(r));
        boost::json::object p{
            {"status", m.payload.status},
            {"relays", std::move(relays)},
        };
        if (m.payload.expected_size) p ["expected_size"] = *m.payload.expected_size;
        
        o ["payload"] = std::move(p);
        return o;
    }

    inline boost::json::object toJson(const Packets::PublishMsg& m) {
        auto o = toJson(m.env);
        boost::json::array relays;
        for (const auto& r : m.payload.relays) relays.push_back(toJson(r));
        o ["payload"] = boost::json::object{
            {"relays", std::move(relays)},
        };
        return o;
    }

    inline boost::json::object toJson(const Packets::AckMsg& m) {
        auto o = toJson(m.env);
        boost::json::object p;
        if (m.payload.stored) o ["stored"] = *m.payload.stored;
        if (m.payload.duplicates) o ["duplicates"] = *m.payload.duplicates;

        o ["payload"] = std::move(p);

        return o;
    }

    inline boost::json::object toJson(const Packets::NackMsg& m) {
        auto o = toJson(m.env);
        

        o ["payload"] = boost::json::object{
            {"code", m.payload.code},
            {"reason", m.payload.reason},
        };

        return o;
    }

    inline std::string serialize(const Message& msg) {
        boost::json::object obj = std::visit([](const auto& m) { return toJson(m); }, msg);
        return boost::json::serialize(obj);
    }

    // ---------- Десериализация ----------

    inline Envelope parse_envelope(const boost::json::object& o) {
        Envelope e;
        e.type = msgTypeFromString(o.at("type").as_string());
        e.seq = o.at("seq").as_uint64();
        e.node_id = std::string(o.at("node_id").as_string());
        e.relay_id = std::string(o.at("relay_id").as_string());
        e.timestamp_ms = o.at("timestamp").as_int64();
        if (auto it = o.find("reply_to"); it != o.end())
            e.reply_to = it->value().as_uint64();
        return e;
    }

    inline Packets::RelayInfo parse_relay(const boost::json::value& v) {
        const auto& o = v.as_object();
        return Packets::RelayInfo{
            std::string(o.at("id").as_string()),
            std::string(o.at("addr").as_string()),
            std::string(),
            o.contains("load") ? o.at("load").as_double() : 0.0,
        };
    }

    inline Message parse(std::string_view text) {
        boost::json::value v = boost::json::parse(text);
        const auto& o = v.as_object();

        Envelope env = parse_envelope(o);
        const auto& p = o.at("payload").as_object();

        switch (env.type) {
        case MsgType::Init: {
            Packets::InitPayload pl;
            pl.action = std::string(p.at("action").as_string());

            if (auto it = p.find("since"); it != p.end() && !it->value().is_null())
                pl.since_ms = it->value().as_int64();

            return Packets::InitMsg{ std::move(env), std::move(pl) };
        }

        case MsgType::InitAck: {
            Packets::InitAckPayload pl;
            pl.status = std::string(p.at("status").as_string());

            if (auto it = p.find("expected_size"); it != p.end() && !it->value().is_null())
                pl.expected_size = static_cast<std::uint32_t>(it->value().as_int64());

            if (auto it = p.find("relays"); it != p.end() && !it->value().is_null()) {
                for (const auto& r : it->value().as_array())
                    pl.relays.push_back(parse_relay(r));
            }

            return Packets::InitAckMsg{ std::move(env), std::move(pl) };
        }

        case MsgType::Publish: {
            Packets::PublishPayload pl;

            if (auto it = p.find("relays"); it != p.end() && !it->value().is_null()) {
                for (const auto& r : it->value().as_array())
                    pl.relays.push_back(parse_relay(r));
            }

            return Packets::PublishMsg{ std::move(env), std::move(pl) };
        }

        case MsgType::Ack: {
            Packets::AckPayload pl;

            if (auto it = p.find("stored"); it != p.end() && !it->value().is_null())
                pl.stored = static_cast<std::uint32_t>(it->value().as_int64());

            if (auto it = p.find("duplicates"); it != p.end() && !it->value().is_null())
                pl.duplicates = static_cast<std::uint32_t>(it->value().as_int64());

            return Packets::AckMsg{ std::move(env), std::move(pl) };
        }

        case MsgType::Nack:
        case MsgType::Error: {
            Packets::NackPayload pl;
            pl.code = std::string(p.at("code").as_string());
            pl.reason = std::string(p.at("reason").as_string());

            return Packets::NackMsg{ std::move(env), std::move(pl) };
        }
        }

        throw std::runtime_error("unknown message type: " + toString(env.type));
    }
}