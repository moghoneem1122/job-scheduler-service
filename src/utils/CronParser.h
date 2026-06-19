#pragma once
#include <string>
#include <vector>
#include <set>
#include <map>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <cctype>

// ============================================================================
// CronParser — Parses, validates, and evaluates standard 5-field cron expressions
//
// Format:  "minute hour day-of-month month day-of-week"
//
//   ┌───────────── minute        (0 - 59)
//   │ ┌─────────── hour          (0 - 23)
//   │ │ ┌───────── day of month  (1 - 31)
//   │ │ │ ┌─────── month         (1 - 12 or JAN - DEC)
//   │ │ │ │ ┌───── day of week   (0 - 6  or SUN - SAT, 0 = Sunday)
//   * * * * *
//
// Supported syntax per field:
//   *        — any value
//   */N      — every N-th value (step)
//   N        — exact value
//   N-M      — inclusive range
//   N-M/S    — range with step S
//   N,M,...  — list of values/ranges
//   JAN-DEC  — named months (field 4)
//   SUN-SAT  — named weekdays (field 5)
// ============================================================================

struct CronValidationResult {
    bool valid = true;
    std::string errorMessage;
    int errorField = -1;           // 0-4, which field has the error (-1 = none)
    std::string errorFieldName;    // human-readable name of the bad field
};

struct CronSchedule {
    std::set<int> minutes;         // 0-59
    std::set<int> hours;           // 0-23
    std::set<int> daysOfMonth;     // 1-31
    std::set<int> months;          // 1-12
    std::set<int> daysOfWeek;      // 0-6
};

class CronParser {
public:

    // ==========================================
    // Validate a cron expression string
    // Returns a CronValidationResult with details
    // ==========================================
    static CronValidationResult validate(const std::string& expression)
    {
        CronValidationResult result;

        if (expression.empty())
        {
            result.valid = false;
            result.errorMessage = "Cron expression is empty";
            return result;
        }

        std::vector<std::string> fields = splitFields(expression);

        if (fields.size() != 5)
        {
            result.valid = false;
            result.errorMessage = "Expected 5 fields (minute hour day month weekday), got "
                + std::to_string(fields.size());
            return result;
        }

        // Field definitions: { name, min, max, field_index }
        struct FieldDef {
            const char* name;
            int min;
            int max;
            int index;
        };

        FieldDef defs[] = {
            { "minute",       0, 59, 0 },
            { "hour",         0, 23, 1 },
            { "day-of-month", 1, 31, 2 },
            { "month",        1, 12, 3 },
            { "day-of-week",  0,  6, 4 },
        };

        for (const auto& def : defs)
        {
            // Pre-process: replace named values before validation
            std::string processed = fields[def.index];
            if (def.index == 3) processed = replaceMonthNames(processed);
            if (def.index == 4) processed = replaceDayNames(processed);

            std::string fieldError = validateField(processed, def.min, def.max);

            if (!fieldError.empty())
            {
                result.valid = false;
                result.errorField = def.index;
                result.errorFieldName = def.name;
                result.errorMessage = "Field '" + std::string(def.name) + "' ("
                    + fields[def.index] + "): " + fieldError;
                return result;
            }
        }

        return result;
    }

    // ==========================================
    // Parse a cron expression into a CronSchedule
    // Call validate() first, or check the returned
    // schedule for empty sets on error.
    // ==========================================
    static CronSchedule parse(const std::string& expression)
    {
        CronSchedule schedule;

        std::vector<std::string> fields = splitFields(expression);
        if (fields.size() != 5)
            return schedule;

        // Pre-process named values
        fields[3] = replaceMonthNames(fields[3]);
        fields[4] = replaceDayNames(fields[4]);

        schedule.minutes     = expandField(fields[0], 0, 59);
        schedule.hours       = expandField(fields[1], 0, 23);
        schedule.daysOfMonth = expandField(fields[2], 1, 31);
        schedule.months      = expandField(fields[3], 1, 12);
        schedule.daysOfWeek  = expandField(fields[4], 0, 6);

        return schedule;
    }

