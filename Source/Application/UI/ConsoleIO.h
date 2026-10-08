#pragma once

#include "CliSession.h"
#include "RuntimeApi.h"

#include <iostream>
#include <memory>
#include <string>
#include <thread>

class ConsoleIO {
    std::shared_ptr<IRuntimeAPI> runtimeAPI;
    Wyvern::Ui::CliSession session;
    std::jthread consoleThread;

public:
    ConsoleIO(std::shared_ptr<IRuntimeAPI> api,
        std::shared_ptr<Wyvern::ApplicationEvents> events,
        std::string selfId)
        : runtimeAPI(std::move(api))
        , session(runtimeAPI, std::move(events), std::move(selfId))
    {
        session.start();
        consoleThread = std::jthread([this](std::stop_token st) {
            lineParseLoop(st);
            });
    }

    ~ConsoleIO() {
        if (consoleThread.joinable())
            consoleThread.request_stop();
    }

private:
    void lineParseLoop(std::stop_token st) {
        std::string line;
        while (!st.stop_requested()) {
            if (!std::getline(std::cin, line))
                break;
            session.postLine(std::move(line));
        }
    }
};
