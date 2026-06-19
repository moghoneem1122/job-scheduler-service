#pragma once
#include "CronParser.h"
#include <string>
#include <ctime>
#include <chrono>

// ============================================================================
// TimeManager — computes nextRunTime for once, interval, and cron jobs
//
// Time format used throughout: ISO 8601 local time "YYYY-MM-DDTHH:MM:SS"
// Delegates all cron logic to the dedicated CronParser class.
// ============================================================================

class TimeManager {
public:

    // =====================
    // Get current local time as ISO string
    // =====================
    static std::string now()
    {
        auto tp = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(tp);
        std::tm tm{};
        localtime_r(&tt, &tm);
        return formatTm(tm);
    }

    // =====================
    // Parse an ISO time string → std::tm
    // =====================
    static std::tm parseTime(const std::string& timeStr)
    {
        std::tm tm{};
        // Expected: "YYYY-MM-DDTHH:MM:SS"
        if (sscanf(timeStr.c_str(), "%d-%d-%dT%d:%d:%d",
            &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
            &tm.tm_hour, &tm.tm_min, &tm.tm_sec) < 6)
        {
            // Try without seconds: "YYYY-MM-DDTHH:MM"
            sscanf(timeStr.c_str(), "%d-%d-%dT%d:%d",
                &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                &tm.tm_hour, &tm.tm_min);
            tm.tm_sec = 0;
        }

        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        tm.tm_isdst = -1;

        return tm;
    }

    // =====================
    // Format std::tm → ISO string
    // =====================
    static std::string formatTm(const std::tm& tm)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec);
        return std::string(buf);
    }

    // =====================
    // ONE-TIME JOBS
    // Returns the provided time if it's in the future, empty string otherwise.
    // =====================
    static std::string computeOnceNext(const std::string& scheduledTime)
    {
        std::tm scheduled = parseTime(scheduledTime);
        std::time_t scheduledT = mktime(&scheduled);
        std::time_t nowT = std::time(nullptr);

        if (scheduledT > nowT)
        {
            return formatTm(scheduled);
        }

        return "";
    }

    // =====================
    // INTERVAL JOBS
    // Compute next run time = baseTime + intervalSeconds.
    // If the result is still in the past, leap forward to the
    // next future occurrence (skip missed intervals).
    // =====================
    static std::string computeIntervalNext(const std::string& lastRunTime,
                                           int intervalSeconds)
    {
        if (intervalSeconds <= 0)
            return "";

        std::tm baseTm = parseTime(lastRunTime);
        std::time_t baseT = mktime(&baseTm);
        std::time_t nowT = std::time(nullptr);

        std::time_t nextT = baseT + intervalSeconds;

        // If we've missed multiple intervals, leap forward
        if (nextT <= nowT)
        {
            long long elapsed = nowT - baseT;
            long long periods = elapsed / intervalSeconds;
            nextT = baseT + (periods + 1) * intervalSeconds;
        }

        std::tm nextTm{};
        localtime_r(&nextT, &nextTm);

        return formatTm(nextTm);
    }

    // =====================
    // CRON JOBS
    // Delegates to CronParser for expression parsing and
    // next-execution-time calculation.
    // =====================
    static std::string computeCronNext(const std::string& cronExpr,
                                       const std::string& afterTime = "")
    {
        // Validate the expression first
        CronValidationResult vr = CronParser::validate(cronExpr);
        if (!vr.valid)
            return "";

        // Parse into schedule
        CronSchedule schedule = CronParser::parse(cronExpr);

        // Determine the "after" time
        std::tm afterTm{};
        if (!afterTime.empty())
        {
            afterTm = parseTime(afterTime);
            mktime(&afterTm);
        }
        else
        {
            std::time_t nowT = std::time(nullptr);
            localtime_r(&nowT, &afterTm);
        }

        // Calculate next execution
        std::tm nextTm{};
        if (CronParser::nextExecution(schedule, afterTm, nextTm))
        {
            return formatTm(nextTm);
        }

        return "";
    }

    // =====================
    // Validate a cron expression (convenience wrapper)
    // =====================
    static CronValidationResult validateCron(const std::string& cronExpr)
    {
        return CronParser::validate(cronExpr);
    }
};