    // ==========================================
    // Compute the next execution time matching
    // the cron expression, starting after 'afterTm'.
    // Returns true and fills 'nextTm' on success.
    // Returns false if no match within 2 years.
    // ==========================================
    static bool nextExecution(const CronSchedule& schedule,
                              const std::tm& afterTm,
                              std::tm& nextTm)
    {
        if (schedule.minutes.empty() || schedule.hours.empty() ||
            schedule.daysOfMonth.empty() || schedule.months.empty() ||
            schedule.daysOfWeek.empty())
            return false;

        // Start from afterTm + 1 minute, seconds zeroed
        std::tm candidate = afterTm;
        candidate.tm_sec = 0;
        candidate.tm_min += 1;
        candidate.tm_isdst = -1;
        mktime(&candidate); // normalize

        // Search up to 2 years ahead (covers leap years, any cron pattern)
        const int MAX_ITERATIONS = 2 * 366 * 24 * 60;

        for (int i = 0; i < MAX_ITERATIONS; ++i)
        {
            // Check month (tm_mon is 0-based, cron months are 1-based)
            int mon = candidate.tm_mon + 1;
            if (schedule.months.find(mon) == schedule.months.end())
            {
                // Skip to the 1st of next month
                candidate.tm_mon += 1;
                candidate.tm_mday = 1;
                candidate.tm_hour = 0;
                candidate.tm_min = 0;
                candidate.tm_isdst = -1;
                mktime(&candidate);
                continue;
            }

            // Check day of month
            if (schedule.daysOfMonth.find(candidate.tm_mday) == schedule.daysOfMonth.end())
            {
                advanceDay(candidate);
                continue;
            }

            // Check day of week (tm_wday: 0=Sunday, same as cron)
            if (schedule.daysOfWeek.find(candidate.tm_wday) == schedule.daysOfWeek.end())
            {
                advanceDay(candidate);
                continue;
            }

            // Check hour
            if (schedule.hours.find(candidate.tm_hour) == schedule.hours.end())
            {
                candidate.tm_hour += 1;
                candidate.tm_min = 0;
                candidate.tm_isdst = -1;
                mktime(&candidate);
                continue;
            }

            // Check minute
            if (schedule.minutes.find(candidate.tm_min) == schedule.minutes.end())
            {
                candidate.tm_min += 1;
                candidate.tm_isdst = -1;
                mktime(&candidate);
                continue;
            }

            // All fields match
            nextTm = candidate;
            return true;
        }

        return false;
    }

    // ==========================================
    // Convenience: compute next from expression string
    // Returns formatted time string, or "" on failure.
    // ==========================================
    static std::string nextExecutionStr(const std::string& expression,
                                        const std::tm& afterTm)
    {
        CronSchedule schedule = parse(expression);
        std::tm nextTm{};

        if (nextExecution(schedule, afterTm, nextTm))
        {
            return formatTm(nextTm);
        }

        return "";
    }

    // ==========================================
    // Check if a given time matches a cron schedule
    // ==========================================
    static bool matches(const CronSchedule& schedule, const std::tm& tm)
    {
        int mon = tm.tm_mon + 1;

        return schedule.minutes.count(tm.tm_min) &&
               schedule.hours.count(tm.tm_hour) &&
               schedule.daysOfMonth.count(tm.tm_mday) &&
               schedule.months.count(mon) &&
               schedule.daysOfWeek.count(tm.tm_wday);
    }

