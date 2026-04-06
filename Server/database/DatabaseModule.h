#pragma once

#include "BaseModule.h"
#include <boost/asio.hpp>
#include <boost/asio/strand.hpp>
#include <pqxx/pqxx>
#include <memory>
#include <thread>
#include <vector>
#include <atomic>
#include <iostream>

class DatabaseModule : public BaseModule {
private:
    std::string db_connection_string_;

    boost::asio::io_context& io_context_;

    std::unique_ptr<pqxx::connection> conn_;
    std::atomic<bool> db_ready_{ false };

    // SQL-скрипт создания схемы
    const std::string init_schema_sql_ = R"(
        CREATE TABLE IF NOT EXISTS "Department" (
	"ID" SERIAL,
	"DepName" VARCHAR(100) NOT NULL,
	"PhoneNumber" VARCHAR(20),
	PRIMARY KEY("ID")
);




CREATE TABLE IF NOT EXISTS "Post" (
	"ID" SERIAL,
	"PostName" VARCHAR(100) NOT NULL UNIQUE,
	"BaseSalary" DECIMAL(12,2) NOT NULL,
	"DepartmentID" INTEGER NOT NULL,
	PRIMARY KEY("ID")
);




CREATE TABLE IF NOT EXISTS "Employee" (
	"ID" SERIAL,
	"FirstName" VARCHAR(60) NOT NULL,
	"LastName" VARCHAR(60) NOT NULL,
	"Patronymic" VARCHAR(60),
	"PhoneNumber" VARCHAR(20),
	"WorkStatus" VARCHAR(50) DEFAULT 'Active',
	"PostName" VARCHAR(100) NOT NULL,
	"Salary" DECIMAL(12,2) NOT NULL,
	PRIMARY KEY("ID")
);




CREATE TABLE IF NOT EXISTS "Salary" (
	"ID" SERIAL,
	"EmployeeID" INTEGER NOT NULL UNIQUE,
	"BaseSalary" DECIMAL(12,2) NOT NULL,
	"Multiplier" DECIMAL(4,2) NOT NULL DEFAULT 1.00,
	"AgreeSalary" DECIMAL(12,2) NOT NULL,
	"ContractNumber" VARCHAR(50),
	PRIMARY KEY("ID")
);




CREATE TABLE IF NOT EXISTS "TimeRecords" (
	"ID" SERIAL,
	"EmployeeID" INTEGER NOT NULL,
	"Date" DATE NOT NULL,
	"TimeIn" TIME,
	"TimeOut" TIME,
	PRIMARY KEY("ID")
);




CREATE TABLE IF NOT EXISTS "Applicant" (
	"ID" SERIAL,
	"FirstName" VARCHAR(60) NOT NULL,
	"LastName" VARCHAR(60) NOT NULL,
	"Patronymic" VARCHAR(60),
	"PostName" VARCHAR(100) NOT NULL,
	"resume" TEXT,
	"PhoneNumber" VARCHAR(20),
	"Salary" DECIMAL(12,2),
	"WorkStatus" VARCHAR(50) DEFAULT 'Кандидат',
	PRIMARY KEY("ID")
);



ALTER TABLE "Post"
ADD FOREIGN KEY("DepartmentID") REFERENCES "Department"("ID")
ON UPDATE NO ACTION ON DELETE RESTRICT;
ALTER TABLE "Employee"
ADD FOREIGN KEY("PostName") REFERENCES "Post"("PostName")
ON UPDATE NO ACTION ON DELETE RESTRICT;
ALTER TABLE "Salary"
ADD FOREIGN KEY("EmployeeID") REFERENCES "Employee"("ID")
ON UPDATE NO ACTION ON DELETE CASCADE;
ALTER TABLE "TimeRecords"
ADD FOREIGN KEY("EmployeeID") REFERENCES "Employee"("ID")
ON UPDATE NO ACTION ON DELETE CASCADE;
    )";

public:
    // Новый конструктор — принимает io_context по ссылке
    explicit DatabaseModule(
        boost::asio::io_context& ioc,
        const std::string& conn_str = "dbname=hr_db user=postgres password=postgres host=127.0.0.1 port=5432"
    );

    ~DatabaseModule() override;

    DatabaseModule(const DatabaseModule&) = delete;
    DatabaseModule& operator=(const DatabaseModule&) = delete;

    pqxx::connection* getConnection() {
        return db_ready_.load() ? conn_.get() : nullptr;
    }

    bool isDatabaseReady() const { return db_ready_.load(); }

protected:
    bool onInitialize() override;
    void onShutdown() override;

private:

    // Асинхронная инициализация базы
    void asyncInitializeDatabase();
};