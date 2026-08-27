#include "Runtime.h"

boost::asio::ip::address Utilities::getSelfAddr(boost::asio::io_context& io) {//Не совсем мне нравится такой способ нахождения реле, однако пусть будет, ибо в любом случае оно будет лежать в месте с белым адресом.
    using udp = boost::asio::ip::udp;
    udp::socket s{ io };
    s.open(udp::v4());
    // connect UDP ничего не шлёт, только выбирает маршрут
    s.connect(udp::endpoint{ boost::asio::ip::make_address("8.8.8.8"), 53 });
    return s.local_endpoint().address();
}


void RelayServer::setupServer() {
    try {
        const boost::asio::ip::tcp::endpoint
            endpoint(boost::asio::ip::make_address("0.0.0.0"), 9002);

        acceptor = std::make_unique<boost::asio::ip::tcp::acceptor>(ioc);
        acceptor->open(endpoint.protocol());
        acceptor->set_option(boost::asio::socket_base::reuse_address(true));
        acceptor->bind(endpoint);
        acceptor->listen(boost::asio::socket_base::max_listen_connections);

        acceptConnections();
        return true;
    }
    catch (const std::exception& e) {
        acceptor.reset();
        return false;
    }
}











void SignalingServer::start() {

    try {
        const boost::asio::ip::tcp::endpoint 
            endpoint(boost::asio::ip::make_address("0.0.0.0"), 9002);

        acceptor = std::make_unique<boost::asio::ip::tcp::acceptor>(ioc);
        acceptor->open(endpoint.protocol());
        acceptor->set_option(boost::asio::socket_base::reuse_address(true));
        acceptor->bind(endpoint);
        acceptor->listen(boost::asio::socket_base::max_listen_connections);

        acceptConnections();
        return true;
    }
    catch (const std::exception& e) {
        acceptor.reset();
        return false;
    }
}

void SignalingServer::acceptConnections() {
    if (!running_.load() || !acceptor_ || !acceptor_->is_open()) {
        return;
    }

    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(ioc_);
    acceptor_->async_accept(*socket, [this, socket](const boost::system::error_code& ec) {
        if (!ec) {
            std::cout << "[RelayServer] New connection from " << socket->remote_endpoint() << "\n";
            handleConnection(socket);
        }
        else if (running_.load()) {
            std::cerr << "[RelayServer] Accept error: " << ec.message() << "\n";
        }

        if (running_.load()) {
            acceptConnections();
        }
        });
}

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
