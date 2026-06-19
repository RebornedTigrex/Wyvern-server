#define BOOST_TEST_MODULE DataStorageTests
#include <boost/test/unit_test.hpp>

#include "Application/modules/dataStorage/InMemoryRelayStore.h"
#include <chrono>

using namespace Wyvern::DataStorage;

BOOST_AUTO_TEST_SUITE(InMemoryRelayStoreTests)

BOOST_AUTO_TEST_CASE(RegisterAndGetPeer) {
    InMemoryRelayStore store;

    IRelayStore::RegisteredPeer peer;
    peer.overlayId = "overlay_id_1";
    peer.sessionId = "session_1";
    peer.endpoint = "127.0.0.1:12345";
    peer.registrationTime = 
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

    BOOST_CHECK(store.registerPeer(peer));
    BOOST_CHECK(store.isPeerRegistered("overlay_id_1"));

    auto retrieved = store.getPeer("overlay_id_1");
    BOOST_REQUIRE(retrieved.has_value());
    BOOST_CHECK_EQUAL(retrieved->overlayId, "overlay_id_1");
    BOOST_CHECK_EQUAL(retrieved->sessionId, "session_1");
}

BOOST_AUTO_TEST_CASE(UnregisterPeer) {
    InMemoryRelayStore store;

    IRelayStore::RegisteredPeer peer;
    peer.overlayId = "overlay_id_1";
    peer.sessionId = "session_1";
    peer.endpoint = "127.0.0.1:12345";
    peer.registrationTime = 
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

    store.registerPeer(peer);
    BOOST_CHECK(store.isPeerRegistered("overlay_id_1"));

    BOOST_CHECK(store.unregisterPeer("overlay_id_1"));
    BOOST_CHECK(!store.isPeerRegistered("overlay_id_1"));
}

BOOST_AUTO_TEST_CASE(GetAllPeers) {
    InMemoryRelayStore store;

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    for (int i = 0; i < 5; ++i) {
        IRelayStore::RegisteredPeer peer;
        peer.overlayId = "overlay_id_" + std::to_string(i);
        peer.sessionId = "session_" + std::to_string(i);
        peer.endpoint = "127.0.0.1:" + std::to_string(12345 + i);
        peer.registrationTime = now;
        store.registerPeer(peer);
    }

    auto peers = store.getAllPeers();
    BOOST_CHECK_EQUAL(peers.size(), 5);
}

BOOST_AUTO_TEST_CASE(CreateAndGetSession) {
    InMemoryRelayStore store;

    std::string sessionId = store.createSession("overlay_a", "overlay_b");
    BOOST_CHECK(!sessionId.empty());

    auto session = store.getSession(sessionId);
    BOOST_REQUIRE(session.has_value());
    BOOST_CHECK_EQUAL(session->peerAOverlayId, "overlay_a");
    BOOST_CHECK_EQUAL(session->peerBOverlayId, "overlay_b");
    BOOST_CHECK(session->isActive);
}

BOOST_AUTO_TEST_CASE(CloseSession) {
    InMemoryRelayStore store;

    std::string sessionId = store.createSession("overlay_a", "overlay_b");
    BOOST_CHECK(store.closeSession(sessionId));

    auto session = store.getSession(sessionId);
    BOOST_REQUIRE(session.has_value());
    BOOST_CHECK(!session->isActive);
}

BOOST_AUTO_TEST_CASE(EnqueueAndDequeueMessages) {
    InMemoryRelayStore store;

    IRelayStore::PendingMessage msg1;
    msg1.targetOverlayId = "overlay_b";
    msg1.envelope = {0x01, 0x02, 0x03};
    msg1.createdAt = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    msg1.retryCount = 0;

    std::string msgId1 = store.enqueuePendingMessage(msg1);
    BOOST_CHECK(!msgId1.empty());

    auto dequeued = store.dequeueNextPendingMessage();
    BOOST_REQUIRE(dequeued.has_value());
    BOOST_CHECK_EQUAL(dequeued->messageId, msgId1);
    BOOST_CHECK_EQUAL(dequeued->targetOverlayId, "overlay_b");
}

BOOST_AUTO_TEST_CASE(AcknowledgeMessage) {
    InMemoryRelayStore store;

    IRelayStore::PendingMessage msg;
    msg.targetOverlayId = "overlay_b";
    msg.envelope = {0x01, 0x02, 0x03};
    msg.createdAt = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    msg.retryCount = 0;

    std::string msgId = store.enqueuePendingMessage(msg);
    
    // Не деквируем, просто подтверждаем
    BOOST_CHECK(store.acknowledgeMessage(msgId));

    // После подтверждения, очередь должна быть пуста
    auto next = store.dequeueNextPendingMessage();
    BOOST_CHECK(!next.has_value());
}

BOOST_AUTO_TEST_CASE(GetPendingMessagesFor) {
    InMemoryRelayStore store;

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    for (int i = 0; i < 3; ++i) {
        IRelayStore::PendingMessage msg;
        msg.targetOverlayId = "overlay_b";
        msg.envelope = {static_cast<uint8_t>(i)};
        msg.createdAt = now;
        msg.retryCount = 0;
        store.enqueuePendingMessage(msg);
    }

    for (int i = 0; i < 2; ++i) {
        IRelayStore::PendingMessage msg;
        msg.targetOverlayId = "overlay_c";
        msg.envelope = {static_cast<uint8_t>(i)};
        msg.createdAt = now;
        msg.retryCount = 0;
        store.enqueuePendingMessage(msg);
    }

    auto forB = store.getPendingMessagesFor("overlay_b");
    BOOST_CHECK_EQUAL(forB.size(), 3);

    auto forC = store.getPendingMessagesFor("overlay_c");
    BOOST_CHECK_EQUAL(forC.size(), 2);
}

BOOST_AUTO_TEST_CASE(ClearStore) {
    InMemoryRelayStore store;

    IRelayStore::RegisteredPeer peer;
    peer.overlayId = "overlay_id_1";
    peer.sessionId = "session_1";
    peer.endpoint = "127.0.0.1:12345";
    peer.registrationTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    store.registerPeer(peer);

    store.createSession("overlay_a", "overlay_b");

    IRelayStore::PendingMessage msg;
    msg.targetOverlayId = "overlay_b";
    msg.envelope = {0x01};
    msg.createdAt = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    msg.retryCount = 0;
    store.enqueuePendingMessage(msg);

    BOOST_CHECK(store.isPeerRegistered("overlay_id_1"));
    
    store.clear();

    BOOST_CHECK(!store.isPeerRegistered("overlay_id_1"));
    auto peers = store.getAllPeers();
    BOOST_CHECK_EQUAL(peers.size(), 0);
}

BOOST_AUTO_TEST_CASE(DebugInfo) {
    InMemoryRelayStore store;

    IRelayStore::RegisteredPeer peer;
    peer.overlayId = "overlay_id_1";
    peer.sessionId = "session_1";
    peer.endpoint = "127.0.0.1:12345";
    peer.registrationTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    store.registerPeer(peer);

    std::string debug = store.getDebugInfo();
    BOOST_CHECK(!debug.empty());
    BOOST_CHECK(debug.find("peers=1") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
