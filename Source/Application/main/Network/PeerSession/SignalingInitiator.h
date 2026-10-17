#include <rtc/rtc.h>


#include <Network/NetMessageBus.h>
#include <Network/LowLevelConnections.h>

namespace Wyvern::Network::PeerSession {
    class SignalingInitiator {
        SignalingBus& signalingBus;
        std::unique_ptr<rtc::PeerConnection> pc;

    public:
        SignalingInitiator(SignalingBus& bus) : signalingBus(bus) {}

        void finalazeInitiatorBuild(std::unique_ptr<rtc::PeerConnection> _pc) {
            pc = std::move(_pc);
        }

        void setupSignalingSequence() {

        }

    };
}