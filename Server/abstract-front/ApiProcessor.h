#pragma once

#include <boost/json.hpp>
#include <pqxx/pqxx>
#include <string>
#include <optional>
#include <vector>
#include <regex>

#include <boost/system/error_code.hpp>  
#include <pqxx/params>                  

#include "macros.h"  // Для http::request, http::response и т.д.

class DatabaseModule;

namespace bj = boost::json;
namespace http = boost::beast::http;

class ApiProcessor {
private:
    DatabaseModule* db_module_;

    pqxx::connection* getConn();

    void sendJsonError(http::response<http::string_body>& res,
        http::status status,
        const std::string& message);

    bj::object employeeToJson(const pqxx::row& row);
    bj::object hoursToJson(const pqxx::row& row);
    bj::object penaltyToJson(const pqxx::row& row);
    bj::object bonusToJson(const pqxx::row& row);

    std::optional<std::string> getQueryParam(const std::string& target, const std::string& param_name);
    std::optional<int> parseIdFromPath(const std::string& path, const std::string& prefix);

public:
    explicit ApiProcessor(DatabaseModule* db_module);

    void handleGetAllData(const http::request<http::string_body>& req, http::response<http::string_body>& res);
    void handleAddEmployee(const http::request<http::string_body>& req, http::response<http::string_body>& res);
    void handleUpdateEmployee(const http::request<http::string_body>& req, http::response<http::string_body>& res);
    void handleAddHours(const http::request<http::string_body>& req, http::response<http::string_body>& res);
    void handleAddPenalty(const http::request<http::string_body>& req, http::response<http::string_body>& res);
    void handleAddBonus(const http::request<http::string_body>& req, http::response<http::string_body>& res);
};