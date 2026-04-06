#pragma once

#include "DatabaseMapper.h"
#include "Models.h"
#include <boost/json.hpp>
#include <string>
#include <optional>

class HRDatabaseHandlers {
public:
    explicit HRDatabaseHandlers(DatabaseMapper& mapper) : mapper_(mapper) {}

    // GET /api/all-data?since=2025-01-01T00:00:00Z
    boost::json::object getAllData(const std::optional<std::string>& since) {
        std::string since_filter;
        if (since) {
            since_filter = " WHERE last_updated > " + mapper_.db_.quote(*since);
        }

        // Employees
        auto employees = mapper_.query<Employee>(
            "SELECT id, fullname, status, salary, penalties, bonuses, total_penalties, total_bonuses, last_updated "
            "FROM employees" + since_filter + " ORDER BY id");

        // Hours (upsert логика на фронте, но храним одну запись на сотрудника)
        auto hours = mapper_.query<Hours>(
            "SELECT employee_id, regular_hours, overtime, undertime, last_updated "
            "FROM hours" + since_filter + " ORDER BY employee_id");

        // Penalties & Bonuses
        auto penalties = mapper_.query<Penalty>(
            "SELECT id, employee_id, reason, amount, date "
            "FROM penalties" + since_filter + " ORDER BY date DESC");

        auto bonuses = mapper_.query<Bonus>(
            "SELECT id, employee_id, note, amount, date "
            "FROM bonuses" + since_filter + " ORDER BY date DESC");

        // Dashboard aggregates (только для hired)
        Dashboard dash{};
        auto res = mapper_.db_.query(
            R"(SELECT 
                COALESCE(SUM(penalties), 0) AS penalties,
                COALESCE(SUM(bonuses), 0) AS bonuses,
                COALESCE(SUM(undertime), 0) AS undertime
               FROM employees e
               LEFT JOIN hours h ON h.employee_id = e.id
               WHERE e.status = 'hired')");
        if (!res.size().empty()) {
            dash.penalties = res.get<int>(0, "penalties");
            dash.bonuses = res.get<int>(0, "bonuses");
            dash.undertime = res.get<double>(0, "undertime");
        }

        // Последнее обновление
        std::string current_ts = getCurrentTimestamp();

        boost::json::object response;
        response["dashboard"] = boost::json::value_from(dash);
        response["employees"] = boost::json::value_from(employees);
        response["hours"] = boost::json::value_from(hours);
        response["penalties"] = boost::json::value_from(penalties);
        response["bonuses"] = boost::json::value_from(bonuses);
        response["lastUpdated"] = current_ts;

        return response;
    }

    // POST /api/employees
    Employee createEmployee(const Employee& input) {
        return mapper_.insert<Employee>(
            R"(INSERT INTO employees (fullname, status, salary, penalties, bonuses, total_penalties, total_bonuses)
               VALUES ($1, $2, $3, 0, 0, 0.0, 0.0)
               RETURNING id, fullname, status, salary, penalties, bonuses, total_penalties, total_bonuses, last_updated)",
            input.fullname, input.status, input.salary);
    }

    // POST /api/hours/:employeeId (upsert)
    Hours upsertHours(int employee_id, const Hours& input) {
        return mapper_.insert<Hours>(
            R"(INSERT INTO hours (employee_id, regular_hours, overtime, undertime)
               VALUES ($1, $2, $3, $4)
               ON CONFLICT (employee_id) DO UPDATE
               SET regular_hours = EXCLUDED.regular_hours,
                   overtime = EXCLUDED.overtime,
                   undertime = EXCLUDED.undertime
               RETURNING employee_id, regular_hours, overtime, undertime, last_updated)",
            employee_id, input.regular_hours, input.overtime, input.undertime);
    }

    // POST /api/employees/:employeeId/penalties
    Penalty createPenalty(int employee_id, const Penalty& input) {
        // Транзакция: штраф + обновление счётчиков сотрудника
        mapper_.execute("BEGIN");

        auto penalty = mapper_.insert<Penalty>(
            R"(INSERT INTO penalties (employee_id, reason, amount, date)
               VALUES ($1, $2, $3, NOW())
               RETURNING id, employee_id, reason, amount, date)",
            employee_id, input.reason, input.amount);

        mapper_.execute(
            R"(UPDATE employees
               SET penalties = penalties + 1,
                   total_penalties = total_penalties + $1
               WHERE id = $2)",
            input.amount, employee_id);

        mapper_.execute("COMMIT");

        return penalty;
    }

    // POST /api/employees/:employeeId/bonuses
    Bonus createBonus(int employee_id, const Bonus& input) {
        mapper_.execute("BEGIN");

        auto bonus = mapper_.insert<Bonus>(
            R"(INSERT INTO bonuses (employee_id, note, amount, date)
               VALUES ($1, $2, $3, NOW())
               RETURNING id, employee_id, note, amount, date)",
            employee_id, input.note, input.amount);

        mapper_.execute(
            R"(UPDATE employees
               SET bonuses = bonuses + 1,
                   total_bonuses = total_bonuses + $1
               WHERE id = $2)",
            input.amount, employee_id);

        mapper_.execute("COMMIT");

        return bonus;
    }

private:
    DatabaseMapper& mapper_;

    std::string getCurrentTimestamp() {
        auto res = mapper_.db_.query("SELECT NOW()::text");
        return res.get<std::string>(0, "now");
    }
};