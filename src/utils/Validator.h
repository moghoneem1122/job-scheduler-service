#pragma once
#include <string>
#include "CronParser.h"

inline bool isValidJobType(const std::string& type)
{
    return type == "once" || type == "interval" || type == "cron";
}

inline bool isEmpty(const std::string& s)
{
    return s.empty();
}

// Validate a cron expression using the dedicated CronParser
// Returns the validation result with detailed error info
inline CronValidationResult isValidCronExpression(const std::string& expr)
{
    return CronParser::validate(expr);
}