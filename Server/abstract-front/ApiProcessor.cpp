#include "ApiProcessor.h"
#include "DatabaseModule.h"

#include <boost/algorithm/string.hpp>
#include <boost/json.hpp>
#include <pqxx/pqxx>

#include <sstream>
#include <iostream>
#include <regex>

namespace bj = boost::json;
namespace http = boost::beast::http;

ApiProcessor::ApiProcessor(DatabaseModule* db_module) : db_module_(db_module) {}

pqxx::connection* ApiProcessor::getConn() {
    if (!db_module_ || !db_module_->isDatabaseReady()) {
        return nullptr;
    }
    return db_module_->getConnection();
}

void ApiProcessor::sendJsonError(http::response<http::string_body>& res,
    http::status status,
    const std::string& message) {
    bj::object err;
    err["error"] = message;
    res.result(status);
    res.set(http::field::content_type, "application/json");
    res.body() = bj::serialize(err);
    res.prepare_payload();
}

bj::object ApiProcessor::employeeToJson(const pqxx::row& row) {
    bj::object obj;
    obj["id"] = row["id"].as<int>();
    obj["fullname"] = row["fullname"].c_str();
    obj["status"] = row["status"].c_str();
    obj["salary"] = row["salary"].as<double>();
    obj["penalties"] = row["penalties_count"].as<int>();
    obj["bonuses"] = row["bonuses_count"].as<int>();
    obj["totalPenalties"] = row["total_penalties"].as<double>();
    obj["totalBonuses"] = row["total_bonuses"].as<double>();
    return obj;
}

bj::object ApiProcessor::hoursToJson(const pqxx::row& row) {
    bj::object obj;
    obj["employeeId"] = row["employee_id"].as<int>();
    obj["regularHours"] = row["regular_hours"].as<double>();
    obj["overtime"] = row["overtime"].as<double>();
    obj["undertime"] = row["undertime"].as<double>();
    return obj;
}

bj::object ApiProcessor::penaltyToJson(const pqxx::row& row) {
    bj::object obj;
    obj["id"] = row["id"].as<int>();
    obj["employeeId"] = row["employee_id"].as<int>();
    obj["reason"] = row["reason"].c_str();
    obj["amount"] = row["amount"].as<double>();
    obj["date"] = row["created_at"].c_str();
    return obj;
}

bj::object ApiProcessor::bonusToJson(const pqxx::row& row) {
    bj::object obj;
    obj["id"] = row["id"].as<int>();
    obj["employeeId"] = row["employee_id"].as<int>();
    obj["note"] = row["note"].c_str();
    obj["amount"] = row["amount"].as<double>();
    obj["date"] = row["created_at"].c_str();
    return obj;
}

std::optional<std::string> ApiProcessor::getQueryParam(const std::string& target,
    const std::string& param_name) {
    size_t pos = target.find('?');
    if (pos == std::string::npos) return std::nullopt;

    std::string query = target.substr(pos + 1);
    std::vector<std::string> pairs;
    boost::split(pairs, query, boost::is_any_of("&"));

    for (const auto& pair : pairs) {
        std::vector<std::string> kv;
        boost::split(kv, pair, boost::is_any_of("="));
        if (kv.size() == 2 && kv[0] == param_name) {
            return kv[1];
        }
    }
    return std::nullopt;
}

std::optional<int> ApiProcessor::parseIdFromPath(const std::string& path,
    const std::string& prefix) {
    std::regex re(prefix + "(\\d+)");
    std::smatch match;
    if (std::regex_search(path, match, re) && match.size() > 1) {
        return std::stoi(match.str(1));
    }
    return std::nullopt;
}

