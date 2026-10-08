#pragma once

#include "SupportUtils.h"

#include <string>

// Командный порт. UI только зовёт, исполнение — на io_context ядра.
class IRuntimeAPI {
public:
    IRuntimeAPI() = default;
    virtual ~IRuntimeAPI() = default;

    virtual void callShutdown() = 0;

    virtual void callConnectToPeer(const std::string& remoteID) = 0;

    virtual void callConnectToRelay(const Wyvern::Endpoint& relay) = 0;
    virtual void callConnectToRelay(const std::string& relayID) = 0;

    // Ядро заново публикует снимок чатов. UI не читает модель напрямую.
    virtual void callPublishSnapshot() = 0;
};
