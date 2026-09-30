#pragma once

#include <sodium.h>
#include <filesystem>
#include <fstream>
#include <array>
#include <span>
#include <stdexcept>

#include <mutex>

namespace Wyvern::Encryption {

    class IdentityStore {
        std::mutex mtx;
        std::array<unsigned char, crypto_sign_SECRETKEYBYTES> sk_{};
        std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> pk_{};
        std::string id_;

        static std::string fingerprint(std::span<const unsigned char, 32> pk) {


            std::array<unsigned char, crypto_hash_sha256_BYTES> hash{};
            crypto_hash_sha256(hash.data(), pk.data(), pk.size());

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

        void derive_id() {
            id_ = fingerprint(std::span<const unsigned char, 32>{pk_.data(), pk_.size()});
        }

    public:
        IdentityStore(const IdentityStore&) = delete;
        IdentityStore& operator=(const IdentityStore&) = delete;

        IdentityStore() : noexcept {}

        IdentityStore load_or_create(const std::filesystem::path& path) {
            if (sodium_init() < 0)
                throw std::runtime_error("sodium_init failed");

            IdentityStore store;

            mtx.lock();
            if (std::filesystem::exists(path)) {
                std::ifstream in(path, std::ios::binary);
                std::array<unsigned char, crypto_sign_SEEDBYTES> seed{};
                in.read(reinterpret_cast<char*>(seed.data()), seed.size());
                if (!in || in.gcount() != static_cast<std::streamsize>(seed.size()))
                    throw std::runtime_error("identity file truncated");

                crypto_sign_seed_keypair(store.pk_.data(), store.sk_.data(), seed.data());
                sodium_memzero(seed.data(), seed.size());
            }
            else {
                if (crypto_sign_keypair(store.pk_.data(), store.sk_.data()) != 0)
                    throw std::runtime_error("crypto_sign_keypair failed");

                std::filesystem::create_directories(path.parent_path());
                {
                    std::ofstream out(path, std::ios::binary | std::ios::trunc);
                    // sk = seed || pk; seed — первые 32 байта
                    out.write(reinterpret_cast<const char*>(store.sk_.data()),
                        crypto_sign_SEEDBYTES);
                }
                std::filesystem::permissions(path,
                    std::filesystem::perms::owner_read |
                    std::filesystem::perms::owner_write,
                    std::filesystem::perm_options::replace);
            }

            store.derive_id();
            mtx.unlock();
            return store;
        }

        std::string_view id() const noexcept { return id_; }

        std::span<const unsigned char, 32> pk() const noexcept 
        {
            return std::span<const unsigned char, 32>{pk_.data(), pk_.size()};
        }

        std::array<unsigned char, crypto_sign_BYTES>
            sign(std::span<const unsigned char> msg) const 
        {
            std::array<unsigned char, crypto_sign_BYTES> sig{};
            unsigned long long siglen = 0;
            if (crypto_sign_detached(sig.data(), &siglen,
                msg.data(), msg.size(),
                sk_.data()) != 0)
                throw std::runtime_error("crypto_sign_detached failed");
            return sig;
        }

        static bool verify(std::span<const unsigned char, 32> pk,
            std::span<const unsigned char> msg,
            std::span<const unsigned char, 64> sig) 
        {
            return crypto_sign_verify_detached(
                sig.data(), msg.data(), msg.size(), pk.data()) == 0;
        }
    };

}