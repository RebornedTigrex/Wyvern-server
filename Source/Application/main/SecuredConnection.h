#pragma once

#include "Encryption.h"
#include "LowLevelConnections.h"

#include "Serialize.h"


namespace Wyvern::Network{
	namespace Wire {
		namespace Protocol {

			using Ed25519 = int;//TODO: Заглушка

			struct UnencryptedHeader {
				std::uint8_t version;
				std::string routeID; // Айди маршрута. //TODO: продумать
				std::array <std::byte, 24> nonce;
			};

			struct HandshakePayload {

			};
			struct HandshakeAckPayload {

			};

			struct HandshakeMsg {
				Ed25519 signature;
				HandshakePayload payload;
			};

			struct HandshakeAckMsg {
				Ed25519 signature;
				HandshakeAckPayload payload;
			};

			using WireMessage = std::variant<HandshakeMsg, HandshakeAckMsg>;
		}
		namespace Serialization {
			inline void serialize(std::vector<std::byte>& buf,
				const Protocol::UnencryptedHeader& h) {
				using Wyvern::Binary::serialize;
				serialize(buf, h.version);
				serialize(buf, h.routeID);
				serialize(buf, h.nonce);
			}
			inline void deserialize(const std::byte* data, size_t& pos,
				size_t size, Protocol::UnencryptedHeader& v) {
				using Wyvern::Binary::deserialize;
				deserialize(data, pos, size, v.version);
				deserialize(data, pos, size, v.routeID);
				deserialize(data, pos, size, v.nonce);
			}
		}
	}


	class SecuredConnection : public IConnection{
		template <typename T>
		concept RawConnection =
			std::derived_from<std::decay_t<T>, IConnection> &&
			!std::is_same_v<std::decay_t<T>, SecuredConnection>;

		std::shared_ptr<IConnection> connection;

		std::function<void(std::vector<std::byte>)> onMessageCallback;
	public:
		template <RawConnection T>
		SecuredConnection(std::shared_ptr<T> con = std::make_shared<T>())
			: connection(std::move(con)) {
		}

		void handshakeSeqenceCallback(std::vector<std::byte> incomingMsg) {

		}

		void setOnMessageCallback(std::function<void(std::vector<std::byte>)> callback) override {
			
		}
	public:

		void connect(std::shared_ptr<Wyvern::Endpoint> endpoint) override {//Присоединиться к конкретному реле
			connection->connect()
			connection->send()
		} 
		void connect(std::string PeerIdentification) override {

		}

		void disconnect() override {

		}

		void send(std::vector<std::byte> msg) override {

		}
	};
}