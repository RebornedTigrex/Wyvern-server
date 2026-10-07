#pragma once

#include <thread>
#include <string>
#include <vector>

class IRuntimeAPI {// TODO: Что-то придумать с этим непонятным интерфейсом
public:

    IRuntimeAPI() = default;
    virtual ~IRuntimeAPI() = default;

    virtual void callShutdown() = 0;

    virtual void callConnectToPeer(const std::string& remoteID) = 0;

    virtual void callConnectToRelay(const Wyvern::Endpoint& relay) = 0;
    virtual void callConnectToRelay(const std::string& relayID) = 0;
};

class ConsoleIO {
    std::shared_ptr<IRuntimeAPI> runtimeAPI;
    std::jthread consoleThread;

public:
    explicit ConsoleIO(std::shared_ptr<IRuntimeAPI> api)
        : runtimeAPI(std::move(api))
    {
        consoleThread = std::jthread([this](std::stop_token st) {
            lineParseLoop(st);
            });
    }

    ~ConsoleIO() {
        if (consoleThread.joinable()) {
            consoleThread.request_stop();
        }
    }

private:
    void lineParseLoop(std::stop_token st) {
        std::string line;

        while (!st.stop_requested()) {
            if (!std::getline(std::cin, line)) {
                break; // EOF / cin закрыт
            }
            parseLine(line);
        }
    }

    static std::vector<std::string> tokenize(std::string_view line) {
        std::vector<std::string> tokens;
        std::istringstream iss{ std::string{line} };
        std::string tok;
        while (iss >> tok) {
            tokens.push_back(std::move(tok));
        }
        return tokens;
    }

    void parseLine(std::string_view line) {
        auto args = tokenize(line);
        if (args.empty()) {
            return;
        }

        const std::string& cmd = args[0];

        if (cmd == "connect") {
            handleConnect(args);
        }
        else if (cmd == "disconnect") {
            handleDisconnect(args);
        }
        else if (cmd == "help") {
            printHelp();
        }
        else if (cmd == "quit" || cmd == "exit") {
            runtimeAPI->callShutdown();
            consoleThread.request_stop();
        }
        else {
            std::cerr << "Unknown command: " << cmd << "\n"
                << "Type 'help' for list of commands.\n";
        }
    }

    void handleConnect(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            std::cerr << "Usage: connect <peer>\n";
            return;
        }
        runtimeAPI->callConnectToPeer(args[1]);
    }

    void handleDisconnect(const std::vector<std::string>& args) {
        if (args.size() < 2) {
            printWIPmsg();
            //std::cerr << "Usage: disconnect <peer>\n";
            return;
        }
        //runtimeAPI->callDisconnectFromPeer(args [1]);
    }

    static void printHelp() {
        std::cout
            << "Commands:\n"
            << "  connect <peer>      Connect to peer\n"
            << "  disconnect <peer>   Disconnect from peer [WIP]\n"
            << "  status              Show current status [WIP]\n"
            << "  help                This message\n"
            << "  quit | exit         Stop runtime [WIP]\n";
    }
    void printWIPmsg() {
        printf("WIP\n");
    }
};