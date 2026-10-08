#pragma once

#include "ApplicationEvents.h"
#include "CliIo.h"
#include "RuntimeApi.h"

#include <boost/signals2.hpp>

#include <charconv>
#include <future>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Wyvern::Ui {

    enum class CliScreen {
        Relay,
        Hub,
        Chat
    };

    struct CliCommandRelay { Wyvern::Endpoint endpoint; };
    struct CliCommandConnect { std::string peerId; };
    struct CliCommandOpen { std::string peerId; };
    struct CliCommandMessages { std::optional<std::string> peerId; };
    struct CliCommandChats {};
    struct CliCommandBack {};
    struct CliCommandHelp {};
    struct CliCommandQuit {};
    struct CliCommandUnknown { std::string raw; };
    struct CliCommandEmpty {};

    using CliCommand = std::variant<
        CliCommandEmpty,
        CliCommandRelay,
        CliCommandConnect,
        CliCommandOpen,
        CliCommandMessages,
        CliCommandChats,
        CliCommandBack,
        CliCommandHelp,
        CliCommandQuit,
        CliCommandUnknown>;

    // Разбор строки. Ничего не вызывает — только команда.
    class CliCommandParser {
    public:
        CliCommand parse(std::string_view line) const {
            auto args = tokenize(line);
            if (args.empty())
                return CliCommandEmpty{};

            const std::string& cmd = args[0];
            if (cmd == "relay")
                return parseRelay(args);
            if (cmd == "connect")
                return parseId(args, "connect");
            if (cmd == "open")
                return parseOpen(args);
            if (cmd == "messages")
                return parseMessages(args);
            if (cmd == "chats")
                return CliCommandChats{};
            if (cmd == "back")
                return CliCommandBack{};
            if (cmd == "help")
                return CliCommandHelp{};
            if (cmd == "quit" || cmd == "exit")
                return CliCommandQuit{};
            return CliCommandUnknown{ cmd };
        }

    private:
        static std::vector<std::string> tokenize(std::string_view line) {
            std::vector<std::string> tokens;
            std::istringstream iss{ std::string{line} };
            std::string tok;
            while (iss >> tok)
                tokens.push_back(std::move(tok));
            return tokens;
        }

        static CliCommand parseRelay(const std::vector<std::string>& args) {
            if (args.size() == 2) {
                const auto colon = args[1].rfind(':');
                if (colon == std::string::npos || colon == 0)
                    return CliCommandUnknown{ "relay" };
                auto host = args[1].substr(0, colon);
                auto port = parsePort(args[1].substr(colon + 1));
                if (!port)
                    return CliCommandUnknown{ "relay" };
                return CliCommandRelay{ Wyvern::Endpoint{ std::move(host), *port } };
            }
            if (args.size() == 3) {
                auto port = parsePort(args[2]);
                if (!port)
                    return CliCommandUnknown{ "relay" };
                return CliCommandRelay{ Wyvern::Endpoint{ args[1], *port } };
            }
            return CliCommandUnknown{ "relay" };
        }

        static CliCommand parseId(const std::vector<std::string>& args, std::string_view) {
            if (args.size() != 2 || args[1].empty())
                return CliCommandUnknown{ args[0] };
            return CliCommandConnect{ args[1] };
        }

        static CliCommand parseOpen(const std::vector<std::string>& args) {
            if (args.size() != 2 || args[1].empty())
                return CliCommandUnknown{ "open" };
            return CliCommandOpen{ args[1] };
        }

        static CliCommand parseMessages(const std::vector<std::string>& args) {
            if (args.size() == 1)
                return CliCommandMessages{};
            if (args.size() == 2)
                return CliCommandMessages{ args[1] };
            return CliCommandUnknown{ "messages" };
        }

        static std::optional<std::uint16_t> parsePort(std::string_view text) {
            unsigned value = 0;
            auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (ec != std::errc{} || ptr != text.data() + text.size() || value == 0 || value > 65535)
                return std::nullopt;
            return static_cast<std::uint16_t>(value);
        }
    };

    // Зеркало шины. Пишется только со strand CLI.
    class CliView {
    public:
        std::optional<Wyvern::Endpoint> relay;
        bool relayOpen{ false };
        std::string relayStatus{ "relay не задан" };
        std::vector<std::string> chatIds;
        std::unordered_map<std::string, std::vector<Wyvern::ChatMessage>> messages;
        std::optional<std::string> activeChat;
        CliScreen screen{ CliScreen::Relay };

        void upsertChat(const std::string& peerId) {
            if (messages.contains(peerId))
                return;
            chatIds.push_back(peerId);
            messages.emplace(peerId, std::vector<ChatMessage>{});
        }
    };

    // Сессия CLI: свой io_context, подписка на шину, этапы экрана.
    class CliSession {
        std::shared_ptr<IRuntimeAPI> api;
        std::shared_ptr<ApplicationEvents> events;
        CliIoContext io;
        CliCommandParser parser;
        CliView view;
        std::vector<boost::signals2::scoped_connection> subscriptions;
        std::string selfId;

    public:
        CliSession(std::shared_ptr<IRuntimeAPI> runtimeApi,
            std::shared_ptr<ApplicationEvents> bus,
            std::string nodeId)
            : api(std::move(runtimeApi))
            , events(std::move(bus))
            , selfId(std::move(nodeId))
        {
            subscribe();
        }

        void start() {
            io.post([this] {
                printBanner();
                enterRelay();
                printPrompt();
                });
        }

        // Ждёт конец этапов. false — exit/quit, getline больше не звать.
        bool submitLine(std::string line) {
            auto done = std::make_shared<std::promise<bool>>();
            auto result = done->get_future();
            io.post([this, line = std::move(line), done] {
                runLine(line, done);
                });
            return result.get();
        }

    private:
        void subscribe() {
            subscriptions.push_back(events->relayDialed.connect([this](Wyvern::Endpoint ep) {
                io.post([this, ep] {
                    view.relay = ep;
                    view.relayStatus = "набор " + ep.to_ws_url();
                    std::cout << "[relay] " << view.relayStatus << "\n";
                    });
                }));
            subscriptions.push_back(events->relayOpen.connect([this](Wyvern::Endpoint ep) {
                io.post([this, ep] {
                    view.relay = ep;
                    view.relayOpen = true;
                    view.relayStatus = "открыт " + ep.to_ws_url();
                    std::cout << "[relay] " << view.relayStatus << "\n";
                    if (view.screen == CliScreen::Relay)
                        enterHub();
                    printPrompt();
                    });
                }));
            subscriptions.push_back(events->relayFailed.connect([this](std::string reason) {
                io.post([this, reason = std::move(reason)] {
                    view.relayOpen = false;
                    view.relayStatus = "ошибка: " + reason;
                    std::cout << "[relay] " << view.relayStatus << "\n";
                    });
                }));
            subscriptions.push_back(events->peerDialed.connect([this](std::string peerId) {
                io.post([this, peerId = std::move(peerId)] {
                    std::cout << "[peer] набор " << peerId << "\n";
                    });
                }));
            subscriptions.push_back(events->chatUpserted.connect([this](std::string peerId) {
                io.post([this, peerId = std::move(peerId)] {
                    view.upsertChat(peerId);
                    std::cout << "[chat] " << peerId << "\n";
                    });
                }));
            subscriptions.push_back(events->messageArrived.connect([this](Wyvern::ChatMessage msg) {
                io.post([this, msg = std::move(msg)] {
                    view.upsertChat(msg.peerId);
                    view.messages[msg.peerId].push_back(msg);
                    if (view.activeChat && *view.activeChat == msg.peerId)
                        printMessage(view.messages[msg.peerId].back());
                    });
                }));
            subscriptions.push_back(events->chatSnapshot.connect([this](std::vector<std::string> ids) {
                io.post([this, ids = std::move(ids)] {
                    for (const auto& id : ids)
                        view.upsertChat(id);
                    });
                }));
        }

        void runLine(const std::string& line, std::shared_ptr<std::promise<bool>> done) {
            auto seq = std::make_shared<StageSequence>(io.context());
            auto command = std::make_shared<CliCommand>();
            auto finish = [done](bool keepReading) {
                done->set_value(keepReading);
            };

            seq->add([this, line, command](StageSequence::Next next) {
                *command = parser.parse(line);
                next();
                });
            seq->add([this, command, finish](StageSequence::Next next) {
                if (!screenAllows(*command)) {
                    std::cout << "Сначала relay <ip> <port>\n";
                    printPrompt();
                    finish(true);
                    return; // цепочка встаёт: команда отклонена этапом экрана
                }
                next();
                });
            seq->add([this, command, finish](StageSequence::Next next) {
                if (std::holds_alternative<CliCommandQuit>(*command)) {
                    api->callShutdown();
                    finish(false); // до следующего getline не доходим
                    return;
                }
                dispatch(*command);
                next();
                });
            seq->add([this, finish](StageSequence::Next) {
                printPrompt();
                finish(true);
                });
            seq->run();
        }

        bool screenAllows(const CliCommand& command) const {
            if (view.screen != CliScreen::Relay)
                return true;
            return std::holds_alternative<CliCommandRelay>(command)
                || std::holds_alternative<CliCommandHelp>(command)
                || std::holds_alternative<CliCommandQuit>(command)
                || std::holds_alternative<CliCommandEmpty>(command)
                || std::holds_alternative<CliCommandUnknown>(command);
        }

        void dispatch(const CliCommand& command) {
            std::visit([this](const auto& cmd) { handle(cmd); }, command);
        }

        void handle(const CliCommandEmpty&) {}

        void handle(const CliCommandRelay& cmd) {
            api->callConnectToRelay(cmd.endpoint);
        }

        void handle(const CliCommandConnect& cmd) {
            api->callConnectToPeer(cmd.peerId);
            view.activeChat = cmd.peerId;
        }

        void handle(const CliCommandOpen& cmd) {
            view.activeChat = cmd.peerId;
            view.screen = CliScreen::Chat;
            printMessages(cmd.peerId);
        }

        void handle(const CliCommandMessages& cmd) {
            const auto id = cmd.peerId ? cmd.peerId : view.activeChat;
            if (!id) {
                std::cout << "Нет открытого чата. open <id> или messages <id>\n";
                return;
            }
            view.activeChat = *id;
            view.screen = CliScreen::Chat;
            printMessages(*id);
        }

        void handle(const CliCommandChats&) {
            api->callPublishSnapshot();
            printChats();
        }

        void handle(const CliCommandBack&) {
            view.activeChat.reset();
            enterHub();
        }

        void handle(const CliCommandHelp&) { printHelp(); }

        void handle(const CliCommandQuit&) {}

        void handle(const CliCommandUnknown& cmd) {
            std::cout << "Неизвестная команда: " << cmd.raw << "\n";
            printHelp();
        }

        void enterRelay() {
            view.screen = CliScreen::Relay;
            std::cout << "Этап 1. endpoint реле: relay <ip> <port>\n";
        }

        void enterHub() {
            view.screen = CliScreen::Hub;
            view.activeChat.reset();
            std::cout << "Этап 2. чаты и набор ноды. chats | connect <id> | open <id>\n";
        }

        void printBanner() const {
            std::cout << "Wyvern CLI. self id: " << selfId << "\n";
            printHelp();
        }

        void printHelp() const {
            std::cout
                << "Команды:\n"
                << "  relay <ip> <port>     Подключиться к реле\n"
                << "  chats                 Список чатов (id пира)\n"
                << "  connect <id>          Набор ноды по id\n"
                << "  open <id>             Открыть чат и показать сообщения\n"
                << "  messages [id]         Список сообщений чата\n"
                << "  back                  К списку чатов\n"
                << "  help\n"
                << "  quit | exit\n";
        }

        void printChats() const {
            std::cout << "Чаты (" << view.chatIds.size() << "):\n";
            if (view.chatIds.empty()) {
                std::cout << "  (пусто — connect <id> создаёт чат)\n";
                return;
            }
            for (const auto& id : view.chatIds)
                std::cout << "  " << id << "\n";
        }

        void printMessages(const std::string& peerId) const {
            std::cout << "Чат " << peerId << ":\n";
            auto it = view.messages.find(peerId);
            if (it == view.messages.end() || it->second.empty()) {
                std::cout << "  (нет сообщений)\n";
                return;
            }
            for (const auto& msg : it->second)
                printMessage(msg);
        }

        static void printMessage(const Wyvern::ChatMessage& msg) {
            std::cout << "  [" << msg.timestampMs << "] " << msg.body << "\n";
        }

        void printPrompt() const {
            switch (view.screen) {
            case CliScreen::Relay: std::cout << "relay> " << std::flush; break;
            case CliScreen::Hub: std::cout << "hub> " << std::flush; break;
            case CliScreen::Chat:
                std::cout << "chat:" << view.activeChat.value_or("?") << "> " << std::flush;
                break;
            }
        }
    };

}
