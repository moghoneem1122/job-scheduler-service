#pragma once
#include <string>
#include "crow.h"

struct Job {
    int id = 0;
    std::string name;
    std::string type;
    std::string status;
    std::string nextRunTime;
    int retryPolicy = 0;
    int retryCount = 0;
};

// Convert Job → JSON
inline crow::json::wvalue toJson(const Job& job)
{
    crow::json::wvalue j;

    j["id"] = job.id;
    j["name"] = job.name;
    j["type"] = job.type;
    j["status"] = job.status;
    j["nextRunTime"] = job.nextRunTime;
    j["retryPolicy"] = job.retryPolicy;
    j["retryCount"] = job.retryCount;

    return j;
}