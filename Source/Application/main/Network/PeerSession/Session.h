#include <memory>

#include <rtc/rtc.h>
#include <Network/PeerSession/SignalingInitiator.h>

namespace Wyvern::Network::PeerSession {

    class NodeConnection { //TODO: Пока не реализовано 
        rtc::Configuration config{};
        std::unique_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::DataChannel> dc;

        std::unique_ptr<SignalingInitiator> signalingUnit;

    public:
        NodeConnection(std::unique_ptr<SignalingInitiator> sigUnit) : 
            signalingUnit(std::move(sigUnit)), pc(std::make_unique<rtc::PeerConnection>(config))
        {
            signalingUnit->finalazeInitiatorBuild(std::move(pc));
        }

    };
}
