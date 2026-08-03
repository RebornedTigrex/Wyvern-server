#pragma once
#include <mutex>
#include <string>
#include <queue>


namespace utils{
    class Logger {
    public:
        Logger(std::string name_);
        ~Logger() = default;
    private:
        std::string loggerName;

    public:
        enum class Level { Info, Warning, Error, Debug };

        void Log(Level level, const std::string& msg);

        std::string getName();

    private:
        void printTimestamp();
        void printName();
    };
}