void ApiProcessor::handleGetAllData(const http::request<http::string_body>& req,
    http::response<http::string_body>& res) {
    auto* conn = getConn();
    if (!conn) {
        return sendJsonError(res, http::status::service_unavailable, "Database not ready");
    }

    if (req.method() != http::verb::get) {
        return sendJsonError(res, http::status::method_not_allowed, "Only GET allowed");
    }

    std::string target_str = std::string(req.target());
    auto since_opt = getQueryParam(target_str, "since");
    std::string since_clause;
    if (since_opt) {
        since_clause = " WHERE updated_at > " + conn->quote(*since_opt);
    }

    try {
        pqxx::work txn(*conn);

        bj::object dashboard;
        auto agg = txn.exec(
            "SELECT "
            "COALESCE(SUM(penalties_count), 0) AS penalties, "
            "COALESCE(SUM(bonuses_count), 0) AS bonuses, "
            "COALESCE(SUM(wh.undertime), 0) AS undertime "
            "FROM employees e "
            "LEFT JOIN work_hours wh ON e.id = wh.employee_id "
            "WHERE e.status = 'hired'");

        dashboard["penalties"] = agg[0]["penalties"].as<int64_t>();
        dashboard["bonuses"] = agg[0]["bonuses"].as<int64_t>();
        dashboard["undertime"] = agg[0]["undertime"].as<double>();

        bj::array employees_arr;
        auto emp_res = txn.exec(pqxx::zview("SELECT * FROM employees" + since_clause));
        for (const auto& row : emp_res) employees_arr.emplace_back(employeeToJson(row));

        bj::array hours_arr;
        auto hours_res = txn.exec(pqxx::zview("SELECT * FROM work_hours" + since_clause));
        for (const auto& row : hours_res) hours_arr.emplace_back(hoursToJson(row));

        bj::array penalties_arr;
        auto pen_res = txn.exec(pqxx::zview("SELECT * FROM penalties" + since_clause));
        for (const auto& row : pen_res) penalties_arr.emplace_back(penaltyToJson(row));

        bj::array bonuses_arr;
        auto bon_res = txn.exec(pqxx::zview("SELECT * FROM bonuses" + since_clause));
        for (const auto& row : bon_res) bonuses_arr.emplace_back(bonusToJson(row));

        auto last_res = txn.exec(pqxx::zview(R"(
            SELECT GREATEST(
                COALESCE((SELECT MAX(updated_at) FROM employees),  '1970-01-01'::timestamp),
                COALESCE((SELECT MAX(updated_at) FROM work_hours),  '1970-01-01'::timestamp),
                COALESCE((SELECT MAX(created_at) FROM penalties), '1970-01-01'::timestamp),
                COALESCE((SELECT MAX(created_at) FROM bonuses),   '1970-01-01'::timestamp)
            ) AS ts
        )"));

        std::string last_updated = last_res[0]["ts"].as<std::string>();

        bj::object response;
        response["dashboard"] = dashboard;
        response["employees"] = std::move(employees_arr);
        response["hours"] = std::move(hours_arr);
        response["penalties"] = std::move(penalties_arr);
        response["bonuses"] = std::move(bonuses_arr);
        response["lastUpdated"] = last_updated;

        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = bj::serialize(response);
        res.prepare_payload();
    }
    catch (const std::exception& e) {
        sendJsonError(res, http::status::internal_server_error, e.what());
    }
}

void ApiProcessor::handleAddEmployee(const http::request<http::string_body>& req,
    http::response<http::string_body>& res) {
    auto* conn = getConn();
    if (!conn) return sendJsonError(res, http::status::service_unavailable, "Database not ready");

    if (req.method() != http::verb::post) {
        return sendJsonError(res, http::status::method_not_allowed, "Only POST allowed");
    }

    //std::cout << "Received request target: " << req.target() << std::endl;
    //std::cout << "Received body: |" << req.body() << "|" << std::endl;

    try {
        bj::value jv = bj::parse(req.body());
        if (jv.is_array()) {
            std::cout << "Received unexpected array instead of object" << std::endl;
            return sendJsonError(res, http::status::bad_request, "Expected JSON object, got array");
        }
        if (!jv.is_object()) {
            return sendJsonError(res, http::status::bad_request, "Invalid JSON: not an object");
        }
        const bj::object& body = jv.as_object();

        std::string fullname = std::string(body.at("fullname").as_string());
        std::string status = std::string(body.at("status").as_string());
        double salary = 0.0;
        if (body.at("salary").is_int64()) {
            salary = static_cast<double>(body.at("salary").as_int64());
        }
        else if (body.at("salary").is_double()) {
            salary = body.at("salary").as_double();
        }
        else {
            return sendJsonError(res, http::status::bad_request, "Salary must be a number");
        }
        if (fullname.size() < 3) return sendJsonError(res, http::status::bad_request, "Fullname too short");
        if (status != "hired" && status != "fired" && status != "interview") {
            return sendJsonError(res, http::status::bad_request, "Invalid status");
        }
        if (salary <= 0) return sendJsonError(res, http::status::bad_request, "Salary must be > 0");

        pqxx::work txn(*conn);

        auto r = txn.exec(pqxx::zview(
            "INSERT INTO employees (fullname, status, salary) VALUES ($1, $2, $3) RETURNING *"),
            pqxx::params{ fullname, status, salary });

        int new_id = r[0]["id"].as<int>();

        txn.exec(pqxx::zview("INSERT INTO work_hours (employee_id) VALUES ($1)"),
            pqxx::params{ new_id });

        txn.commit();

        res.result(http::status::created);
        res.set(http::field::content_type, "application/json");
        res.body() = bj::serialize(employeeToJson(r[0]));
        res.prepare_payload();
    }
    catch (const boost::system::system_error& se) {
        std::cout << "Parse error: " << se.what() << std::endl; //FIXME: Будет срать ошибками boost в фронт
        sendJsonError(res, http::status::bad_request, "Invalid JSON");
    }
    catch (const std::exception& e) {
        sendJsonError(res, http::status::bad_request, e.what());
    }
}

void ApiProcessor::handleUpdateEmployee(const http::request<http::string_body>& req,
    http::response<http::string_body>& res) {
    auto* conn = getConn();
    if (!conn) return sendJsonError(res, http::status::service_unavailable, "Database not ready");

    if (req.method() != http::verb::put) {
        return sendJsonError(res, http::status::method_not_allowed, "Only PUT allowed");
    }

    std::string target_str = std::string(req.target());
    auto id_opt = parseIdFromPath(target_str, "/api/employees/");
    if (!id_opt) return sendJsonError(res, http::status::bad_request, "Invalid employee ID");
    int id = *id_opt;

    try {
        bj::value jv = bj::parse(req.body());
        const bj::object& body = jv.as_object();

        std::string set_clause;
        pqxx::params update_params;

        if (body.contains("fullname")) {
            std::string fn = std::string(body.at("fullname").as_string());
            if (fn.size() < 3) return sendJsonError(res, http::status::bad_request, "Fullname too short");
            set_clause += "fullname = $" + std::to_string(update_params.size() + 1) + ", ";
            update_params.append(fn);
        }
        if (body.contains("status")) {
            std::string st = std::string(body.at("status").as_string());
            if (st != "hired" && st != "fired" && st != "interview") {
                return sendJsonError(res, http::status::bad_request, "Invalid status");
            }
            set_clause += "status = $" + std::to_string(update_params.size() + 1) + ", ";
            update_params.append(st);
        }
        if (body.contains("salary")) {
            double sal = 0.0;
            if (body.at("salary").is_int64()) {
                sal = static_cast<double>(body.at("salary").as_int64());
            }
            else if (body.at("salary").is_double()) {
                sal = body.at("salary").as_double();
            }
            if (sal <= 0) return sendJsonError(res, http::status::bad_request, "Salary must be > 0");
            set_clause += "salary = $" + std::to_string(update_params.size() + 1) + ", ";
            update_params.append(sal);
        }

        if (set_clause.empty()) {
            return sendJsonError(res, http::status::bad_request, "No fields to update");
        }

        set_clause += "updated_at = CURRENT_TIMESTAMP";
        update_params.append(id); // последний параметр — id

        pqxx::work txn(*conn);
        std::string query = "UPDATE employees SET " + set_clause +
            " WHERE id = $" + std::to_string(update_params.size()) + " RETURNING *";

        auto r = txn.exec(pqxx::zview(query), update_params);

        if (r.empty()) {
            return sendJsonError(res, http::status::not_found, "Employee not found");
        }

        txn.commit();

        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = bj::serialize(employeeToJson(r[0]));
        res.prepare_payload();
    }
    catch (const boost::system::system_error&) {
        sendJsonError(res, http::status::bad_request, "Invalid JSON");
    }
    catch (const std::exception& e) {
        sendJsonError(res, http::status::internal_server_error, e.what());
    }
}

void ApiProcessor::handleAddHours(const http::request<http::string_body>& req,
    http::response<http::string_body>& res) {
    auto* conn = getConn();
    if (!conn) return sendJsonError(res, http::status::service_unavailable, "Database not ready");

    if (req.method() != http::verb::post) {
        return sendJsonError(res, http::status::method_not_allowed, "Only POST allowed");
    }

    std::string target_str = std::string(req.target());
    auto id_opt = parseIdFromPath(target_str, "/api/hours/");
    if (!id_opt) return sendJsonError(res, http::status::bad_request, "Invalid employee ID");
    int employee_id = *id_opt;

    try {
        bj::value jv = bj::parse(req.body());
        const bj::object& body = jv.as_object();

        double regular = 0.0;
        if (body.contains("regularHours")) {
            if (body.at("regularHours").is_int64()) {
                regular = static_cast<double>(body.at("regularHours").as_int64());
            }
            else if (body.at("regularHours").is_double()) {
                regular = body.at("regularHours").as_double();
            }
        }
        double overtime = 0.0;
        if (body.contains("overtime")) {
            if (body.at("overtime").is_int64()) {
                overtime = static_cast<double>(body.at("overtime").as_int64());
            }
            else if (body.at("overtime").is_double()) {
                overtime = body.at("overtime").as_double();
            }
        }
        double undertime = 0.0;
        if (body.contains("undertime")) {
            if (body.at("undertime").is_int64()) {
                undertime = static_cast<double>(body.at("undertime").as_int64());
            }
            else if (body.at("undertime").is_double()) {
                undertime = body.at("undertime").as_double();
            }
        }

        if (regular < 0 || overtime < 0 || undertime < 0) {
            return sendJsonError(res, http::status::bad_request, "Hours cannot be negative");
        }

        pqxx::work txn(*conn);

        auto r = txn.exec(pqxx::zview(
            "INSERT INTO work_hours (employee_id, regular_hours, overtime, undertime) "
            "VALUES ($1, $2, $3, $4) "
            "ON CONFLICT (employee_id) DO UPDATE SET "
            "regular_hours = EXCLUDED.regular_hours, "
            "overtime = EXCLUDED.overtime, "
            "undertime = EXCLUDED.undertime "
            "RETURNING *"),
            pqxx::params{ employee_id, regular, overtime, undertime });

        txn.commit();

        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = bj::serialize(hoursToJson(r[0]));
        res.prepare_payload();
    }
    catch (const boost::system::system_error& se) {
        std::cout << "ApiProcessor Error: " << se.what();
        sendJsonError(res, http::status::bad_request, "Invalid JSON");
    }
    catch (const std::exception& e) {
        sendJsonError(res, http::status::internal_server_error, e.what());
    }
}

void ApiProcessor::handleAddPenalty(const http::request<http::string_body>& req,
    http::response<http::string_body>& res) {
    auto* conn = getConn();
    if (!conn) return sendJsonError(res, http::status::service_unavailable, "Database not ready");

    if (req.method() != http::verb::post) {
        return sendJsonError(res, http::status::method_not_allowed, "Only POST allowed");
    }

    std::string target_str = std::string(req.target());
    auto id_opt = parseIdFromPath(target_str, "/api/employees/");
    if (!id_opt) return sendJsonError(res, http::status::bad_request, "Invalid employee ID");
    int employee_id = *id_opt;

    try {
        bj::value jv = bj::parse(req.body());
        const bj::object& body = jv.as_object();

        std::string reason = std::string(body.at("reason").as_string());
        double amount = 0.0;
        if (body.at("amount").is_int64()) {
            amount = static_cast<double>(body.at("amount").as_int64());
        }
        else if (body.at("amount").is_double()) {
            amount = body.at("amount").as_double();
        }

        if (reason.size() < 3) return sendJsonError(res, http::status::bad_request, "Reason too short");
        if (amount <= 0) return sendJsonError(res, http::status::bad_request, "Amount must be > 0");

        pqxx::work txn(*conn);

        auto check = txn.exec(pqxx::zview("SELECT 1 FROM employees WHERE id = $1 AND status = 'hired'"),
            pqxx::params{ employee_id });
        if (check.empty()) return sendJsonError(res, http::status::bad_request, "Employee not found or not hired");

        auto r = txn.exec(pqxx::zview(
            "INSERT INTO penalties (employee_id, reason, amount) VALUES ($1, $2, $3) RETURNING *"),
            pqxx::params{ employee_id, reason, amount });

        txn.commit();

        res.result(http::status::created);
        res.set(http::field::content_type, "application/json");
        res.body() = bj::serialize(penaltyToJson(r[0]));
        res.prepare_payload();
    }
    catch (const boost::system::system_error& se) {
        std::cout << "ApiProcessor Error:" << se.what();
        sendJsonError(res, http::status::bad_request, "Invalid JSON");
    }
    catch (const std::exception& e) {
        sendJsonError(res, http::status::internal_server_error, e.what());
    }
}

void ApiProcessor::handleAddBonus(const http::request<http::string_body>& req,
    http::response<http::string_body>& res) {
    auto* conn = getConn();
    if (!conn) return sendJsonError(res, http::status::service_unavailable, "Database not ready");

    if (req.method() != http::verb::post) {
        return sendJsonError(res, http::status::method_not_allowed, "Only POST allowed");
    }

    std::string target_str = std::string(req.target());
    auto id_opt = parseIdFromPath(target_str, "/api/employees/");
    if (!id_opt) return sendJsonError(res, http::status::bad_request, "Invalid employee ID");
    int employee_id = *id_opt;

    try {
        bj::value jv = bj::parse(req.body());
        const bj::object& body = jv.as_object();

        std::string note = std::string(body.at("note").as_string());
        double amount = 0.0;
        if (body.at("amount").is_int64()) {
            amount = static_cast<double>(body.at("amount").as_int64());
        }
        else if (body.at("amount").is_double()) {
            amount = body.at("amount").as_double();
        }

        if (note.size() < 3) return sendJsonError(res, http::status::bad_request, "Note too short");
        if (amount <= 0) return sendJsonError(res, http::status::bad_request, "Amount must be > 0");

        pqxx::work txn(*conn);

        auto check = txn.exec(pqxx::zview("SELECT 1 FROM employees WHERE id = $1 AND status = 'hired'"),
            pqxx::params{ employee_id });
        if (check.empty()) return sendJsonError(res, http::status::bad_request, "Employee not found or not hired");

        auto r = txn.exec(pqxx::zview(
            "INSERT INTO bonuses (employee_id, note, amount) VALUES ($1, $2, $3) RETURNING *"),
            pqxx::params{ employee_id, note, amount });

        txn.commit();

        res.result(http::status::created);
        res.set(http::field::content_type, "application/json");
        res.body() = bj::serialize(bonusToJson(r[0]));
        res.prepare_payload();
    }
    catch (const boost::system::system_error&) {
        sendJsonError(res, http::status::bad_request, "Invalid JSON");
    }
    catch (const std::exception& e) {
        sendJsonError(res, http::status::internal_server_error, e.what());
    }
}