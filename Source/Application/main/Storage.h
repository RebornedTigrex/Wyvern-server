#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <sys/stat.h>
#endif

namespace Wyvern::Storage {

    class IRecordStore {
    public:
        virtual ~IRecordStore() = default;

        virtual std::optional<std::vector<std::byte>> tryLoad(std::string_view key) const = 0;
        virtual void store(std::string_view key, std::span<const std::byte> bytes) = 0;
        virtual void erase(std::string_view key) = 0;
    };

    class FileRecordStore final : public IRecordStore {
        std::filesystem::path root;
        mutable std::mutex mutex;

    public:
        explicit FileRecordStore(std::filesystem::path rootDir)
            : root(std::move(rootDir)) {
            if (root.empty())
                throw std::invalid_argument("storage: empty root");
        }

        std::optional<std::vector<std::byte>> tryLoad(std::string_view key) const override {
            std::lock_guard lock(mutex);
            const auto path = pathFor(key);
            if (!std::filesystem::exists(path))
                return std::nullopt;

            std::ifstream in(path, std::ios::binary);
            if (!in)
                throw std::runtime_error("storage: cannot read " + path.string());

            in.seekg(0, std::ios::end);
            const auto n = in.tellg();
            if (n < 0)
                throw std::runtime_error("storage: cannot size " + path.string());
            in.seekg(0, std::ios::beg);

            std::vector<std::byte> bytes(static_cast<std::size_t>(n));
            if (n > 0) {
                in.read(reinterpret_cast<char*>(bytes.data()), n);
                if (!in)
                    throw std::runtime_error("storage: short read " + path.string());
            }
            return bytes;
        }

        void store(std::string_view key, std::span<const std::byte> bytes) override {
            std::lock_guard lock(mutex);
            std::filesystem::create_directories(root);

            const auto path = pathFor(key);
            const auto tmp = path.string() + ".tmp";

            {
                std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
                if (!out)
                    throw std::runtime_error("storage: cannot write " + tmp);
                if (!bytes.empty())
                    out.write(reinterpret_cast<const char*>(bytes.data()),
                        static_cast<std::streamsize>(bytes.size()));
                out.flush();
                if (!out)
                    throw std::runtime_error("storage: write failed " + tmp);
            }
            restrictToOwner(tmp);
            replaceFile(tmp, path);
        }

        void erase(std::string_view key) override {
            std::lock_guard lock(mutex);
            std::filesystem::remove(pathFor(key));
        }

    private:
        std::filesystem::path pathFor(std::string_view key) const {
            if (key.empty()
                || key.find('/') != std::string_view::npos
                || key.find('\\') != std::string_view::npos
                || key.find("..") != std::string_view::npos)
                throw std::invalid_argument("storage: illegal key");
            return root / (std::string(key) + ".bin");
        }

        static void restrictToOwner(const std::filesystem::path& path) {
#ifdef _WIN32
            (void)path;
#else
            ::chmod(path.c_str(), S_IRUSR | S_IWUSR);
#endif
        }

        static void replaceFile(const std::filesystem::path& tmp, const std::filesystem::path& path) {
            std::error_code ec;
            std::filesystem::rename(tmp, path, ec);
            if (!ec)
                return;
            std::filesystem::remove(path, ec);
            std::filesystem::rename(tmp, path);
        }
    };

}
