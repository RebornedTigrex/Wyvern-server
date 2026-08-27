#pragma once

#include "rtc/rtc.hpp"
#include "Boost/asio.hpp"

#include <memory>
#include <iostream>
#include <functional>
#include <map>
#include "boost/json.hpp"

typedef Server;
typedef Node;

namespace Utilities {



	inline boost::asio::ip::address getSelfAddr(boost::asio::io_context& io);
}

enum class NodeActivityStatus {
	Online,
	Offline
};

class RelayServer {
	struct RelayConfiguration {
		boost::asio::ip::address adressIPV4;
	};


	Server serverInstance;

	std::unordered_map<Node,std::vector<boost::json::object>> storedNodes; //FIXME: Возможный техдолг, бутылочное горлышко. Но давайте с этим потом


	bool storeDelayedMessage(Node, boost::json::object);
	bool changeNodeActivity(Node, NodeActivityStatus);

	bool isNodeActive(Node);

public:
	RelayServer();

	void setupServer();
	void serverWorker();

	void onReqest(std::function<void>);

private:
	std::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor;
	boost::asio::io_context ioc;
	
};


class PeerWork {
	rtc::Configuration config;
	std::shared_ptr<rtc::PeerConnection> peerConnection;
	std::shared_ptr<rtc::DataChannel> dc;

	void setupRuntime();

	void startConnectionTo(std::string_view ipAddr);

	void loop();

public:
	PeerWork() : peerConnection(std::make_shared<rtc::PeerConnection>(config)) {
		setupRuntime();
	};

};

class SignalingServer {
	boost::asio::io_context ioc;
	std::shared_ptr<boost::asio::ip::tcp::acceptor> acceptor;

	void start();
public:
	SignalingServer() {};
};
