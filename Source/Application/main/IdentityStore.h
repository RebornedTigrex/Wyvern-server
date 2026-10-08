#pragma once

#include <Encryption.h>
#include <Storage.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace Wyvern {

    // Знает только ключ записи и формат IdentityEntity.
    // Куда байты ложатся — решает IRecordStore.
    class IdentityStore {
        static constexpr std::string_view kKey = "identity";
        static constexpr char kMagic[11] = "WYVRN_ID01";

        Storage::IRecordStore& store;

    public:
        explicit IdentityStore(Storage::IRecordStore& records) : store(records) {}

        // Файл есть — поднимаем его. Нет — создаём и сразу пишем.
        // Битый файл не перезаписываем: иначе нода молча сменит id.
        std::shared_ptr<IdentityEntity> loadOrCreate() {
            if (auto bytes = store.tryLoad(kKey))
                return std::make_shared<IdentityEntity>(decode(*bytes));

            auto created = std::make_shared<IdentityEntity>();
            store.store(kKey, encode(*created));
            return created;
        }

    private:
        static std::vector<std::byte> encode(const IdentityEntity& entity) {
            std::vector<std::byte> out;
            out.resize(sizeof(kMagic) + entity._secretKey.size() + entity._publicKey.size());

            std::size_t pos = 0;
            std::memcpy(out.data() + pos, kMagic, sizeof(kMagic));
            pos += sizeof(kMagic);
            std::memcpy(out.data() + pos, entity._secretKey.data(), entity._secretKey.size());
            pos += entity._secretKey.size();
            std::memcpy(out.data() + pos, entity._publicKey.data(), entity._publicKey.size());
            return out;
        }

        static IdentityEntity decode(const std::vector<std::byte>& bytes) {
            constexpr std::size_t need =
                sizeof(kMagic) + crypto_sign_SECRETKEYBYTES + crypto_sign_PUBLICKEYBYTES;

            if (bytes.size() != need || std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0)
                throw std::runtime_error("identity store: corrupt record");

            std::array<unsigned char, crypto_sign_SECRETKEYBYTES> secret{};
            std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> pub{};
            const auto* p = bytes.data() + sizeof(kMagic);
            std::memcpy(secret.data(), p, secret.size());
            std::memcpy(pub.data(), p + secret.size(), pub.size());
            return IdentityEntity(secret, pub);
        }
    };

}
