#include "Runtime.h"










void PeerWork::startConnectionTo(std::string_view ipAddr){
    
}




void PeerWork::loop() {
    while (1) {

    }
}






















void PeerWork::setupRuntime() {
    config.bindAddress = "127.0.0.1";         // ← обязательно

    auto pc = std::make_shared<rtc::PeerConnection>(config);

    pc->onLocalDescription([](rtc::Description desc) {
        std::cout << "\n========== LOCAL DESCRIPTION (скопируй ВСЁ это в offerer → 1) ==========\n";
        std::cout << std::string(desc) << std::endl;
        std::cout << "========== КОНЕЦ ==========\n";
        });

    pc->onLocalCandidate([](rtc::Candidate cand) {
        std::cout << "\n--- LOCAL CANDIDATE (скопируй в offerer → 2) ---\n";
        std::cout << std::string(cand) << "\n";
        });

    pc->onStateChange([](rtc::PeerConnection::State state) {
        std::cout << "[PC State] " << state << std::endl;
        if (state == rtc::PeerConnection::State::Failed)
            std::cerr << "!!! FAILED\n";
        });

    pc->onIceStateChange([](rtc::PeerConnection::IceState state) {
        std::cout << "[ICE State] " << state << std::endl;
        });

    std::shared_ptr<rtc::DataChannel> dc;

    pc->onDataChannel([&](std::shared_ptr<rtc::DataChannel> incoming) {
        std::cout << "\n>>> Got DataChannel: " << incoming->label() << " <<<\n";
        dc = incoming;

        dc->onOpen([dc]() {
            std::cout << ">>> DataChannel OPEN <<<\n";
            const std::string json = R"({"isSuccess":"True"})";
            std::cout << "Sending: " << json << std::endl;
            dc->send(json);
            });

        dc->onMessage([](auto data) {
            if (std::holds_alternative<std::string>(data))
                std::cout << "[Received] " << std::get<std::string>(data) << std::endl;
            });
        });



}
