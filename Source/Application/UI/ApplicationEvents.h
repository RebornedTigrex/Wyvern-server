#pragma once

#include "SupportUtils.h"

#include <boost/signals2.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Wyvern {

    // Сообщение, которое UI умеет показать. Тело пока строка:
    // протокол чата ещё не прибит, каталог — шов.
    struct ChatMessage {
        std::string peerId;
        std::string body;
        std::int64_t timestampMs{ 0 };
    };

    // Шина ядра. Слоты вешает UI. Эмит только со strand ядра.
    // Повторно использовать может любой ведомый интерфейс, не только CLI.
    class ApplicationEvents {
    public:
        boost::signals2::signal<void(Endpoint)> relayDialed;
        boost::signals2::signal<void(Endpoint)> relayOpen;
        boost::signals2::signal<void(std::string)> relayFailed;

        boost::signals2::signal<void(std::string)> peerDialed;
        boost::signals2::signal<void(std::string)> chatUpserted;
        boost::signals2::signal<void(ChatMessage)> messageArrived;

        boost::signals2::signal<void(std::vector<std::string>)> chatSnapshot;
    };

}
