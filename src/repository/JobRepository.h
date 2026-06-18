#pragma once
#include "../models/Job.h"
#include "../db/Database.h"
#include "../utils/Logger.h"
#include "../utils/Validator.h"
#include <vector>

class JobRepository {
private:
    Database& db;

public:
    JobRepository(Database& database) : db(database) {}

    std::vector<Job> getAll()
    {
        Logger::log(LogLevel::INFO, "Fetching all jobs from DB");

        sqlite3_stmt* stmt;

        sqlite3_prepare_v2(
            db.get(),
            "SELECT id, name, type, status, next_run_time, retry_policy, retry_count FROM jobs",
            -1,
            &stmt,
            nullptr
        );

        std::vector<Job> jobs;

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            Job job;
            job.id = sqlite3_column_int(stmt, 0);
            job.name = (const char*)sqlite3_column_text(stmt, 1);
            job.type = (const char*)sqlite3_column_text(stmt, 2);
            job.status = (const char*)sqlite3_column_text(stmt, 3);
            job.nextRunTime = (const char*)sqlite3_column_text(stmt, 4);
            job.retryPolicy = sqlite3_column_int(stmt, 5);
            job.retryCount = sqlite3_column_int(stmt, 6);

            jobs.push_back(job);
        }

        sqlite3_finalize(stmt);

        Logger::log(LogLevel::INFO, "Fetched " + std::to_string(jobs.size()) + " jobs");

        return jobs;
    }

    Job getById(int id)
    {
        Logger::log(LogLevel::INFO, "Fetching job by ID: " + std::to_string(id));

        sqlite3_stmt* stmt;

        sqlite3_prepare_v2(
            db.get(),
            "SELECT id, name, type, status, next_run_time, retry_policy, retry_count FROM jobs WHERE id=?",
            -1,
            &stmt,
            nullptr
        );

        sqlite3_bind_int(stmt, 1, id);

        Job job;

        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            job.id = sqlite3_column_int(stmt, 0);
            job.name = (const char*)sqlite3_column_text(stmt, 1);
            job.type = (const char*)sqlite3_column_text(stmt, 2);
            job.status = (const char*)sqlite3_column_text(stmt, 3);
            job.nextRunTime = (const char*)sqlite3_column_text(stmt, 4);
            job.retryPolicy = sqlite3_column_int(stmt, 5);
            job.retryCount = sqlite3_column_int(stmt, 6);

            Logger::log(LogLevel::INFO, "Job found: " + job.name);
        }
        else
        {
            Logger::log(LogLevel::WARN, "Job not found with ID: " + std::to_string(id));
        }

        sqlite3_finalize(stmt);
        return job;
    }

    void create(const Job& job)
    {
        Logger::log(LogLevel::INFO, "Creating job: " + job.name);

        std::string sql =
            "INSERT INTO jobs (name, type, status, next_run_time, retry_policy, retry_count) VALUES ('" +
            job.name + "','" +
            job.type + "','" +
            job.status + "','" +
            job.nextRunTime + "'," +
            std::to_string(job.retryPolicy) + "," +
            std::to_string(job.retryCount) + ");";

        db.exec(sql);

        Logger::log(LogLevel::INFO, "Job created successfully");
    }

    bool remove(int id)
{
    Logger::log(LogLevel::INFO, "Deleting job ID: " + std::to_string(id));

    std::string sql = "DELETE FROM jobs WHERE id=" + std::to_string(id);
    db.exec(sql);

    return sqlite3_changes(db.get()) > 0;
}

    bool updateStatus(int id, const std::string& status)
{
    db.exec(
        "UPDATE jobs SET status='" + status +
        "' WHERE id=" + std::to_string(id)
    );

    return sqlite3_changes(db.get()) > 0;
}
};