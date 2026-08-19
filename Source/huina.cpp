#include "rtc/rtc.hpp"
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using namespace std::chrono_literals;

int main() {
    //rtc::InitLogger(rtc::LogLevel::Debug);   // важно для диагностики

    rtc::Configuration config;
    config.bindAddress = "127.0.0.1";         // ← обязательно для loopback

    auto pc = std::make_shared<rtc::PeerConnection>(config);

    pc->onLocalDescription([](rtc::Description desc) {
        std::cout << "\n========== LOCAL DESCRIPTION (скопируй ВСЁ это в answerer → 1) ==========\n";
        std::cout << std::string(desc) << std::endl;
        std::cout << "========== КОНЕЦ ==========\n";
        });

    pc->onLocalCandidate([](rtc::Candidate cand) {
        std::cout << "\n--- LOCAL CANDIDATE (скопируй в answerer → 2) ---\n";
        std::cout << std::string(cand) << "\n";
        });

    pc->onStateChange([](rtc::PeerConnection::State state) {
        std::cout << "[PC State] " << state << std::endl;
        if (state == rtc::PeerConnection::State::Failed) {
            std::cerr << "!!! FAILED — проверь bindAddress и правильность paste\n";
        }
        });

    pc->onIceStateChange([](rtc::PeerConnection::IceState state) {
        std::cout << "[ICE State] " << state << std::endl;
        });

    auto dc = pc->createDataChannel("test");

    dc->onOpen([dc]() {
        std::cout << "\n>>> DataChannel OPEN <<<\n";
        const std::string json = R"({"isSuccess":"True"})";
        std::cout << "Sending: " << json << std::endl;
        dc->send(json);
        });

    dc->onMessage([](auto data) {
        if (std::holds_alternative<std::string>(data))
            std::cout << "[Received] " << std::get<std::string>(data) << std::endl;
        });

    std::this_thread::sleep_for(300ms);

    bool running = true;
    while (running) {
        std::cout << "\n0 Exit | 1 Paste remote description | 2 Paste candidate | 3 Send msg\n> ";
        int cmd;
        if (!(std::cin >> cmd)) break;
        std::cin.ignore();

        try {
            switch (cmd) {
            case 0: running = false; break;
            case 1: {
                std::cout << "Вставь remote SDP (пустая строка = конец):\n";
                std::string sdp, line;
                while (std::getline(std::cin, line) && !line.empty())
                    sdp += line + "\r\n";
                pc->setRemoteDescription(sdp);
                break;
            }
            case 2: {
                std::cout << "Вставь candidate:\n";
                std::string cand;
                std::getline(std::cin, cand);
                if (!cand.empty())
                    pc->addRemoteCandidate(cand);
                break;
            }
            case 3: {
                if (dc && dc->isOpen()) {
                    std::string msg;
                    std::getline(std::cin, msg);
                    dc->send(msg);
                }
                else std::cout << "Channel not open\n";
                break;
            }
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
        }
    }
    return 0;
}