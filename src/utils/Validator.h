#pragma once
#include <string>

inline bool isValidJobType(const std::string& type)
{
    return type == "once" || type == "interval" || type == "cron";
}

inline bool isEmpty(const std::string& s)
{
    return s.empty();
}