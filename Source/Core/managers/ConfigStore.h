#pragma once

#include "runtime/ConfigSection.h"

#include <boost/json.hpp>

#include <filesystem>
#include <string>

namespace core::managers {

// Хранит распарсенный JSON-конфиг и умеет докатывать дефолты модулей.
// Ответственность ограничена: загрузка файла, deep-merge дефолтов в секцию
// modules.<moduleType>, сериализация обратно в файл при изменениях.
class ConfigStore {
public:
    // Загружает конфиг из файла. Если файла нет — store остаётся пустым,
    // hasFile()=false, dirty()=false. Бросает std::runtime_error при ошибке
    // парсинга или невалидном корне (root должен быть JSON-объектом).
    void load(const std::filesystem::path& path);

    // Возвращает копию секции modules.<moduleType>, докатив в неё отсутствующие
    // поля из defaults. При любом добавлении выставляет dirty=true.
    core::runtime::ConfigSection moduleConfig(const std::string& moduleType,
                                              const boost::json::object& defaults);

    // Записывает текущее состояние в файл с человекочитаемыми отступами.
    // No-op, если dirty=false. Не атомарна (для прототипа достаточно overwrite).
    void commit(const std::filesystem::path& path);

    bool isDirty() const { return dirty_; }
    bool hasFile() const { return hasFile_; }

private:
    boost::json::object root_;
    bool dirty_ = false;
    bool hasFile_ = false;
};

} // namespace core::managers
