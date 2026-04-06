#pragma once
#include "BaseModule.h"  // Наследование от BaseModule
#include <filesystem>
#include <string>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <shared_mutex>
#include <optional>
#include <vector>
#include <functional>
#include <mutex>

namespace fs = std::filesystem;

class FileCache : public BaseModule {  // UPDATED: Наследник BaseModule
public:
    enum Mode { None = 0, CleanFileType = 1 };

private:
    int fileCacheMode;

    struct CachedFile {
        std::string content;
        std::string mime_type;
        std::chrono::system_clock::time_point last_modified;
        std::chrono::system_clock::time_point last_accessed;
        size_t size;
        fs::path file_path;
    };

    fs::path base_directory_;
    std::unordered_map<std::string, CachedFile> file_cache_;
    std::unordered_map<std::string, std::string> route_to_path_;
    mutable std::shared_mutex cache_mutex_;
    bool cache_enabled_;
    size_t max_cache_size_;
    size_t total_cache_size_;

    // Вспомогательные методы (без изменений)
    std::string get_mime_type(const std::string& extension) const;
    std::string normalize_route(const fs::path& file_path) const;
    std::optional<CachedFile> load_file_from_disk(const fs::path& file_path) const;
    void evict_if_needed();
    void scan_directory(const fs::path& directory);

public:
    // FIXED: Вернул оригинальный конструктор с args (rebuild_file_map() внутри)
    FileCache(const std::string& base_dir, bool enable_cache = true, size_t max_cache = 100, int chache_mode = Mode::None);
    ~FileCache() = default;

    // Запрещаем копирование/перемещение
    FileCache(const FileCache&) = delete;
    FileCache& operator=(const FileCache&) = delete;
    FileCache(FileCache&&) = delete;
    FileCache& operator=(FileCache&&) = delete;

    // UPDATED: Модульные методы (onInitialize логирует, без дублирования)
    bool onInitialize() override;
    void onShutdown() override;

    // Основной API (без изменений)
    void rebuild_file_map();
    std::optional<CachedFile> get_file(const std::string& route);
    std::optional<CachedFile> get_file_by_path(const std::string& file_path);
    bool preload_file(const std::string& route);
    bool evict_from_cache(const std::string& route);
    void clear_cache();

    // Информационные методы (без изменений)
    std::vector<std::string> get_all_routes() const;
    std::vector<std::string> find_routes(const std::string& pattern) const;
    bool route_exists(const std::string& route) const;
    std::optional<std::string> get_mime_type_for_route(const std::string& route) const;
    bool refresh_file(const std::string& route);

    // Структуры для статистики (без изменений)
    struct CacheInfo {
        size_t cached_files_count;
        size_t total_routes_count;
        size_t total_cache_size_bytes;
        size_t max_cache_size;
        bool cache_enabled;
    };
    struct CacheStats {
        struct FileStat {
            std::string route;
            size_t size;
            std::chrono::system_clock::time_point last_accessed;
            std::chrono::system_clock::time_point last_modified;
        };
        std::vector<FileStat> files;
        size_t total_size;
        size_t average_file_size;
    };

    // Статистика (без изменений)
    CacheInfo get_cache_info() const;
    CacheStats get_detailed_stats() const;

    // Геттеры/сеттеры (без изменений)
    std::string get_base_directory() const { return base_directory_.string(); }
    bool is_cache_enabled() const { return cache_enabled_; }
    void set_cache_enabled(bool enabled) { cache_enabled_ = enabled; }
    size_t get_max_cache_size() const { return max_cache_size_; }
    void set_max_cache_size(size_t max_size);
};