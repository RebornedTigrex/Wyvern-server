#pragma once

#include "ApplicationEvents.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Wyvern {

    // Лидер владеет чатами. Чат = id пира, отдельного имени нет.
    // Трогать только со strand NodeRuntime.
    class ChatDirectory {
        struct Chat {
            std::string peerId;
            std::vector<ChatMessage> messages;
        };

        ApplicationEvents& events;
        std::vector<std::string> order;
        std::unordered_map<std::string, Chat> chats;

    public:
        explicit ChatDirectory(ApplicationEvents& bus) : events(bus) {}

        void ensure(std::string peerId) {
            if (peerId.empty() || chats.contains(peerId))
                return;
            order.push_back(peerId);
            chats.emplace(peerId, Chat{ peerId, {} });
            events.chatUpserted(peerId);
        }

        void append(ChatMessage msg) {
            ensure(msg.peerId);
            chats[msg.peerId].messages.push_back(msg);
            events.messageArrived(std::move(msg));
        }

        void publishSnapshot() {
            events.chatSnapshot(order);
        }

        std::vector<ChatMessage> messagesOf(std::string_view peerId) const {
            auto it = chats.find(std::string{ peerId });
            if (it == chats.end())
                return {};
            return it->second.messages;
        }

    private:
        // ensure() выше уже шлёт chatUpserted. Повторный ensure молчит.
    };

}
