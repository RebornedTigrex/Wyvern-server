#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace Wyvern::DataStorage {

/// Интерфейс для ephemeral-хранилища relay-состояния.
/// Все данные хранятся только в памяти (RAM), не персистим.
/// Используется для управления активными relay-сессиями, регистрациями и очередями форвардинга.
class IRelayStore {
public:
    virtual ~IRelayStore() = default;

    /// Информация о зарегистрированном пире в relay
    struct RegisteredPeer {
        std::string overlayId;           // 32-байтный overlay ID (из Ed25519 pubkey)
        std::string sessionId;           // уникальный идентификатор сессии
        std::string endpoint;            // адрес сокета пира (для direct коммуникации)
        uint64_t registrationTime;       // время регистрации (мс с эпохи)
    };

    /// Информация о pending-сообщении для форвардинга
    struct PendingMessage {
        std::string messageId;           // уникальный ID сообщения
        std::string targetOverlayId;     // целевой overlay ID
        std::vector<uint8_t> envelope;   // непрозрачная DirectMessageEnvelope
        uint64_t createdAt;              // время создания (мс)
        int retryCount;                  // количество попыток отправки
    };

    /// Информация о relay-сессии между двумя пирами
    struct RelaySession {
        std::string sessionId;           // уникальный ID сессии
        std::string peerAOverlayId;      // overlay ID первого пира
        std::string peerBOverlayId;      // overlay ID второго пира
        uint64_t startTime;              // время создания сессии
        bool isActive;                   // активна ли сессия сейчас
    };

    /// Регистрирует пира в relay по overlay ID.
    /// Если пир уже зарегистрирован, обновляет его данные.
    /// @param peer информация о пире
    /// @return true если регистрация успешна
    virtual bool registerPeer(const RegisteredPeer& peer) = 0;

    /// Отменяет регистрацию пира по overlay ID.
    /// @param overlayId overlay ID пира
    /// @return true если отмена успешна
    virtual bool unregisterPeer(const std::string& overlayId) = 0;

    /// Получает информацию о зарегистрированном пире.
    /// @param overlayId overlay ID пира
    /// @return информация о пире, или std::nullopt если не найден
    virtual std::optional<RegisteredPeer> getPeer(const std::string& overlayId) = 0;

    /// Проверяет, зарегистрирован ли пир.
    /// @param overlayId overlay ID пира
    /// @return true если пир зарегистрирован
    virtual bool isPeerRegistered(const std::string& overlayId) = 0;

    /// Получает список всех зарегистрированных пиров.
    /// @return вектор информации о зарегистрированных пирах
    virtual std::vector<RegisteredPeer> getAllPeers() = 0;

    /// Создает новую relay-сессию между двумя пирами.
    /// @param peerAOverlayId overlay ID первого пира
    /// @param peerBOverlayId overlay ID второго пира
    /// @return ID созданной сессии
    virtual std::string createSession(
        const std::string& peerAOverlayId,
        const std::string& peerBOverlayId
    ) = 0;

    /// Закрывает relay-сессию.
    /// @param sessionId ID сессии
    /// @return true если сессия была закрыта
    virtual bool closeSession(const std::string& sessionId) = 0;

    /// Получает информацию о relay-сессии.
    /// @param sessionId ID сессии
    /// @return информация о сессии, или std::nullopt если не найдена
    virtual std::optional<RelaySession> getSession(const std::string& sessionId) = 0;

    /// Добавляет сообщение в очередь форвардинга.
    /// @param message информация о сообщении
    /// @return ID добавленного сообщения
    virtual std::string enqueuePendingMessage(const PendingMessage& message) = 0;

    /// Получает следующее pending-сообщение для отправки.
    /// @return сообщение, или std::nullopt если очередь пуста
    virtual std::optional<PendingMessage> dequeueNextPendingMessage() = 0;

    /// Помечает pending-сообщение как успешно отправленное и удаляет его.
    /// @param messageId ID сообщения
    /// @return true если сообщение было удалено
    virtual bool acknowledgeMessage(const std::string& messageId) = 0;

    /// Получает все pending-сообщения для целевого пира.
    /// @param targetOverlayId целевой overlay ID
    /// @return вектор pending-сообщений
    virtual std::vector<PendingMessage> getPendingMessagesFor(
        const std::string& targetOverlayId
    ) = 0;

    /// Очищает всё ephemeral-состояние (используется в тестах/shutdown).
    virtual void clear() = 0;

    /// Получает статистику хранилища для отладки.
    /// @return строка с информацией о количестве зарегистрированных пиров, сессий и pending-сообщений
    virtual std::string getDebugInfo() = 0;
};

} // namespace Wyvern::DataStorage