    // ==========================================
    // Format std::tm → ISO string
    // ==========================================
    static std::string formatTm(const std::tm& tm)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec);
        return std::string(buf);
    }

    // ==========================================
    // Describe a cron expression in human-readable form
    // ==========================================
    static std::string describe(const std::string& expression)
    {
        CronValidationResult vr = validate(expression);
        if (!vr.valid)
            return "Invalid: " + vr.errorMessage;

        std::vector<std::string> fields = splitFields(expression);
        std::string desc;

        // Minute
        if (fields[0] == "*")       desc += "Every minute";
        else if (fields[0].find('/') != std::string::npos)
            desc += "Every " + fields[0].substr(fields[0].find('/') + 1) + " minutes";
        else                        desc += "At minute " + fields[0];

        // Hour
        if (fields[1] == "*")       desc += ", every hour";
        else if (fields[1].find('/') != std::string::npos)
            desc += ", every " + fields[1].substr(fields[1].find('/') + 1) + " hours";
        else                        desc += ", at hour " + fields[1];

        // Day of month
        if (fields[2] != "*")       desc += ", on day " + fields[2];

        // Month
        if (fields[3] != "*")       desc += ", in month " + fields[3];

        // Day of week
        if (fields[4] != "*")
        {
            static const char* dayNames[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
            desc += ", on ";

            // Try to show names for simple cases
            std::string processed = replaceDayNames(fields[4]);
            std::set<int> days = expandField(processed, 0, 6);
            bool first = true;
            for (int d : days)
            {
                if (!first) desc += ",";
                desc += dayNames[d];
                first = false;
            }
        }

        return desc;
    }

private:

    // ==========================================
    // Split whitespace-delimited tokens
    // ==========================================
    static std::vector<std::string> splitFields(const std::string& expr)
    {
        std::vector<std::string> fields;
        std::istringstream iss(expr);
        std::string token;
        while (iss >> token)
            fields.push_back(token);
        return fields;
    }

    // ==========================================
    // Advance std::tm by one day, reset hour/min
    // ==========================================
    static void advanceDay(std::tm& tm)
    {
        tm.tm_mday += 1;
        tm.tm_hour = 0;
        tm.tm_min = 0;
        tm.tm_isdst = -1;
        mktime(&tm);
    }

    // ==========================================
    // Replace named months → numeric
    // JAN=1, FEB=2, ..., DEC=12
    // ==========================================
    static std::string replaceMonthNames(const std::string& field)
    {
        static const std::map<std::string, std::string> names = {
            {"JAN","1"}, {"FEB","2"},  {"MAR","3"},  {"APR","4"},
            {"MAY","5"}, {"JUN","6"},  {"JUL","7"},  {"AUG","8"},
            {"SEP","9"}, {"OCT","10"}, {"NOV","11"}, {"DEC","12"}
        };
        return replaceNames(field, names);
    }

    // ==========================================
    // Replace named weekdays → numeric
    // SUN=0, MON=1, ..., SAT=6
    // ==========================================
    static std::string replaceDayNames(const std::string& field)
    {
        static const std::map<std::string, std::string> names = {
            {"SUN","0"}, {"MON","1"}, {"TUE","2"}, {"WED","3"},
            {"THU","4"}, {"FRI","5"}, {"SAT","6"}
        };
        return replaceNames(field, names);
    }

    // ==========================================
    // Generic named-value replacer (case-insensitive)
    // ==========================================
    static std::string replaceNames(const std::string& field,
                                    const std::map<std::string, std::string>& names)
    {
        std::string result = toUpper(field);

        for (const auto& [name, value] : names)
        {
            size_t pos = 0;
            while ((pos = result.find(name, pos)) != std::string::npos)
            {
                result.replace(pos, name.length(), value);
                pos += value.length();
            }
        }

        return result;
    }

    // ==========================================
    // Convert string to uppercase
    // ==========================================
    static std::string toUpper(const std::string& s)
    {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(), ::toupper);
        return result;
    }

    // ==========================================
    // Validate a single cron field (after name replacement)
    // Returns empty string on success, error message on failure.
    // ==========================================
    static std::string validateField(const std::string& field, int min, int max)
    {
        if (field.empty())
            return "field is empty";

        // Split by comma (list)
        std::vector<std::string> parts;
        std::istringstream iss(field);
        std::string part;
        while (std::getline(iss, part, ','))
            parts.push_back(part);

        if (parts.empty())
            return "field is empty after splitting";

        for (const auto& p : parts)
        {
            if (p.empty())
                return "empty element in list";

            // Split by '/' for step
            size_t slashPos = p.find('/');
            std::string base = p;
            std::string stepStr;

            if (slashPos != std::string::npos)
            {
                base = p.substr(0, slashPos);
                stepStr = p.substr(slashPos + 1);

                if (stepStr.empty())
                    return "missing step value after '/'";

                if (!isNumber(stepStr))
                    return "step '" + stepStr + "' is not a valid number";

                int step = std::stoi(stepStr);
                if (step <= 0)
                    return "step must be > 0, got " + stepStr;
                if (step > (max - min + 1))
                    return "step " + stepStr + " exceeds field range ["
                        + std::to_string(min) + "-" + std::to_string(max) + "]";
            }

            if (base == "*")
            {
                continue; // wildcard is always valid
            }
            else if (base.find('-') != std::string::npos)
            {
                // Range: N-M
                size_t dashPos = base.find('-');
                std::string startStr = base.substr(0, dashPos);
                std::string endStr = base.substr(dashPos + 1);

                if (!isNumber(startStr))
                    return "range start '" + startStr + "' is not a valid number";
                if (!isNumber(endStr))
                    return "range end '" + endStr + "' is not a valid number";

                int rangeStart = std::stoi(startStr);
                int rangeEnd = std::stoi(endStr);

                if (rangeStart < min || rangeStart > max)
                    return "range start " + startStr + " out of bounds ["
                        + std::to_string(min) + "-" + std::to_string(max) + "]";
                if (rangeEnd < min || rangeEnd > max)
                    return "range end " + endStr + " out of bounds ["
                        + std::to_string(min) + "-" + std::to_string(max) + "]";
                if (rangeStart > rangeEnd)
                    return "range start (" + startStr + ") > end (" + endStr + ")";
            }
            else
            {
                // Literal value
                if (!isNumber(base))
                    return "'" + base + "' is not a valid number";

                int val = std::stoi(base);
                if (val < min || val > max)
                    return "value " + base + " out of bounds ["
                        + std::to_string(min) + "-" + std::to_string(max) + "]";
            }
        }

        return ""; // valid
    }

    // ==========================================
    // Check if a string is a non-negative integer
    // ==========================================
    static bool isNumber(const std::string& s)
    {
        if (s.empty()) return false;
        for (char c : s)
        {
            if (!std::isdigit(static_cast<unsigned char>(c)))
                return false;
        }
        return true;
    }

    // ==========================================
    // Expand a cron field into the set of matching values
    // ==========================================
    static std::set<int> expandField(const std::string& field, int min, int max)
    {
        std::set<int> result;

        // Split by comma (list)
        std::vector<std::string> parts;
        std::istringstream iss(field);
        std::string part;
        while (std::getline(iss, part, ','))
            parts.push_back(part);

        for (const auto& p : parts)
        {
            size_t slashPos = p.find('/');
            int step = 1;
            std::string base = p;

            if (slashPos != std::string::npos)
            {
                std::string stepStr = p.substr(slashPos + 1);
                if (isNumber(stepStr))
                    step = std::stoi(stepStr);
                if (step <= 0) step = 1;
                base = p.substr(0, slashPos);
            }

            if (base == "*")
            {
                for (int i = min; i <= max; i += step)
                    result.insert(i);
            }
            else if (base.find('-') != std::string::npos)
            {
                size_t dashPos = base.find('-');
                std::string startStr = base.substr(0, dashPos);
                std::string endStr = base.substr(dashPos + 1);

                if (!isNumber(startStr) || !isNumber(endStr))
                    continue;

                int rangeStart = std::stoi(startStr);
                int rangeEnd = std::stoi(endStr);

                if (rangeStart < min) rangeStart = min;
                if (rangeEnd > max) rangeEnd = max;

                for (int i = rangeStart; i <= rangeEnd; i += step)
                    result.insert(i);
            }
            else
            {
                if (!isNumber(base))
                    continue;

                int val = std::stoi(base);
                if (val >= min && val <= max)
                    result.insert(val);
            }
        }

        return result;
    }
};
