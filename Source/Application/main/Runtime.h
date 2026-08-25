#pragma once

#include "rtc/rtc.hpp"
#include <memory>
#include <iostream>


class PeerWork {
	rtc::Configuration config;
	std::shared_ptr<rtc::PeerConnection> peerConnection;
	std::shared_ptr<rtc::DataChannel> dc;

	void setupRuntime();

	void loop();

public:
	PeerWork() : peerConnection(std::make_shared<rtc::PeerConnection>(config)) {
		setupRuntime();
	};

};
