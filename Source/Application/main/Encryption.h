#pragma once

#include <sodium.h>
#include <tuple>
#include <span>
#include <string>
#include <exception>


namespace Wyvern::Encryption {

    inline std::pair<std::array<unsigned char, crypto_sign_SECRETKEYBYTES>, std::array<unsigned char, crypto_sign_PUBLICKEYBYTES>> createKeyPair()
    {
        std::array<unsigned char, crypto_sign_SECRETKEYBYTES> secretKey{};
        std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> publicKey{};

        crypto_sign_keypair(publicKey.data(), secretKey.data());
        return { secretKey,publicKey };
    }

    inline std::string takeFingerprint(std::span<const unsigned char, 32> publicKey) {
        std::array<unsigned char, crypto_hash_sha256_BYTES> hash{};

        crypto_hash_sha256(hash.data(), publicKey.data(), publicKey.size());

        const std::size_t enc_len =
            sodium_base64_encoded_len(hash.size(),
                sodium_base64_VARIANT_URLSAFE_NO_PADDING);

        std::string out(enc_len, '\0');
        sodium_bin2base64(out.data(), out.size(), hash.data(), hash.size(),
            sodium_base64_VARIANT_URLSAFE_NO_PADDING);

        if (!out.empty() && out.back() == '\0')
            out.pop_back(); // sodium пишет terminating NUL в буфер
        return out;
    }

    inline std::array<unsigned char, crypto_sign_BYTES>
        sign(std::array<unsigned char, crypto_sign_SECRETKEYBYTES> secretKey, std::span<const unsigned char> msg)
    {
        std::array<unsigned char, crypto_sign_BYTES> sig{};
        unsigned long long siglen = 0;
        if (crypto_sign_detached(sig.data(), &siglen,
            msg.data(), msg.size(),
            secretKey.data()) != 0)
            throw std::runtime_error("crypto_sign_detached failed");
        return sig;
    }
}

namespace Wyvern {
    struct IdentityEntity {
        std::array<unsigned char, crypto_sign_SECRETKEYBYTES> _secretKey{};
        std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> _publicKey{};
        std::string id;

        IdentityEntity() {
            auto keys = Encryption::createKeyPair();

            _secretKey = keys.first;
            _publicKey = keys.second;
        }

        std::string_view node_id() const noexcept { return id; }

        std::span<const unsigned char, 32> publicKey() const noexcept {
            return std::span<const unsigned char, 32>{_publicKey.data(), _publicKey.size()};
        }

        std::array<unsigned char, crypto_sign_BYTES>
            sign(std::span<const unsigned char> msg) const {

            std::array<unsigned char, crypto_sign_BYTES> sig{};
            unsigned long long siglen = 0;
            if (crypto_sign_detached(sig.data(), &siglen,
                msg.data(), msg.size(), _secretKey.data()) != 0)
                throw std::runtime_error("crypto_sign_detached failed");
            return sig;
        }
    };

    class Identity {
        static std::atomic<std::shared_ptr<const IdentityEntity>>& slot() {
            static std::atomic<std::shared_ptr<const IdentityEntity>> s;
            return s;
        }
    public:
        static void publish(std::shared_ptr<const IdentityEntity> next) {
            slot().store(std::move(next));
        }
        static std::shared_ptr<const IdentityEntity> current() {
            auto p = slot().load();
            if (!p) throw std::logic_error("identity not published");
            return p;
        }
        static std::string getSelfID() {
            return std::string{ current()->node_id() };
        }
    };
};