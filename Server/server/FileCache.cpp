#include "FileCache.h"
#include <iostream>
#include <fstream>
#include <algorithm>  // Для std::transform
#include <sstream>
#include <iomanip>
#include <ctime>
#include <unordered_map>  // Для mime_types
#include <chrono>  // Уже в .h, но для ясности

namespace fs = std::filesystem;

// Вспомогательные функции (как в оригинале)
namespace {
    // Конвертация времени файловой системы в системное время
    std::chrono::system_clock::time_point file_time_to_system_time(const fs::file_time_type& ftime) {
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        return sctp;
    }

    // Функция для безопасного чтения файла
    std::optional<std::string> read_file_contents(const fs::path& file_path) {
        try {
            if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
                return std::nullopt;
            }
            std::ifstream file(file_path.string(), std::ios::binary | std::ios::ate);
            if (!file) {
                return std::nullopt;
            }
            std::streamsize size = file.tellg();
            if (size <= 0) {
                return std::string(); // Пустой файл
            }
            file.seekg(0, std::ios::beg);
            std::string content;
            content.resize(size);
            if (file.read(&content[0], size)) {
                return content;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error reading file " << file_path << ": " << e.what() << std::endl;
        }
        return std::nullopt;
    }
}

// Конструктор (как оригинал, с вызовом rebuild_file_map)
FileCache::FileCache(const std::string& base_dir, bool enable_cache, size_t max_cache, int chache_mode)
    : BaseModule("File Cache Module"), fileCacheMode(chache_mode), cache_enabled_(enable_cache), max_cache_size_(max_cache), total_cache_size_(0) {
    base_directory_ = fs::absolute(base_dir);
    if (!fs::exists(base_directory_) || !fs::is_directory(base_directory_)) {
        throw std::runtime_error("Base directory does not exist or is not accessible: " + base_dir);
    }
    rebuild_file_map();  // Инициализируем карту маршрутов
    std::cout << "FileCache constructed for " << base_directory_ << std::endl;
}

// onInitialize (модульный: лог + проверка)
bool FileCache::onInitialize() {
    if (route_to_path_.empty()) {
        std::cerr << "Warning: No routes mapped in FileCache for " << base_directory_ << std::endl;
        return false;
    }
    std::cout << "FileCache onInitialize: " << route_to_path_.size() << " routes ready." << std::endl;
    return true;
}

// onShutdown (модульный: clear + лог)
void FileCache::onShutdown() {
    clear_cache();
    std::cout << "FileCache onShutdown: Cache cleared." << std::endl;
}

