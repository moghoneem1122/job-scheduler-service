#pragma once
#include <string>
#include "crow.h"

struct Job {
    int id = 0;
    std::string name;
    std::string command;
    std::string type;
    std::string status;
    std::string nextRunTime;
    std::string cronExpression;
    int intervalSeconds = 0;
    int retryPolicy = 0;        // max number of retries allowed
    int retryCount = 0;         // current retry attempt
    int retryDelaySeconds = 0;  // base delay between retries (exponential backoff)
    std::string lastRunTime;
    std::string lastResult;
};

// Convert Job → JSON
inline crow::json::wvalue toJson(const Job& job)
{
    crow::json::wvalue j;

    j["id"] = job.id;
    j["name"] = job.name;
    j["command"] = job.command;
    j["type"] = job.type;
    j["status"] = job.status;
    j["nextRunTime"] = job.nextRunTime;
    j["cronExpression"] = job.cronExpression;
    j["intervalSeconds"] = job.intervalSeconds;
    j["retryPolicy"] = job.retryPolicy;
    j["retryCount"] = job.retryCount;
    j["retryDelaySeconds"] = job.retryDelaySeconds;
    j["lastRunTime"] = job.lastRunTime;
    j["lastResult"] = job.lastResult;

    return j;
}