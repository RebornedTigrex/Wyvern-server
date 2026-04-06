// ServerConfig.h

//Всё это - временное решение навайбкоженное за 3 минуты
#pragma once

#include <boost/program_options.hpp>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
namespace po = boost::program_options;

struct ServerConfig {
    std::string address = "0.0.0.0";
    int         port = 8080;
    std::string directory = "static";

    // Метод для парсинга и валидации аргументов
    static ServerConfig parse(int argc, char* argv[]) {
        ServerConfig config;

        po::options_description desc("Available options");
        desc.add_options()
            ("help,h", "Show help")
            ("address,a", po::value<std::string>(&config.address)->default_value("0.0.0.0"),
                "IP address to listen on")
            ("port,p", po::value<int>(&config.port)->default_value(8080),
                "Port to listen on")
            ("directory,d", po::value<std::string>(&config.directory)->default_value("static"),
                "Path to static files directory");

        po::variables_map vm;
        try {
            po::store(po::parse_command_line(argc, argv, desc), vm);
            po::notify(vm);

            if (vm.count("help")) {
                std::cout << desc << "\n";
                std::exit(EXIT_SUCCESS);
            }

            // Валидация порта
            if (config.port <= 0 || config.port > 65535) {
                std::cerr << "Error: port must be in the range 1-65535\n";
                std::exit(EXIT_FAILURE);
            }

            // Проверка существования директории (не критично, только предупреждение)
            if (!fs::exists(config.directory)) {
                std::cerr << "Warning: directory '" << config.directory << "' does not exist\n";
            }
        }
        catch (const po::error& e) {
            std::cerr << "Argument parsing error: " << e.what() << "\n";
            std::cerr << desc << "\n";
            std::exit(EXIT_FAILURE);
        }

        // Вывод текущей конфигурации
        std::cout << "Server configuration:\n"
            << " Address: " << config.address << "\n"
            << " Port: " << config.port << "\n"
            << " Directory: " << config.directory << "\n\n";

        return config;
    }
};