// Получение MIME типа по расширению файла (оригинал)
std::string FileCache::get_mime_type(const std::string& extension) const {
    static const std::unordered_map<std::string, std::string> mime_types = {
        {".html", "text/html; charset=utf-8"},
        {".htm", "text/html; charset=utf-8"},
        {".css", "text/css; charset=utf-8"},
        {".js", "application/javascript; charset=utf-8"},
        {".mjs", "application/javascript; charset=utf-8"},
        {".json", "application/json; charset=utf-8"},
        {".xml", "application/xml; charset=utf-8"},
        {".txt", "text/plain; charset=utf-8"},
        {".md", "text/markdown; charset=utf-8"},
        {".csv", "text/csv; charset=utf-8"},
        {".pdf", "application/pdf"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".png", "image/png"},
        {".gif", "image/gif"},
        {".svg", "image/svg+xml"},
        {".ico", "image/x-icon"},
        {".webp", "image/webp"},
        {".bmp", "image/bmp"},
        {".tiff", "image/tiff"},
        {".mp3", "audio/mpeg"},
        {".mp4", "video/mp4"},
        {".webm", "video/webm"},
        {".ogg", "audio/ogg"},
        {".oga", "audio/ogg"},
        {".ogv", "video/ogg"},
        {".wav", "audio/wav"},
        {".woff", "font/woff"},
        {".woff2", "font/woff2"},
        {".ttf", "font/ttf"},
        {".otf", "font/otf"},
        {".eot", "application/vnd.ms-fontobject"},
        {".zip", "application/zip"},
        {".rar", "application/x-rar-compressed"},
        {".7z", "application/x-7z-compressed"},
        {".tar", "application/x-tar"},
        {".gz", "application/gzip"},
        {".bz2", "application/x-bzip2"},
        {".xz", "application/x-xz"},
        {".doc", "application/msword"},
        {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {".xls", "application/vnd.ms-excel"},
        {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {".ppt", "application/vnd.ms-powerpoint"},
        {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"}
    };
    // Приводим расширение к нижнему регистру
    std::string ext_lower = extension;
    std::transform(ext_lower.begin(), ext_lower.end(), ext_lower.begin(), ::tolower);
    auto it = mime_types.find(ext_lower);
    if (it != mime_types.end()) {
        return it->second;
    }
    // Для неизвестных расширений пытаемся определить по категории
    if (ext_lower == ".c" || ext_lower == ".cpp" || ext_lower == ".h" || ext_lower == ".hpp" ||
        ext_lower == ".py" || ext_lower == ".java" || ext_lower == ".cs" || ext_lower == ".php" ||
        ext_lower == ".rb" || ext_lower == ".go" || ext_lower == ".rs" || ext_lower == ".swift") {
        return "text/plain; charset=utf-8";
    }
    return "application/octet-stream";
}

// Нормализация маршрута (оригинал)
std::string FileCache::normalize_route(const fs::path& file_path) const {
    // Получаем относительный путь от базовой директории
    fs::path relative_path;
    try {
        relative_path = fs::relative(file_path, base_directory_);
    }
    catch (const fs::filesystem_error&) {
        // Если не можем получить относительный путь, используем полный
        return "/invalid_path";
    }
    std::string filename;
    std::string route = "/";

    if (fileCacheMode == 0) {
        route += relative_path.string();
        filename = relative_path.string();
    }
    else if (fileCacheMode == 1) {
        // Добавляем родительские директории
        if (relative_path.has_parent_path() && relative_path.parent_path() != ".") {
            route += relative_path.parent_path().string() + "/";
        }
        // Добавляем имя файла без расширения
        filename = relative_path.stem().string();
        if (!filename.empty()) {
            route += filename;
        }
        
    }
    std::string filename_lower = filename;
    // Специальная обработка для index файлов
    std::transform(filename_lower.begin(), filename_lower.end(), filename_lower.begin(), ::tolower);
    if (filename_lower == "index" or filename_lower == "index.html") {
        // Если это index в корне - возвращаем "/"
        if (!relative_path.has_parent_path() || relative_path.parent_path() == ".") {
            return "/";
        }
        // Если index в поддиректории - возвращаем путь до директории
        return "/" + relative_path.parent_path().string() + "/";
    }

    // Заменяем обратные слеши на прямые (для Windows)
    std::replace(route.begin(), route.end(), '\\', '/');
    // Удаляем двойные слеши
    size_t pos;
    while ((pos = route.find("//")) != std::string::npos) {
        route.replace(pos, 2, "/");
    }
    return route;
}

// Сканирование директории (оригинал)
void FileCache::scan_directory(const fs::path& directory) {
    try {
        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (fs::is_regular_file(entry.path())) {
                std::string route = normalize_route(entry.path());
                if (route != "/invalid_path") {
                    route_to_path_[route] = entry.path().string();
                    // Также добавляем альтернативный вариант без конечного слэша
                    if (route.back() == '/' && route != "/") {
                        std::string alt_route = route.substr(0, route.length() - 1);
                        route_to_path_[alt_route] = entry.path().string();
                    }
                }
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error scanning directory " << directory << ": " << e.what() << std::endl;
    }
}

// Загрузка файла с диска (оригинал)
std::optional<FileCache::CachedFile> FileCache::load_file_from_disk(const fs::path& file_path) const {
    auto content_opt = read_file_contents(file_path);
    if (!content_opt) {
        return std::nullopt;
    }
    try {
        CachedFile cached_file;
        cached_file.content = std::move(*content_opt);
        cached_file.size = cached_file.content.size();
        cached_file.file_path = file_path;
        cached_file.mime_type = get_mime_type(file_path.extension().string());
        // Время последнего изменения файла
        auto ftime = fs::last_write_time(file_path);
        cached_file.last_modified = file_time_to_system_time(ftime);
        cached_file.last_accessed = std::chrono::system_clock::now();
        return cached_file;
    }
    catch (const std::exception& e) {
        std::cerr << "Error creating cached file for " << file_path << ": " << e.what() << std::endl;
        return std::nullopt;
    }
}

// Вытеснение файлов при переполнении кэша (оригинал)
void FileCache::evict_if_needed() {
    if (file_cache_.size() <= max_cache_size_) {
        return;
    }
    // Находим файл с самым старым временем доступа
    auto oldest = file_cache_.begin();
    for (auto it = file_cache_.begin(); it != file_cache_.end(); ++it) {
        if (it->second.last_accessed < oldest->second.last_accessed) {
            oldest = it;
        }
    }
    // Удаляем его
    if (oldest != file_cache_.end()) {
        total_cache_size_ -= oldest->second.size;
        file_cache_.erase(oldest);
    }
}

// Перестроение карты файлов (оригинал + лог)
void FileCache::rebuild_file_map() {
    std::unique_lock lock(cache_mutex_);
    route_to_path_.clear();
    scan_directory(base_directory_);
    std::cout << "File map rebuilt. Total routes: " << route_to_path_.size()
        << " in directory: " << base_directory_ << std::endl;
}

// Получение файла по маршруту (оригинал — это ключевой метод для RequestHandler!)
std::optional<FileCache::CachedFile> FileCache::get_file(const std::string& route) {
    std::unique_lock lock(cache_mutex_);
    // Проверяем, существует ли такой маршрут
    auto path_it = route_to_path_.find(route);
    if (path_it == route_to_path_.end()) {
        return std::nullopt;
    }
    fs::path file_path = path_it->second;
    // Если кэш отключен, загружаем файл с диска каждый раз
    if (!cache_enabled_) {
        return load_file_from_disk(file_path);
    }
    // Проверяем, есть ли файл в кэше
    auto cache_it = file_cache_.find(route);
    if (cache_it != file_cache_.end()) {
        // Обновляем время доступа
        cache_it->second.last_accessed = std::chrono::system_clock::now();
        return cache_it->second;
    }
    // Загружаем файл с диска
    auto cached_file = load_file_from_disk(file_path);
    if (!cached_file) {
        return std::nullopt;
    }
    // Проверяем, не переполнен ли кэш
    evict_if_needed();
    // Добавляем в кэш
    file_cache_[route] = *cached_file;
    total_cache_size_ += cached_file->size;
    return cached_file;
}

// Получение файла по прямому пути (оригинал)
std::optional<FileCache::CachedFile> FileCache::get_file_by_path(const std::string& file_path) {
    fs::path path(file_path);
    if (!path.is_absolute()) {
        path = base_directory_ / path;
    }
    if (!fs::exists(path) || !fs::is_regular_file(path)) {
        return std::nullopt;
    }
    // Создаем временный маршрут для кэширования
    std::string temp_route = "/file" + std::to_string(std::hash<std::string>{}(path.string()));
    std::unique_lock lock(cache_mutex_);
    if (cache_enabled_) {
        auto cache_it = file_cache_.find(temp_route);
        if (cache_it != file_cache_.end()) {
            cache_it->second.last_accessed = std::chrono::system_clock::now();
            return cache_it->second;
        }
    }
    auto cached_file = load_file_from_disk(path);
    if (!cached_file) {
        return std::nullopt;
    }
    if (cache_enabled_) {
        evict_if_needed();
        file_cache_[temp_route] = *cached_file;
        total_cache_size_ += cached_file->size;
    }
    return cached_file;
}

// Принудительное кэширование файла (оригинал)
bool FileCache::preload_file(const std::string& route) {
    std::unique_lock lock(cache_mutex_);
    auto path_it = route_to_path_.find(route);
    if (path_it == route_to_path_.end()) {
        return false;
    }
    fs::path file_path = path_it->second;
    // Если файл уже в кэше, просто обновляем время доступа
    auto cache_it = file_cache_.find(route);
    if (cache_it != file_cache_.end()) {
        cache_it->second.last_accessed = std::chrono::system_clock::now();
        return true;
    }
    // Загружаем файл
    auto cached_file = load_file_from_disk(file_path);
    if (!cached_file) {
        return false;
    }
    if (cache_enabled_) {
        evict_if_needed();
        file_cache_[route] = *cached_file;
        total_cache_size_ += cached_file->size;
    }
    return true;
}

// Удаление файла из кэша (оригинал)
bool FileCache::evict_from_cache(const std::string& route) {
    std::unique_lock lock(cache_mutex_);
    auto it = file_cache_.find(route);
    if (it != file_cache_.end()) {
        total_cache_size_ -= it->second.size;
        file_cache_.erase(it);
        return true;
    }
    return false;
}

// Очистка всего кэша (оригинал — фиксит ошибку!)
void FileCache::clear_cache() {
    std::unique_lock lock(cache_mutex_);
    file_cache_.clear();
    total_cache_size_ = 0;
}

// Получение списка всех маршрутов (оригинал)
std::vector<std::string> FileCache::get_all_routes() const {
    std::shared_lock lock(cache_mutex_);
    std::vector<std::string> routes;
    routes.reserve(route_to_path_.size());
    for (const auto& pair : route_to_path_) {
        routes.push_back(pair.first);
    }
    return routes;
}

// Поиск маршрутов по шаблону (оригинал)
std::vector<std::string> FileCache::find_routes(const std::string& pattern) const {
    std::shared_lock lock(cache_mutex_);
    std::vector<std::string> matches;
    for (const auto& pair : route_to_path_) {
        if (pair.first.find(pattern) != std::string::npos) {
            matches.push_back(pair.first);
        }
    }
    return matches;
}

// Проверка существования маршрута (оригинал)
bool FileCache::route_exists(const std::string& route) const {
    std::shared_lock lock(cache_mutex_);
    return route_to_path_.find(route) != route_to_path_.end();
}

// Получение информации о кэше (оригинал)
FileCache::CacheInfo FileCache::get_cache_info() const {
    std::shared_lock lock(cache_mutex_);
    CacheInfo info;
    info.cached_files_count = file_cache_.size();
    info.total_routes_count = route_to_path_.size();
    info.total_cache_size_bytes = total_cache_size_;
    info.max_cache_size = max_cache_size_;
    info.cache_enabled = cache_enabled_;
    return info;
}

// Получение детальной статистики (оригинал)
FileCache::CacheStats FileCache::get_detailed_stats() const {
    std::shared_lock lock(cache_mutex_);
    CacheStats stats;
    stats.total_size = total_cache_size_;
    for (const auto& pair : file_cache_) {
        CacheStats::FileStat file_stat;
        file_stat.route = pair.first;
        file_stat.size = pair.second.size;
        file_stat.last_accessed = pair.second.last_accessed;
        file_stat.last_modified = pair.second.last_modified;
        stats.files.push_back(file_stat);
    }
    if (!file_cache_.empty()) {
        stats.average_file_size = total_cache_size_ / file_cache_.size();
    }
    else {
        stats.average_file_size = 0;
    }
    return stats;
}

// Обновление файла в кэше (оригинал)
bool FileCache::refresh_file(const std::string& route) {
    std::unique_lock lock(cache_mutex_);
    auto path_it = route_to_path_.find(route);
    if (path_it == route_to_path_.end()) {
        return false;
    }
    fs::path file_path = path_it->second;
    try {
        // Проверяем, изменился ли файл
        auto ftime = fs::last_write_time(file_path);
        auto last_write_time = file_time_to_system_time(ftime);
        auto cache_it = file_cache_.find(route);
        if (cache_it != file_cache_.end()) {
            // Если файл не изменился, просто обновляем время доступа
            if (last_write_time <= cache_it->second.last_modified) {
                cache_it->second.last_accessed = std::chrono::system_clock::now();
                return true;
            }
            // Удаляем старую версию из кэша
            total_cache_size_ -= cache_it->second.size;
        }
        // Загружаем новую версию
        auto cached_file = load_file_from_disk(file_path);
        if (!cached_file) {
            if (cache_it != file_cache_.end()) {
                file_cache_.erase(cache_it);
            }
            return false;
        }
        file_cache_[route] = *cached_file;
        total_cache_size_ += cached_file->size;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Error refreshing file " << route << ": " << e.what() << std::endl;
        return false;
    }
}

// Получение MIME типа для маршрута (оригинал)
std::optional<std::string> FileCache::get_mime_type_for_route(const std::string& route) const {
    std::shared_lock lock(cache_mutex_);
    auto path_it = route_to_path_.find(route);
    if (path_it == route_to_path_.end()) {
        return std::nullopt;
    }
    fs::path file_path = path_it->second;
    return get_mime_type(file_path.extension().string());
}

// Установка максимального размера кэша (оригинал)
void FileCache::set_max_cache_size(size_t max_size) {
    std::unique_lock lock(cache_mutex_);
    max_cache_size_ = max_size;
    // Если новый размер меньше текущего, вытесняем лишние файлы
    while (file_cache_.size() > max_cache_size_) {
        evict_if_needed();
    }
}