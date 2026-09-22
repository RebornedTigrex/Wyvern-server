#pragma once

#include <random>
#include <memory>

using NodeID = std::string;

enum class RelaySpecific {
    Closest,
    Fastest
};


enum class P2PConnectionType {
    PreferRelay,
    PreferDirect
};


namespace Wyvern {
    class Configuration {
    public:
        Configuration() {};

        RelaySpecific getPreferRelaySpecific() const { return RelaySpecific::Closest; };
        P2PConnectionType getPreferConnectionType() const { return P2PConnectionType::PreferRelay; };

        int getRelayTimeoutMS() const { return 500; } // TODO: заглушка
        int getMaxReconnectAttempts() const { return 5; }

    };


    class Endpoint {
    public:
        Endpoint(std::string _host = "", uint16_t _port = 0) {}

        uint16_t port{ 0 };
        std::string host{ "" };

        std::string to_ws_url(const std::string& peerId) const {
            // Формирует RFC-совместимый URL (IPv6 в квадратных скобках)
            bool is_ipv6 = host.find(':') != std::string::npos && host.front() != '[';
            std::string formatted_host = is_ipv6 ? ("[" + host + "]") : host;
            return "ws://" + formatted_host + ":" + std::to_string(port) + "/" + peerId;
        }
    };
    static const std::shared_ptr<Endpoint> selfEndpoint = std::make_shared<Endpoint>("0.0.0.0", 9854);
}

namespace Wyvern::Utilities {
    static inline std::string normalize_path(std::string p) {
        if (!p.empty() && p.front() == '/') p.erase(p.begin());
        return p;
    }

    static inline std::string generateRandNumSeq(int seqSize) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<int> digit(0, 9);

        static std::mutex _mutex;


        std::string result;
        result.reserve(seqSize);

        {
            std::lock_guard lock(_mutex);
            for (int i = 0; i < seqSize; i++) {
                result += char(digit(gen));
            }
        }


        return result;
    }

    static inline NodeID getSelfID() {
        static NodeID localID = "SOME-KIND-OF-ID-" + Wyvern::Utilities::generateRandNumSeq(4);//TODO: Заглушка
        return localID;
    }

    static inline std::shared_ptr<Endpoint> getRelayBy(RelaySpecific)//Выбор ссылки не реле по какому-то признаку //TODO: Заглушка, которая всегда отдаёт себя
    {
        return selfEndpoint;
    };
};