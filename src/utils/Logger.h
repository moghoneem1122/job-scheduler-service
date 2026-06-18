#pragma once
#include <iostream>
#include <string>
#include <chrono>
#include <ctime>

enum class LogLevel {
    INFO,
    WARN,
    ERROR
};

class Logger {
public:
    static void log(LogLevel level, const std::string& message)
{
    std::string prefix;

    switch (level)
    {
        case LogLevel::INFO:  prefix = "[INFO] "; break;
        case LogLevel::WARN:  prefix = "[WARN] "; break;
        case LogLevel::ERROR: prefix = "[ERROR] "; break;
    }

    std::time_t now = std::time(nullptr);

    // remove newline from ctime
    std::string timeStr = std::ctime(&now);
    timeStr.pop_back();

    std::cout << prefix << timeStr << " - " << message << std::endl;
}
};