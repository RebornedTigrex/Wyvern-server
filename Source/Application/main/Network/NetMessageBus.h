#include <boost/signals2.hpp>

#include <Protocol.h>

namespace Wyvern::Network{
    struct RawMessage {
        std::uint64_t producerId{};
        std::vector<std::byte> payload;
    };

    struct TypedMessage {
        std::uint64_t producerId{};
        Protocol::Message message;
    };

    class NetMessageBus {
    public:
        static struct Raw {
            boost::signals2::signal<void(const RawMessage&)> received;
            boost::signals2::signal<void(const RawMessage&)> toSend;
        };

        static struct Typed {
            boost::signals2::signal<void(const TypedMessage&)> received;
            boost::signals2::signal<void(const TypedMessage&)> toSend;
        };
    };
}
