#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace Wyvern::DataStorage {

/// Интерфейс для durable-операций с хранилищем узлов.
/// Все операции работают с чистыми C++ типами, без Python.
/// Реализация может использовать Python mesh_node_db внутри, но не выпускает py:: наружу.
class INodeStore {
public:
    virtual ~INodeStore() = default;

    /// Информация о пире (минимальная для хранения)
    struct PeerInfo {
        std::string peerId;              // уникальный идентификатор пира
        std::vector<uint8_t> publicKey;  // Ed25519 публичный ключ (32 байта)
        std::string displayName;         // отображаемое имя
    };

    /// Информация о чате
    struct ChatInfo {
        std::string chatId;              // уникальный идентификатор чата
        std::string peerIds;             // разделенные запятыми ID пиров в чате
        std::string metadata;            // дополнительные метаданные
    };

    /// Информация о сообщении
    struct MessageInfo {
        std::string messageId;           // уникальный идентификатор
        std::string chatId;              // ID чата, которому принадлежит сообщение
        std::string senderPeerId;        // ID отправителя
        std::vector<uint8_t> ciphertext; // зашифрованное содержимое
        uint64_t timestamp;              // временная метка (мс)
    };

    /// Обеспечивает существование пира в хранилище.
    /// Если пир уже существует, обновляет его данные.
    /// @param peer информация о пире
    /// @return true если пир был создан или обновлен успешно
    virtual bool ensurePeer(const PeerInfo& peer) = 0;

    /// Обеспечивает существование чата в хранилище.
    /// Если чат уже существует, обновляет его данные.
    /// @param chat информация о чате
    /// @return true если чат был создан или обновлен успешно
    virtual bool ensureChat(const ChatInfo& chat) = 0;

    /// Сохраняет сообщение в хранилище.
    /// @param message информация о сообщении
    /// @return true если сообщение было сохранено успешно
    virtual bool saveMessage(const MessageInfo& message) = 0;

    /// Загружает сообщения для чата, начиная с указанного смещения.
    /// @param chatId ID чата
    /// @param offset смещение с начала (0 = с самого начала)
    /// @param limit максимальное количество сообщений для загрузки
    /// @return вектор сообщений, отсортированный по timestamp возрастанию
    virtual std::vector<MessageInfo> loadMessages(
        const std::string& chatId,
        uint64_t offset = 0,
        uint64_t limit = 1000
    ) = 0;

    /// Получает пира по ID.
    /// @param peerId ID пира
    /// @return информация о пире, или std::nullopt если не найден
    virtual std::optional<PeerInfo> getPeer(const std::string& peerId) = 0;

    /// Получает чат по ID.
    /// @param chatId ID чата
    /// @return информация о чате, или std::nullopt если не найден
    virtual std::optional<ChatInfo> getChat(const std::string& chatId) = 0;

    /// Удаляет пира из хранилища.
    /// @param peerId ID пира
    /// @return true если пир был удален успешно
    virtual bool deletePeer(const std::string& peerId) = 0;

    /// Удаляет чат из хранилища.
    /// @param chatId ID чата
    /// @return true если чат был удален успешно
    virtual bool deleteChat(const std::string& chatId) = 0;
};

} // namespace Wyvern::DataStorage
